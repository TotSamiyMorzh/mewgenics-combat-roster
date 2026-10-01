// game.h -- addresses, offsets and fault-safe memory access for Mewgenics.
//
// Every address is pinned to Mewgenics.exe 1.1.21239 (Steam build 25143593,
// SHA256 4127cd6a...77ea) and is resolved by a unique byte signature at startup,
// with the RVA only as a cross-check. A signature that does not resolve turns
// its feature off rather than letting us call or hook the wrong function.
//
// Provenance for each offset is in MODLOG.md. Most battle offsets were found by
// the mgmp project (SanTertrust/mgmp, MIT); the ones marked [CR] are ours.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cr {

extern uintptr_t g_base;   // Mewgenics.exe image base

// --- signatures --------------------------------------------------------------

struct Sig {
    const char* name;
    uint32_t    rva_hint;
    const char* pattern;   // IDA-style, "?" is a wildcard byte
};

// Returns the absolute address of the unique match in .text, or 0.
uintptr_t resolve(const Sig& sig);

// --- fault-safe reads ----------------------------------------------------------

bool read_bytes(const void* src, void* dst, size_t n);

template <class T>
inline bool rd(const void* base, uintptr_t off, T& out) {
    if (!base) return false;
    return read_bytes((const uint8_t*)base + off, &out, sizeof(T));
}

template <class T>
inline T rdv(const void* base, uintptr_t off, T fallback = T{}) {
    T v{};
    return rd(base, off, v) ? v : fallback;
}

inline const void* rdp(const void* base, uintptr_t off) { return rdv<const void*>(base, off, nullptr); }

// MSVC release std::string / std::wstring (32 bytes: SSO buffer or pointer,
// size at +0x10, capacity at +0x18). Writes UTF-8. Never faults.
bool read_string(const void* str, char* out, size_t out_size);
bool read_wstring(const void* str, char* out_utf8, size_t out_size);

// "glaiel::Poison" from any polymorphic object, via MSVC RTTI. "?" on failure.
const char* rtti_name(const void* obj, char* buf, size_t buf_size);

// True if obj's class is, or derives from, a class whose decorated name
// contains `fragment` (e.g. "Status@glaiel"). Walks the RTTI base class array.
bool rtti_is_a(const void* obj, const char* fragment);

// --- battle offsets (mgmp, verified for this build) ----------------------------

