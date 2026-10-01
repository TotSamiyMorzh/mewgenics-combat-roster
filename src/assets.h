// assets.h -- the game's own art and data, read from the user's resources.gpak.
//
// A worker thread opens the archive, parses swfs/ui.swf + swfs/portraits.swf
// and the GON definitions we need (names/descriptions keys, portraits). Images
// are rasterised by the worker on request and uploaded to GL by the game
// thread. Nothing is cached to disk or shipped.
#pragma once

#include <cstdint>
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

void assets_upload_pending();   // game thread, GL context current
void assets_gl_lost();          // the GL context was recreated

// --- data ------------------------------------------------------------------
struct TextKeys {
    std::string name, desc, desc_stacks;
};
const TextKeys* keys_item(const std::string& id);
const TextKeys* keys_ability(const std::string& id);
const TextKeys* keys_passive(const std::string& id);
const TextKeys* keys_class(const std::string& id);
const TextKeys* keys_keyword(const std::string& id);
// Portrait clip for a non-cat character, by its name key (Character+0x248).
std::string portrait_for(const std::string& name_key);

// --- status icons ----------------------------------------------------------
// StatusIcon frame (0-based) for a status class name and stack sign, or -1 if
// the game shows no icon for it. Read from the game's own table.
int status_icon_frame(const std::string& status, bool negative);
bool status_icons_init();   // game thread; resolves the table

}  // namespace cr
