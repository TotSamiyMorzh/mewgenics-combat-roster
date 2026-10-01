// gpak.h -- read files out of the user's own resources.gpak.
//
// Format (mgmp CLAUDE.md, GPak::LoadIndex): u32 count, then per entry
// { u16 name_len, char name[name_len], u32 size }; payloads follow the whole
// index back to back. Nothing from the archive is ever written or shipped.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace cr {

class GPak {
public:
    bool open(const std::string& path);
    bool read(const std::string& name, std::vector<uint8_t>& out) const;
    std::vector<std::string> list(const std::string& prefix, const std::string& suffix) const;

private:
    struct Entry { uint64_t offset; uint32_t size; };
    std::string path_;
    std::unordered_map<std::string, Entry> entries_;
};

}  // namespace cr