namespace off {
// TurnControl -> the battle's character list.
constexpr uintptr_t TC_Scene      = 0x18;
constexpr uintptr_t Scene_Sub     = 0x08;
constexpr uintptr_t Sub_Holder    = 0x20;
constexpr uintptr_t Holder_List   = 0x1F90;
constexpr uintptr_t List_Count    = 12;    // u32
constexpr uintptr_t List_Data     = 16;    // Character**
constexpr uintptr_t TC_CurActor   = 0x68;  // Character*

// Character
constexpr uintptr_t Ch_TObj       = 0x60;  // TacticsObject*
constexpr uintptr_t Ch_Brain      = 0x68;  // Brain*
constexpr uintptr_t Ch_Move       = 0xD0;  // Ability*
constexpr uintptr_t Ch_Attack     = 0xD8;  // Ability*
constexpr uintptr_t Ch_Bonus      = 0xE0;  // Ability*
constexpr uintptr_t Ch_Team       = 0x350; // i16, current team [CR]
constexpr uintptr_t Ch_TeamOrig   = 0x358; // i16, original team [CR]
constexpr uintptr_t Ch_Kind       = 0xCF4; // i32: 2 boss, 3 cat, 4 object [CR]
constexpr uintptr_t Ch_NameKey    = 0x248; // std::string loc key, e.g. ENEMY_X_NAME [CR]
constexpr uintptr_t Ch_DescKey    = 0x268; // std::string loc key [CR]
constexpr uintptr_t Ch_DisplayName= 0x290; // std::wstring, final localised name (refresh_name) [CR]
constexpr uintptr_t Ch_Class      = 0x2B0; // std::string: cat class ("Medic") or "Boss"/"Enemy" [CR]
constexpr uintptr_t Ch_Mana       = 0xD18; // i32 [CR] (verified live on 6 units)
constexpr uintptr_t Ch_MaxMana    = 0xD1C; // i32 [CR]
constexpr uintptr_t Ch_SpellCount = 0xEC;  // u32
constexpr uintptr_t Ch_SpellData  = 0xF0;  // Ability**
constexpr uintptr_t Ch_HP         = 0x4B0; // i32
constexpr uintptr_t Ch_Shield     = 0x4B4; // i32
constexpr uintptr_t Ch_MaxHP      = 0x4BC; // i32
constexpr uintptr_t Ch_Dead       = 0x4C2; // bool

// TacticsObject
constexpr uintptr_t TO_Tile       = 0x48;  // iVec2D
constexpr uintptr_t TO_Removed    = 0x60;  // bool
constexpr uintptr_t TO_Char       = 0x98;  // Character* (back link)
constexpr int       OffBoard      = -5000; // sentinel tile coordinate

// Passive cache: {u32 cap, u32 count, Passive** data}. Lazily rebuilt by the
// game (dirty flag +0xE50); we only read it, never call anything that rebuilds.
constexpr uintptr_t Ch_PassCount  = 0xE74;
constexpr uintptr_t Ch_PassData   = 0xE78;

// Ability
constexpr uintptr_t Ab_Def        = 0x28;  // -> definition; +0x88 = std::string GON name
constexpr uintptr_t Def_Name      = 0x88;
// Ability cost block (Ability GON parser sub_14002B3B0) [CR]
constexpr uintptr_t Ab_ManaCost   = 0x4C;
constexpr uintptr_t Ab_Charge     = 0x50;
constexpr uintptr_t Ab_UsesPerFight = 0x64;
constexpr uintptr_t Pass_Stacks   = 0x5C;  // [CR] Passive stack count (Trample 3 / BoostHeals 2 / Metal 1 matched GON live)

// CatData (p0lymeric/mewgenics_analysis glaiel_cat.hpp, sizeof 0xC58; field
// offsets cross-checked against cat-bridge: libido 0xBB8, aggression 0xBE8,
// fertility 0xBF0, COI 0xC50; and mgmp: CatStats block at 0x70C).
constexpr uintptr_t Cat_Name      = 0x018;  // std::wstring
constexpr uintptr_t Cat_StatsBase = 0x6F0;  // CatStats (7 x i32) heritable
constexpr uintptr_t Cat_StatsLvl  = 0x70C;  // CatStats delta from levelling
constexpr uintptr_t Cat_StatsInj  = 0x728;  // CatStats delta from injuries
constexpr uintptr_t Cat_Passive0  = 0x910;  // std::string, +0x28 next
constexpr uintptr_t Cat_Passive1  = 0x938;
constexpr uintptr_t Cat_Mutation0 = 0x960;
constexpr uintptr_t Cat_Mutation1 = 0x988;
constexpr uintptr_t Cat_Equip     = 0x9B0;  // 5 x Equipment (0x60): head face neck weapon trinket
constexpr uintptr_t Equip_Size    = 0x60;
constexpr uintptr_t Equip_Name    = 0x08;   // std::string GON id
constexpr uintptr_t Cat_Level     = 0xC30;  // u32
constexpr uintptr_t Cat_SqlKey    = 0xC48;  // i64
constexpr uintptr_t Cat_Coi       = 0xC50;  // double
// BodyParts at CatData+0x60: texture idx +0x18, heritable palette +0x1C, then
// 14 BodyPartDescriptors of 0x54 bytes from +0x2C (part sprite idx at +4):
// body head tail leg1 leg2 arm1 arm2 leye reye lbrow rbrow lear rear mouth.
constexpr uintptr_t Cat_Texture   = 0x60 + 0x18;
constexpr uintptr_t Cat_Palette   = 0x60 + 0x1C;
constexpr uintptr_t Cat_PartIdx(int part) { return 0x60 + 0x2C + part * 0x54 + 4; }

// StatusMenu (the battle HUD component)
constexpr uintptr_t SM_HoverTile  = 124;   // iVec2D, unaligned
}  // namespace off

// --- functions we call ----------------------------------------------------------

using fn_im_game_ui = void*(__fastcall*)(void* component);
using fn_tile_piece = void*(__fastcall*)(void* ui, void* id, int layer, void* anim,
                                         uint64_t tile, const float* rgba, int frame,
                                         const double* scale);

// A std::string the game may consume. Kept <= 15 chars so its destructor is a
// no-op and no memory crosses between our CRT heap and the game's.
struct GameStr {
    char     buf[16];
    uint64_t size;
    uint64_t cap;
};
bool make_str(GameStr& s, const char* text);

struct alignas(16) Rgba  { float  v[4]; };   // MOVAPS in tile_piece: must be 16-aligned
struct alignas(16) Scale { double v[3]; };

}  // namespace cr
