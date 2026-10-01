// panel.cpp -- the roster panel itself. Pure ImGui; reads only the Roster
// snapshot, never game memory.
#include "panel.h"

#include "imgui.h"

#include <cfloat>
#include <cstdio>
#include <cstring>

namespace cr {
namespace {

constexpr float kWidth = 270.0f;     // unscaled
constexpr float kTabW  = 22.0f;
constexpr float kTop   = 90.0f;

const char* kStatNames[7] = {"STR", "DEX", "CON", "INT", "SPD", "CHA", "LCK"};
const char* kSlotNames[5] = {"Head", "Face", "Neck", "Weapon", "Trinket"};

ImU32 rgba(float r, float g, float b, float a = 1.0f) { return ImGui::GetColorU32(ImVec4(r, g, b, a)); }

bool has(const char* s, const char* sub) {
    // case-insensitive substring
    for (; *s; ++s) {
        const char *a = s, *b = sub;
        while (*a && *b && ((*a | 32) == (*b | 32))) ++a, ++b;
        if (!*b) return true;
    }
    return false;
}

// Colour a status by what it is, from its name. Unknown names get a neutral tone.
ImU32 status_colour(const char* n) {
    if (has(n, "poison") || has(n, "toxic"))                  return rgba(0.35f, 0.80f, 0.30f);
    if (has(n, "bleed"))                                      return rgba(0.85f, 0.15f, 0.20f);
    if (has(n, "burn") || has(n, "fire"))                     return rgba(1.00f, 0.50f, 0.10f);
    if (has(n, "freez") || has(n, "frozen") || has(n, "chill") || has(n, "ice"))
                                                              return rgba(0.45f, 0.80f, 1.00f);
    if (has(n, "stun") || has(n, "sleep") || has(n, "confus")) return rgba(0.80f, 0.70f, 0.20f);
    if (has(n, "shield") || has(n, "armor") || has(n, "thorn")) return rgba(0.55f, 0.60f, 0.70f);
    if (has(n, "weak") || has(n, "brittle") || has(n, "blind") || has(n, "slow") || has(n, "curse") ||
        has(n, "doom") || has(n, "vulner") || has(n, "infect") || has(n, "rot") || has(n, "madness"))
                                                              return rgba(0.60f, 0.30f, 0.70f);
    if (has(n, "strength") || has(n, "haste") || has(n, "regen") || has(n, "bless") ||
        has(n, "buff") || has(n, "dodge") || has(n, "inspire") || has(n, "rage") || has(n, "fury"))
                                                              return rgba(0.95f, 0.80f, 0.25f);
    return rgba(0.45f, 0.45f, 0.50f);
}

// A stable colour per class/type name, so the same kind of unit always gets the
// same portrait tone.
ImU32 portrait_colour(const char* key) {
    uint32_t h = 2166136261u;
    for (const char* p = key; *p; ++p) h = (h ^ (uint8_t)*p) * 16777619u;
    float hue = (h % 360) / 360.0f;
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(hue, 0.45f, 0.55f, r, g, b);
    return rgba(r, g, b);
}

// First UTF-8 codepoint of s, as a string.
void initial(const char* s, char out[8]) {
    size_t n = 1;
    unsigned char c = (unsigned char)s[0];
    if (c >= 0xF0) n = 4; else if (c >= 0xE0) n = 3; else if (c >= 0xC0) n = 2;
    size_t len = strlen(s);
    if (n > len) n = len;
    memcpy(out, s, n);
    out[n] = 0;
}

void bar(ImDrawList* dl, ImVec2 p, ImVec2 sz, float frac, float extra, ImU32 fill, ImU32 extra_col, const char* text) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    float r = sz.y * 0.35f;
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), rgba(0.05f, 0.05f, 0.06f, 0.9f), r);
    if (frac > 0) dl->AddRectFilled(p, ImVec2(p.x + sz.x * frac, p.y + sz.y), fill, r);
    if (extra > 0) {
        float e = extra > 1 ? 1 : extra;
        dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + sz.x * e, p.y + sz.y * 0.35f), extra_col, r);
    }
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), rgba(0, 0, 0, 0.8f), r);
    if (text) {
        ImVec2 ts = ImGui::CalcTextSize(text);
        ImVec2 tp(p.x + (sz.x - ts.x) * 0.5f, p.y + (sz.y - ts.y) * 0.5f);
        dl->AddText(ImVec2(tp.x + 1, tp.y + 1), rgba(0, 0, 0, 0.9f), text);
        dl->AddText(tp, rgba(1, 1, 1), text);
    }
}

