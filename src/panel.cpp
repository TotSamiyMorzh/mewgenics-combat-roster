// panel.cpp -- the roster panel, drawn in the game's own visual language:
// paper cards with heavy ink outlines, the game's portraits, heart / mana /
// stat / status icons rasterised from swfs/ui.swf, and every name and
// description in the player's language from the game's StringsDatabase.
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

namespace cr {
namespace {

// --- palette (sampled from the game's HUD) -----------------------------------
const ImU32 kInk      = IM_COL32(20, 18, 20, 255);
const ImU32 kPaper    = IM_COL32(236, 231, 219, 245);
const ImU32 kPaperDim = IM_COL32(214, 207, 192, 255);
const ImU32 kHpFill   = IM_COL32(196, 58, 62, 255);
const ImU32 kHpBack   = IM_COL32(70, 32, 34, 255);
const ImU32 kManaFill = IM_COL32(64, 112, 196, 255);
const ImU32 kManaBack = IM_COL32(30, 38, 66, 255);
const ImU32 kShield   = IM_COL32(150, 190, 225, 255);
const ImU32 kGold     = IM_COL32(242, 196, 64, 255);
const ImU32 kAlly     = IM_COL32(126, 186, 120, 255);
const ImU32 kBoss     = IM_COL32(176, 96, 92, 255);

constexpr float kWidth = 300.0f;   // unscaled

// --- the mod's own UI words (everything else comes from the game) -------------
struct Words {
    const char *party, *stats, *equip, *abilities, *passives, *mutations, *statuses, *charge, *per_fight,
        *off_board, *hide, *show, *drag, *none, *mana, *level;
};
const Words kRu = {"Отряд", "Характеристики", "Снаряжение", "Способности", "Пассивки", "Мутации", "Эффекты",
                   "зарядка", "за бой", "вне поля", "Спрятать отряд", "Показать отряд",
                   "Перетащи, чтобы подвинуть", "пусто", "маны", "ур."};
const Words kEn = {"Party", "Stats", "Equipment", "Abilities", "Passives", "Mutations", "Effects", "charge",
                   "per fight", "off the board", "Hide party", "Show party", "Drag to move", "empty", "mana", "Lv"};
const Words& W() {
    static std::string lang = loc_lang();
    return lang.rfind("ru", 0) == 0 ? kRu : kEn;
}

const char* kStatIcons[7] = {"FontIcon_str", "FontIcon_dex", "FontIcon_con", "FontIcon_int",
                             "FontIcon_spd", "FontIcon_cha", "FontIcon_lck"};

// --- small drawing helpers ------------------------------------------------------

void text_outlined(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const char* t, float o = 1.0f) {
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            if (dx || dy) dl->AddText(f, size, ImVec2(p.x + dx * o, p.y + dy * o), kInk, t);
    dl->AddText(f, size, p, col, t);
}

// An image fitted into a box (keeping aspect), or nothing if not rasterised yet.
bool image_fit(ImDrawList* dl, Swf swf, const std::string& sym, int frame, ImVec2 a, ImVec2 b, int px = 96) {
    Tex t = asset_image(swf, sym, frame, px);
    if (!t.id) return false;
    float bw = b.x - a.x, bh = b.y - a.y, s = std::fmin(bw / t.w, bh / t.h);
    ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    ImVec2 h(t.w * s * 0.5f, t.h * s * 0.5f);
    dl->AddImage((ImTextureID)t.id, ImVec2(c.x - h.x, c.y - h.y), ImVec2(c.x + h.x, c.y + h.y));
    return true;
}

void icon_inline(Swf swf, const std::string& sym, int frame, float size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    image_fit(ImGui::GetWindowDrawList(), swf, sym, frame, p, ImVec2(p.x + size, p.y + size), 64);
}

// A HUD-style bar: ink outline, dark back, coloured fill, icon on the left.
void bar(ImDrawList* dl, ImVec2 p, ImVec2 sz, float frac, ImU32 back, ImU32 fill, const char* icon, const char* text,
         float s, float extra = 0.0f) {
    frac = std::fmax(0.0f, std::fmin(1.0f, frac));
    float r = sz.y * 0.5f;
    dl->AddRectFilled(ImVec2(p.x - 2 * s, p.y - 2 * s), ImVec2(p.x + sz.x + 2 * s, p.y + sz.y + 2 * s), kInk, r + 2 * s);
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), back, r);
    if (frac > 0) dl->AddRectFilled(p, ImVec2(p.x + std::fmax(sz.y, sz.x * frac), p.y + sz.y), fill, r);
    if (extra > 0)
        dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + sz.x * std::fmin(1.0f, extra), p.y + sz.y * 0.4f), kShield, r);
    if (icon) {
        float is = sz.y * 1.9f;
        image_fit(dl, Swf::Ui, icon, 0, ImVec2(p.x - is * 0.55f, p.y + sz.y * 0.5f - is * 0.5f),
                  ImVec2(p.x + is * 0.45f, p.y + sz.y * 0.5f + is * 0.5f), 64);
    }
    if (text) {
        ImFont* f = ImGui::GetFont();
        float fs = ImGui::GetFontSize() * 0.92f;
        ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, text);
        text_outlined(dl, f, fs, ImVec2(p.x + (sz.x - ts.x) * 0.5f + sz.y * 0.3f, p.y + (sz.y - ts.y) * 0.5f),
                      IM_COL32_WHITE, text, 1.2f * s);
    }
}

