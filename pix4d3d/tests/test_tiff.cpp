// TIFF/GeoTIFF: 축척·DPI ↔ 픽셀 크기, GeoKey·Tiepoint·PixelScale 검증(직접 파싱 + gdalinfo 가 있으면 교차 확인)
#include <cstdio>
#include <fstream>
#include <map>
#include "catch_amalgamated.hpp"
#include "asec/export.hpp"
#include "asec/tiff.hpp"
#include "common.hpp"

using namespace asec;
using Catch::Approx;

struct TiffTags { std::map<int, std::vector<double>> num; std::map<int, std::string> str; };
static bool readTiffTags(const fs::path& p, TiffTags& T) {
    std::vector<uint8_t> b;
    if (!readFileBytes(p, b) || b.size() < 8 || b[0] != 'I') return false;
    auto u16 = [&](size_t o) { return uint16_t(b[o] | b[o + 1] << 8); };
    auto u32 = [&](size_t o) { return uint32_t(b[o] | b[o + 1] << 8 | b[o + 2] << 16 | uint32_t(b[o + 3]) << 24); };
    size_t ifd = u32(4);
    int n = u16(ifd);
    for (int i = 0; i < n; ++i) {
        size_t e = ifd + 2 + size_t(i) * 12;
        int id = u16(e), type = u16(e + 2); uint32_t cnt = u32(e + 4);
        int sz = type == 3 ? 2 : type == 4 ? 4 : type == 5 ? 8 : type == 12 ? 8 : 1;
        size_t off = (size_t(sz) * cnt <= 4) ? e + 8 : u32(e + 8);
        if (type == 2) { T.str[id] = std::string(reinterpret_cast<char*>(&b[off]), cnt ? cnt - 1 : 0); continue; }
        for (uint32_t k = 0; k < cnt; ++k) {
            size_t o = off + size_t(k) * sz;
            double v = 0;
            if (type == 3) v = u16(o); else if (type == 4) v = u32(o); else if (type == 5) v = double(u32(o)) / u32(o + 4);
            else if (type == 12) std::memcpy(&v, &b[o], 8); else v = b[o];
            T.num[id].push_back(v);
        }
    }
    return true;
}

TEST_CASE("축척·DPI → 지상 해상도") {
    CHECK(groundResolution(20, 300) == Approx(0.0254 * 20 / 300));   // 1.693 mm
    CHECK(groundResolution(100, 300) == Approx(0.0084667).epsilon(1e-4));
    CHECK(pixelsPerMeter(20, 300) == Approx(590.55).epsilon(1e-4));
    CHECK(groundResolution(10, 600) == Approx(0.000423333).epsilon(1e-5));
}

TEST_CASE("평면 GeoTIFF: EPSG:5186, Tiepoint=왼쪽 위 실좌표, PixelScale=축척/DPI") {
    auto dir = tmpDir("tiff");
    RgbaImage im; im.w = 300; im.h = 200; im.px.assign(size_t(300) * 200 * 4, 0);
    for (size_t i = 0; i < im.px.size(); i += 4) { im.px[i] = uint8_t(i / 4 % 300); im.px[i + 1] = 100; im.px[i + 2] = 50; im.px[i + 3] = 255; }
    SrsInfo s; s.srs = "EPSG:5186"; s.origin = Vec3(200000, 450000, 40); s.hasOrigin = true;
    const double denom = 20, dpi = 300, res = groundResolution(denom, dpi);
    GeoRef g = planGeoRef(s, 2.0, 10.0, res);  // 로컬 왼쪽 위 (2, 10)
    TiffOptions o; o.dpi = dpi; o.geo = g;
    std::string err;
    REQUIRE(writeTiff(dir / "plan.tif", im, o, &err));
    REQUIRE(writeWorldFile(dir / "plan.tfw", g, &err));
    TiffTags T;
    REQUIRE(readTiffTags(dir / "plan.tif", T));
    CHECK(T.num[256][0] == 300); CHECK(T.num[257][0] == 200);
    CHECK(T.num[282][0] == Approx(300)); CHECK(T.num[296][0] == 2);         // 300 dpi, 인치
    REQUIRE(T.num[33550].size() == 3);
    CHECK(T.num[33550][0] == Approx(res)); CHECK(T.num[33550][1] == Approx(res));
    REQUIRE(T.num[33922].size() == 6);
    CHECK(T.num[33922][3] == Approx(200002.0)); CHECK(T.num[33922][4] == Approx(450010.0));
    auto& gk = T.num[34735];
    REQUIRE(gk.size() >= 4);
    std::map<int, int> keys;
    for (size_t i = 4; i + 3 < gk.size(); i += 4) keys[int(gk[i])] = int(gk[i + 3]);
    CHECK(keys[1024] == 1);     // Projected
    CHECK(keys[1025] == 1);     // PixelIsArea
    CHECK(keys[3072] == 5186);  // EPSG
    CHECK(keys[3076] == 9001);  // metre
    // 월드 파일: 픽셀 중심
    std::ifstream w(dir / "plan.tfw"); double a, d, b2, e, c, f; w >> a >> d >> b2 >> e >> c >> f;
    CHECK(a == Approx(res)); CHECK(e == Approx(-res)); CHECK(c == Approx(200002.0 + res / 2)); CHECK(f == Approx(450010.0 - res / 2));
    // gdalinfo 교차 확인(있으면)
    std::string cmd = "gdalinfo -json \"" + (dir / "plan.tif").string() + "\" > \"" + (dir / "gi.json").string() + "\" 2>/dev/null";
    if (std::system(cmd.c_str()) == 0) {
        std::vector<uint8_t> js; readFileBytes(dir / "gi.json", js);
        std::string J(js.begin(), js.end());
        CHECK(J.find("5186") != std::string::npos);
        CHECK(J.find("200002") != std::string::npos);
    }
}