void tooltip(const UnitInfo& u) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);

    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", u.name[0] ? u.name : u.cls);
    if (u.level > 0) { ImGui::SameLine(); ImGui::TextDisabled("Lv %d", u.level); }
    if (u.name[0] && u.cls[0]) ImGui::TextDisabled("%s", u.cls);
    ImGui::Text("HP %d / %d", u.hp, u.max_hp);
    if (u.shield > 0) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), " Shield %d", u.shield); }
    if (u.max_mana > 0) ImGui::Text("Mana %d / %d", u.mana, u.max_mana);

    if (u.has_cat) {
        ImGui::SeparatorText("Stats");
        if (ImGui::BeginTable("st", 7, ImGuiTableFlags_SizingFixedSame)) {
            for (int i = 0; i < 7; ++i) { ImGui::TableNextColumn(); ImGui::TextDisabled("%s", kStatNames[i]); }
            for (int i = 0; i < 7; ++i) { ImGui::TableNextColumn(); ImGui::Text("%d", u.stats[i]); }
            ImGui::EndTable();
        }

        bool any = false;
        for (int i = 0; i < 5; ++i) any |= u.equip[i][0] != 0;
        ImGui::SeparatorText("Equipment");
        if (!any) ImGui::TextDisabled("none");
        for (int i = 0; i < 5; ++i)
            if (u.equip[i][0]) { ImGui::TextDisabled("%-8s", kSlotNames[i]); ImGui::SameLine(); ImGui::TextUnformatted(u.equip[i]); }

        if (u.mutations[0][0] || u.mutations[1][0]) {
            ImGui::SeparatorText("Mutations");
            for (int i = 0; i < 2; ++i) if (u.mutations[i][0]) ImGui::BulletText("%s", u.mutations[i]);
        }
        if (u.passives[0][0] || u.passives[1][0]) {
            ImGui::SeparatorText("Passives");
            for (int i = 0; i < 2; ++i) if (u.passives[i][0]) ImGui::BulletText("%s", u.passives[i]);
        }
    }

    if (u.n_abilities > 0) {
        ImGui::SeparatorText("Abilities");
        for (int i = 0; i < u.n_abilities; ++i) {
            const AbilityInfo& a = u.abilities[i];
            if (a.cooldown > 0)
                ImGui::TextColored(ImVec4(0.75f, 0.55f, 0.55f, 1.0f), "%s  (%d turn%s)", a.name, a.cooldown, a.cooldown == 1 ? "" : "s");
            else if (a.cooldown == 0)
                ImGui::TextColored(ImVec4(0.6f, 0.95f, 0.6f, 1.0f), "%s  ready", a.name);
            else
                ImGui::TextUnformatted(a.name);
        }
    }

    if (u.n_statuses > 0) {
        ImGui::SeparatorText("Statuses");
        for (int i = 0; i < u.n_statuses; ++i) {
            const StatusInfo& s = u.statuses[i];
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(status_colour(s.name)));
            if (s.stacks > 1) ImGui::BulletText("%s x%d", s.name, s.stacks);
            else ImGui::BulletText("%s", s.name);
            ImGui::PopStyleColor();
        }
    }
    if (!u.on_board) ImGui::TextDisabled("(off the board)");

    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// One unit row. Returns true while the mouse is over it.
