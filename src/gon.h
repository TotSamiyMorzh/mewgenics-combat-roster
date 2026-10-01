// gon.h -- a small reader for GON, the game's data format
// (https://github.com/TylerGlaiel/GON): `key value`, `key { ... }`, `key [ ... ]`,
// `//` and `/* */` comments; commas and colons are whitespace.
#pragma once

#include <string>
#include <vector>

namespace cr {

struct Gon {
    std::string key;
    std::string value;          // for leaves
    std::vector<Gon> kids;      // for objects and arrays
    bool is_obj = false, is_arr = false;

    const Gon* get(const char* k) const {
        for (const Gon& c : kids) if (c.key == k) return &c;
        return nullptr;
    }
    std::string str(const char* k, const std::string& def = {}) const {
        const Gon* c = get(k);
        return c && !c->is_obj && !c->is_arr ? c->value : def;
    }
};

// Parses a whole file into an object whose kids are the top-level entries.
Gon gon_parse(const char* text, size_t len);

}  // namespace cr
