#pragma once

#include "roster.h"

namespace cr {

struct PanelState {
    bool  collapsed = false;
    float slide = 1.0f;   // 1 = fully shown, 0 = hidden behind the screen edge
};

// Draws the roster panel. Returns the Character* whose row the mouse is over
// (for the board highlight), or nullptr.
const void* panel_draw(const Roster& r, PanelState& st, float dt, float scale);

}  // namespace cr