// Card background: paper with a heavy ink border, like the game's tooltips.
void card(ImDrawList* dl, ImVec2 a, ImVec2 b, float s, ImU32 fill = kPaper) {
    float r = 10 * s, t = 3 * s;
    dl->AddRectFilled(ImVec2(a.x + 4 * s, a.y + 5 * s), ImVec2(b.x + 4 * s, b.y + 5 * s), IM_COL32(0, 0, 0, 70), r);
    dl->AddRectFilled(a, b, kInk, r);
    dl->AddRectFilled(ImVec2(a.x + t, a.y + t), ImVec2(b.x - t, b.y - t), fill, r - t);
}

// --- names ----------------------------------------------------------------------------

std::string tr_or(const TextKeys* k, const char* id) {
    if (k) { const std::string& t = tr(k->name); if (!t.empty()) return t; }
    return id;
}
std::string item_name(const char* id) { return tr_or(keys_item(id), id); }
std::string ability_name(const char* id) { return tr_or(keys_ability(id), id); }
std::string passive_name(const char* id) { return tr_or(keys_passive(id), id); }
std::string status_name(const char* id) { return tr_or(keys_keyword(id), id); }
std::string status_desc(const StatusInfo& s) {
    const TextKeys* k = keys_keyword(s.id);
    if (!k) return {};
    const std::string& d = tr(s.stacks != 0 && !k->desc_stacks.empty() ? k->desc_stacks : k->desc);
    return with_stacks(d, s.stacks);
}
std::string class_name(const char* id) {
    const TextKeys* k = keys_class(id);
    return k ? tr(k->name) : std::string();
}

// The game shows an icon only for statuses its table gives one; so do we.
bool visible_status(const StatusInfo& s) { return status_icon_frame(s.id, s.stacks < 0) >= 0; }

// Portrait clip: the character's own portrait if it has one, else the class
// portrait for cats, else the class icon.
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

void section(const char* title) {
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 92, 60, 255));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y - 1), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y - 1),
                                        IM_COL32(120, 92, 60, 120), 1.5f);
    ImGui::Dummy(ImVec2(0, 2));
}

void wrapped_dim(const std::string& t) {
    if (t.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(90, 80, 70, 255));
    ImGui::TextWrapped("%s", t.c_str());
    ImGui::PopStyleColor();
}

