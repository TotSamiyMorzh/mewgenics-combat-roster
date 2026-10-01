// panel.cpp -- the roster panel, drawn in the game's own visual language:
// the game's watercolour paper (ui.swf bitmap) for cards, slanted hand-inked
// bars like the battle HUD, the game's portraits / heart / mana / stat / status
// icons, the game's fonts, and every name and description in the player's
// language from the game's StringsDatabase.
// Reads only the Roster snapshot, never game memory.
#include "panel.h"

#include "assets.h"
#include "loc.h"

#include "imgui.h"
#include "imgui_internal.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

namespace cr {
namespace {

// --- palette (sampled from the game's HUD) -----------------------------------
const ImU32 kInk       = IM_COL32(24, 20, 22, 255);
const ImU32 kInkSoft   = IM_COL32(96, 80, 66, 255);
const ImU32 kPaperTint = IM_COL32(255, 249, 234, 255);   // multiplies the grey paper
const ImU32 kAllyTint  = IM_COL32(212, 236, 200, 255);   // the game's ally tooltip green
const ImU32 kHpFill    = IM_COL32(184, 96, 96, 255);
const ImU32 kHpBack    = IM_COL32(58, 36, 38, 255);
const ImU32 kManaFill  = IM_COL32(110, 134, 186, 255);
const ImU32 kManaBack  = IM_COL32(34, 40, 60, 255);
const ImU32 kShield    = IM_COL32(170, 214, 222, 255);
const ImU32 kGold      = IM_COL32(242, 196, 64, 255);
const ImU32 kAlly      = IM_COL32(150, 196, 140, 255);
const ImU32 kBoss      = IM_COL32(190, 112, 108, 255);

constexpr int   kPaperBitmap = 1;      // ui.swf: 920x923 watercolour paper with an inked edge
constexpr float kWidth       = 300.0f; // unscaled
constexpr float kMaxHeight   = 0.66f;  // of the display height; the list scrolls past it

// --- the mod's own UI words (everything else comes from the game) -------------
struct Words {
    const char *party, *stats, *equip, *abilities, *passives, *mutations, *statuses, *charge, *per_fight,
        *off_board, *hide, *show, *drag, *opacity, *level, *resize;
};
const Words kRu = {"Отряд", "Характеристики", "Снаряжение", "Способности", "Пассивки", "Мутации", "Эффекты",
                   "зарядка", "за бой", "вне поля", "Спрятать отряд", "Показать отряд",
                   "Тяни, чтобы подвинуть", "Прозрачность", "ур.",
                   "Тяни, чтобы изменить высоту (двойной клик — авто)"};
const Words kEn = {"Party", "Stats", "Equipment", "Abilities", "Passives", "Mutations", "Effects", "charge",
                   "per fight", "off the board", "Hide party", "Show party", "Drag to move", "Opacity", "Lv",
                   "Drag to resize (double-click: auto)"};
const Words& W() {
    static std::string lang = loc_lang();
    return lang.rfind("ru", 0) == 0 ? kRu : kEn;
}

const char* kStatIcons[7] = {"FontIcon_str", "FontIcon_dex", "FontIcon_con", "FontIcon_int",
                             "FontIcon_spd", "FontIcon_cha", "FontIcon_lck"};

ImFont* g_title = nullptr;

// --- "hand-drawn" helpers -------------------------------------------------------

// Deterministic jitter so inked shapes wobble a little but do not flicker.
float jitter(uint32_t seed, int i) {
    uint32_t h = seed * 2654435761u ^ (uint32_t)i * 40503u;
    h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15;
    return (h & 0xFFFF) / 65535.0f - 0.5f;
}

void text_outlined(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const char* t, float o) {
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            if (dx || dy) dl->AddText(f, size, ImVec2(p.x + dx * o, p.y + dy * o), kInk, t);
    dl->AddText(f, size, p, col, t);
}

bool image_fit(ImDrawList* dl, Swf swf, const std::string& sym, int frame, ImVec2 a, ImVec2 b, int px = 96) {
    Tex t = asset_image(swf, sym, frame, px);
    if (!t.id) return false;
    float s = std::fmin((b.x - a.x) / t.w, (b.y - a.y) / t.h);
    ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), h(t.w * s * 0.5f, t.h * s * 0.5f);
    dl->AddImage((ImTextureID)t.id, ImVec2(c.x - h.x, c.y - h.y), ImVec2(c.x + h.x, c.y + h.y));
    return true;
}

void icon_inline(Swf swf, const std::string& sym, int frame, float size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    image_fit(ImGui::GetWindowDrawList(), swf, sym, frame, p, ImVec2(p.x + size, p.y + size), 64);
}

// The game's paper as a 9-slice: the inked edge keeps its thickness, the
// watercolour middle stretches. Falls back to a flat card until loaded.
void paper(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 tint, float s) {
    dl->AddRectFilled(ImVec2(a.x + 4 * s, a.y + 6 * s), ImVec2(b.x + 4 * s, b.y + 6 * s), IM_COL32(0, 0, 0, 80), 6 * s);
    Tex t = asset_bitmap(kPaperBitmap);
    if (!t.id) {
        dl->AddRectFilled(a, b, kInk, 6 * s);
        dl->AddRectFilled(ImVec2(a.x + 3 * s, a.y + 3 * s), ImVec2(b.x - 3 * s, b.y - 3 * s), IM_COL32(226, 220, 205, 255), 4 * s);
        return;
    }
    const float mt = 48.0f;                                   // texture margin
    float m = std::fmin(28.0f * s, std::fmin((b.x - a.x), (b.y - a.y)) * 0.45f);   // screen margin
    float xs[4] = {a.x, a.x + m, b.x - m, b.x}, ys[4] = {a.y, a.y + m, b.y - m, b.y};
    float us[4] = {0, mt / t.w, 1 - mt / t.w, 1}, vs[4] = {0, mt / t.h, 1 - mt / t.h, 1};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            dl->AddImage((ImTextureID)t.id, ImVec2(xs[i], ys[j]), ImVec2(xs[i + 1], ys[j + 1]), ImVec2(us[i], vs[j]),
                         ImVec2(us[i + 1], vs[j + 1]), tint);
}

// A slanted, hand-inked bar like the battle HUD's health/mana bars.
void hud_bar(ImDrawList* dl, ImVec2 p, ImVec2 sz, float frac, ImU32 back, ImU32 fill, const char* icon, const char* text,
             float s, uint32_t seed, float extra = 0.0f) {
    frac = std::fmax(0.0f, std::fmin(1.0f, frac));
    float sk = sz.y * 0.45f;   // slant
    auto quad = [&](float x0, float x1, float grow, int k, ImU32 col) {
        ImVec2 q[4] = {ImVec2(p.x + sk + x0 - grow, p.y - grow), ImVec2(p.x + sk + x1 + grow, p.y - grow),
                       ImVec2(p.x + x1 + grow, p.y + sz.y + grow), ImVec2(p.x + x0 - grow, p.y + sz.y + grow)};
        for (int i = 0; i < 4; ++i) { q[i].x += jitter(seed + k, i * 2) * 1.6f * s; q[i].y += jitter(seed + k, i * 2 + 1) * 1.2f * s; }
        dl->AddConvexPolyFilled(q, 4, col);
    };
    quad(0, sz.x - sk, 2.4f * s, 0, kInk);
    quad(0, sz.x - sk, 0, 1, back);
    if (frac > 0) {
        float w = (sz.x - sk) * frac;
        quad(0, w, 0, 2, fill);
        // a lighter band along the top, like the HUD's painted highlight
        ImU32 hi = (fill & 0x00FFFFFF) | 0x40000000;
        dl->AddRectFilled(ImVec2(p.x + sk * 0.8f, p.y + 1.5f * s), ImVec2(p.x + sk * 0.6f + w, p.y + sz.y * 0.32f), IM_COL32(255, 255, 255, 46));
        (void)hi;
    }
    if (extra > 0) {
        float w = (sz.x - sk) * std::fmin(1.0f, extra);
        dl->AddRectFilled(ImVec2(p.x + sk * 0.9f, p.y + 1 * s), ImVec2(p.x + sk * 0.9f + w, p.y + sz.y * 0.35f), kShield);
    }
    if (icon) {
        float is = sz.y * 2.0f;
        image_fit(dl, Swf::Ui, icon, 0, ImVec2(p.x - is * 0.62f, p.y + sz.y * 0.5f - is * 0.5f),
                  ImVec2(p.x + is * 0.38f, p.y + sz.y * 0.5f + is * 0.5f), 64);
    }
    if (text) {
        ImFont* f = ImGui::GetFont();
        float fs = sz.y * 1.05f;
        ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, text);
        text_outlined(dl, f, fs, ImVec2(p.x + (sz.x - ts.x) * 0.5f + sk * 0.3f, p.y + (sz.y - ts.y) * 0.5f), IM_COL32_WHITE,
                      text, 1.2f * s);
    }
}

