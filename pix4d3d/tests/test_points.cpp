// 점군 내보내기: SRSOrigin 적용 실좌표, 영역(띠), 솎기, LAS 헤더/점
#include <fstream>
#include <sstream>
#include "catch_amalgamated.hpp"
#include "asec/pointcloud.hpp"
#include "common.hpp"
#include "synth.hpp"

using namespace asec;
using Catch::Approx;

static fs::path makeScene(const std::string& name) {
    auto dir = tmpDir(name);
    synth::Params p; p.dir = dir; p.leafSpacing = 0.1; p.texSize = 64;
    fs::path f; std::string err;
    REQUIRE(synth::write(p, &f, &err));
    return f;
}

TEST_CASE("XYZ: 잎 꼭짓점, 실좌표 = 로컬 + SRSOrigin, 타일 경계 중복 제거, 텍스처 RGB") {
    auto f = makeScene("xyz");
    TmxScene s; std::string err; REQUIRE(readTmxScene(f, s, &err));
    TileCache c; c.textureDecoder = decodeTexStb;
    PointExportOptions o;
    PointExportStats st;
    auto out = f.parent_path() / "all.xyz";
    REQUIRE(exportPointsTmx(c, s.rootFile, s.srs, o, out, &st, &err));
    // 잎 격자 0.1 m, 16×12 m → 161×121 고유 점(4x4 타일 경계 공유점은 한 번)
    CHECK(st.points == size_t(161) * 121);
    CHECK(st.vertices > st.points);
    std::ifstream in(out);
    std::string line; bool foundOrigin = false; size_t n = 0; double maxErr = 0;
    while (std::getline(in, line)) {
        double X, Y, Z; int R, G, B;
        std::istringstream ss(line); ss >> X >> Y >> Z >> R >> G >> B;
        ++n;
        double lx = X - 200000.0, ly = Y - 450000.0;
        maxErr = std::max(maxErr, std::fabs((Z - 40.0) - synth::height(lx, ly)));
        if (std::fabs(lx) < 1e-6 && std::fabs(ly) < 1e-6) foundOrigin = true;
        CHECK(R >= 0); CHECK(B <= 255);
    }
    CHECK(n == st.points);
    CHECK(foundOrigin);          // 로컬 (0,0) → 200000.000, 450000.000
    CHECK(maxErr < 0.0011);      // MG2 0.5 mm + 소수 3자리
    CHECK(st.worldBounds.mn.x == Approx(200000.0).margin(0.001));
    CHECK(st.worldBounds.mx.y == Approx(450012.0).margin(0.001));
}

TEST_CASE("띠 영역 + 솎기 5 cm / 1 cm") {
    auto f = makeScene("band");
    TmxScene s; std::string err; REQUIRE(readTmxScene(f, s, &err));
    TileCache c;
    PointExportOptions o; o.area = PointArea::Band; o.rgb = false;
    o.band = sectionBand(SectionLine{{1.95, 6.05}, {14.05, 6.05}, 0.0, 0.3});
    PointExportStats a, b;
    REQUIRE(exportPointsTmx(c, s.rootFile, s.srs, o, f.parent_path() / "band.xyz", &a, &err));
    // y 6.05~6.35 → 격자 y 6.1,6.2,6.3 3줄 × x 2..14 121열
    CHECK(a.points == size_t(3) * 121);
    o.spacing = 0.5;
    REQUIRE(exportPointsTmx(c, s.rootFile, s.srs, o, f.parent_path() / "band5.xyz", &b, &err));
    CHECK(b.points < a.points);
    CHECK(b.points >= 24);  // 12 m / 0.5 × (1~2) × z 복셀
}

TEST_CASE("LAS 1.2 형식 2: 헤더 오프셋·축척, 점 좌표 복원") {
    auto f = makeScene("las");
    TmxScene s; std::string err; REQUIRE(readTmxScene(f, s, &err));
    TileCache c; c.textureDecoder = decodeTexStb;
    PointExportOptions o; o.format = PointFormat::LAS;
    PointExportStats st;
    auto out = f.parent_path() / "all.las";
    REQUIRE(exportPointsTmx(c, s.rootFile, s.srs, o, out, &st, &err));
    std::vector<uint8_t> b; REQUIRE(readFileBytes(out, b));
    const uint32_t off = [&] { uint32_t v; std::memcpy(&v, &b[96], 4); return v; }();
    CHECK(off == 227 + 54 + 32);                     // GeoKeyDirectory VLR(EPSG:5186, 수직 없음)
    CHECK(std::string(reinterpret_cast<const char*>(&b[227 + 2])) == "LASF_Projection");
    REQUIRE(b.size() == off + st.points * 26);
    CHECK(std::string(b.begin(), b.begin() + 4) == "LASF");
    auto rd = [&](size_t off, auto v) { std::memcpy(&v, &b[off], sizeof v); return v; };
    CHECK(int(b[24]) == 1); CHECK(int(b[25]) == 2);
    CHECK(rd(104, uint8_t()) == 2);                  // 점 형식
    CHECK(rd(105, uint16_t()) == 26);
    CHECK(rd(107, uint32_t()) == st.points);
    CHECK(rd(131, double()) == Approx(0.001));       // x 축척
    CHECK(rd(155, double()) == Approx(200000.0));    // x 오프셋
    CHECK(rd(171, double()) == Approx(40.0));        // z 오프셋
    CHECK(rd(179, double()) == Approx(200016.0).margin(0.001));  // max X
    double maxErr = 0;
    for (size_t i = 0; i < st.points; ++i) {
        size_t o2 = off + i * 26;
        double X = rd(o2, int32_t()) * 0.001 + 200000.0, Y = rd(o2 + 4, int32_t()) * 0.001 + 450000.0, Z = rd(o2 + 8, int32_t()) * 0.001 + 40.0;
        maxErr = std::max(maxErr, std::fabs(Z - 40.0 - synth::height(X - 200000.0, Y - 450000.0)));
    }
    CHECK(maxErr < 0.0011);
}
