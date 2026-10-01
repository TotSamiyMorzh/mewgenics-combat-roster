#include "gpak.h"

#include <cstdio>

namespace cr {

bool GPak::open(const std::string& path) {
    path_ = path;
    entries_.clear();
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") || !f) return false;
    uint32_t count = 0;
    bool ok = fread(&count, 4, 1, f) == 1 && count < 1000000;
    std::vector<std::pair<std::string, uint32_t>> idx;
    idx.reserve(count);
    for (uint32_t i = 0; ok && i < count; ++i) {
        uint16_t len = 0;
        uint32_t size = 0;
        std::string name;
        ok = fread(&len, 2, 1, f) == 1;
        name.resize(len);
        ok = ok && fread(name.data(), 1, len, f) == len && fread(&size, 4, 1, f) == 1;
        if (ok) idx.emplace_back(std::move(name), size);
    }
    // The data section starts after the WHOLE index, so offsets are only known now.
    uint64_t at = ok ? (uint64_t)_ftelli64(f) : 0;
    fclose(f);
    if (!ok) return false;
    for (auto& [name, size] : idx) {
        entries_[name] = {at, size};
        at += size;
    }
    return true;
}

bool GPak::read(const std::string& name, std::vector<uint8_t>& out) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) return false;
    FILE* f = nullptr;
    if (fopen_s(&f, path_.c_str(), "rb") || !f) return false;
    out.resize(it->second.size);
    bool ok = _fseeki64(f, (long long)it->second.offset, SEEK_SET) == 0 &&
              fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

std::vector<std::string> GPak::list(const std::string& prefix, const std::string& suffix) const {
    std::vector<std::string> out;
    for (auto& [name, e] : entries_)
        if (name.rfind(prefix, 0) == 0 && name.size() >= suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            out.push_back(name);
    return out;
}

}  // namespace cr