// An inked ring with a slightly uneven thickness.
void ink_ring(ImDrawList* dl, ImVec2 c, float r, float thick, ImU32 col, uint32_t seed) {
    const int N = 36;
    for (int i = 0; i < N; ++i) {
        float a0 = 6.2831853f * i / N, a1 = 6.2831853f * (i + 1) / N;
        float t0 = thick * (1.0f + 0.35f * jitter(seed, i)), t1 = thick * (1.0f + 0.35f * jitter(seed, (i + 1) % N));
        ImVec2 q[4] = {ImVec2(c.x + cosf(a0) * (r - t0 * 0.5f), c.y + sinf(a0) * (r - t0 * 0.5f)),
                       ImVec2(c.x + cosf(a1) * (r - t1 * 0.5f), c.y + sinf(a1) * (r - t1 * 0.5f)),
                       ImVec2(c.x + cosf(a1) * (r + t1 * 0.5f), c.y + sinf(a1) * (r + t1 * 0.5f)),
                       ImVec2(c.x + cosf(a0) * (r + t0 * 0.5f), c.y + sinf(a0) * (r + t0 * 0.5f))};
        dl->AddConvexPolyFilled(q, 4, col);
    }
}

// A hand-drawn rule: a wavy ink line.
void ink_rule(ImDrawList* dl, float x0, float x1, float y, float s, uint32_t seed, ImU32 col) {
    const int N = 12;
    ImVec2 pts[N + 1];
    for (int i = 0; i <= N; ++i) pts[i] = ImVec2(x0 + (x1 - x0) * i / N, y + jitter(seed, i) * 1.6f * s);
    dl->AddPolyline(pts, N + 1, col, 0, 1.6f * s);
}

// --- names ----------------------------------------------------------------------------

std::string tr_name(const TextKeys* k) {
    if (!k || k->name.empty() || k->name.find('{') != std::string::npos) return {};
    return tr(k->name);
}
std::string or_id(std::string s, const char* id) { return s.empty() ? std::string(id) : s; }
std::string item_name(const char* id) { return or_id(tr_name(keys_item(id)), id); }
std::string passive_name(const char* id) { return or_id(tr_name(keys_passive(id)), id); }
std::string status_name(const char* id) { return or_id(tr_name(keys_keyword(id)), id); }
std::string class_name(const char* id) { return tr_name(keys_class(id)); }

