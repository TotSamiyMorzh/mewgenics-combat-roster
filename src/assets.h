// assets.h -- the game's own art and data, read from the user's resources.gpak.
//
// A worker thread opens the archive, parses swfs/ui.swf + swfs/portraits.swf
// and the GON definitions we need (names/descriptions keys, portraits). Images
// are rasterised by the worker on request and uploaded to GL by the game
// thread. Nothing is cached to disk or shipped.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace cr {

void assets_start(const std::string& game_dir);   // once; spawns the worker
bool assets_ready();                                // data maps + SWFs loaded

// --- images ----------------------------------------------------------------
enum class Swf { Ui, Portraits };

struct Tex {
    uint64_t id = 0;   // GL texture name as an ImTextureID; 0 = not ready yet
    float w = 0, h = 0;
};
// Rasterised at `px` (longer side). Queues the render on first use.
Tex asset_image(Swf swf, const std::string& symbol, int frame, int px);
bool asset_has(Swf swf, const std::string& symbol);
// A raw bitmap from ui.swf by character id (e.g. the paper texture).
Tex asset_bitmap(int id);
// A PNG from the archive, e.g. "textures/cursor/default.png".
Tex asset_png(const std::string& path);
// Cursor hotspot in texture pixels (textures/cursor/hotspots.gon).
void cursor_hotspot(const std::string& state, float& x, float& y);

void assets_upload_pending();   // game thread, GL context current
void assets_gl_lost();          // the GL context was recreated

// --- data ------------------------------------------------------------------
struct TextKeys {
    std::string name, desc, desc_stacks;
    std::string ability;   // items: the ability the item grants
};
const TextKeys* keys_item(const std::string& id);
const TextKeys* keys_ability(const std::string& id);
const TextKeys* keys_passive(const std::string& id);
const TextKeys* keys_class(const std::string& id);
const TextKeys* keys_keyword(const std::string& id);
// Portrait clip for a non-cat character, by its name key (Character+0x248).
std::string portrait_for(const std::string& name_key);

// --- fonts -----------------------------------------------------------------
struct SwfFont;
// The game's fonts (from swfs/international_fonts.swf, Latin + Cyrillic).
std::shared_ptr<SwfFont> font_body();    // TikaFontIntl
std::shared_ptr<SwfFont> font_title();   // Mewgenics Organ Grinder Cyr

// --- status icons ----------------------------------------------------------
// StatusIcon frame (0-based) for a status class name and stack sign, or -1 if
// the game shows no icon for it. Read from the game's own table.
int status_icon_frame(const std::string& status, bool negative);
bool status_icons_init();   // game thread; resolves the table

}  // namespace cr