bool unit_row(const UnitInfo& u, int idx, float s, float inner_w) {
    ImGui::PushID(idx);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float pic = 44.0f * s;
    float bar_h = 13.0f * s;
    float icon = 16.0f * s;
    float row_h = pic + (u.n_statuses > 0 ? icon + 3 * s : 0);

    ImGui::InvisibleButton("row", ImVec2(inner_w, row_h));
    bool hovered = ImGui::IsItemHovered();
    if (hovered)
        dl->AddRectFilled(ImVec2(p0.x - 3 * s, p0.y - 2 * s), ImVec2(p0.x + inner_w + 3 * s, p0.y + row_h + 2 * s),
                          rgba(1.0f, 0.85f, 0.3f, 0.12f), 4 * s);

    // Portrait: a tinted tile with the unit's initial.
    const char* key = u.cls[0] ? u.cls : u.name;
    ImVec2 a = p0, b(p0.x + pic, p0.y + pic);
    dl->AddRectFilled(a, b, portrait_colour(key), 6 * s);
    char ini[8];
    initial(u.name[0] ? u.name : (u.cls[0] ? u.cls : "?"), ini);
    ImFont* f = ImGui::GetFont();
    float fs = pic * 0.55f;
    ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, ini);
    dl->AddText(f, fs, ImVec2(a.x + (pic - ts.x) * 0.5f, a.y + (pic - ts.y) * 0.5f), rgba(1, 1, 1, 0.95f), ini);
    dl->AddRect(a, b, u.is_current ? rgba(1.0f, 0.85f, 0.25f) : rgba(0, 0, 0, 0.7f), 6 * s, 0,
                u.is_current ? 3 * s : 1 * s);

    // Name + bars.
    float x = p0.x + pic + 8 * s;
    float w = inner_w - pic - 8 * s;
    const char* nm = u.name[0] ? u.name : u.cls;
    dl->PushClipRect(ImVec2(x, p0.y), ImVec2(x + w, p0.y + pic), true);
    dl->AddText(ImVec2(x, p0.y), u.is_current ? rgba(1.0f, 0.88f, 0.45f) : rgba(0.92f, 0.92f, 0.92f), nm);
    dl->PopClipRect();

    char hp_txt[48];
    if (u.shield > 0) sprintf_s(hp_txt, "%d/%d  +%d", u.hp, u.max_hp, u.shield);
    else sprintf_s(hp_txt, "%d/%d", u.hp, u.max_hp);
    float frac = u.max_hp > 0 ? (float)u.hp / (float)u.max_hp : 0.0f;
    ImU32 hp_col = frac > 0.5f ? rgba(0.30f, 0.72f, 0.30f) : frac > 0.25f ? rgba(0.85f, 0.65f, 0.15f) : rgba(0.85f, 0.20f, 0.20f);
    float y = p0.y + ImGui::GetFontSize() + 3 * s;
    float shield_frac = u.max_hp > 0 ? (float)u.shield / (float)u.max_hp : 0.0f;
    bar(dl, ImVec2(x, y), ImVec2(w, bar_h), frac, shield_frac, hp_col, rgba(0.55f, 0.80f, 1.0f, 0.9f), hp_txt);

    if (u.max_mana > 0) {
        y += bar_h + 3 * s;
        char mp_txt[32];
        sprintf_s(mp_txt, "%d/%d", u.mana, u.max_mana);
        bar(dl, ImVec2(x, y), ImVec2(w, bar_h * 0.75f), (float)u.mana / (float)u.max_mana, 0, rgba(0.25f, 0.45f, 0.95f), 0, mp_txt);
    }

    // Status micro-icons under the row, each with its own tooltip.
    if (u.n_statuses > 0) {
        ImVec2 ip(p0.x, p0.y + pic + 3 * s);
        for (int i = 0; i < u.n_statuses; ++i) {
            const StatusInfo& st = u.statuses[i];
            ImVec2 q(ip.x + i * (icon + 3 * s), ip.y);
            if (q.x + icon > p0.x + inner_w) break;
            dl->AddRectFilled(q, ImVec2(q.x + icon, q.y + icon), status_colour(st.name), 3 * s);
            dl->AddRect(q, ImVec2(q.x + icon, q.y + icon), rgba(0, 0, 0, 0.8f), 3 * s);
            char ab[3] = {st.name[0], st.name[0] ? st.name[1] : 0, 0};
            float afs = icon * 0.62f;
            ImVec2 as = f->CalcTextSizeA(afs, FLT_MAX, 0, ab);
            dl->AddText(f, afs, ImVec2(q.x + (icon - as.x) * 0.5f, q.y + (icon - as.y) * 0.5f), rgba(0, 0, 0, 0.9f), ab);
            if (st.stacks > 1) {
                char n[8];
                sprintf_s(n, "%d", st.stacks);
                float nfs = icon * 0.55f;
                dl->AddText(f, nfs, ImVec2(q.x + icon - nfs * 0.45f, q.y + icon - nfs * 0.8f), rgba(1, 1, 1), n);
            }
            if (ImGui::IsMouseHoveringRect(q, ImVec2(q.x + icon, q.y + icon))) {
                ImGui::BeginTooltip();
                if (st.stacks > 1) ImGui::Text("%s x%d", st.name, st.stacks);
                else ImGui::TextUnformatted(st.name);
                ImGui::EndTooltip();
                hovered = false;   // the icon's tooltip wins over the unit tooltip
            }
        }
    }

    if (hovered) tooltip(u);
    ImGui::PopID();
    return ImGui::IsItemHovered() || hovered;
}

}  // namespace