void tooltip(const UnitInfo& u, float s) {
    const Words& w = W();
    float icon = ImGui::GetFontSize() * 1.25f;
    ImGui::PushStyleColor(ImGuiCol_PopupBg, 0);
    ImGui::PushStyleColor(ImGuiCol_Border, 0);
    ImGui::PushStyleColor(ImGuiCol_Text, kInk);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * s, 12 * s));
    ImGui::BeginTooltip();
    // Paper card under the contents: draw contents on channel 1, card on 0.
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    ImDrawList* tdl = ImGui::GetWindowDrawList();
    tdl->ChannelsSplit(2);
    tdl->ChannelsSetCurrent(1);
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.15f);
    ImGui::TextUnformatted(u.name[0] ? u.name : u.cls);
    ImGui::PopFont();
    std::string cls = class_name(u.cls);
    if (!cls.empty() || u.level > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(110, 100, 90, 255));
        if (!cls.empty() && u.level > 0) ImGui::Text("%s  •  %s %d", cls.c_str(), w.level, u.level);
        else if (!cls.empty()) ImGui::TextUnformatted(cls.c_str());
        else ImGui::Text("%s %d", w.level, u.level);
        ImGui::PopStyleColor();
    }
    if (!u.has_cat) wrapped_dim(tr(u.desc_key));

    // HP / shield / mana with the game's icons.
    icon_inline(Swf::Ui, "HealthIcon", 0, icon);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
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

    if (u.has_cat) {
        section(w.stats);
        for (int i = 0; i < 7; ++i) {
            if (i) ImGui::SameLine(0, 8 * s);
            ImGui::BeginGroup();
            icon_inline(Swf::Ui, kStatIcons[i], 0, icon);
            ImGui::SameLine(0, 2 * s);
            ImGui::Text("%d", u.stats[i]);
            ImGui::EndGroup();
        }

        bool any = false;
        for (auto& e : u.equip) any |= e[0] != 0;
        if (any) {
            section(w.equip);
            for (auto& e : u.equip) {
                if (!e[0]) continue;
                ImGui::BulletText("%s", item_name(e).c_str());
                if (const TextKeys* k = keys_item(e)) {
                    ImGui::Indent(ImGui::GetFontSize());
                    wrapped_dim(tr(k->desc));
                    ImGui::Unindent(ImGui::GetFontSize());
                }
            }
        }
        if (u.passives[0][0] || u.passives[1][0]) {
            section(w.passives);
            for (auto& p : u.passives) {
                if (!p[0]) continue;
                ImGui::BulletText("%s", passive_name(p).c_str());
                if (const TextKeys* k = keys_passive(p)) {
                    ImGui::Indent(ImGui::GetFontSize());
                    wrapped_dim(tr(k->desc));
                    ImGui::Unindent(ImGui::GetFontSize());
                }
            }
        }
        if (u.mutations[0][0] || u.mutations[1][0]) {
            section(w.mutations);
            for (auto& m : u.mutations) if (m[0]) ImGui::BulletText("%s", passive_name(m).c_str());
        }
    }

    if (u.n_abilities > 0) {
        section(w.abilities);
        for (int i = 0; i < u.n_abilities; ++i) {
            const AbilityInfo& a = u.abilities[i];
            ImGui::BulletText("%s", ability_name(a.id).c_str());
            if (a.mana_cost > 0) {
                ImGui::SameLine();
                icon_inline(Swf::Ui, "ManaIcon", 0, ImGui::GetFontSize());
                ImGui::SameLine(0, 2 * s);
                ImGui::Text("%d", a.mana_cost);
            }
            if (a.charge > 0) { ImGui::SameLine(); ImGui::TextDisabled("(%s %d)", w.charge, a.charge); }
            if (a.uses_per_fight > 0) { ImGui::SameLine(); ImGui::TextDisabled("(%d %s)", a.uses_per_fight, w.per_fight); }
        }
    }

    bool any_status = false;
    for (int i = 0; i < u.n_statuses; ++i) any_status |= visible_status(u.statuses[i]);
    if (any_status) {
        section(w.statuses);
        for (int i = 0; i < u.n_statuses; ++i) {
            const StatusInfo& st = u.statuses[i];
            if (!visible_status(st)) continue;
            icon_inline(Swf::Ui, "StatusIcon", status_icon_frame(st.id, st.stacks < 0), icon);
            ImGui::SameLine();
            ImGui::BeginGroup();
            if (st.stacks > 1 || st.stacks < 0) ImGui::Text("%s  x%d", status_name(st.id).c_str(), st.stacks);
            else ImGui::TextUnformatted(status_name(st.id).c_str());
            wrapped_dim(status_desc(st));
            ImGui::EndGroup();
        }
    }
    if (!u.on_board) wrapped_dim(std::string("(") + w.off_board + ")");

    ImGui::PopTextWrapPos();
    tdl->ChannelsSetCurrent(0);
    card(tdl, win->Pos, ImVec2(win->Pos.x + win->Size.x, win->Pos.y + win->Size.y), s);
    tdl->ChannelsMerge();
    ImGui::EndTooltip();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
}

