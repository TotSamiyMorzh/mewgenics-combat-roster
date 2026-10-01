#pragma once

#include <cstdint>

namespace cr {

// overlay.cpp
void overlay_set_game_dir(const char* dir);
void overlay_prepare();                    // DllMain: remember the SDL swap slot
void overlay_try_install(uint64_t frame);  // every FrameBegin until installed

// hooks.cpp
bool battle_active();                      // the battle HUD ticked this frame
void set_hover_unit(const void* ch);       // panel -> board highlight

}  // namespace cr
