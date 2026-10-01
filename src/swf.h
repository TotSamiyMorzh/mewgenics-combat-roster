// swf.h -- a small Flash (SWF) vector renderer for the game's own UI art.
//
// Mewgenics draws its UI and portraits from SWF movie clips (swfs/ui.swf,
// swfs/portraits.swf). We read them from the user's resources.gpak and
// rasterise one frame of an exported clip into an RGBA image:
//   shapes (DefineShape 1-4): solid, linear/radial gradient and bitmap fills,
//   strokes (round joins/caps); sprites: display list of a frame, matrices,
//   colour transforms. Not supported (not needed for icons): text, filters,
//   blend modes, morph shapes, masks (mask layers are skipped).
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cr {

struct SwfImage {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;   // straight alpha, row-major
};

class SwfDoc {
public:
    bool load(std::vector<uint8_t>&& file);
    bool has(const std::string& symbol) const { return symbols_.count(symbol) != 0; }
    int  frame_count(const std::string& symbol) const;

    // Renders `symbol` at `frame`, scaled so its longer side is `size` px.
    bool render(const std::string& symbol, int frame, int size, SwfImage& out) const;

    struct Impl;
private:
    std::vector<uint8_t> body_;
    std::unordered_map<std::string, uint16_t> symbols_;
    std::shared_ptr<Impl> impl_;
};

}  // namespace cr
