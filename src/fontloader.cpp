#include "fontloader.h"

#include "swf.h"

#include "imgui.h"
#include "imgui_internal.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cmath>
#include <vector>

namespace cr {
namespace {

std::vector<std::shared_ptr<SwfFont>> g_keep;   // fonts referenced by the atlas

SwfFont* font_of(ImFontConfig* src) { return (SwfFont*)src->FontLoaderData; }

// ImGui's "size" is a line height; like stb's ScaleForPixelHeight we map the
// font's ascent+descent onto it.
float unit_scale(const SwfFont* f, float size) {
    float h = f->ascent + f->descent;
    return size / (h > 0 ? h : SwfFont::kEm);
}

// The atlas copies FontData, so the font is found by its source name instead
// (also what makes a later atlas rebuild re-init correctly).
bool src_init(ImFontAtlas*, ImFontConfig* src) {
    for (auto& f : g_keep)
        if (f->name.compare(0, sizeof(src->Name) - 1, src->Name) == 0) { src->FontLoaderData = f.get(); return true; }
    return false;
}

void src_destroy(ImFontAtlas*, ImFontConfig* src) { src->FontLoaderData = nullptr; }

bool contains(ImFontAtlas*, ImFontConfig* src, ImWchar cp) {
    SwfFont* f = font_of(src);
    return f && f->index.count((uint32_t)cp) != 0;
}

bool baked_init(ImFontAtlas*, ImFontConfig* src, ImFontBaked* baked, void*) {
    SwfFont* f = font_of(src);
    if (!src->MergeMode) {
        float s = unit_scale(f, baked->Size);
        baked->Ascent = std::ceil(f->ascent * s);
        baked->Descent = -std::floor(f->descent * s);
    }
    return true;
}

bool load_glyph(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, void*, ImWchar cp, ImFontGlyph* out,
                float* out_advance) {
    SwfFont* f = font_of(src);
    auto it = f->index.find((uint32_t)cp);
    if (it == f->index.end()) return false;
    int gi = it->second;
    float scale = unit_scale(f, baked->Size);
    float density = src->RasterizerDensity * baked->RasterizerDensity;
    float advance = f->glyphs[gi].advance * scale;
    if (out_advance) { *out_advance = advance; return true; }

    out->Codepoint = cp;
    out->AdvanceX = advance;
    int w = 0, h = 0;
    float ox = 0, oy = 0;
    std::vector<uint8_t> alpha;
    if (!f->raster(gi, scale * density, w, h, ox, oy, alpha)) return true;   // advance only
    if (w == 0 || h == 0) return true;                                         // space

    ImFontAtlasRectId id = ImFontAtlasPackAddRect(atlas, w, h);
    if (id == ImFontAtlasRectId_Invalid) return false;
    ImTextureRect* r = ImFontAtlasPackGetRect(atlas, id);
    float inv = 1.0f / density;
    float y_base = std::round(baked->Ascent);
    out->X0 = ox * inv;
    out->Y0 = oy * inv + y_base;
    out->X1 = (ox + w) * inv;
    out->Y1 = (oy + h) * inv + y_base;
    out->Visible = true;
    out->PackId = id;
    ImFontAtlasBakedSetFontGlyphBitmap(atlas, baked, src, out, r, alpha.data(), ImTextureFormat_Alpha8, w);
    return true;
}

const ImFontLoader* loader() {
    static ImFontLoader l;
    if (!l.Name) {
        l.Name = "mewgenics_swf";
        l.FontSrcInit = src_init;
        l.FontSrcDestroy = src_destroy;
        l.FontSrcContainsGlyph = contains;
        l.FontBakedInit = baked_init;
        l.FontBakedLoadGlyph = load_glyph;
    }
    return &l;
}

}  // namespace

ImFont* add_swf_font(std::shared_ptr<SwfFont> font, float size_px) {
    if (!font || font->index.empty()) return nullptr;
    g_keep.push_back(font);
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.FontLoader = loader();
    cfg.SizePixels = size_px;
    ImFormatString(cfg.Name, IM_ARRAYSIZE(cfg.Name), "%s", font->name.c_str());
    ImFont* out = io.Fonts->AddFont(&cfg);
    if (!out) return nullptr;

    // Fallback for scripts the game font does not cover (CJK, symbols).
    char path[MAX_PATH];
    GetWindowsDirectoryA(path, MAX_PATH);
    strcat_s(path, "\\Fonts\\segoeuib.ttf");
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        ImFontConfig merge;
        merge.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(path, size_px, &merge);
    }
    return out;
}

}  // namespace cr
