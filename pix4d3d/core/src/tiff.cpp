#include "asec/tiff.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <zlib.h>

namespace asec {
namespace {
struct Tag { uint16_t id, type; uint32_t count; std::vector<uint8_t> data; };
enum { T_BYTE = 1, T_ASCII = 2, T_SHORT = 3, T_LONG = 4, T_RATIONAL = 5, T_DOUBLE = 12 };

template <class T> void put(std::vector<uint8_t>& v, T x) { uint8_t b[sizeof(T)]; std::memcpy(b, &x, sizeof(T)); v.insert(v.end(), b, b + sizeof(T)); }
Tag shorts(uint16_t id, const std::vector<uint16_t>& s) { Tag t{id, T_SHORT, uint32_t(s.size()), {}}; for (auto x : s) put(t.data, x); return t; }
Tag longs(uint16_t id, const std::vector<uint32_t>& s) { Tag t{id, T_LONG, uint32_t(s.size()), {}}; for (auto x : s) put(t.data, x); return t; }
Tag doubles(uint16_t id, const std::vector<double>& s) { Tag t{id, T_DOUBLE, uint32_t(s.size()), {}}; for (auto x : s) put(t.data, x); return t; }
Tag ascii(uint16_t id, const std::string& s) { Tag t{id, T_ASCII, uint32_t(s.size() + 1), {}}; t.data.assign(s.begin(), s.end()); t.data.push_back(0); return t; }
Tag rational(uint16_t id, double v) { Tag t{id, T_RATIONAL, 1, {}}; uint32_t den = 10000; put(t.data, uint32_t(std::llround(v * den))); put(t.data, den); return t; }
}  // namespace

bool writeTiff(const std::filesystem::path& p, const RgbaImage& img, const TiffOptions& o, std::string* err) {
    if (img.empty()) { if (err) *err = "빈 영상"; return false; }
    const int W = img.w, H = img.h, spp = o.alpha ? 4 : 3;
    const size_t rowBytes = size_t(W) * spp;
    uint32_t rps = uint32_t(std::max<size_t>(1, (256 * 1024) / rowBytes));
    if (rps > uint32_t(H)) rps = uint32_t(H);
    const uint32_t nStrips = (uint32_t(H) + rps - 1) / rps;
    std::ofstream f(p, std::ios::binary);
    if (!f) { if (err) *err = "쓸 수 없음: " + p.u8string(); return false; }
    std::vector<uint8_t> hdr{'I', 'I', 42, 0, 0, 0, 0, 0};
    f.write(reinterpret_cast<char*>(hdr.data()), 8);
    uint64_t pos = 8;
    std::vector<uint32_t> offs, cnts;
    std::vector<uint8_t> raw, comp;
    for (uint32_t s = 0; s < nStrips; ++s) {
        int r0 = int(s * rps), r1 = std::min(H, int((s + 1) * rps));
        raw.resize(size_t(r1 - r0) * rowBytes);
        for (int r = r0; r < r1; ++r) {
            uint8_t* dst = &raw[size_t(r - r0) * rowBytes];
            const uint8_t* src = &img.px[size_t(r) * W * 4];
            if (spp == 4) std::memcpy(dst, src, rowBytes);
            else for (int x = 0; x < W; ++x) {  // 흰 바탕 합성
                unsigned a = src[x * 4 + 3];
                for (int k = 0; k < 3; ++k) dst[x * 3 + k] = uint8_t((src[x * 4 + k] * a + 255 * (255 - a) + 127) / 255);
            }
            if (o.deflate) for (size_t i = rowBytes - 1; i >= size_t(spp); --i) dst[i] = uint8_t(dst[i] - dst[i - spp]);  // 예측자 2
        }
        const std::vector<uint8_t>* out = &raw;
        if (o.deflate) {
            uLongf cl = compressBound(uLong(raw.size()));
            comp.resize(cl);
            if (compress2(comp.data(), &cl, raw.data(), uLong(raw.size()), 6) != Z_OK) { if (err) *err = "압축 실패"; return false; }
            comp.resize(cl);
            out = &comp;
        }
        if (pos + out->size() > 0xFFFFFFF0ULL) { if (err) *err = "TIFF 4 GB 한도 초과(축척·DPI 를 낮추세요)"; return false; }
        offs.push_back(uint32_t(pos)); cnts.push_back(uint32_t(out->size()));
        f.write(reinterpret_cast<const char*>(out->data()), std::streamsize(out->size()));
        pos += out->size();
    }
    std::vector<Tag> tags;
    tags.push_back(longs(256, {uint32_t(W)}));
    tags.push_back(longs(257, {uint32_t(H)}));
    tags.push_back(shorts(258, std::vector<uint16_t>(size_t(spp), 8)));
    tags.push_back(shorts(259, {uint16_t(o.deflate ? 8 : 1)}));
    tags.push_back(shorts(262, {2}));
    if (!o.description.empty()) tags.push_back(ascii(270, o.description));
    tags.push_back(longs(273, offs));
    tags.push_back(shorts(277, {uint16_t(spp)}));
    tags.push_back(longs(278, {rps}));
    tags.push_back(longs(279, cnts));
    tags.push_back(rational(282, o.dpi));
    tags.push_back(rational(283, o.dpi));
    tags.push_back(shorts(284, {1}));
    tags.push_back(shorts(296, {2}));
    tags.push_back(ascii(305, o.software));
    if (o.deflate) tags.push_back(shorts(317, {2}));
    if (spp == 4) tags.push_back(shorts(338, {2}));
    if (o.geo.enabled) {
        tags.push_back(doubles(33550, {o.geo.scaleX, o.geo.scaleY, 0.0}));
        tags.push_back(doubles(33922, {0, 0, 0, o.geo.tieX, o.geo.tieY, 0}));
        std::string asc = (o.geo.citation.empty() ? std::string("ExcavSection") : o.geo.citation) + "|";
        std::vector<uint16_t> k{1, 1, 0, 0};
        auto key = [&](uint16_t id, uint16_t loc, uint16_t cnt, uint16_t val) { k.insert(k.end(), {id, loc, cnt, val}); k[3]++; };
        key(1024, 0, 1, 1);                                  // GTModelType = Projected
        key(1025, 0, 1, 1);                                  // GTRasterType = PixelIsArea
        key(1026, 34737, uint16_t(asc.size()), 0);           // GTCitation
        key(3072, 0, 1, uint16_t(o.geo.epsg > 0 && o.geo.epsg < 32767 ? o.geo.epsg : 32767));  // ProjectedCSType
        key(3076, 0, 1, 9001);                               // ProjLinearUnits = metre
        if (o.geo.vertEpsg > 0 && o.geo.vertEpsg < 32767) {
            key(4096, 0, 1, uint16_t(o.geo.vertEpsg));       // VerticalCSType (예 5711)
            key(4099, 0, 1, 9001);                           // VerticalUnits = metre
        }
        tags.push_back(shorts(34735, k));
        tags.push_back(ascii(34737, asc));
    }
    // IFD: 워드 정렬
    if (pos & 1) { char z = 0; f.write(&z, 1); pos++; }
    uint64_t ifdPos = pos;
    uint64_t dataPos = ifdPos + 2 + tags.size() * 12 + 4;
    std::vector<uint8_t> ifd, extra;
    put(ifd, uint16_t(tags.size()));
    for (auto& t : tags) {
        put(ifd, t.id); put(ifd, t.type); put(ifd, t.count);
        if (t.data.size() <= 4) { auto d = t.data; d.resize(4, 0); ifd.insert(ifd.end(), d.begin(), d.end()); }
        else {
            put(ifd, uint32_t(dataPos + extra.size()));
            extra.insert(extra.end(), t.data.begin(), t.data.end());
            if (extra.size() & 1) extra.push_back(0);
        }
    }
    put(ifd, uint32_t(0));
    f.write(reinterpret_cast<char*>(ifd.data()), std::streamsize(ifd.size()));
    f.write(reinterpret_cast<char*>(extra.data()), std::streamsize(extra.size()));
    f.seekp(4);
    uint32_t ip = uint32_t(ifdPos);
    f.write(reinterpret_cast<char*>(&ip), 4);
    return bool(f);
}

bool writeWorldFile(const std::filesystem::path& p, const GeoRef& g, std::string* err) {
    std::ofstream f(p);
    if (!f) { if (err) *err = "쓸 수 없음: " + p.u8string(); return false; }
    char b[512];
    std::snprintf(b, sizeof b, "%.10f\n0.0\n0.0\n%.10f\n%.6f\n%.6f\n", g.scaleX, -g.scaleY, g.tieX + g.scaleX / 2, g.tieY - g.scaleY / 2);
    f << b;
    return bool(f);
}

}  // namespace asec
