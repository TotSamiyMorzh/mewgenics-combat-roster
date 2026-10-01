// fontloader.h -- Dear ImGui font loader backed by the game's own SWF fonts
// (DefineFont3 outlines from swfs/international_fonts.swf), rasterised on
// demand by our SWF rasteriser. Missing glyphs fall through to a merged
// system font.
#pragma once

#include <memory>

struct ImFont;

namespace cr {

struct SwfFont;

// Adds `font` as a new ImGui font (plus a merged system fallback). The
// SwfFont must outlive the atlas. Returns null on failure.
ImFont* add_swf_font(std::shared_ptr<SwfFont> font, float size_px);

}  // namespace cr
