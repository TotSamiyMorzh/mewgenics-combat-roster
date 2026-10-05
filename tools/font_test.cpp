// Offline check of the SWF font loader: bakes glyphs through ImGui and dumps the atlas.
#include "fontloader.h"
#include "gpak.h"
#include "swf.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "stb_image_write.h"

#include <cstdio>

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    cr::GPak g;
    if (argc < 3 || !g.open(argv[1])) return 1;
    std::vector<uint8_t> buf;
    cr::SwfDoc intl;
    if (!g.read("swfs/international_fonts.swf", buf) || !intl.load(std::move(buf))) { printf("load failed\n"); return 1; }
    auto body = intl.font("TikaFontIntl");
    auto title = intl.font("Mewgenics Organ Grinder Cyr");
    printf("body %s glyphs=%zu asc=%.0f desc=%.0f\n", body ? body->name.c_str() : "-", body ? body->glyphs.size() : 0,
           body ? body->ascent : 0, body ? body->descent : 0);
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.DisplaySize = ImVec2(800, 600);
    std::vector<uint8_t> ub;
    cr::SwfDoc uni;
    std::shared_ptr<cr::SwfFont> cjk;
    if (g.read("swfs/unicodefont.swf", ub)) { uni.load(std::move(ub)); cjk = uni.font("Noto Sans CJK"); }
    printf("cjk %s glyphs=%zu asc=%.0f desc=%.0f\n", cjk ? cjk->name.c_str() : "-", cjk ? cjk->glyphs.size() : 0,
           cjk ? cjk->ascent : 0, cjk ? cjk->descent : 0);
    ImFont* f1 = cr::add_swf_font(body, 18.0f, cjk);
    ImFont* f2 = cr::add_swf_font(title, 18.0f, cjk);
    printf("fonts %p %p\n", (void*)f1, (void*)f2);
    io.FontDefault = f1;
    ImGui::NewFrame();
    ImGui::PushFont(f1, 36.0f);
    ImVec2 sz = ImGui::CalcTextSize("Элайда Мясник 40/40 Кровотечение");
    ImVec2 szc = ImGui::CalcTextSize("队伍 流血 パーティ 파티");
    for (ImWchar c : {(ImWchar)0x961F, (ImWchar)0x30D1, (ImWchar)0xD30C, (ImWchar)0x4E00})
        printf("glyph U+%04X %s\n", c, ImGui::GetFontBaked()->FindGlyphNoFallback(c) ? "found" : "MISSING");
    printf("cjk text size %.0fx%.0f\n", szc.x, szc.y);
    ImGui::PopFont();
    ImGui::PushFont(f2, 36.0f);
    ImVec2 sz2 = ImGui::CalcTextSize("Отряд Характеристики");
    ImGui::PopFont();
    printf("text sizes %.0fx%.0f  %.0fx%.0f\n", sz.x, sz.y, sz2.x, sz2.y);
    ImGui::Render();
    ImTextureData* t = io.Fonts->TexData;
    printf("atlas %dx%d fmt=%d\n", t->Width, t->Height, (int)t->Format);
    if (t->Format == ImTextureFormat_RGBA32)
        stbi_write_png(argv[2], t->Width, t->Height, 4, t->Pixels, t->Width * 4);
    else
        stbi_write_png(argv[2], t->Width, t->Height, 1, t->Pixels, t->Width);
    ImGui::DestroyContext();
}
