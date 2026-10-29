#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include "catch_amalgamated.hpp"
#include "asec/export.hpp"
#include "common.hpp"

using namespace asec;
using Catch::Approx;

static std::vector<std::pair<int, std::string>> pairsOf(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::pair<int, std::string>> v;
    std::string c, val;
    while (std::getline(in, c) && std::getline(in, val)) v.emplace_back(std::stoi(c), val);
    return v;
}

static SectionResult sampleResult() {
    SectionResult r;
    r.line = SectionLine{{10, 20}, {13, 24}, 0, 0.5};  // 길이 5 m
    r.srs.srs = "EPSG:5186"; r.srs.origin = Vec3(200000, 450000, 40); r.srs.hasOrigin = true;
    r.profile = {{{0, 5.12}, {1.5, 5.30}, {2.0, 4.70}, {5.0, 4.75}}};
    r.zMin = 4.6; r.zMax = 5.4;
    return r;
}

TEST_CASE("DXF 2D: 절대표고 Y, 레이어, 레벨선 10cm, 라벨 겹침 없음, 중복 꼭짓점 없음") {
    auto r = sampleResult();
    DxfExportOptions o; o.scaleDenom = 20;
    o.image = true; o.imageFile = "sec_image.png"; o.imageW = 1250; o.imageH = 200; o.imageS0 = 0; o.imageZ0 = 4.6; o.imageRes = 0.004;
    std::string txt, err;
    REQUIRE(exportSectionDxf(r, o, {}, &err, &txt));
    auto P = pairsOf(txt);
    // 단면선: LWPOLYLINE 4점, Y = 로컬 z + 40
    bool found = false;
    for (size_t i = 0; i < P.size(); ++i) {
        if (P[i] == std::make_pair(0, std::string("LWPOLYLINE"))) {
            std::vector<double> xs, ys; std::string layer;
            for (size_t k = i + 1; k < P.size() && P[k].first != 0; ++k) {
                if (P[k].first == 8) layer = P[k].second;
                if (P[k].first == 10) xs.push_back(std::stod(P[k].second));
                if (P[k].first == 20) ys.push_back(std::stod(P[k].second));
            }
            if (layer == "SECTION_PROFILE") {
                found = true;
                REQUIRE(ys.size() == 4);
                CHECK(ys[0] == Approx(45.12)); CHECK(ys[2] == Approx(44.70));
                CHECK(xs[3] == Approx(5.0));
                for (size_t k = 1; k < xs.size(); ++k) CHECK(std::hypot(xs[k] - xs[k - 1], ys[k] - ys[k - 1]) > 1e-6);
            }
        }
    }
    CHECK(found);
    // 레벨선 개수: 44.6 ~ 45.4 → 9개
    int lv = 0; std::set<std::string> labels; int labelCount = 0;
    for (size_t i = 0; i < P.size(); ++i) {
        if (P[i].first == 8 && (P[i].second == "LEVEL_10CM" || P[i].second == "LEVEL_50CM" || P[i].second == "LEVEL_1M") && P[i - 4].second == "LINE") ++lv;
        if (P[i].first == 1 && P[i].second.size() == 4 && P[i].second[2] == '.') { labels.insert(P[i].second); ++labelCount; }
    }
    CHECK(lv == 9);
    CHECK(labels.count("45.0") == 1);   // 50 cm 마다, 소수 1자리
    CHECK(labels.count("45.3") == 0);   // 10 cm 선에는 라벨 없음
    // 핸들 유일
    std::set<std::string> handles; bool dupHandle = false;
    for (auto& p : P) if (p.first == 5 || p.first == 105) { if (!handles.insert(p.second).second) dupHandle = true; }
    CHECK_FALSE(dupHandle);
    // IMAGE 배치: 삽입점 (0, 44.6), 픽셀 벡터 0.004
    CHECK(txt.find("IMAGE\n") != std::string::npos);
    CHECK(txt.find("IMAGEDEF\n") != std::string::npos);
    CHECK(txt.find("sec_image.png") != std::string::npos);
    CHECK(txt.find("ACAD_IMAGE_DICT") != std::string::npos);
}

TEST_CASE("DXF 라벨 간격: 1/100 이면 10cm 마다 라벨 안 붙임(겹침 방지)") {
    auto r = sampleResult();
    DxfExportOptions o; o.scaleDenom = 100;  // 글자 20 cm
    std::string txt, err;
    REQUIRE(exportSectionDxf(r, o, {}, &err, &txt));
    CHECK(txt.find("\n1\n45.3\n") == std::string::npos);
    CHECK(txt.find("\n1\n45.0\n") != std::string::npos);
}

