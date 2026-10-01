#include "assets.h"

#include "catportrait.h"

#include "game.h"
#include "gon.h"
#include "gpak.h"
#include "log.h"
#include "swf.h"

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

#include <atomic>
#include <cstdlib>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace cr {
namespace {

struct Img {
    int kind = 0;            // 0 swf symbol, 1 ui.swf bitmap id, 2 png in the archive, 3 cat face
    CatLook look;
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
    SwfDoc ui, portraits, catparts, ability_icons;

    std::unordered_map<std::string, TextKeys> items, abilities, passives, classes, keywords;
    std::unordered_map<std::string, std::string> portrait_by_name;
    std::unordered_map<std::string, std::pair<float, float>> hotspots;
    std::unordered_map<std::string, int> class_palettes;
    std::vector<uint8_t> palette;   // textures/palette.png, 256 rows x 16 RGB
    std::shared_ptr<SwfFont> body_font, title_font;

    std::mutex mu;
    std::condition_variable cv;
    std::unordered_map<std::string, Img> imgs;   // key -> image
    std::deque<std::string> queue;

    // status icons, from the game's own unordered_map<string, StatusIconInfo>
    std::unordered_map<std::string, std::pair<int, int>> icons;
} g;

const SwfDoc& doc_of(Swf s) {
    switch (s) {
    case Swf::Portraits: return g.portraits;
    case Swf::CatParts: return g.catparts;
    case Swf::AbilityIcons: return g.ability_icons;
    default: return g.ui;
    }
}

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
        TextKeys k{e.str("name"), e.str("desc"), {}, e.str("ability"), {}, atoi(e.str("frame", "0").c_str())};
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
        k.base = base;
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
        if (const Gon* gr = e.get("graphics")) {
            std::string p = gr->str("palette");
            if (!p.empty()) g.class_palettes[e.key] = atoi(p.c_str());
        }
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
    if (g.gpak.read("textures/cursor/hotspots.gon", buf)) {
        Gon root = gon_parse((const char*)buf.data(), buf.size());
        for (const Gon& e : root.kids)
            if (e.is_arr && e.kids.size() >= 2)
                g.hotspots[e.key] = {(float)atof(e.kids[0].value.c_str()), (float)atof(e.kids[1].value.c_str())};
    }
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
    bool cp_ok = g.gpak.read("swfs/catparts.swf", buf) && g.catparts.load(std::move(buf));
    bool ai_ok = g.gpak.read("swfs/ability_icons.swf", buf) && g.ability_icons.load(std::move(buf));
    // Named instances the game shows/hides from code (slot hints, text labels).
    for (SwfDoc* d : {&g.ui, &g.portraits, &g.catparts, &g.ability_icons}) d->hide_instances({"sloticon", "label"});
    log_line("assets: catparts.swf %s, ability_icons.swf %s", cp_ok ? "ok" : "FAILED", ai_ok ? "ok" : "FAILED");
    build_maps();
    {
        std::vector<uint8_t> png;
        int w = 0, h = 0, n = 0;
        if (g.gpak.read("textures/palette.png", png))
            if (uint8_t* px = stbi_load_from_memory(png.data(), (int)png.size(), &w, &h, &n, 3)) {
                if (w == 16) g.palette.assign(px, px + (size_t)w * h * 3);
                stbi_image_free(px);
            }
        log_line("assets: palette %s (%zu rows), %zu class palettes", g.palette.empty() ? "MISSING" : "ok",
                 g.palette.size() / 48, g.class_palettes.size());
    }
    {
        SwfDoc intl;   // 86 MB; keep only the two fonts we use
        if (g.gpak.read("swfs/international_fonts.swf", buf) && intl.load(std::move(buf))) {
            g.body_font = intl.font("TikaFontIntl");
            g.title_font = intl.font("Mewgenics Organ Grinder Cyr");
        }
        log_line("assets: fonts body=%s title=%s", g.body_font ? "ok" : "MISSING", g.title_font ? "ok" : "MISSING");
    }
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
            job.kind = it->second.kind; job.swf = it->second.swf; job.symbol = it->second.symbol;
            job.look = it->second.look;
            job.frame = it->second.frame; job.px = it->second.px;
        }
        SwfImage im;
        bool ok = false;
        try {
            if (job.kind == 0) {
                ok = doc_of(job.swf).render(job.symbol, job.frame, job.px, im);
            } else if (job.kind == 3) {
                int row = job.look.palette;
                if (!g.palette.empty() && row >= 0 && (size_t)row * 48 < g.palette.size()) {
                    SwfRenderOpts o = cat_face_opts(job.look, &g.palette[(size_t)row * 48]);
                    ok = g.catparts.render_ex("CatHeadPlacements", job.look.head - 1, job.px, o, im);
                }
            } else if (job.kind == 1) {
                ok = g.ui.bitmap((uint16_t)job.frame, im);
            } else {
                std::vector<uint8_t> png;
                int w = 0, h = 0, n = 0;
                if (g.gpak.read(job.symbol, png)) {
                    if (uint8_t* px = stbi_load_from_memory(png.data(), (int)png.size(), &w, &h, &n, 4)) {
                        im.w = w; im.h = h; im.rgba.assign(px, px + (size_t)w * h * 4);
                        stbi_image_free(px);
                        ok = true;
                    }
                }
            }
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
    return doc_of(swf).has(symbol);
}

