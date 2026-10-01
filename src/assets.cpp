#include "assets.h"

#include "game.h"
#include "gon.h"
#include "gpak.h"
#include "log.h"
#include "swf.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace cr {
namespace {

struct Img {
    Swf swf;
    std::string symbol;
    int frame, px;
    int state = 0;           // 0 queued, 1 rendered (pixels ready), 2 uploaded, -1 failed
    SwfImage pixels;
    GLuint tex = 0;
};

struct State {
    std::atomic<bool> ready{false};
    std::thread worker;
    GPak gpak;
    SwfDoc ui, portraits;

    std::unordered_map<std::string, TextKeys> items, abilities, passives, classes, keywords;
    std::unordered_map<std::string, std::string> portrait_by_name;

    std::mutex mu;
    std::condition_variable cv;
    std::unordered_map<std::string, Img> imgs;   // key -> image
    std::deque<std::string> queue;

    // status icons, from the game's own unordered_map<string, StatusIconInfo>
    std::unordered_map<std::string, std::pair<int, int>> icons;
} g;

std::string img_key(Swf s, const std::string& sym, int frame, int px) {
    return std::to_string((int)s) + "|" + sym + "|" + std::to_string(frame) + "|" + std::to_string(px);
}

// --- GON maps -----------------------------------------------------------------

void load_gon_dir(const char* prefix, void (*fn)(const Gon& entry)) {
    for (const std::string& f : g.gpak.list(prefix, ".gon")) {
        std::vector<uint8_t> buf;
        if (!g.gpak.read(f, buf)) continue;
        Gon root = gon_parse((const char*)buf.data(), buf.size());
        for (const Gon& e : root.kids) if (e.is_obj) fn(e);
    }
}

void build_maps() {
    load_gon_dir("data/items/", [](const Gon& e) {
        TextKeys k{e.str("name"), e.str("desc"), {}};
        if (!k.name.empty()) g.items[e.key] = k;
    });
    load_gon_dir("data/passives/", [](const Gon& e) {
        TextKeys k{e.str("name"), e.str("desc"), {}};
        if (!k.name.empty()) g.passives[e.key] = k;
    });
    load_gon_dir("data/abilities/", [](const Gon& e) {
        TextKeys k;
        if (const Gon* meta = e.get("meta")) { k.name = meta->str("name"); k.desc = meta->str("desc"); }
        // variant_of: inherit what the variant does not override
        std::string base = e.str("variant_of");
        if (!base.empty()) {
            auto it = g.abilities.find(base);
            if (it != g.abilities.end()) {
                if (k.name.empty()) k.name = it->second.name;
                if (k.desc.empty()) k.desc = it->second.desc;
            }
        }
        if (!k.name.empty() || !k.desc.empty()) g.abilities[e.key] = k;
    });
    load_gon_dir("data/classes/", [](const Gon& e) {
        if (const Gon* meta = e.get("meta")) g.classes[e.key] = {meta->str("name"), meta->str("description"), {}};
    });
    load_gon_dir("data/characters/", [](const Gon& e) {
        const Gon* gr = e.get("graphics");
        if (!gr) return;
        std::string name = gr->str("name");
        std::string portrait = gr->str("portrait");
        if (portrait.empty()) { std::string mc = gr->str("movieclip"); if (!mc.empty()) portrait = mc + "Portrait"; }
        if (!name.empty() && !portrait.empty() && !g.portrait_by_name.count(name)) g.portrait_by_name[name] = portrait;
    });

    std::vector<uint8_t> buf;
    if (g.gpak.read("data/keyword_tooltips.gon", buf)) {
        Gon root = gon_parse((const char*)buf.data(), buf.size());
        std::unordered_map<std::string, std::string> alias;
        for (const Gon& e : root.kids) {
            if (!e.is_obj) continue;
            std::string a = e.str("alias");
            if (!a.empty()) { alias[e.key] = a; continue; }
            TextKeys k;
            k.name = e.str("name");
            k.desc = e.str("tooltip", e.str("tooltip_stackless", e.str("tooltip_stacks_pos")));
            k.desc_stacks = e.str("tooltip_stacks", e.str("tooltip_stacks_pos", k.desc));
            if (k.desc == "none") k.desc.clear();
            if (k.desc_stacks == "none") k.desc_stacks.clear();
            g.keywords[e.key] = k;
        }
        for (auto& [from, to] : alias) {
            auto it = g.keywords.find(to);
            if (it != g.keywords.end()) g.keywords[from] = it->second;
        }
    }
}

// --- worker ---------------------------------------------------------------------

void worker_body(const std::string& game_dir);

// An exception escaping a std::thread calls std::terminate and would take the
// game down with it; the worker must never let one out.
void worker_main(std::string game_dir) {
    try {
        worker_body(game_dir);
    } catch (...) {
        log_line("!! assets worker stopped by an exception");
    }
}

void worker_body(const std::string& game_dir) {
    std::string path = game_dir + "\\resources.gpak";
    if (!g.gpak.open(path)) { log_line("!! assets: cannot open %s", path.c_str()); return; }
    std::vector<uint8_t> buf;
    bool ui_ok = g.gpak.read("swfs/ui.swf", buf) && g.ui.load(std::move(buf));
    bool pt_ok = g.gpak.read("swfs/portraits.swf", buf) && g.portraits.load(std::move(buf));
    build_maps();
    log_line("assets: ui.swf %s, portraits.swf %s; %zu items, %zu abilities, %zu passives, %zu classes, "
             "%zu keywords, %zu portraits",
             ui_ok ? "ok" : "FAILED", pt_ok ? "ok" : "FAILED", g.items.size(), g.abilities.size(),
             g.passives.size(), g.classes.size(), g.keywords.size(), g.portrait_by_name.size());
    g.ready = true;

    while (true) {
        std::string key;
        Img job;
        {
            std::unique_lock<std::mutex> lk(g.mu);
            g.cv.wait(lk, [] { return !g.queue.empty(); });
            key = g.queue.front();
            g.queue.pop_front();
            auto it = g.imgs.find(key);
            if (it == g.imgs.end()) continue;
            job.swf = it->second.swf; job.symbol = it->second.symbol;
            job.frame = it->second.frame; job.px = it->second.px;
        }
        const SwfDoc& doc = job.swf == Swf::Ui ? g.ui : g.portraits;
        SwfImage im;
        bool ok = false;
        try {
            ok = doc.render(job.symbol, job.frame, job.px, im);
        } catch (...) {
            ok = false;
        }
        std::lock_guard<std::mutex> lk(g.mu);
        Img& dst = g.imgs[key];
        if (ok) { dst.pixels = std::move(im); dst.state = 1; }
        else dst.state = -1;
    }
}

}  // namespace