// --- one unit row --------------------------------------------------------------------------

bool unit_row(const UnitInfo& u, int idx, float s, float inner_w) {
    ImGui::PushID(idx);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* f = ImGui::GetFont();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float pic = 56 * s, bar_h = 15 * s, icon = 22 * s;

    int n_icons = 0;
    for (int i = 0; i < u.n_statuses; ++i) n_icons += visible_status(u.statuses[i]);
    float row_h = pic + (n_icons ? icon + 4 * s : 0);

    ImGui::InvisibleButton("row", ImVec2(inner_w, row_h));
    bool row_hover = ImGui::IsItemHovered();
    if (row_hover)
        dl->AddRectFilled(ImVec2(p0.x - 6 * s, p0.y - 3 * s), ImVec2(p0.x + inner_w + 6 * s, p0.y + row_h + 3 * s),
                          IM_COL32(242, 196, 64, 60), 8 * s);

    // Portrait: the game's own portrait art on a coloured disc with an ink ring.
    ImVec2 c(p0.x + pic * 0.5f, p0.y + pic * 0.5f);
    float r = pic * 0.5f;
    ImU32 disc = u.kind == 2 ? kBoss : kAlly;
    dl->AddCircleFilled(c, r, kInk, 32);
    dl->AddCircleFilled(c, r - 3 * s, disc, 32);
    Swf swf;
    std::string sym = portrait_symbol(u, swf);
    dl->PushClipRect(ImVec2(c.x - r + 3 * s, c.y - r + 3 * s), ImVec2(c.x + r - 3 * s, c.y + r - 3 * s), true);
    if (sym.empty() || !image_fit(dl, swf, sym, 0, ImVec2(c.x - r * 0.95f, c.y - r * 0.95f),
                                  ImVec2(c.x + r * 0.95f, c.y + r * 0.95f), 112)) {
        // not rasterised yet (or no art): the initial, like v0.1
        char ini[8] = {};
        const char* nm = u.name[0] ? u.name : "?";
        size_t n = (unsigned char)nm[0] >= 0xE0 ? 3 : (unsigned char)nm[0] >= 0xC0 ? 2 : 1;
        memcpy(ini, nm, n);
        ImVec2 ts = f->CalcTextSizeA(pic * 0.5f, FLT_MAX, 0, ini);
        text_outlined(dl, f, pic * 0.5f, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32_WHITE, ini, 1.5f * s);
    }
    dl->PopClipRect();
    dl->AddCircle(c, r - 1.5f * s, u.is_current ? kGold : kInk, 32, u.is_current ? 4.5f * s : 3 * s);

    // Name + bars.
    float x = p0.x + pic + 16 * s;
    float wbar = inner_w - pic - 18 * s;
    dl->PushClipRect(ImVec2(x - 8 * s, p0.y), ImVec2(p0.x + inner_w, p0.y + pic), true);
    dl->AddText(f, ImGui::GetFontSize(), ImVec2(x - 6 * s, p0.y - 1 * s), u.is_current ? IM_COL32(150, 100, 10, 255) : kInk,
                u.name[0] ? u.name : u.cls);
    dl->PopClipRect();

    char txt[48];
    if (u.shield > 0) sprintf_s(txt, "%d/%d  +%d", u.hp, u.max_hp, u.shield);
    else sprintf_s(txt, "%d/%d", u.hp, u.max_hp);
    float y = p0.y + ImGui::GetFontSize() + 5 * s;
    bar(dl, ImVec2(x, y), ImVec2(wbar, bar_h), u.max_hp ? (float)u.hp / u.max_hp : 0, kHpBack, kHpFill, "HealthIcon", txt,
        s, u.max_hp ? (float)u.shield / u.max_hp : 0);
    if (u.max_mana > 0) {
        y += bar_h + 7 * s;
        sprintf_s(txt, "%d/%d", u.mana, u.max_mana);
        bar(dl, ImVec2(x, y), ImVec2(wbar, bar_h * 0.85f), (float)u.mana / u.max_mana, kManaBack, kManaFill, "ManaIcon",
            txt, s);
    }

    // Status icons along the bottom, each with its own tooltip.
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
                float fs = icon * 0.55f;
                ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, n);
                text_outlined(dl, f, fs, ImVec2(b.x - ts.x + 1 * s, b.y - ts.y + 2 * s), IM_COL32_WHITE, n, 1.0f * s);
            }
            if (ImGui::IsMouseHoveringRect(a, b)) {
                icon_hover = true;
                ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(236, 231, 219, 250));
                ImGui::PushStyleColor(ImGuiCol_Text, kInk);
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 18.0f);
                ImGui::TextUnformatted(status_name(st.id).c_str());
                wrapped_dim(status_desc(st));
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
                ImGui::PopStyleColor(2);
            }
            ix += icon + 4 * s;
        }
    }

    if (row_hover && !icon_hover) tooltip(u, s);
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
    WritePrivateProfileStringA("panel", "collapsed", st.collapsed ? "1" : "0", st.ini_path.c_str());
}

}  // namespace

