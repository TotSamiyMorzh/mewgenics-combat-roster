#pragma once

#include "roster.h"

#include <string>

namespace cr {

struct PanelState {
    bool  loaded = false;
    bool  collapsed = false;
    float slide = 1.0f;          // 1 = fully shown, 0 = tucked behind its edge
    float fx = -1.0f, fy = 0.30f; // top-left as a fraction of the display; fx<0 = snap right
    bool  dragging = false;
    std::string ini_path;        // <game>/mods/combat_roster.ini
};

void panel_load(PanelState& st, const std::string& game_dir);

// Draws the roster panel. Returns the Character* whose row the mouse is over
// (for the board highlight), or nullptr.
const void* panel_draw(const Roster& r, PanelState& st, float dt, float scale);

}  // namespace cr