// Item abilities are named "{itemname}" in their GON: the item granting them
// supplies the name, exactly as the game shows it.
std::string ability_name(const AbilityInfo& a, const UnitInfo& u) {
    std::string n = tr_name(keys_ability(a.id));
    if (!n.empty()) return n;
    for (auto& e : u.equip) {
        const TextKeys* ik = e[0] ? keys_item(e) : nullptr;
        if (ik && ik->ability == a.id) return item_name(e);
    }
    return {};   // internal ability with no name in the game either: not shown
}

void wrapped_dim(const std::string& t);

// --- icons for list entries --------------------------------------------------------------

struct Icon { Swf swf; std::string sym; int frame = -1; };

const char* kSlotIcon[5] = {"HeadItemIcon", "FaceItemIcon", "NeckItemIcon", "WeaponIcon", "TrinketIcon"};

Icon item_icon(int slot, const char* id) {
    const TextKeys* k = keys_item(id);
    if (!k || k->frame <= 0 || slot < 0 || slot > 4) return {};
    return {Swf::CatParts, kSlotIcon[slot], k->frame - 1};
}

Icon labelled(const char* sym, const char* id) {
    int f = asset_frame_of_label(Swf::AbilityIcons, sym, id);
    return f >= 0 ? Icon{Swf::AbilityIcons, sym, f} : Icon{};
}

Icon ability_icon(const AbilityInfo& a, const UnitInfo& u) {
    Icon ic = labelled("AbilityIcon", a.id);
    if (ic.frame >= 0) return ic;
    if (const TextKeys* k = keys_ability(a.id); k && !k->base.empty()) {
        ic = labelled("AbilityIcon", k->base.c_str());
        if (ic.frame >= 0) return ic;
    }
    for (int i = 0; i < 5; ++i) {   // item abilities show their item
        const TextKeys* ik = u.equip[i][0] ? keys_item(u.equip[i]) : nullptr;
        if (ik && ik->ability == a.id) return item_icon(i, u.equip[i]);
    }
    return {};
}

Icon passive_icon(const char* id) { return labelled("PassiveIcon", id); }

// An icon-led entry: a big icon on the left, the name and (optional)
// description in a column beside it, both vertically centred on the icon.
void icon_entry(const Icon& ic, const std::string& name, const std::string& desc, float s,
                const std::function<void()>& after_name = nullptr) {
    float fs = ImGui::GetFontSize(), sz = fs * 2.3f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    float y0 = ImGui::GetCursorPosY();
    ImGui::Dummy(ImVec2(sz, sz));
    if (ic.frame >= 0) image_fit(ImGui::GetWindowDrawList(), ic.swf, ic.sym, ic.frame, p, ImVec2(p.x + sz, p.y + sz), 96);
    else ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + sz * 0.5f, p.y + sz * 0.5f), 3.0f * s, kInk);
    ImGui::SameLine(0, 8 * s);
    // Measure the text block to centre it on the icon when it is shorter.
    float wrap = ImGui::GetContentRegionAvail().x;
    float th = fs;
    if (!desc.empty()) th += ImGui::GetStyle().ItemSpacing.y + ImGui::CalcTextSize(desc.c_str(), nullptr, false, wrap).y;
    ImGui::SetCursorPosY(y0 + std::fmax(0.0f, (sz - th) * 0.5f));
    ImGui::BeginGroup();
    ImGui::TextUnformatted(name.c_str());
    if (after_name) after_name();   // e.g. mana cost, on the name's line
    wrapped_dim(desc);
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, 2 * s));
}
void icon_line(const Icon& ic, const std::string& text, float s) { icon_entry(ic, text, {}, s); }

std::string status_desc(const StatusInfo& s) {
    const TextKeys* k = keys_keyword(s.id);
    if (!k) return {};
    return with_stacks(tr(s.stacks != 0 && !k->desc_stacks.empty() ? k->desc_stacks : k->desc), s.stacks);
}

bool visible_status(const StatusInfo& s) { return status_icon_frame(s.id, s.stacks < 0) >= 0; }

std::string portrait_symbol(const UnitInfo& u, Swf& swf) {
    swf = Swf::Portraits;
    std::string p = portrait_for(u.name_key);
    if (!p.empty() && asset_has(Swf::Portraits, p)) return p;
    if (u.cls[0]) {
        std::string c = u.cls;
        if (c == "Necromancer") c = "Necro";
        if (asset_has(Swf::Portraits, c + "CatPortrait")) return c + "CatPortrait";
        swf = Swf::Ui;
        if (asset_has(Swf::Ui, "FontIcon_" + std::string(u.cls))) return "FontIcon_" + std::string(u.cls);
    }
    swf = Swf::Portraits;
    return asset_has(Swf::Portraits, "KittenPortrait") ? "KittenPortrait" : std::string();
}

// --- tooltip ----------------------------------------------------------------------------

void section(const char* title, uint32_t seed, float s) {
    ImGui::Dummy(ImVec2(0, 3 * s));
    if (g_title) ImGui::PushFont(g_title, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, kInkSoft);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    if (g_title) ImGui::PopFont();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ink_rule(ImGui::GetWindowDrawList(), p.x, p.x + ImGui::GetContentRegionAvail().x, p.y, s, seed, IM_COL32(96, 80, 66, 150));
    ImGui::Dummy(ImVec2(0, 4 * s));
}