const void* panel_draw(const Roster& r, PanelState& st, float dt, float s) {
    ImGuiIO& io = ImGui::GetIO();
    float target = st.collapsed ? 0.0f : 1.0f;
    float k = dt * 10.0f;
    st.slide += (target - st.slide) * (k > 1 ? 1 : k);
    if (st.slide < 0.002f) st.slide = 0.0f;
    if (st.slide > 0.998f) st.slide = 1.0f;

    float w = kWidth * s, tab = kTabW * s;
    float x = io.DisplaySize.x - w * st.slide;
    float top = kTop * s;
    const void* hover = nullptr;

    // The panel.
    if (st.slide > 0.0f) {
        ImGui::SetNextWindowPos(ImVec2(x, top));
        ImGui::SetNextWindowSize(ImVec2(w, 0));
        ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, io.DisplaySize.y - top - 20 * s));
        ImGuiWindowFlags fl = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                              ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##cr_panel", nullptr, fl)) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.45f, 1.0f), "Party");
            ImGui::SameLine();
            ImGui::TextDisabled("%d", r.n);
            ImGui::Separator();
            float inner = ImGui::GetContentRegionAvail().x;
            if (r.n == 0) ImGui::TextDisabled("no friendly units");
            for (int i = 0; i < r.n; ++i) {
                if (unit_row(r.units[i], i, s, inner)) hover = r.units[i].ch;
                if (i + 1 < r.n) ImGui::Dummy(ImVec2(0, 2 * s));
            }
        }
        ImGui::End();
    }

    // The collapse tab, always visible, glued to the panel's left edge.
    ImGui::SetNextWindowPos(ImVec2(x - tab, top));
    ImGui::SetNextWindowSize(ImVec2(tab, 44 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGuiWindowFlags tf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##cr_tab", nullptr, tf)) {
        if (ImGui::InvisibleButton("toggle", ImVec2(tab, 44 * s))) st.collapsed = !st.collapsed;
        bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetItemRectMin(), q = ImGui::GetItemRectMax();
        if (hov) dl->AddRectFilled(p, q, rgba(1.0f, 0.85f, 0.3f, 0.15f));
        // Arrow: points left (open) when collapsed, right (hide) when shown.
        float cx = (p.x + q.x) * 0.5f, cy = (p.y + q.y) * 0.5f, a = 5.0f * s;
        ImU32 c = rgba(1.0f, 0.85f, 0.45f);
        if (st.collapsed) dl->AddTriangleFilled(ImVec2(cx + a * 0.6f, cy - a), ImVec2(cx + a * 0.6f, cy + a), ImVec2(cx - a * 0.6f, cy), c);
        else              dl->AddTriangleFilled(ImVec2(cx - a * 0.6f, cy - a), ImVec2(cx - a * 0.6f, cy + a), ImVec2(cx + a * 0.6f, cy), c);
        if (hov) ImGui::SetTooltip(st.collapsed ? "Show party" : "Hide party");
    }
    ImGui::End();
    ImGui::PopStyleVar();

    return hover;
}

}  // namespace cr