TEST_CASE("DXF 3D: 실좌표(EPSG) 수직면 배치") {
    auto r = sampleResult();
    DxfExportOptions o; o.mode = DxfCoordMode::World3D;
    o.image = true; o.imageFile = "x.png"; o.imageW = 100; o.imageH = 20; o.imageS0 = 0; o.imageZ0 = 4.6; o.imageRes = 0.05;
    std::string txt, err;
    REQUIRE(exportSectionDxf(r, o, {}, &err, &txt));
    Vec3 w = sectionToWorld(r, 5.0, 4.75);
    CHECK(w.x == Approx(200013.0)); CHECK(w.y == Approx(450024.0)); CHECK(w.z == Approx(44.75));
    CHECK(txt.find("AcDb3dPolyline") != std::string::npos);
    CHECK(txt.find("200013.0") != std::string::npos);
    CHECK(txt.find("\n210\n") != std::string::npos);  // 수직면 문자(OCS)
}

TEST_CASE("DXF 파일 저장(ezdxf 감사는 tests/dxf_audit.py)") {
    auto dir = tmpDir("dxf");
    auto r = sampleResult();
    DxfExportOptions o; o.image = true; o.imageFile = "sec_image.png"; o.imageW = 10; o.imageH = 10; o.imageRes = 0.1; o.imageZ0 = 4.6;
    std::string err;
    REQUIRE(exportSectionDxf(r, o, dir / "sec.dxf", &err));
    o.mode = DxfCoordMode::World3D;
    REQUIRE(exportSectionDxf(r, o, dir / "sec3d.dxf", &err));
    CHECK(std::filesystem::file_size(dir / "sec.dxf") > 1000);
}

TEST_CASE("DXF: 닫힌 단면 고리는 닫힘 플래그 LWPOLYLINE, 단면선 레이어 색 = 빨강(1)") {
    SectionResult r;
    r.line = SectionLine{{0, 0}, {10, 0}, 0, 0.5};
    r.zMin = 4; r.zMax = 6;
    Polyline loop{{4, 5}, {5, 5}, {5, 5.5}, {4, 5.5}, {4, 5}};
    r.profile = {Polyline{{0, 4.5}, {10, 4.5}}, loop};
    DxfExportOptions o;
    std::string txt, err;
    auto dir = tmpDir("dxfloop");
    REQUIRE(exportSectionDxf(r, o, dir / "l.dxf", &err, &txt));
    CHECK(txt.find("SECTION_PROFILE\n70\n0\n62\n1\n") != std::string::npos);
    // 두 번째 LWPOLYLINE: 꼭짓점 4개, 70 = 1
    CHECK(txt.find("AcDbPolyline\n90\n4\n70\n1\n") != std::string::npos);
}

TEST_CASE("DXF 그리기 순서: 레벨선 → IMAGE → 단면선, 라벨은 50 cm 마다 소수 1자리") {
    SectionResult r;
    r.line = SectionLine{{0, 0}, {10, 0}, 0, 0.5};
    r.zMin = 4.0; r.zMax = 6.0;
    r.srs.origin = Vec3(0, 0, 74.0);  // 표고 78.0 ~ 80.0
    r.profile = {Polyline{{0, 5.0}, {10, 5.2}}};
    DxfExportOptions o; o.image = true; o.imageFile = "x.png"; o.imageW = 10; o.imageH = 10; o.imageRes = 0.1; o.imageZ0 = 4.0;
    std::string txt, err;
    auto dir = tmpDir("dxforder");
    REQUIRE(exportSectionDxf(r, o, dir / "o.dxf", &err, &txt));
    size_t ent = txt.find("\n2\nENTITIES\n");
    size_t firstLevel = txt.find("LEVEL_10CM", ent), img = txt.find("\nIMAGE\n", ent), prof = txt.find("SECTION_PROFILE", ent);
    REQUIRE(firstLevel != std::string::npos); REQUIRE(img != std::string::npos); REQUIRE(prof != std::string::npos);
    CHECK(firstLevel < img);
    CHECK(img < prof);
    CHECK(txt.find("\n1\n78.5\n", ent) != std::string::npos);
    CHECK(txt.find("\n1\n79.0\n", ent) != std::string::npos);
    CHECK(txt.find("\n1\n78.6\n", ent) == std::string::npos);   // 10 cm 선에는 라벨 없음
    CHECK(txt.find("\n1\n78.50\n", ent) == std::string::npos);  // 소수 2자리 아님
}
