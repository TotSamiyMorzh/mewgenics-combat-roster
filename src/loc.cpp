#include "loc.h"

#include "game.h"
#include "log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace cr {
namespace {

// Character::refresh_name: `mov rbx,[rdi+468h]; lea r15,[rip+StringsDatabase]`.
const Sig kSigDbRef = {"StringsDatabase ref", 0x0011C8D7, "48 8B 9F 68 04 00 00 4C 8D 3D"};
// StringsDatabase::lookup(db, std::wstring* out, const std::string* key, bool fallback)
const Sig kSigLookup = {"StringsDatabase::lookup", 0x009616D0,
    "48 89 5C 24 10 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D9 48 81 EC B0 00 00 00 49 8B D8 4C 8B EA"};

struct MsvcStr {        // layout-compatible with the game's std::string / std::wstring
    union { char buf[16]; char* ptr; };
    uint64_t size;
    uint64_t cap;
};
using fn_lookup = void*(__fastcall*)(void* db, MsvcStr* out, const MsvcStr* key, bool fallback);

void* g_db = nullptr;
fn_lookup g_lookup = nullptr;
std::unordered_map<std::string, std::string> g_cache;
const std::string kEmpty;

bool call_lookup(const std::string& key, MsvcStr& out) {
    MsvcStr k{};
    if (key.size() <= 15) {
        memcpy(k.buf, key.data(), key.size());
        k.cap = 15;
    } else {
        k.ptr = const_cast<char*>(key.c_str());   // read-only: the lookup takes a const&
        k.cap = key.size();
    }
    k.size = key.size();
    memset(&out, 0, sizeof(out));
    __try {
        g_lookup(g_db, &out, &k, true);
        return true;
    } __except (log_exception("tr", GetExceptionInformation())) {
        g_lookup = nullptr;
        return false;
    }
}

std::string clean(const std::wstring& w) {
    // Strip [tag], [tag:arg], [/tag].
    std::wstring s;
    s.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) {
        if (w[i] == L'[') {
            size_t j = w.find(L']', i);
            if (j != std::wstring::npos && j - i < 40) { i = j; continue; }
        }
        s += w[i];
    }
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\n')) s.pop_back();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

}  // namespace

bool loc_init() {
    uintptr_t ref = resolve(kSigDbRef);
    g_lookup = (fn_lookup)resolve(kSigLookup);
    if (!ref || !g_lookup) {
        log_line("!! localisation unavailable (signatures not found)");
        g_lookup = nullptr;
        return false;
    }
    const uint8_t* lea = (const uint8_t*)ref + 7;
    g_db = (void*)(lea + 7 + rdv<int32_t>(lea, 3));
    return true;
}

const std::string& tr(const std::string& key) {
    if (key.empty()) return kEmpty;
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second;
    std::string text;
    MsvcStr out;
    if (g_lookup && call_lookup(key, out) && out.size < 4096) {
        const wchar_t* p = out.cap > 7 ? (const wchar_t*)out.ptr : (const wchar_t*)out.buf;
        std::wstring w(p, (size_t)out.size);
        // A missing key comes back as the key itself; treat that as missing.
        if (!w.empty() && !(w.size() == key.size() && std::equal(key.begin(), key.end(), w.begin())))
            text = clean(w);
        // The result buffer belongs to the game's heap. It is deliberately not
        // freed: each key is looked up once per session, so this is bounded.
    }
    return g_cache.emplace(key, std::move(text)).first->second;
}

std::string loc_lang() {
    char lang[16] = {};
    if (g_db) read_string((const uint8_t*)g_db + 0x40, lang, sizeof(lang));   // StringsDatabase+0x40
    return lang;
}

std::string with_stacks(const std::string& text, int stacks) {
    std::string out = text;
    for (const char* ph : {"{stacks}", "{absstacks}"}) {
        size_t at;
        std::string v = std::to_string(std::abs(stacks));
        while ((at = out.find(ph)) != std::string::npos) out.replace(at, strlen(ph), v);
    }
    return out;
}

}  // namespace cr
