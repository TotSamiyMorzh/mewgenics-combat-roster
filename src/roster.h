// roster.h -- the live list of friendly units in the current battle.
//
// Rebuilt from game memory once per frame on the game's main thread (from the
// swap hook, between frames), so nothing here needs a lock. Every read is
// fault-safe; a unit whose pointers do not round-trip is dropped, not guessed.
#pragma once

#include <cstdint>

namespace cr {

constexpr int kMaxUnits     = 32;
constexpr int kMaxStatuses  = 16;
constexpr int kMaxAbilities = 12;

struct StatusInfo {
    char name[48];
    int  stacks;   // -1 when unknown
};

struct AbilityInfo {
    char name[48];
    int  cooldown;  // turns remaining; 0 = ready; -1 = unknown
};

struct UnitInfo {
    const void* ch = nullptr;   // Character*, valid only this frame
    int  hp = 0, shield = 0, max_hp = 0;
    int  mana = -1, max_mana = -1;
    int  tile_x = 0, tile_y = 0;
    bool on_board = false;
    bool is_current = false;    // it is this unit's turn

    char cls[48]  = {};         // RTTI / character type, e.g. "Cat"
    char name[64] = {};

    bool has_cat = false;       // CatData resolved
    int  stats[7] = {};         // str dex con int spd cha lck
    int  level = 0;
    char equip[5][48] = {};     // head face neck weapon trinket
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
    int      total_listed = 0;   // every character in the list, any side
};

// Called from the battle hooks.
bool roster_init();                 // resolves the team tables
void roster_set_turn_control(void* tc);
void* roster_turn_control();
void roster_invalidate();

// Rebuild `out` from memory. Returns out.valid.
bool roster_build(Roster& out);

// True if `ch` is still a live member of the current battle (used before we
// hand a pointer back to the game, e.g. for the board highlight).
bool roster_contains(const void* ch);

}  // namespace cr
