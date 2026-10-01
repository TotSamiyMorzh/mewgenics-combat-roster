// Offline check of the GON reader against the user's resources.gpak.
#include "gon.h"
#include "gpak.h"

#include <cstdio>

int main(int argc, char** argv) {
    cr::GPak g;
    if (argc < 2 || !g.open(argv[1])) return 1;
    for (const char* f : {"data/items/trinkets.gon", "data/abilities/medic_abilities.gon", "data/keyword_tooltips.gon",
                          "data/characters/kaijus.gon", "data/classes/classes.gon"}) {
        std::vector<uint8_t> b;
        if (!g.read(f, b)) { printf("%s: read failed\n", f); continue; }
        cr::Gon root = cr::gon_parse((const char*)b.data(), b.size());
        printf("%s: %zu entries\n", f, root.kids.size());
        for (size_t i = 0; i < root.kids.size() && i < 3; ++i) {
            const cr::Gon& e = root.kids[i];
            const cr::Gon* meta = e.get("meta");
            const cr::Gon* gr = e.get("graphics");
            printf("  %s name=%s meta.name=%s graphics.name=%s movieclip=%s tooltip=%s\n", e.key.c_str(), e.str("name").c_str(),
                   meta ? meta->str("name").c_str() : "-", gr ? gr->str("name").c_str() : "-",
                   gr ? gr->str("movieclip").c_str() : "-", e.str("tooltip_stacks").c_str());
        }
    }
}
