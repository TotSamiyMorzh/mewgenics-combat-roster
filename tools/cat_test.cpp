// Offline check of cat face composition against the user's own resources.gpak.
#include "catportrait.h"
#include "gpak.h"
#include "swf.h"

#define STB_IMAGE_IMPLEMENTATION_UNUSED
#include "stb_image.h"
#include "stb_image_write.h"

#include <cstdio>

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    cr::GPak g;
    if (argc < 3 || !g.open(argv[1])) return 1;
    std::vector<uint8_t> buf;
    cr::SwfDoc cp;
    if (!g.read("swfs/catparts.swf", buf) || !cp.load(std::move(buf))) { printf("catparts failed\n"); return 1; }
    cp.hide_instances({"sloticon", "label"});
    std::vector<uint8_t> png;
    g.read("textures/palette.png", png);
    int pw, ph, pn;
    uint8_t* pal = stbi_load_from_memory(png.data(), (int)png.size(), &pw, &ph, &pn, 3);
    printf("palette %dx%d\n", pw, ph);
    struct { const char* name; cr::CatLook c; } cats[] = {
        {"zhmilek", {173, 70, 38, 212, 243, 43, 52, 0, 181}},
        {"elaida", {173, 106, 246, 245, 204, 43, 64, 0, 0}},
        {"alisa", {13, 70, 82, 212, 204, 106, 51, 145, 0}},
        {"lusik", {97, 212, 167, 187, 155, 141, 63, 53, 173}},
    };
    for (auto& k : cats) {
        for (int brows = 0; brows < 2; ++brows) {
            cr::CatLook c = k.c;
            if (!brows) c.brow = 0;
            cr::SwfImage im;
            bool ok = cp.render_ex("CatHeadPlacements", c.head - 1, 220, cr::cat_face_opts(c, pal + c.palette * 16 * 3), im);
            std::string out = std::string(argv[2]) + "/" + k.name + (brows ? "_brows" : "") + ".png";
            printf("%s brows=%d: %s %dx%d\n", k.name, brows, ok ? "ok" : "FAIL", im.w, im.h);
            if (ok) stbi_write_png(out.c_str(), im.w, im.h, 4, im.rgba.data(), im.w * 4);
        }
    }
}