void assets_start(const std::string& game_dir) {
    if (g.worker.joinable()) return;
    g.worker = std::thread(worker_main, game_dir);
    g.worker.detach();
}

bool assets_ready() { return g.ready; }

bool asset_has(Swf swf, const std::string& symbol) {
    if (!g.ready) return false;
    return (swf == Swf::Ui ? g.ui : g.portraits).has(symbol);
}

Tex asset_image(Swf swf, const std::string& symbol, int frame, int px) {
    Tex t;
    if (!g.ready || symbol.empty()) return t;
    std::string key = img_key(swf, symbol, frame, px);
    std::lock_guard<std::mutex> lk(g.mu);
    auto it = g.imgs.find(key);
    if (it == g.imgs.end()) {
        Img im;
        im.swf = swf; im.symbol = symbol; im.frame = frame; im.px = px;
        g.imgs.emplace(key, std::move(im));
        g.queue.push_back(key);
        g.cv.notify_one();
        return t;
    }
    if (it->second.state == 2) {
        t.id = it->second.tex;
        t.w = (float)it->second.pixels.w;
        t.h = (float)it->second.pixels.h;
    }
    return t;
}

void assets_upload_pending() {
    std::lock_guard<std::mutex> lk(g.mu);
    int budget = 16;
    for (auto& [key, im] : g.imgs) {
        if (im.state != 1 || budget-- <= 0) continue;
        GLint prev = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
        glGenTextures(1, &im.tex);
        glBindTexture(GL_TEXTURE_2D, im.tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);   // CLAMP_TO_EDGE
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, im.pixels.w, im.pixels.h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     im.pixels.rgba.data());
        glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
        im.state = 2;   // pixels kept so a lost context can re-upload
    }
}

