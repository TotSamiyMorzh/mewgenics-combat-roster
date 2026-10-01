// loc.h -- text in the player's language, from the game's own StringsDatabase.
//
// glaiel::StringsDatabase lives at a global (resolved by signature from
// Character::refresh_name); its lookup takes a key such as "KEYWORD_BLEED_NAME"
// and returns the string in whatever language the game is set to. We only ever
// call it from the game thread (the swap hook) and cache every answer.
#pragma once

#include <string>

namespace cr {

bool loc_init();

// UTF-8 text for `key`, with the game's markup ([m], [img:x], ...) removed.
// Empty if the key is empty or unknown.
const std::string& tr(const std::string& key);

// The game's current language code ("ru", "en", ...), read from the database.
std::string loc_lang();

// Replaces {stacks}/{absstacks} with `stacks`.
std::string with_stacks(const std::string& text, int stacks);

}  // namespace cr
