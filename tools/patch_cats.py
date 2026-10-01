import os
root = os.path.join(os.path.dirname(__file__), '..', 'src')
def edit(name, pairs):
    p = os.path.join(root, name)
    s = open(p, encoding='utf-8').read()
    for old, new in pairs:
        assert old in s, (name, old[:60])
        s = s.replace(old, new, 1)
    open(p, 'w', encoding='utf-8').write(s)

# --- game.h: CatData body-part offsets
edit('game.h', [('''constexpr uintptr_t Cat_Coi       = 0xC50;  // double''', '''constexpr uintptr_t Cat_Coi       = 0xC50;  // double
// BodyParts at CatData+0x60: texture idx +0x18, heritable palette +0x1C, then
// 14 BodyPartDescriptors of 0x54 bytes from +0x2C (part sprite idx at +4):
// body head tail leg1 leg2 arm1 arm2 leye reye lbrow rbrow lear rear mouth.
constexpr uintptr_t Cat_Texture   = 0x60 + 0x18;
constexpr uintptr_t Cat_Palette   = 0x60 + 0x1C;
constexpr uintptr_t Cat_PartIdx(int part) { return 0x60 + 0x2C + part * 0x54 + 4; }''')])

# --- roster: CatLook in UnitInfo
edit('roster.h', [('#include <cstdint>\n', '#include "catportrait.h"\n\n#include <cstdint>\n'),
                  ('''    bool has_cat = false;       // CatData resolved''', '''    bool has_cat = false;       // CatData resolved
    CatLook look;               // face parts; look.palette = heritable row (panel swaps in the class row)''')])
edit('roster.cpp', [('''void read_cat(const void* cat, UnitInfo& u) {
    u.has_cat = true;''', '''void read_cat(const void* cat, UnitInfo& u) {
    u.has_cat = true;
    u.look.head  = rdv<int32_t>(cat, off::Cat_PartIdx(1));
    u.look.eye   = rdv<int32_t>(cat, off::Cat_PartIdx(7));
    u.look.brow  = rdv<int32_t>(cat, off::Cat_PartIdx(9));
    u.look.ear   = rdv<int32_t>(cat, off::Cat_PartIdx(11));
    u.look.mouth = rdv<int32_t>(cat, off::Cat_PartIdx(13));
    u.look.tex   = rdv<int32_t>(cat, off::Cat_Texture);
    u.look.palette = rdv<int32_t>(cat, off::Cat_Palette);''')])

# --- assets: palette.png, class palettes, cat renders
edit('assets.h', [('''// Cursor hotspot in texture pixels''', '''// A cat's face composed from catparts.swf (see catportrait.h).
struct CatLook;
Tex asset_cat(const CatLook& look, int px);
// palette.png row for a class in battle (classes.gon graphics.palette), or -1.
int class_palette(const std::string& cls);
// Cursor hotspot in texture pixels''')])
edit('assets.cpp', [('#include "assets.h"\n', '#include "assets.h"\n\n#include "catportrait.h"\n'),
    ('''    int kind = 0;            // 0 swf symbol, 1 ui.swf bitmap id, 2 png in the archive''',
     '''    int kind = 0;            // 0 swf symbol, 1 ui.swf bitmap id, 2 png in the archive, 3 cat face
    CatLook look;'''),
    ('''    std::unordered_map<std::string, std::pair<float, float>> hotspots;''',
     '''    std::unordered_map<std::string, std::pair<float, float>> hotspots;
    std::unordered_map<std::string, int> class_palettes;
    std::vector<uint8_t> palette;   // textures/palette.png, 256 rows x 16 RGB'''),
    ('''    load_gon_dir("data/classes/", [](const Gon& e) {
        if (const Gon* meta = e.get("meta")) g.classes[e.key] = {meta->str("name"), meta->str("description"), {}};''',
     '''    load_gon_dir("data/classes/", [](const Gon& e) {
        if (const Gon* gr = e.get("graphics")) {
            std::string p = gr->str("palette");
            if (!p.empty()) g.class_palettes[e.key] = atoi(p.c_str());
        }
        if (const Gon* meta = e.get("meta")) g.classes[e.key] = {meta->str("name"), meta->str("description"), {}};'''),
    ('''    build_maps();''', '''    build_maps();
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
    }'''),
    ('''            job.kind = it->second.kind; job.swf = it->second.swf; job.symbol = it->second.symbol;''',
     '''            job.kind = it->second.kind; job.swf = it->second.swf; job.symbol = it->second.symbol;
            job.look = it->second.look;'''),
    ('''            } else if (job.kind == 1) {''', '''            } else if (job.kind == 3) {
                int row = job.look.palette;
                if (!g.palette.empty() && row >= 0 && (size_t)row * 48 < g.palette.size()) {
                    SwfRenderOpts o = cat_face_opts(job.look, &g.palette[(size_t)row * 48]);
                    ok = g.catparts.render_ex("CatHeadPlacements", job.look.head - 1, job.px, o, im);
                }
            } else if (job.kind == 1) {'''),
    ('''Tex asset_bitmap(int id) { return request(1, Swf::Ui, "#bitmap", id, 0); }''',
     '''Tex asset_bitmap(int id) { return request(1, Swf::Ui, "#bitmap", id, 0); }

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
}'''),
])

# --- panel: real faces for cats
edit('panel.cpp', [('''    Swf swf;
    std::string sym = portrait_symbol(u, swf);
    dl->PushClipRect(ImVec2(c.x - r + 2 * s, c.y - r + 2 * s), ImVec2(c.x + r - 2 * s, c.y + r - 2 * s), true);
    if (sym.empty() || !image_fit(dl, swf, sym, 0, ImVec2(c.x - r * 0.95f, c.y - r * 0.95f),
                                  ImVec2(c.x + r * 0.95f, c.y + r * 0.95f), 112)) {''', '''    Swf swf;
    std::string sym = portrait_symbol(u, swf);
    dl->PushClipRect(ImVec2(c.x - r + 2 * s, c.y - r + 2 * s), ImVec2(c.x + r - 2 * s, c.y + r - 2 * s), true);
    bool drawn = false;
    if (u.has_cat && u.look.head > 0) {
        // The cat's own face, in its class colours like on the battlefield.
        CatLook look = u.look;
        int cp = class_palette(u.cls);
        if (cp >= 0) look.palette = cp;
        Tex t = asset_cat(look, 160);
        if (t.id) {
            float k = std::fmin(r * 1.9f / t.w, r * 1.75f / t.h);
            ImVec2 h(t.w * k * 0.5f, t.h * k * 0.5f);
            dl->AddImage((ImTextureID)t.id, ImVec2(c.x - h.x, c.y - h.y + r * 0.08f), ImVec2(c.x + h.x, c.y + h.y + r * 0.08f));
            drawn = true;
        }
    }
    if (!drawn && (sym.empty() || !image_fit(dl, swf, sym, 0, ImVec2(c.x - r * 0.95f, c.y - r * 0.95f),
                                  ImVec2(c.x + r * 0.95f, c.y + r * 0.95f), 112))) {''')])
print('ok')
