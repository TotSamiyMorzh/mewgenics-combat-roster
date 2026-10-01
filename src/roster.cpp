#include "roster.h"

#include "game.h"
#include "log.h"

#include <cstdio>
#include <cstring>

namespace cr {
namespace {

void* g_tc = nullptr;              // TurnControl* of the battle on screen
const void* g_members[256];        // this frame's live list, for roster_contains
uint32_t g_member_count = 0;

const void* char_list() {
    const void* scene  = rdp(g_tc, off::TC_Scene);
    const void* sub    = rdp(scene, off::Scene_Sub);
    const void* holder = rdp(sub, off::Sub_Holder);
    return rdp(holder, off::Holder_List);
}

// A Character we can trust: its TacticsObject points back at it.
bool linked(const void* ch) {
    const void* tobj = rdp(ch, off::Ch_TObj);
    return tobj && rdp(tobj, off::TO_Char) == ch;
}

void strip_ns(const char* in, char* out, size_t n) {
    const char* p = strrchr(in, ':');
    strcpy_s(out, n, p ? p + 1 : in);
}

// --- team -----------------------------------------------------------------------
//
// [CR] Character+0x350 is the CURRENT team (int16), +0x358 the original one
// (they differ under mind control). The game classifies a team through three
// 14-entry byte tables filled at startup, all addressed off one base that
// sub_14011A410 ("is neutral") loads with `lea r11, [rip+X]`:
//     base+0x0E  is-ally      (used by the portrait-frame picker @ 0x14011DC41)
//     base+0xD2  is-enemy     (sub_14011A3C0)
// Teams above 0xD fall back to "team == 1". We read the ally table at runtime
// rather than copying it, so modded or patched team tables stay correct.
const Sig kSigIsNeutral = {"Character::is_neutral", 0x0011A410,
    "84 D2 74 08 8B 81 50 03 00 00 EB 06 8B 81 58 03 00 00 4C 8D 1D"};
const uint8_t* g_team_tables = nullptr;

bool is_friendly(const void* ch) {
    int16_t team = rdv<int16_t>(ch, off::Ch_Team, -1);
    if (team < 0) return false;
    if (team > 0xD) return team == 1;
    return rdv<uint8_t>(g_team_tables, 0x0E + team) != 0;
}

// --- Character -> CatData ----------------------------------------------------------
//
// [CR] The offset of the CatData* inside Character is not known statically yet,
// so it is DISCOVERED: the first friendly Character of a battle is scanned for
// a pointer to something that passes several independent CatData checks. The
// result is logged and reused. No game function is called (CatData::by_id can
// load and construct a cat on a cache miss -- not something a UI mod may do).
constexpr uintptr_t kCatScanLimit = 0x1400;
int g_cat_off = -1;        // -1 unknown, -2 searched and not found this session
int g_cat_misses = 0;

bool looks_like_catdata(const void* q) {
    if (!q || ((uintptr_t)q & 7)) return false;
    double coi = -1;
    int64_t key = -1;
    if (!rd(q, off::Cat_Coi, coi) || !(coi >= 0.0 && coi <= 1.0)) return false;
    if (!rd(q, off::Cat_SqlKey, key) || key < 0 || key > (1ll << 40)) return false;
    int32_t base[7];
    if (!rd(q, off::Cat_StatsBase, base)) return false;
    for (int s : base) if (s < 0 || s > 40) return false;
    uint64_t name_size = 0, name_cap = 0;
    if (!rd(q, off::Cat_Name + 0x10, name_size) || !rd(q, off::Cat_Name + 0x18, name_cap)) return false;
    return name_size > 0 && name_size < 64 && name_cap >= 7 && name_cap >= name_size;
}

void discover_cat_offset(const void* ch) {
    for (uintptr_t o = 0; o < kCatScanLimit; o += 8) {
        if (looks_like_catdata(rdp(ch, o))) {
            g_cat_off = (int)o;
            log_line("roster: Character+0x%X -> CatData (discovered)", (unsigned)o);
            return;
        }
    }
}

const void* cat_of(const void* ch) {
    if (g_cat_off >= 0) {
        const void* q = rdp(ch, (uintptr_t)g_cat_off);
        return looks_like_catdata(q) ? q : nullptr;
    }
    return nullptr;
}

void read_cat(const void* cat, UnitInfo& u) {
    u.has_cat = true;
    u.look.head  = rdv<int32_t>(cat, off::Cat_PartIdx(1));
    u.look.eye   = rdv<int32_t>(cat, off::Cat_PartIdx(7));
    u.look.brow  = rdv<int32_t>(cat, off::Cat_PartIdx(9));
    u.look.ear   = rdv<int32_t>(cat, off::Cat_PartIdx(11));
    u.look.mouth = rdv<int32_t>(cat, off::Cat_PartIdx(13));
    u.look.tex   = rdv<int32_t>(cat, off::Cat_Texture);
    u.look.palette = rdv<int32_t>(cat, off::Cat_Palette);
    int32_t a[7] = {}, b[7] = {}, c[7] = {};
    rd(cat, off::Cat_StatsBase, a);
    rd(cat, off::Cat_StatsLvl, b);
    rd(cat, off::Cat_StatsInj, c);
    for (int i = 0; i < 7; ++i) u.stats[i] = a[i] + b[i] + c[i];
    u.level = (int)rdv<uint32_t>(cat, off::Cat_Level);
    for (int i = 0; i < 5; ++i)
        read_string((const uint8_t*)cat + off::Cat_Equip + i * off::Equip_Size + off::Equip_Name, u.equip[i], sizeof(u.equip[i]));
    read_string((const uint8_t*)cat + off::Cat_Mutation0, u.mutations[0], sizeof(u.mutations[0]));
    read_string((const uint8_t*)cat + off::Cat_Mutation1, u.mutations[1], sizeof(u.mutations[1]));
    read_string((const uint8_t*)cat + off::Cat_Passive0, u.passives[0], sizeof(u.passives[0]));
    read_string((const uint8_t*)cat + off::Cat_Passive1, u.passives[1], sizeof(u.passives[1]));
    // "none" is how an empty slot is spelled in the save.
    auto clear_none = [](char* s) { if (!strcmp(s, "none") || !strcmp(s, "None")) s[0] = 0; };
    for (auto& e : u.equip) clear_none(e);
    for (auto& m : u.mutations) clear_none(m);
    for (auto& p : u.passives) clear_none(p);
}

// --- statuses -----------------------------------------------------------------------
//
// Every status (Bleed, Poison, DodgeChance_Status, ...) is a glaiel::Passive
// subclass whose RTTI class name is the key keyword_tooltips.gon and the game's
// status-icon table use, so the class name IS the status id. [CR] Stack count is
// Passive+0x5C (Trample 3 / BoostHeals 2 / Metal 1 matched their GON values live).

bool g_dumped = false;   // one diagnostic dump per session (verifies stacks)

void dump_passives(const void* ch) {
    uint32_t n = rdv<uint32_t>(ch, off::Ch_PassCount);
    const void* data = rdp(ch, off::Ch_PassData);
    log_line("dump: Character %p passives=%u", ch, n);
    for (uint32_t i = 0; i < n && i < 32; ++i) {
        const void* p = rdp(data, i * sizeof(void*));
        char cls[96];
        rtti_name(p, cls, sizeof(cls));
        log_line("  [%u] %s stacks=%d @40=%d", i, cls, rdv<int32_t>(p, off::Pass_Stacks), rdv<int32_t>(p, 0x40));
    }
}

void read_statuses(const void* ch, UnitInfo& u) {
    uint32_t n = rdv<uint32_t>(ch, off::Ch_PassCount);
    const void* data = rdp(ch, off::Ch_PassData);
    if (!data || n > 128) return;
    for (uint32_t i = 0; i < n && u.n_statuses < kMaxStatuses; ++i) {
        const void* p = rdp(data, i * sizeof(void*));
        if (!p) continue;
        char cls[96];
        rtti_name(p, cls, sizeof(cls));
        if (cls[0] == '?') continue;
        StatusInfo& s = u.statuses[u.n_statuses++];
        strip_ns(cls, s.id, sizeof(s.id));
        s.stacks = rdv<int32_t>(p, off::Pass_Stacks);
    }
}

bool read_ability(const void* ab, AbilityInfo& out) {
    out = AbilityInfo{};
    const void* def = rdp(ab, off::Ab_Def);
    if (!read_string((const uint8_t*)def + off::Def_Name, out.id, sizeof(out.id)) || !out.id[0]) return false;
    out.mana_cost      = rdv<int32_t>(ab, off::Ab_ManaCost);
    out.charge         = rdv<int32_t>(ab, off::Ab_Charge);
    out.uses_per_fight = rdv<int32_t>(ab, off::Ab_UsesPerFight);
    return true;
}

void read_unit(const void* ch, UnitInfo& u) {
    u = UnitInfo{};
    u.ch = ch;
    rd(ch, off::Ch_HP, u.hp);
    rd(ch, off::Ch_Shield, u.shield);
    rd(ch, off::Ch_MaxHP, u.max_hp);

    const void* tobj = rdp(ch, off::Ch_TObj);
    int tile[2] = {off::OffBoard, off::OffBoard};
    rd(tobj, off::TO_Tile, tile);
    u.tile_x = tile[0];
    u.tile_y = tile[1];
    u.on_board = tile[0] != off::OffBoard;
    u.is_current = rdp(g_tc, off::TC_CurActor) == ch;

    u.kind = rdv<int32_t>(ch, off::Ch_Kind);
    rd(ch, off::Ch_Mana, u.mana);
    rd(ch, off::Ch_MaxMana, u.max_mana);
    read_wstring((const uint8_t*)ch + off::Ch_DisplayName, u.name, sizeof(u.name));
    read_string((const uint8_t*)ch + off::Ch_NameKey, u.name_key, sizeof(u.name_key));
    read_string((const uint8_t*)ch + off::Ch_DescKey, u.desc_key, sizeof(u.desc_key));
    read_string((const uint8_t*)ch + off::Ch_Class, u.cls, sizeof(u.cls));

    if (g_cat_off == -1 && g_cat_misses < 8) {
        discover_cat_offset(ch);
        if (g_cat_off == -1 && ++g_cat_misses == 8) log_line("roster: no CatData pointer found in Character (8 units tried)");
    }
    if (const void* cat = cat_of(ch)) read_cat(cat, u);

    if (!g_dumped) { g_dumped = true; dump_passives(ch); }
    read_statuses(ch, u);

    // Basic attack, then the authored spellN slots.
    if (const void* atk = rdp(ch, off::Ch_Attack))
        if (read_ability(atk, u.abilities[u.n_abilities])) ++u.n_abilities;
    uint32_t n = rdv<uint32_t>(ch, off::Ch_SpellCount);
    const void* data = rdp(ch, off::Ch_SpellData);
    for (uint32_t i = 0; i < n && i < 16 && u.n_abilities < kMaxAbilities; ++i) {
        const void* ab = rdp(data, i * sizeof(void*));
        if (ab && read_ability(ab, u.abilities[u.n_abilities])) ++u.n_abilities;
    }
}

}  // namespace

bool roster_init() {
    uintptr_t fn = resolve(kSigIsNeutral);
    if (!fn) return false;
    // `4C 8D 1D rel32` is the last 3 bytes of the pattern + 4 bytes of disp.
    const uint8_t* lea = (const uint8_t*)fn + 18;
    int32_t rel = rdv<int32_t>(lea, 3);
    g_team_tables = lea + 7 + rel;
    return true;
}

void roster_set_turn_control(void* tc) { g_tc = tc; }
void* roster_turn_control() { return g_tc; }
void roster_invalidate() {
    g_tc = nullptr;
    g_member_count = 0;
}

bool roster_build(Roster& out) {
    out.valid = false;
    out.n = 0;
    g_member_count = 0;
    if (!g_tc) return false;

    const void* list = char_list();
    uint32_t count = rdv<uint32_t>(list, off::List_Count);
    const void* data = rdp(list, off::List_Data);
    if (!data || count == 0 || count > 254) return false;

    for (uint32_t i = 0; i < count; ++i) {
        const void* ch = rdp(data, i * sizeof(void*));
        if (!ch || !linked(ch)) continue;
        g_members[g_member_count++] = ch;

        bool dead = rdv<bool>(ch, off::Ch_Dead, true);
        int hp = rdv<int>(ch, off::Ch_HP);
        if (dead || hp <= 0) continue;          // corpses drop out
        if (!is_friendly(ch)) continue;
        if (rdv<int32_t>(ch, off::Ch_Kind) == 4) continue;   // board objects, not units
        if (out.n >= kMaxUnits) break;
        read_unit(ch, out.units[out.n++]);
    }
    out.valid = true;
    return true;
}

bool roster_contains(const void* ch) {
    for (uint32_t i = 0; i < g_member_count; ++i)
        if (g_members[i] == ch) return true;
    return false;
}

}  // namespace cr