void wrapped_dim(const std::string& t) {
    if (t.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(78, 70, 62, 255));
    ImGui::TextWrapped("%s", t.c_str());
    ImGui::PopStyleColor();
}

// A tooltip on the game's paper. Contents go on channel 1, the paper on 0.
// Unit tooltips can be taller than the screen: they are capped and scroll with
// the mouse wheel while the pointer stays on the unit's row (the row owns the
// wheel, so the party list underneath does not scroll at the same time).
float g_tt_wheel = 0.0f;       // wheel delta to apply to the unit tooltip this frame
bool  g_tt_reset = false;      // a different unit: start at the top

template <class F>
void paper_tooltip(float s, ImU32 tint, float wrap_em, F body, bool scrollable = false) {
    ImGui::PushStyleColor(ImGuiCol_PopupBg, 0);
    ImGui::PushStyleColor(ImGuiCol_Border, 0);
    ImGui::PushStyleColor(ImGuiCol_Text, kInk);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, 0);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, IM_COL32(96, 80, 66, 150));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * s, 16 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6 * s);
    if (scrollable)
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, ImGui::GetIO().DisplaySize.y - 24 * s));
    ImGui::BeginTooltip();
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (scrollable) {
        if (g_tt_reset) ImGui::SetScrollY(0.0f);
        else if (g_tt_wheel != 0.0f)
            ImGui::SetScrollY(std::fmax(0.0f, std::fmin(ImGui::GetScrollMaxY(), ImGui::GetScrollY() - g_tt_wheel * ImGui::GetFontSize() * 4)));
    }
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * wrap_em);
    body();
    ImGui::PopTextWrapPos();
    // "There is more below": a small inked arrow at the bottom edge.
    if (scrollable && ImGui::GetScrollY() < ImGui::GetScrollMaxY() - 1.0f) {
        float cx = win->Pos.x + win->Size.x * 0.5f, by = win->Pos.y + win->Size.y - 10 * s, a = 7 * s;
        ImDrawList* fdl = ImGui::GetWindowDrawList();
        fdl->PushClipRectFullScreen();
        fdl->AddTriangleFilled(ImVec2(cx - a, by - a * 0.6f), ImVec2(cx + a, by - a * 0.6f), ImVec2(cx, by + a * 0.4f), kInk);
        fdl->PopClipRect();
    }
    dl->ChannelsSetCurrent(0);
    dl->PushClipRectFullScreen();
    paper(dl, win->Pos, ImVec2(win->Pos.x + win->Size.x, win->Pos.y + win->Size.y), tint, s);
    dl->PopClipRect();
    dl->ChannelsMerge();
    ImGui::EndTooltip();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
}

void unit_tooltip(const UnitInfo& u, float s) {
    const Words& w = W();
    float icon = ImGui::GetFontSize() * 1.3f;
    uint32_t seed = (uint32_t)(uintptr_t)u.ch;
    paper_tooltip(s, kAllyTint, 22.0f, [&] {
        if (g_title) ImGui::PushFont(g_title, ImGui::GetFontSize() * 1.35f);
        ImGui::TextUnformatted(u.name[0] ? u.name : u.cls);
        if (g_title) ImGui::PopFont();
        std::string cls = class_name(u.cls);
        if (!cls.empty() || u.level > 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, kInkSoft);
            if (!cls.empty() && u.level > 0) ImGui::Text("%s  •  %s %d", cls.c_str(), w.level, u.level);
            else if (!cls.empty()) ImGui::TextUnformatted(cls.c_str());
            else ImGui::Text("%s %d", w.level, u.level);
            ImGui::PopStyleColor();
        }
        if (!u.has_cat) wrapped_dim(tr(u.desc_key));

        ImGui::Dummy(ImVec2(0, 2 * s));
        icon_inline(Swf::Ui, "HealthIcon", 0, icon);
        ImGui::SameLine();
        ImGui::Text("%d / %d", u.hp, u.max_hp);
        if (u.shield > 0) {
            ImGui::SameLine(0, 14 * s);
            icon_inline(Swf::Ui, "RawFontIcon_shield", 0, icon);
            ImGui::SameLine();
            ImGui::Text("%d", u.shield);
        }
        if (u.max_mana > 0) {
            ImGui::SameLine(0, 14 * s);
            icon_inline(Swf::Ui, "ManaIcon", 0, icon);
            ImGui::SameLine();
            ImGui::Text("%d / %d", u.mana, u.max_mana);
        }

        int sec = 0;
        if (u.has_cat) {
            section(w.stats, seed + ++sec, s);
            for (int i = 0; i < 7; ++i) {
                if (i) ImGui::SameLine(0, 9 * s);
                icon_inline(Swf::Ui, kStatIcons[i], 0, icon);
                ImGui::SameLine(0, 2 * s);
                ImGui::Text("%d", u.stats[i]);
            }
            bool any = false;
            for (auto& e : u.equip) any |= e[0] != 0;
            if (any) {
                section(w.equip, seed + ++sec, s);
                for (int slot = 0; slot < 5; ++slot) {
                    const char* e = u.equip[slot];
                    if (!e[0]) continue;
                    const TextKeys* k = keys_item(e);
                    icon_entry(item_icon(slot, e), item_name(e), k ? tr(k->desc) : std::string(), s);
                }
            }
            if (u.passives[0][0] || u.passives[1][0]) {
                section(w.passives, seed + ++sec, s);
                for (auto& p : u.passives) {
                    if (!p[0]) continue;
                    const TextKeys* k = keys_passive(p);
                    icon_entry(passive_icon(p), passive_name(p), k ? tr(k->desc) : std::string(), s);
                }
            }
            if (u.mutations[0][0] || u.mutations[1][0]) {
                section(w.mutations, seed + ++sec, s);
                for (auto& m : u.mutations) if (m[0]) icon_line(passive_icon(m), passive_name(m), s);
            }
        }
        int named = 0;
        for (int i = 0; i < u.n_abilities; ++i) named += !ability_name(u.abilities[i], u).empty();
        if (named > 0) {
            section(w.abilities, seed + ++sec, s);
            for (int i = 0; i < u.n_abilities; ++i) {
                const AbilityInfo& a = u.abilities[i];
                std::string an = ability_name(a, u);
                if (an.empty()) continue;
                icon_entry(ability_icon(a, u), an, {}, s, [&] {
                    if (a.mana_cost > 0) {
                        ImGui::SameLine(0, 8 * s);
                        icon_inline(Swf::Ui, "ManaIcon", 0, ImGui::GetFontSize());
                        ImGui::SameLine(0, 2 * s);
                        ImGui::Text("%d", a.mana_cost);
                    }
                    ImGui::PushStyleColor(ImGuiCol_Text, kInkSoft);
                    if (a.charge > 0) { ImGui::SameLine(); ImGui::Text("(%s %d)", w.charge, a.charge); }
                    if (a.uses_per_fight > 0) { ImGui::SameLine(); ImGui::Text("(%d %s)", a.uses_per_fight, w.per_fight); }
                    ImGui::PopStyleColor();
                });
            }
        }
        bool any_status = false;
        for (int i = 0; i < u.n_statuses; ++i) any_status |= visible_status(u.statuses[i]);
        if (any_status) {
            section(w.statuses, seed + ++sec, s);
            for (int i = 0; i < u.n_statuses; ++i) {
                const StatusInfo& st = u.statuses[i];
                if (!visible_status(st)) continue;
                std::string nm = status_name(st.id);
                if (st.stacks > 1 || st.stacks < 0) nm += "  x" + std::to_string(st.stacks);
                icon_entry(Icon{Swf::Ui, "StatusIcon", status_icon_frame(st.id, st.stacks < 0)}, nm, status_desc(st), s);
            }
        }
        if (!u.on_board) wrapped_dim(std::string("(") + w.off_board + ")");
    }, true);
}

