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
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace cr {

struct SwfImage {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;   // straight alpha, row-major
};

// A DefineFont3 font: glyph outlines in font units (EM = 20480).
struct SwfFont {
    struct Glyph {
        std::vector<float> segs;   // x0 y0 x1 y1 per edge, oriented for nonzero fill
        float advance = 0;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // bounds
    };
    std::string name;
    std::unordered_map<uint32_t, int> index;   // codepoint -> glyph
    std::vector<Glyph> glyphs;
    float ascent = 0, descent = 0, leading = 0;
    static constexpr float kEm = 20480.0f;

    // Alpha8 coverage of a glyph at `scale` px per font unit. (ox, oy) is the
    // bitmap's top-left relative to the pen position on the baseline.
    bool raster(int glyph, float scale, int& w, int& h, float& ox, float& oy, std::vector<uint8_t>& alpha) const;
};

class SwfDoc {
public:
    // Font by (prefix of) its name, e.g. "TikaFontIntl". Null if absent.
    std::shared_ptr<SwfFont> font(const std::string& name_prefix) const;
    // A DefineBitsLossless bitmap by character id, as RGBA.
    bool bitmap(uint16_t id, SwfImage& out) const;

    bool load(std::vector<uint8_t>&& file);
    bool has(const std::string& symbol) const { return symbols_.count(symbol) != 0; }
    int  frame_count(const std::string& symbol) const;

    // 0-based frame carrying `label` in `symbol`'s timeline, or -1.
    int frame_of_label(const std::string& symbol, const std::string& label) const;
    // Instances with these names are never drawn (the game toggles them in code).
    void hide_instances(std::vector<std::string> names) { hidden_ = std::move(names); }

    // Renders `symbol` at `frame`, scaled so its longer side is `size` px.
    bool render(const std::string& symbol, int frame, int size, SwfImage& out) const;

    struct Impl;
private:
    std::vector<uint8_t> body_;
    std::unordered_map<std::string, uint16_t> symbols_;
    std::shared_ptr<Impl> impl_;
    std::vector<std::string> hidden_;
    mutable std::unordered_map<std::string, std::unordered_map<std::string, int>> labels_;
    mutable std::mutex labels_mu_;
};

}  // namespace cr