void assets_gl_lost() {
    std::lock_guard<std::mutex> lk(g.mu);
    for (auto& [key, im] : g.imgs)
        if (im.state == 2) { im.tex = 0; im.state = 1; }   // names died with the old context
}

// --- data lookups ---------------------------------------------------------------

namespace {
const TextKeys* find(const std::unordered_map<std::string, TextKeys>& m, const std::string& id) {
    if (!g.ready) return nullptr;
    auto it = m.find(id);
    return it == m.end() ? nullptr : &it->second;
}
}  // namespace

const TextKeys* keys_item(const std::string& id) { return find(g.items, id); }
const TextKeys* keys_ability(const std::string& id) { return find(g.abilities, id); }
const TextKeys* keys_passive(const std::string& id) { return find(g.passives, id); }
const TextKeys* keys_class(const std::string& id) { return find(g.classes, id); }
const TextKeys* keys_keyword(const std::string& id) { return find(g.keywords, id); }

std::string portrait_for(const std::string& name_key) {
    if (!g.ready) return {};
    auto it = g.portrait_by_name.find(name_key);
    return it == g.portrait_by_name.end() ? std::string() : it->second;
}

// --- status icons -----------------------------------------------------------------
//
// glaiel::get_status_icon looks names up in a global std::unordered_map<string,
// StatusIconInfo>; StatusIconInfo starts {int frame_pos, int frame_neg, ...}
// with -1 meaning "no icon" and frames 1-based into ui.swf's StatusIcon clip.
// We read the table (never call into it -- the function consumes its argument).
// The map address comes from `lea rcx,[rip+map]` in that function.

namespace {
const Sig kSigIconMapRef = {"status icon map ref", 0x00493453, "4C 8B C7 48 8D 55 E7 48 8D 0D ? ? ? ? E8"};
}

bool status_icons_init() {
    if (!g.icons.empty()) return true;
    uintptr_t ref = resolve(kSigIconMapRef);
    if (!ref) { log_line("!! status icon table not found"); return false; }
    const uint8_t* lea = (const uint8_t*)ref + 7;
    const uint8_t* map = lea + 7 + rdv<int32_t>(lea, 3);
    // MSVC unordered_map: +8 list head (sentinel node), +16 size. Node: next, prev, value.
    const void* head = rdp(map, 8);
    uint64_t size = rdv<uint64_t>(map, 16);
    const void* node = rdp(head, 0);
    for (uint64_t i = 0; i < size && i < 5000 && node && node != head; ++i) {
        char key[96];
        int32_t info[2];
        if (read_string((const uint8_t*)node + 0x10, key, sizeof(key)) && rd(node, 0x30, info))
            g.icons[key] = {info[0], info[1]};
        node = rdp(node, 0);
    }
    log_line("status icons: %zu entries", g.icons.size());
    return !g.icons.empty();
}

int status_icon_frame(const std::string& status, bool negative) {
    auto it = g.icons.find(status);
    if (it == g.icons.end()) return -1;
    int f = negative && it->second.second >= 0 ? it->second.second : it->second.first;
    return f >= 1 ? f - 1 : -1;
}

}  // namespace cr
