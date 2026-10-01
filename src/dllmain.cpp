// dllmain.cpp -- Combat Roster Panel for Mewgenics.
//
// Loaded by Mewjector (version.dll proxy) from <game>/mods/ during process
// attach, before the game's main thread runs. All we do here is resolve
// addresses and install hooks; everything else happens on the game thread.
#include "assets.h"
#include "game.h"
#include "loc.h"
#include "log.h"
#include "mewjector.h"
#include "overlay.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace cr {
bool hooks_install(MewjectorAPI& mj);
}

namespace {

MewjectorAPI g_mj;

// The image size of the one build these offsets were checked against. Code
// addresses are found by signature and survive a patch; the data-structure
// OFFSETS cannot be checked that way, so on any other build we stay off rather
// than reading the wrong fields.
constexpr DWORD kExpectedSizeOfImage = 0x1574000;

void init() {
    cr::g_base = (uintptr_t)GetModuleHandleW(nullptr);

    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (char* slash = strrchr(exe, '\\')) *slash = 0;
    cr::log_open(exe);
    cr::log_line("Combat Roster Panel v0.5.0 -- base %p", (void*)cr::g_base);

    auto dos = (const IMAGE_DOS_HEADER*)cr::g_base;
    auto nt  = (const IMAGE_NT_HEADERS64*)(cr::g_base + dos->e_lfanew);
    if (nt->OptionalHeader.SizeOfImage != kExpectedSizeOfImage) {
        cr::log_line("!! unsupported game build (SizeOfImage 0x%lX, expected 0x%lX) -- mod stays off",
                     nt->OptionalHeader.SizeOfImage, kExpectedSizeOfImage);
        return;
    }

    if (!MJ_Require("combat_roster") || !MJ_Resolve(&g_mj)) {
        cr::log_line("!! Mewjector v3+ not found -- install Mewjector (version.dll) next to Mewgenics.exe");
        return;
    }

    cr::overlay_set_game_dir(exe);
    cr::overlay_prepare();
    cr::loc_init();
    cr::assets_start(exe);
    if (!cr::hooks_install(g_mj)) {
        cr::log_line("!! hooks incomplete -- mod is inactive");
        return;
    }
    cr::log_line("ready");
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        init();
    }
    return TRUE;
}
