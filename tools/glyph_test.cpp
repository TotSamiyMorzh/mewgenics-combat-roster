#include "gpak.h"
#include "swf.h"
#include "stb_image_write.h"
#include <cstdio>
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    cr::GPak g; g.open(argv[1]);
    std::vector<uint8_t> buf; g.read("swfs/international_fonts.swf", buf);
    cr::SwfDoc d; d.load(std::move(buf));
    auto f = d.font("TikaFontIntl");
    int gi = f->index[0x42D];
    { int w0, h0; float a0, b0; std::vector<uint8_t> t; f->raster(gi, 0.001f, w0, h0, a0, b0, t); }   // parse lazily
    auto& gl = f->glyphs[gi];
    printf("glyph %d segs=%zu bounds %.0f %.0f %.0f %.0f adv %.0f\n", gi, gl.segs.size() / 4, gl.x0, gl.y0, gl.x1, gl.y1, gl.advance);
    for (size_t i = 0; i < gl.segs.size() && i < 40; i += 4) printf("  %.0f,%.0f -> %.0f,%.0f\n", gl.segs[i], gl.segs[i+1], gl.segs[i+2], gl.segs[i+3]);
    int w, h; float ox, oy; std::vector<uint8_t> a;
    f->raster(gi, 64.0f / 28843.0f, w, h, ox, oy, a);
    printf("raster %dx%d off %.1f %.1f\n", w, h, ox, oy);
    stbi_write_png(argv[2], w, h, 1, a.data(), w);
}