TEST_CASE("TIFF 픽셀 왕복(Deflate+예측자): gdal 이 있으면 픽셀값 확인") {
    auto dir = tmpDir("tiff2");
    RgbaImage im; im.w = 7; im.h = 5; im.px.assign(7 * 5 * 4, 0);
    for (int i = 0; i < 35; ++i) { im.px[i * 4] = uint8_t(i * 7); im.px[i * 4 + 1] = uint8_t(255 - i); im.px[i * 4 + 2] = 3; im.px[i * 4 + 3] = 255; }
    TiffOptions o; o.dpi = 150; o.alpha = false;
    std::string err;
    REQUIRE(writeTiff(dir / "a.tif", im, o, &err));
    std::string cmd = "gdal_translate -q -of XYZ -b 1 \"" + (dir / "a.tif").string() + "\" \"" + (dir / "a.xyz").string() + "\" 2>/dev/null";
    if (std::system(cmd.c_str()) == 0) {
        std::ifstream f(dir / "a.xyz"); double x, y, v; int k = 0, bad = 0;
        while (f >> x >> y >> v) { if (int(v) != (k * 7) % 256) ++bad; ++k; }
        CHECK(k == 35); CHECK(bad == 0);
    }
}

TEST_CASE("단면 GeoTIFF: 로컬 단면 좌표(거리·표고), 사용자 정의 CRS, 보조 JSON") {
    auto dir = tmpDir("tiff3");
    SectionResult r;
    r.line = SectionLine{{10, 20}, {13, 24}, 0, 0.5};
    r.srs.srs = "EPSG:5186"; r.srs.origin = Vec3(200000, 450000, 40);
    r.zMin = 4.6; r.zMax = 5.4;
    double res = groundResolution(20, 300);
    GeoRef g = sectionGeoRef(r, -0.1, 45.4, res);
    CHECK(g.epsg == 0);
    CHECK(g.tieX == Approx(-0.1)); CHECK(g.tieY == Approx(45.4));
    CHECK(g.citation.find("200010.000") != std::string::npos);  // A 실좌표
    CHECK(g.citation.find("200013.000") != std::string::npos);  // A′
    CHECK(sectionAzimuthDeg(r.line) == Approx(36.8699).epsilon(1e-4));
    RgbaImage im; im.w = 10; im.h = 10; im.px.assign(400, 255);
    TiffOptions o; o.geo = g;
    std::string err;
    REQUIRE(writeTiff(dir / "s.tif", im, o, &err));
    REQUIRE(writeSectionSidecar(dir / "s.json", r, g, 20, 300, 10, 10, &err));
    TiffTags T; REQUIRE(readTiffTags(dir / "s.tif", T));
    std::map<int, int> keys;
    auto& gk = T.num[34735];
    for (size_t i = 4; i + 3 < gk.size(); i += 4) keys[int(gk[i])] = int(gk[i + 3]);
    CHECK(keys[3072] == 32767);
    CHECK(T.str[34737].find("distance from A") != std::string::npos);
    std::vector<uint8_t> js; readFileBytes(dir / "s.json", js);
    std::string J(js.begin(), js.end());
    CHECK(J.find("\"length_m\": 5.0") != std::string::npos);
    CHECK(J.find("1:20") != std::string::npos);
}

TEST_CASE("복합 EPSG(5186+5711): 수평·수직 분리, GeoTIFF 수직 키, 높이 값 그대로") {
    SrsInfo s; s.srs = "EPSG:5186+5711"; s.origin = Vec3(200000, 450000, 40);
    CHECK(s.epsg() == 5186);
    CHECK(s.verticalEpsg() == 5711);
    SrsInfo s2; s2.srs = "EPSG:5186+EPSG:5711"; CHECK(s2.epsg() == 5186); CHECK(s2.verticalEpsg() == 5711);
    SrsInfo s3; s3.srs = "EPSG:5186"; CHECK(s3.verticalEpsg() == 0);
    CHECK(s.toWorld(Vec3(1, 2, 3)).z == Approx(43.0));  // 높이 변환 없음
    auto dir = tmpDir("tiffv");
    RgbaImage im; im.w = 4; im.h = 4; im.px.assign(64, 255);
    TiffOptions o; o.geo = planGeoRef(s, 0, 4, 0.01);
    CHECK(o.geo.vertEpsg == 5711);
    std::string err;
    REQUIRE(writeTiff(dir / "v.tif", im, o, &err));
    TiffTags T; REQUIRE(readTiffTags(dir / "v.tif", T));
    std::map<int, int> keys;
    auto& gk = T.num[34735];
    for (size_t i = 4; i + 3 < gk.size(); i += 4) keys[int(gk[i])] = int(gk[i + 3]);
    CHECK(keys[3072] == 5186);
    CHECK(keys[4096] == 5711);
    CHECK(keys[4099] == 9001);
    CHECK(int(gk[3]) == int(keys.size()));
}
