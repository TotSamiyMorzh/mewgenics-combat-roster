// hooks.cpp -- the three game functions we hook, and the board highlight.
//
//   FrameBegin         frame counter; installs the overlay once SDL is live
//   TurnControl::NextTurn   captures the battle's TurnControl*
//   StatusMenu::update  "a battle is on screen this frame", and the place we
//                       submit the highlight: the game's immediate-mode UI is
//                       reachable from this component and the submit lands in
//                       the same frame slot as the game's own aim cursor.
//
// The highlight is the game's own aim reticle: ImmediateModeGameUI::tile_piece
// with the "TargetCursor" clip from swfs/ui.swf -- the exact call
// Brain::UpdateDecision makes when you aim a skill. It is immediate-mode: we
// re-submit it every frame the mouse is on a portrait and it disappears by
// itself the first frame we stop. Nothing is allocated, nothing to clean up.
//
// Deliberately NOT used: sub_140139430 ("ability highlight"). Its name suggests
// a draw but it applies statuses to characters -- it mutates the battle (mgmp
// learned this the hard way). A QoL mod must never change the simulation.
#include "game.h"
#include "log.h"
#include "mewjector.h"
#include "overlay.h"
#include "roster.h"

namespace cr {
namespace {

const Sig kSigFrameBegin = {"FrameBegin", 0x009B7870,
    "48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 48 8D 68 A1"};
const Sig kSigNextTurn = {"TurnControl::NextTurn", 0x008E3C60,
    "48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 28 EF FF FF B8 D8 11 00 00 E8 ? ? ? ? 48 2B E0 0F 29 B4 24 C0 11 00 00 48 8B F1"};
const Sig kSigStatusMenu = {"StatusMenu::update", 0x0081AEE0,
    "48 8B C4 48 89 58 08 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 B8 FA FF FF"};
const Sig kSigImGameUI = {"Component::im_game_ui", 0x0013CF90,
    "48 89 5C 24 08 57 48 83 EC 20 48 8B 79 18 BA 6A 04 00 00"};
const Sig kSigTilePiece = {"ImmediateModeGameUI::tile_piece", 0x00340B90,
    "4C 8B DC 49 89 5B 08 49 89 6B 18 4D 89 4B 20"};

using fn_void = void(__fastcall*)(void* self);

fn_void o_frame = nullptr;
fn_void o_next_turn = nullptr;
fn_void o_status_menu = nullptr;
fn_im_game_ui im_game_ui = nullptr;
fn_tile_piece tile_piece = nullptr;

uint64_t g_frame = 0;
uint64_t g_sm_frame = 0;
const void* g_hover = nullptr;
bool g_highlight_ok = false;

void submit_highlight(void* status_menu) {
    if (!g_highlight_ok || !g_hover || !roster_contains(g_hover)) return;
    const void* tobj = rdp(g_hover, off::Ch_TObj);
    int tile[2];
    if (!rd(tobj, off::TO_Tile, tile) || tile[0] == off::OffBoard) return;

    void* ui = im_game_ui(status_menu);
    if (!ui) return;
    GameStr id, anim;
    make_str(id, "crhl");             // our own identity; the game's is "target"
    make_str(anim, "TargetCursor");   // the SWF clip the aim cursor uses
    static Rgba  colour = {{1.00f, 0.85f, 0.25f, 0.95f}};   // gold, readable on the board
    static Scale scale  = {{1.0, 1.0, 1.0}};
    uint64_t packed = (uint32_t)tile[0] | ((uint64_t)(uint32_t)tile[1] << 32);
    tile_piece(ui, &id, 6, &anim, packed, colour.v, -1, scale.v);
}

void __fastcall h_frame(void* self) {
    ++g_frame;
    overlay_try_install(g_frame);
    // The battle HUD stopped ticking a while ago: that battle is over and its
    // TurnControl may be freed. Forget it before anything reads through it.
    if (g_frame - g_sm_frame > 30 && roster_turn_control()) roster_invalidate();
    o_frame(self);
}

void __fastcall h_next_turn(void* self) {
    roster_set_turn_control(self);
    o_next_turn(self);
}

void __fastcall h_status_menu(void* self) {
    o_status_menu(self);
    static int logged = 0;
    uint64_t gap = g_frame - g_sm_frame;
    if (g_sm_frame && gap > 2 && gap <= 30 && logged < 5) {
        ++logged;
        log_line("hud: battle HUD skipped %llu frames (tolerated)", (unsigned long long)(gap - 1));
    }
    g_sm_frame = g_frame;
    __try {
        submit_highlight(self);
    } __except (log_exception("highlight", GetExceptionInformation())) {
        g_highlight_ok = false;
    }
}

bool hook(MewjectorAPI& mj, const Sig& sig, void* fn, fn_void* orig) {
    uintptr_t at = resolve(sig);
    if (!at) {
        log_line("!! signature not found: %s (hint rva 0x%X) -- game updated?", sig.name, sig.rva_hint);
        return false;
    }
    void* tramp = nullptr;
    if (!mj.InstallHook(at - g_base, 0, fn, &tramp, 60, "combat_roster") || !tramp) {
        log_line("!! hook failed: %s", sig.name);
        return false;
    }
    *orig = (fn_void)tramp;
    log_line("hooked %s at rva 0x%llX", sig.name, (unsigned long long)(at - g_base));
    return true;
}

}  // namespace

// The battle HUD does not tick on every single frame (it skips some, e.g. while
// the pointer is parked off-window over our panel). A short gap is not the end
// of a battle; treating it as one made the panel blink. 20 frames ~ 1/3 s.
constexpr uint64_t kHudGapFrames = 20;
bool battle_active() { return g_sm_frame != 0 && g_frame - g_sm_frame <= kHudGapFrames && roster_turn_control(); }

void set_hover_unit(const void* ch) { g_hover = ch; }

bool hooks_install(MewjectorAPI& mj) {
    im_game_ui = (fn_im_game_ui)resolve(kSigImGameUI);
    tile_piece = (fn_tile_piece)resolve(kSigTilePiece);
    g_highlight_ok = im_game_ui && tile_piece;
    if (!g_highlight_ok) log_line("!! board highlight disabled: call targets not found");

    if (!roster_init()) { log_line("!! team table not found -- mod is inactive"); return false; }

    bool ok = hook(mj, kSigFrameBegin, (void*)&h_frame, &o_frame);
    ok = ok && hook(mj, kSigNextTurn, (void*)&h_next_turn, &o_next_turn);
    ok = ok && hook(mj, kSigStatusMenu, (void*)&h_status_menu, &o_status_menu);
    return ok;
}

}  // namespace cr
