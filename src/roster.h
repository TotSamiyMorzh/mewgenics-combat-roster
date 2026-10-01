// roster.h -- the live list of friendly units in the current battle.
//
// Rebuilt from game memory once per frame on the game's main thread (from the
// swap hook, between frames), so nothing here needs a lock. Every read is
// fault-safe; a unit whose pointers do not round-trip is dropped, not guessed.
// Strings here are raw game ids / localisation keys; the panel translates them.
#pragma once

#include "catportrait.h"

#include <cstdint>

namespace cr {

constexpr int kMaxUnits     = 32;
constexpr int kMaxStatuses  = 24;
constexpr int kMaxAbilities = 12;

struct StatusInfo {
    char id[48];       // class name == keyword_tooltips.gon key, e.g. "Bleed"
    int  stacks;
};

struct AbilityInfo {
    char id[48];       // GON id, e.g. "RangedHeal"
    int  mana_cost;
    int  charge;       // turns to charge (cost.charge), 0 = none
    int  uses_per_fight;
};

struct UnitInfo {
    const void* ch = nullptr;   // Character*, valid only this frame
    int  hp = 0, shield = 0, max_hp = 0;
    int  mana = 0, max_mana = 0;
    int  tile_x = 0, tile_y = 0;
    bool on_board = false;
    bool is_current = false;    // it is this unit's turn
    int  kind = 0;              // Character+0xCF4: 2 boss, 3 cat, 4 object

    char name[96]     = {};     // display name the game built (UTF-8)
    char name_key[64] = {};     // e.g. "ENEMY_ZARATANAFRIENDLY_NAME"
    char desc_key[64] = {};
    char cls[32]      = {};     // class id: "Medic", "Butcher"; "Boss"/"Enemy" for others

    bool has_cat = false;       // CatData resolved
    CatLook look;               // face parts; look.palette = heritable row (panel swaps in the class row)
    int  stats[7] = {};         // str dex con int spd cha lck
    int  level = 0;
    char equip[5][48] = {};     // item ids: head face neck weapon trinket
    char mutations[2][48] = {};
    char passives[2][48]  = {};

    StatusInfo  statuses[kMaxStatuses];
    int         n_statuses = 0;
    AbilityInfo abilities[kMaxAbilities];
    int         n_abilities = 0;
};

struct Roster {
    bool     valid = false;      // a battle's character list was read this frame
    int      n = 0;
    UnitInfo units[kMaxUnits];
};

bool roster_init();                 // resolves the team tables
void roster_set_turn_control(void* tc);
void* roster_turn_control();
void roster_invalidate();

// Rebuild `out` from memory. Returns out.valid.
bool roster_build(Roster& out);

// True if `ch` is still a live member of the current battle (checked before a
// pointer is handed back to the game, e.g. for the board highlight).
bool roster_contains(const void* ch);

}  // namespace cr
