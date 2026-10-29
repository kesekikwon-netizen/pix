#pragma once
#include <filesystem>
#include <random>
#include "stb_image.h"
#include "asec/geom.hpp"

inline std::filesystem::path tmpDir(const std::string& name) {
    auto d = std::filesystem::temp_directory_path() / ("asec_test_" + name);
    std::filesystem::remove_all(d);
    std::filesystem::create_directories(d);
    return d;
}
inline void decodeTexStb(asec::Texture& t) {
    int w, h, c;
    unsigned char* p = stbi_load_from_memory(t.encoded.data(), int(t.encoded.size()), &w, &h, &c, 4);
    if (!p) return;
    t.rgba.w = w; t.rgba.h = h; t.rgba.px.assign(p, p + size_t(w) * h * 4);
    stbi_image_free(p);
}