void small_tooltip(const char* title, const std::string& body, float s) {
    paper_tooltip(s, kPaperTint, 18.0f, [&] {
        if (g_title) ImGui::PushFont(g_title, 0.0f);
        ImGui::TextUnformatted(title);
        if (g_title) ImGui::PopFont();
        wrapped_dim(body);
    });
}

// --- one unit row --------------------------------------------------------------------------

bool unit_row(const UnitInfo& u, int idx, float s, float inner_w) {
    ImGui::PushID(idx);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* f = ImGui::GetFont();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float pic = 56 * s, bar_h = 15 * s, icon = 24 * s;
    uint32_t seed = (uint32_t)idx * 7919u + 17u;

    int n_icons = 0;
    for (int i = 0; i < u.n_statuses; ++i) n_icons += visible_status(u.statuses[i]);
    float row_h = pic + (n_icons ? icon + 4 * s : 0);

    ImGui::InvisibleButton("row", ImVec2(inner_w, row_h));
    bool row_hover = ImGui::IsItemHovered();
    if (row_hover) {
        // The wheel scrolls this unit's tooltip, not the party list.
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        static const void* last_unit = nullptr;
        g_tt_reset = last_unit != u.ch;
        last_unit = u.ch;
        g_tt_wheel = ImGui::GetIO().MouseWheel;
    }
    if (row_hover) {
        ImVec2 q[4] = {ImVec2(p0.x - 6 * s, p0.y - 3 * s), ImVec2(p0.x + inner_w + 4 * s, p0.y - 4 * s),
                       ImVec2(p0.x + inner_w + 6 * s, p0.y + row_h + 3 * s), ImVec2(p0.x - 4 * s, p0.y + row_h + 4 * s)};
        dl->AddConvexPolyFilled(q, 4, IM_COL32(242, 196, 64, 70));
    }

    // Portrait: the game's portrait art on a coloured disc inside an inked ring.
    ImVec2 c(p0.x + pic * 0.5f, p0.y + pic * 0.5f);
    float r = pic * 0.5f;
    dl->AddCircleFilled(c, r - 1 * s, u.kind == 2 ? kBoss : kAlly, 40);
    Swf swf;
    std::string sym = portrait_symbol(u, swf);
    dl->PushClipRect(ImVec2(c.x - r + 2 * s, c.y - r + 2 * s), ImVec2(c.x + r - 2 * s, c.y + r - 2 * s), true);
    bool drawn = false;
    if (u.has_cat && u.look.head > 0) {
        // The cat's own face, in its class colours like on the battlefield.
        CatLook look = u.look;
        int cp = class_palette(u.cls);
        if (cp >= 0) look.palette = cp;
        Tex t = asset_cat(look, 480);   // rendered large: the face is ~1/3 of the canvas before cropping
        if (t.id) {
            float k = std::fmin(r * 2.3f / t.w, r * 2.1f / t.h);
            ImVec2 h(t.w * k * 0.5f, t.h * k * 0.5f);
            dl->AddImage((ImTextureID)t.id, ImVec2(c.x - h.x, c.y - h.y + r * 0.08f), ImVec2(c.x + h.x, c.y + h.y + r * 0.08f));
            drawn = true;
        }
    }
    if (!drawn && (sym.empty() || !image_fit(dl, swf, sym, 0, ImVec2(c.x - r * 0.95f, c.y - r * 0.95f),
                                  ImVec2(c.x + r * 0.95f, c.y + r * 0.95f), 112))) {
        char ini[8] = {};
        const char* nm = u.name[0] ? u.name : "?";
        size_t n = (unsigned char)nm[0] >= 0xE0 ? 3 : (unsigned char)nm[0] >= 0xC0 ? 2 : 1;
        memcpy(ini, nm, n);
        ImVec2 ts = f->CalcTextSizeA(pic * 0.5f, FLT_MAX, 0, ini);
        text_outlined(dl, f, pic * 0.5f, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32_WHITE, ini, 1.5f * s);
    }
    dl->PopClipRect();
    ink_ring(dl, c, r - 1.5f * s, (u.is_current ? 4.5f : 3.2f) * s, u.is_current ? kGold : kInk, seed);
    if (u.is_current) ink_ring(dl, c, r + 1.5f * s, 1.4f * s, kInk, seed + 3);

    // Name + bars.
    float x = p0.x + pic + 18 * s, wbar = inner_w - pic - 20 * s;
    dl->PushClipRect(ImVec2(x - 10 * s, p0.y - 2 * s), ImVec2(p0.x + inner_w, p0.y + pic), true);
    ImFont* nf = g_title ? g_title : f;
    dl->AddText(nf, ImGui::GetFontSize() * 1.05f, ImVec2(x - 8 * s, p0.y - 2 * s), u.is_current ? IM_COL32(150, 98, 8, 255) : kInk,
                u.name[0] ? u.name : u.cls);
    dl->PopClipRect();

    char txt[48];
    if (u.shield > 0) sprintf_s(txt, "%d/%d  +%d", u.hp, u.max_hp, u.shield);
    else sprintf_s(txt, "%d/%d", u.hp, u.max_hp);
    float y = p0.y + ImGui::GetFontSize() * 1.15f + 4 * s;
    hud_bar(dl, ImVec2(x, y), ImVec2(wbar, bar_h), u.max_hp ? (float)u.hp / u.max_hp : 0, kHpBack, kHpFill, "HealthIcon",
            txt, s, seed, u.max_hp ? (float)u.shield / u.max_hp : 0);
    if (u.max_mana > 0) {
        y += bar_h + 8 * s;
        sprintf_s(txt, "%d/%d", u.mana, u.max_mana);
        hud_bar(dl, ImVec2(x - 3 * s, y), ImVec2(wbar * 0.94f, bar_h * 0.85f), (float)u.mana / u.max_mana, kManaBack,
                kManaFill, "ManaIcon", txt, s, seed + 101);
    }

    bool icon_hover = false;
    if (n_icons) {
        float ix = p0.x + 2 * s, iy = p0.y + pic + 4 * s;
        for (int i = 0; i < u.n_statuses; ++i) {
            const StatusInfo& st = u.statuses[i];
            int fr = status_icon_frame(st.id, st.stacks < 0);
            if (fr < 0) continue;
            if (ix + icon > p0.x + inner_w) break;
            ImVec2 a(ix, iy), b(ix + icon, iy + icon);
            image_fit(dl, Swf::Ui, "StatusIcon", fr, a, b, 64);
            if (st.stacks > 1 || st.stacks < 0) {
                char n[8];
                sprintf_s(n, "%d", st.stacks);
                float fs = icon * 0.6f;
                ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, n);
                text_outlined(dl, f, fs, ImVec2(b.x - ts.x + 2 * s, b.y - ts.y + 3 * s), IM_COL32_WHITE, n, 1.0f * s);
            }
            if (ImGui::IsMouseHoveringRect(a, b)) {
                icon_hover = true;
                small_tooltip(status_name(st.id).c_str(), status_desc(st), s);
            }
            ix += icon + 4 * s;
        }
    }

    if (row_hover && !icon_hover) unit_tooltip(u, s);
    ImGui::PopID();
    return row_hover;
}