void panel_load(PanelState& st, const std::string& game_dir) {
    st.ini_path = game_dir + "\\mods\\combat_roster.ini";
    char v[32];
    GetPrivateProfileStringA("panel", "x", "-1", v, sizeof(v), st.ini_path.c_str());
    st.fx = (float)atof(v);
    GetPrivateProfileStringA("panel", "y", "0.30", v, sizeof(v), st.ini_path.c_str());
    st.fy = (float)atof(v);
    st.collapsed = GetPrivateProfileIntA("panel", "collapsed", 0, st.ini_path.c_str()) != 0;
    st.slide = st.collapsed ? 0.0f : 1.0f;
    st.loaded = true;
}

const void* panel_draw(const Roster& r, PanelState& st, float dt, float s) {
    const Words& w = W();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 disp = io.DisplaySize;
    float width = kWidth * s, tab = 24 * s;

    float k = dt * 10.0f;
    st.slide += ((st.collapsed ? 0.0f : 1.0f) - st.slide) * (k > 1 ? 1 : k);
    if (st.slide < 0.002f) st.slide = 0.0f;
    if (st.slide > 0.998f) st.slide = 1.0f;

    // Resting position: fx<0 means "against the right edge".
    float rx = st.fx < 0 ? disp.x - width - 8 * s : st.fx * disp.x;
    rx = std::fmax(0.0f, std::fmin(rx, disp.x - width));
    float ry = std::fmax(0.0f, std::fmin(st.fy * disp.y, disp.y - 80 * s));
    // Collapsing slides it behind whichever edge is closer.
    bool to_left = rx + width * 0.5f < disp.x * 0.5f;
    float hidden_x = to_left ? -width - 4 * s : disp.x + 4 * s;
    float x = hidden_x + (rx - hidden_x) * st.slide;

    const void* hover = nullptr;
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground;

    if (st.slide > 0.0f) {
        ImGui::SetNextWindowPos(ImVec2(x, ry));
        ImGui::SetNextWindowSize(ImVec2(width, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * s, 12 * s));
        ImGui::PushStyleColor(ImGuiCol_Text, kInk);
        if (ImGui::Begin("##cr_panel", nullptr, fl)) {
            ImGuiWindow* win = ImGui::GetCurrentWindow();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->ChannelsSplit(2);
            dl->ChannelsSetCurrent(1);

            // Header: the drag handle.
            ImVec2 hp = ImGui::GetCursorScreenPos();
            float inner = ImGui::GetContentRegionAvail().x;
            ImGui::InvisibleButton("drag", ImVec2(inner, ImGui::GetFontSize() * 1.4f));
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 2.0f)) {
                st.dragging = true;
                float nx = rx + io.MouseDelta.x, ny = ry + io.MouseDelta.y;
                // Near the right edge it snaps back to "right-anchored".
                st.fx = nx + width > disp.x - 20 * s ? -1.0f : std::fmax(0.0f, nx) / disp.x;
                st.fy = std::fmax(0.0f, ny) / disp.y;
            } else if (st.dragging && !ImGui::IsItemActive()) {
                st.dragging = false;
                save(st);
            }
            if (ImGui::IsItemHovered() && !st.dragging) ImGui::SetTooltip("%s", w.drag);
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.2f);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), hp, kInk, w.party);
            ImGui::PopFont();
            char cnt[16];
            sprintf_s(cnt, "%d", r.n);
            ImVec2 cs = ImGui::CalcTextSize(cnt);
            dl->AddText(ImVec2(hp.x + inner - cs.x, hp.y + 2 * s), IM_COL32(120, 92, 60, 255), cnt);
            // grip dots
            for (int i = 0; i < 3; ++i)
                dl->AddCircleFilled(ImVec2(hp.x + inner * 0.5f - 10 * s + i * 10 * s, hp.y + ImGui::GetFontSize() * 1.25f),
                                    1.8f * s, IM_COL32(120, 92, 60, 160));
            ImGui::Dummy(ImVec2(0, 2 * s));

            if (r.n == 0) ImGui::TextDisabled("—");
            for (int i = 0; i < r.n; ++i) {
                if (unit_row(r.units[i], i, s, inner)) hover = r.units[i].ch;
                if (i + 1 < r.n) {
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(0, 8 * s));
                    dl->AddLine(ImVec2(p.x + 8 * s, p.y + 4 * s), ImVec2(p.x + inner - 8 * s, p.y + 4 * s), kPaperDim, 2 * s);
                }
            }

            dl->ChannelsSetCurrent(0);
            card(dl, win->Pos, ImVec2(win->Pos.x + win->Size.x, win->Pos.y + win->Size.y), s);
            dl->ChannelsMerge();
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // The collapse tab: a little paper flap on the panel's screen-edge side.
    float tab_h = 52 * s;
    float tx = to_left ? x + width - 2 * s : x - tab + 2 * s;
    ImGui::SetNextWindowPos(ImVec2(tx, ry + 10 * s));
    ImGui::SetNextWindowSize(ImVec2(tab, tab_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("##cr_tab", nullptr, fl & ~ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::InvisibleButton("toggle", ImVec2(tab, tab_h))) {
            st.collapsed = !st.collapsed;
            save(st);
        }
        bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        card(dl, a, b, s * 0.8f, hov ? IM_COL32(250, 236, 196, 255) : kPaper);
        // Arrow points to where the panel will go.
        float cx = (a.x + b.x) * 0.5f, cy = (a.y + b.y) * 0.5f, ah = 7 * s;
        bool point_out = st.collapsed != to_left;   // shown & right side -> point right (hide)
        float dir = point_out ? 1.0f : -1.0f;
        dl->AddTriangleFilled(ImVec2(cx - dir * ah * 0.5f, cy - ah), ImVec2(cx - dir * ah * 0.5f, cy + ah),
                              ImVec2(cx + dir * ah * 0.6f, cy), kInk);
        if (hov) ImGui::SetTooltip("%s", st.collapsed ? w.show : w.hide);
    }
    ImGui::End();
    ImGui::PopStyleVar();

    return hover;
}

}  // namespace cr
