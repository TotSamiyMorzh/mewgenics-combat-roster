#include "game.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstring>
#include <vector>

namespace cr {

uintptr_t g_base = 0;

// --- fault-safe reads -----------------------------------------------------------

bool read_bytes(const void* src, void* dst, size_t n) {
    if (!src || (uintptr_t)src < 0x10000) return false;
    __try {
        memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool read_string(const void* str, char* out, size_t out_size) {
    if (!out || out_size == 0) return false;
    out[0] = 0;
    uint64_t size = 0, cap = 0;
    if (!rd(str, 0x10, size) || !rd(str, 0x18, cap)) return false;
    if (size > 4096 || cap < size) return false;
    const void* data = cap > 15 ? rdp(str, 0) : str;
    size_t n = (size_t)size < out_size - 1 ? (size_t)size : out_size - 1;
    if (!read_bytes(data, out, n)) return false;
    out[n] = 0;
    return true;
}

bool read_wstring(const void* str, char* out_utf8, size_t out_size) {
    if (!out_utf8 || out_size == 0) return false;
    out_utf8[0] = 0;
    uint64_t size = 0, cap = 0;
    if (!rd(str, 0x10, size) || !rd(str, 0x18, cap)) return false;
    if (size > 1024 || cap < size) return false;
    const void* data = cap > 7 ? rdp(str, 0) : str;
    wchar_t tmp[1025];
    if (!read_bytes(data, tmp, (size_t)size * sizeof(wchar_t))) return false;
    tmp[size] = 0;
    int n = WideCharToMultiByte(CP_UTF8, 0, tmp, (int)size, out_utf8, (int)out_size - 1, nullptr, nullptr);
    out_utf8[n > 0 ? n : 0] = 0;
    return n > 0;
}

// MSVC x64 RTTI: vptr[-1] -> CompleteObjectLocator {sig=1, off, cdOff, rvaTD,
// rvaCHD, rvaSelf}; TypeDescriptor+0x10 is the decorated name ".?AVPoison@glaiel@@".
const char* rtti_name(const void* obj, char* buf, size_t buf_size) {
    if (!buf || buf_size < 2) return "?";
    strcpy_s(buf, buf_size, "?");
    const uint8_t* vptr = (const uint8_t*)rdp(obj, 0);
    const uint8_t* col  = (const uint8_t*)rdp(vptr, (uintptr_t)-8);
    uint32_t sig = 0, rva_td = 0, rva_self = 0;
    if (!rd(col, 0, sig) || sig != 1) return buf;
    if (!rd(col, 0x0C, rva_td) || !rd(col, 0x14, rva_self)) return buf;
    const uint8_t* image = col - rva_self;
    char raw[256] = {};
    if (!read_bytes(image + rva_td + 0x10, raw, sizeof(raw) - 1)) return buf;
    if (strncmp(raw, ".?A", 3) != 0) return buf;

    // ".?AVPoison@glaiel@@" -> parts {"Poison","glaiel"} -> "glaiel::Poison"
    const char* p = raw + 4;
    const char* parts[8];
    size_t lens[8];
    int np = 0;
    while (*p && *p != '@' && np < 8) {
        const char* q = strchr(p, '@');
        if (!q) break;
        parts[np] = p;
        lens[np] = (size_t)(q - p);
        ++np;
        p = q + 1;
    }
    size_t at = 0;
    buf[0] = 0;
    for (int i = np - 1; i >= 0; --i) {
        if (at + lens[i] + 3 >= buf_size) break;
        memcpy(buf + at, parts[i], lens[i]);
        at += lens[i];
        if (i > 0) { buf[at++] = ':'; buf[at++] = ':'; }
    }
    buf[at] = 0;
    if (at == 0) strcpy_s(buf, buf_size, "?");
    return buf;
}

// COL+0x10 -> ClassHierarchyDescriptor {sig, attr, numBases@+8, rvaBaseArray@+0xC};
// each BaseClassDescriptor starts with rvaTypeDescriptor.
bool rtti_is_a(const void* obj, const char* fragment) {
    const uint8_t* vptr = (const uint8_t*)rdp(obj, 0);
    const uint8_t* col  = (const uint8_t*)rdp(vptr, (uintptr_t)-8);
    uint32_t sig = 0, rva_chd = 0, rva_self = 0;
    if (!rd(col, 0, sig) || sig != 1) return false;
    if (!rd(col, 0x10, rva_chd) || !rd(col, 0x14, rva_self)) return false;
    const uint8_t* image = col - rva_self;
    const uint8_t* chd = image + rva_chd;
    uint32_t nbases = 0, rva_arr = 0;
    if (!rd(chd, 8, nbases) || !rd(chd, 0x0C, rva_arr) || nbases > 64) return false;
    for (uint32_t i = 0; i < nbases; ++i) {
        uint32_t rva_bcd = 0, rva_td = 0;
        if (!rd(image + rva_arr, i * 4, rva_bcd) || !rd(image + rva_bcd, 0, rva_td)) return false;
        char raw[128] = {};
        if (!read_bytes(image + rva_td + 0x10, raw, sizeof(raw) - 1)) return false;
        if (strstr(raw, fragment)) return true;
    }
    return false;
}

// --- signatures -------------------------------------------------------------------

namespace {

bool parse_pattern(const char* pat, std::vector<int>& out) {
    out.clear();
    for (const char* p = pat; *p;) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (*p == '?') {
            out.push_back(-1);
            while (*p == '?') ++p;
        } else {
            char hex[3] = {p[0], p[1], 0};
            if (!p[1]) return false;
            out.push_back((int)strtoul(hex, nullptr, 16));
            p += 2;
        }
    }
    return !out.empty();
}

bool text_section(uintptr_t base, const uint8_t*& start, size_t& size) {
    auto dos = (const IMAGE_DOS_HEADER*)base;
    auto nt  = (const IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (memcmp(sec->Name, ".text", 5) == 0) {
            start = (const uint8_t*)(base + sec->VirtualAddress);
            size  = sec->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

bool match_at(const uint8_t* p, const std::vector<int>& pat) {
    for (size_t i = 0; i < pat.size(); ++i)
        if (pat[i] >= 0 && p[i] != (uint8_t)pat[i]) return false;
    return true;
}

}  // namespace

uintptr_t resolve(const Sig& sig) {
    std::vector<int> pat;
    if (!g_base || !parse_pattern(sig.pattern, pat)) return 0;

    // Fast path: the pinned RVA still holds the pattern. Uniqueness is still
    // checked below, but only on a miss -- the patterns are minimal-unique on
    // this build by construction (mgmp gen_sigs.py), so a hit at the hint is it.
    const uint8_t* text = nullptr;
    size_t text_size = 0;
    if (!text_section(g_base, text, text_size)) return 0;
    const uint8_t* hint = (const uint8_t*)(g_base + sig.rva_hint);
    if (hint >= text && hint + pat.size() <= text + text_size && match_at(hint, pat))
        return (uintptr_t)hint;

    // Slow path (a game update moved it): the match must be unique.
    uintptr_t found = 0;
    for (size_t i = 0; i + pat.size() <= text_size; ++i) {
        if (match_at(text + i, pat)) {
            if (found) return 0;   // ambiguous -> refuse
            found = (uintptr_t)(text + i);
        }
    }
    return found;
}

bool make_str(GameStr& s, const char* text) {
    size_t n = strlen(text);
    if (n > 15) return false;
    memset(&s, 0, sizeof(s));
    memcpy(s.buf, text, n);
    s.size = n;
    s.cap  = 15;
    return true;
}

}  // namespace cr