void save(const PanelState& st) {
    if (st.ini_path.empty()) return;
    char v[32];
    sprintf_s(v, "%.4f", st.fx);
    WritePrivateProfileStringA("panel", "x", v, st.ini_path.c_str());
    sprintf_s(v, "%.4f", st.fy);
    WritePrivateProfileStringA("panel", "y", v, st.ini_path.c_str());
    sprintf_s(v, "%.2f", st.opacity);
    WritePrivateProfileStringA("panel", "opacity", v, st.ini_path.c_str());
    sprintf_s(v, "%.4f", st.list_h);
    WritePrivateProfileStringA("panel", "height", v, st.ini_path.c_str());
    WritePrivateProfileStringA("panel", "collapsed", st.collapsed ? "1" : "0", st.ini_path.c_str());
}

// Multiplies the alpha of everything the given windows drew this frame.
void fade_windows(ImGuiWindow* root, float a) {
    if (!root || a >= 0.999f) return;
    ImGuiContext& g = *GImGui;
    for (ImGuiWindow* w : g.Windows) {
        if (!w->Active || w->RootWindow != root) continue;
        for (ImDrawVert& v : w->DrawList->VtxBuffer) {
            uint32_t al = (v.col >> IM_COL32_A_SHIFT) & 0xFF;
            v.col = (v.col & ~IM_COL32_A_MASK) | ((uint32_t)(al * a + 0.5f) << IM_COL32_A_SHIFT);
        }
    }
}

}  // namespace