int asset_frame_of_label(Swf swf, const std::string& symbol, const std::string& label) {
    if (!g.ready) return -1;
    return doc_of(swf).frame_of_label(symbol, label);
}

namespace {
Tex request(int kind, Swf swf, const std::string& symbol, int frame, int px);
}

Tex asset_image(Swf swf, const std::string& symbol, int frame, int px) { return request(0, swf, symbol, frame, px); }
Tex asset_bitmap(int id) { return request(1, Swf::Ui, "#bitmap", id, 0); }

Tex asset_cat(const CatLook& look, int px) {
    if (!g.ready || look.head <= 0) return {};
    std::string key = "3|" + look.key() + "|" + std::to_string(px);
    std::lock_guard<std::mutex> lk(g.mu);
    auto it = g.imgs.find(key);
    if (it == g.imgs.end()) {
        Img im;
        im.kind = 3; im.look = look; im.px = px; im.symbol = "#cat";
        g.imgs.emplace(key, std::move(im));
        g.queue.push_back(key);
        g.cv.notify_one();
        return {};
    }
    Tex t;
    if (it->second.state == 2) { t.id = it->second.tex; t.w = (float)it->second.pixels.w; t.h = (float)it->second.pixels.h; }
    return t;
}

int class_palette(const std::string& cls) {
    auto it = g.class_palettes.find(cls);
    return it == g.class_palettes.end() ? -1 : it->second;
}
Tex asset_png(const std::string& path) { return request(2, Swf::Ui, path, 0, 0); }

void cursor_hotspot(const std::string& state, float& x, float& y) {
    auto it = g.hotspots.find(state);
    if (it == g.hotspots.end()) it = g.hotspots.find("default");
    if (it != g.hotspots.end()) { x = it->second.first; y = it->second.second; }
    else { x = 34; y = 7; }
}

std::shared_ptr<SwfFont> font_body() { return g.ready ? g.body_font : nullptr; }
std::shared_ptr<SwfFont> font_title() { return g.ready ? g.title_font : nullptr; }

namespace {
Tex request(int kind, Swf swf, const std::string& symbol, int frame, int px) {
    Tex t;
    if (!g.ready || symbol.empty()) return t;
    std::string key = std::to_string(kind) + "|" + img_key(swf, symbol, frame, px);
    std::lock_guard<std::mutex> lk(g.mu);
    auto it = g.imgs.find(key);
    if (it == g.imgs.end()) {
        Img im;
        im.kind = kind; im.swf = swf; im.symbol = symbol; im.frame = frame; im.px = px;
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
}  // namespace

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
