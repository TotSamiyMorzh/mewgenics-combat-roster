// Offline check of the SWF rasteriser: renders clips from the user's own
// resources.gpak to PNG files (never shipped).
//   swf_test <resources.gpak> <swf path in gpak> <out dir> <symbol[:frame]>...
#include "gpak.h"
#include "swf.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <chrono>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 5) return 1;
    cr::GPak g;
    if (!g.open(argv[1])) { printf("gpak open failed\n"); return 1; }
    std::vector<uint8_t> f;
    if (!g.read(argv[2], f)) { printf("read failed\n"); return 1; }
    cr::SwfDoc doc;
    auto t0 = std::chrono::steady_clock::now();
    if (!doc.load(std::move(f))) { printf("swf load failed\n"); return 1; }
    printf("loaded in %.0f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    for (int i = 4; i < argc; ++i) {
        std::string arg = argv[i], sym = arg;
        int frame = 0, size = 128;
        if (auto c = arg.find(':'); c != std::string::npos) {
            sym = arg.substr(0, c);
            frame = atoi(arg.c_str() + c + 1);
            if (auto c2 = arg.find(':', c + 1); c2 != std::string::npos) size = atoi(arg.c_str() + c2 + 1);
        }
        cr::SwfImage im;
        auto t = std::chrono::steady_clock::now();
        bool ok = doc.render(sym, frame, size, im);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
        printf("%s frame %d: %s %dx%d %.1f ms (frames=%d)\n", sym.c_str(), frame, ok ? "ok" : "FAIL", im.w, im.h, ms, doc.frame_count(sym));
        if (ok) {
            std::string out = std::string(argv[3]) + "/" + sym + "_" + std::to_string(frame) + ".png";
            stbi_write_png(out.c_str(), im.w, im.h, 4, im.rgba.data(), im.w * 4);
        }
    }
}