void panel_load(PanelState& st, const std::string& game_dir) {
    st.ini_path = game_dir + "\\mods\\combat_roster.ini";
    char v[32];
    GetPrivateProfileStringA("panel", "x", "-1", v, sizeof(v), st.ini_path.c_str());
    st.fx = (float)atof(v);
    GetPrivateProfileStringA("panel", "y", "0.20", v, sizeof(v), st.ini_path.c_str());
    st.fy = (float)atof(v);
    GetPrivateProfileStringA("panel", "opacity", "1.0", v, sizeof(v), st.ini_path.c_str());
    st.opacity = std::fmax(0.3f, std::fmin(1.0f, (float)atof(v)));
    GetPrivateProfileStringA("panel", "height", "0", v, sizeof(v), st.ini_path.c_str());
    st.list_h = std::fmax(0.0f, std::fmin(0.95f, (float)atof(v)));
    st.collapsed = GetPrivateProfileIntA("panel", "collapsed", 0, st.ini_path.c_str()) != 0;
    st.slide = st.collapsed ? 0.0f : 1.0f;
    st.loaded = true;
}

const void* panel_draw(const Roster& r, PanelState& st, float dt, float s) {
    const Words& w = W();
    g_title = st.title_font;
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 disp = io.DisplaySize;
    float width = kWidth * s, tab = 26 * s;

    float k = dt * 10.0f;
    st.slide += ((st.collapsed ? 0.0f : 1.0f) - st.slide) * (k > 1 ? 1 : k);
    if (st.slide < 0.002f) st.slide = 0.0f;
    if (st.slide > 0.998f) st.slide = 1.0f;

    float rx = st.fx < 0 ? disp.x - width - 8 * s : st.fx * disp.x;
    rx = std::fmax(0.0f, std::fmin(rx, disp.x - width));
    float ry = std::fmax(0.0f, std::fmin(st.fy * disp.y, disp.y - 120 * s));
    bool to_left = rx + width * 0.5f < disp.x * 0.5f;
    float hidden_x = to_left ? -width - 4 * s : disp.x + 4 * s;
    float x = hidden_x + (rx - hidden_x) * st.slide;

    const void* hover = nullptr;
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground;
    ImGuiWindow* panel_win = nullptr;

    if (st.slide > 0.0f) {
        ImGui::SetNextWindowPos(ImVec2(x, ry));
        ImGui::SetNextWindowSize(ImVec2(width, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * s, 16 * s));
        ImGui::PushStyleColor(ImGuiCol_Text, kInk);
        if (ImGui::Begin("##cr_panel", nullptr, fl)) {
            panel_win = ImGui::GetCurrentWindow();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->ChannelsSplit(2);
            dl->ChannelsSetCurrent(1);

            // Header: title (drag handle) + opacity button.
            ImVec2 hp = ImGui::GetCursorScreenPos();
            float inner = ImGui::GetContentRegionAvail().x;
            float hh = ImGui::GetFontSize() * 1.6f, btn = hh;
            ImGui::InvisibleButton("drag", ImVec2(inner - btn - 4 * s, hh));
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 2.0f)) {
                st.dragging = true;
                float nx = rx + io.MouseDelta.x, ny = ry + io.MouseDelta.y;
                st.fx = nx + width > disp.x - 20 * s ? -1.0f : std::fmax(0.0f, nx) / disp.x;
                st.fy = std::fmax(0.0f, ny) / disp.y;
            } else if (st.dragging && !ImGui::IsItemActive()) {
                st.dragging = false;
                save(st);
            }
            bool drag_hover = ImGui::IsItemHovered() && !st.dragging;
            ImFont* tf = g_title ? g_title : ImGui::GetFont();
            dl->AddText(tf, ImGui::GetFontSize() * 1.3f, ImVec2(hp.x, hp.y), kInk, w.party);
            char cnt[16];
            sprintf_s(cnt, "%d", r.n);
            ImVec2 cs = tf->CalcTextSizeA(ImGui::GetFontSize() * 1.1f, FLT_MAX, 0, cnt);
            float namew = tf->CalcTextSizeA(ImGui::GetFontSize() * 1.3f, FLT_MAX, 0, w.party).x;
            dl->AddText(tf, ImGui::GetFontSize() * 1.1f, ImVec2(hp.x + namew + 8 * s, hp.y + 3 * s), kInkSoft, cnt);
            (void)cs;

            // Opacity button: a half-inked circle.
            ImGui::SameLine(0, 4 * s);
            ImVec2 bp = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("opacity", ImVec2(btn, hh))) ImGui::OpenPopup("##cr_opacity");
            bool ob_hover = ImGui::IsItemHovered();
            ImVec2 oc(bp.x + btn * 0.5f, bp.y + hh * 0.5f);
            float orr = hh * 0.3f;
            dl->AddCircleFilled(oc, orr, IM_COL32(236, 228, 210, 255), 24);
            dl->PathArcTo(oc, orr, -1.5708f, 1.5708f, 16);
            dl->PathFillConvex(kInk);
            ink_ring(dl, oc, orr, 1.8f * s, kInk, 99);
            if (ob_hover) small_tooltip(w.opacity, {}, s);
            else if (drag_hover) small_tooltip(w.drag, {}, s);

            ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(232, 225, 208, 252));
            ImGui::PushStyleColor(ImGuiCol_Border, kInk);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(200, 190, 170, 255));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(210, 200, 180, 255));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(215, 205, 185, 255));
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, kInk);
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(150, 98, 8, 255));
            if (ImGui::BeginPopup("##cr_opacity")) {
                ImGui::TextUnformatted(w.opacity);
                ImGui::SetNextItemWidth(160 * s);
                int pct = (int)std::round(st.opacity * 100);
                if (ImGui::SliderInt("##op", &pct, 30, 100, "%d%%")) st.opacity = pct / 100.0f;
                if (ImGui::IsItemDeactivatedAfterEdit()) save(st);
                ImGui::EndPopup();
            }
            ImGui::PopStyleColor(7);

            ink_rule(dl, hp.x, hp.x + inner, hp.y + hh + 1 * s, s, 7, IM_COL32(96, 80, 66, 170));
            ImGui::Dummy(ImVec2(0, 5 * s));

            // The unit list: either the height the player dragged it to, or fit
            // to content up to a cap. Past that it scrolls (mouse wheel).
            float room = disp.y - ry - 40 * s - (ImGui::GetCursorScreenPos().y - ry);
            float list_cap = std::fmin(disp.y * kMaxHeight - (ImGui::GetCursorScreenPos().y - ry), room);
            float min_h = 90 * s;
            ImGuiChildFlags cf = ImGuiChildFlags_AlwaysUseWindowPadding;
            ImVec2 child_size(0, 0);
            if (st.list_h > 0) child_size.y = std::fmax(min_h, std::fmin(st.list_h * disp.y, room));
            else {
                cf |= ImGuiChildFlags_AutoResizeY;
                ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, std::fmax(min_h, list_cap)));
            }
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, 0);
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, IM_COL32(96, 80, 66, 150));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(96, 80, 66, 210));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, kInk);
            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 8 * s);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6 * s, 4 * s));
            if (ImGui::BeginChild("units", child_size, cf, ImGuiWindowFlags_NoBackground)) {
                float in2 = ImGui::GetContentRegionAvail().x;
                if (r.n == 0) ImGui::TextDisabled("-");
                for (int i = 0; i < r.n; ++i) {
                    if (unit_row(r.units[i], i, s, in2)) hover = r.units[i].ch;
                    if (i + 1 < r.n) {
                        ImVec2 p = ImGui::GetCursorScreenPos();
                        ImGui::Dummy(ImVec2(0, 9 * s));
                        ink_rule(ImGui::GetWindowDrawList(), p.x + 10 * s, p.x + in2 - 10 * s, p.y + 4 * s, s, 31 + i,
                                 IM_COL32(96, 80, 66, 90));
                    }
                }
            }
            ImGui::EndChild();
            float child_h = ImGui::GetItemRectSize().y;
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);

            // Bottom edge: drag to resize the list.
            ImVec2 gp = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("resize", ImVec2(inner, 12 * s));
            bool grip_hover = ImGui::IsItemHovered();
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 1.0f)) {
                if (!st.resizing) { st.resizing = true; if (st.list_h <= 0) st.list_h = child_h / disp.y; }
                st.list_h = std::fmax(min_h, std::fmin(st.list_h * disp.y + io.MouseDelta.y, room)) / disp.y;
            } else if (st.resizing && !ImGui::IsItemActive()) {
                st.resizing = false;
                save(st);
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { st.list_h = 0; save(st); }   // back to auto
            ImU32 gc = grip_hover || st.resizing ? kInk : IM_COL32(96, 80, 66, 160);
            for (int i = 0; i < 2; ++i)
                ink_rule(dl, gp.x + inner * 0.5f - 18 * s + i * 4 * s, gp.x + inner * 0.5f + 18 * s - i * 4 * s,
                         gp.y + 4 * s + i * 4 * s, s, 50 + i, gc);
            if (grip_hover && !st.resizing) small_tooltip(w.resize, {}, s);

            dl->ChannelsSetCurrent(0);
            paper(dl, panel_win->Pos, ImVec2(panel_win->Pos.x + panel_win->Size.x, panel_win->Pos.y + panel_win->Size.y),
                  kPaperTint, s);
            dl->ChannelsMerge();
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // The collapse tab: a paper flap on the panel's screen-edge side.
    float tab_h = 54 * s;
    float tx = to_left ? x + width - 3 * s : x - tab + 3 * s;
    ImGui::SetNextWindowPos(ImVec2(tx, ry + 14 * s));
    ImGui::SetNextWindowSize(ImVec2(tab, tab_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGuiWindow* tab_win = nullptr;
    if (ImGui::Begin("##cr_tab", nullptr, fl & ~ImGuiWindowFlags_AlwaysAutoResize)) {
        tab_win = ImGui::GetCurrentWindow();
        if (ImGui::InvisibleButton("toggle", ImVec2(tab, tab_h))) {
            st.collapsed = !st.collapsed;
            save(st);
        }
        bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        paper(dl, a, b, hov ? IM_COL32(255, 238, 190, 255) : kPaperTint, s * 0.6f);
        float cx = (a.x + b.x) * 0.5f, cy = (a.y + b.y) * 0.5f, ah = 7 * s;
        bool point_out = st.collapsed != to_left;
        float dir = point_out ? 1.0f : -1.0f;
        dl->AddTriangleFilled(ImVec2(cx - dir * ah * 0.5f, cy - ah), ImVec2(cx - dir * ah * 0.5f, cy + ah),
                              ImVec2(cx + dir * ah * 0.6f, cy), kInk);
        if (hov) small_tooltip(st.collapsed ? w.show : w.hide, {}, s);
    }
    ImGui::End();
    ImGui::PopStyleVar();

    fade_windows(panel_win, st.opacity);
    fade_windows(tab_win, st.opacity);
    return hover;
}

}  // namespace cr
