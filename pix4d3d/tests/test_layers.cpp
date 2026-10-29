// 병합 3MX(layers[] 에 meshPyramid 여러 개): 모든 레이어를 읽고, SRS·원점이 다른 레이어는 빼고 경고
#include "catch_amalgamated.hpp"
#include "asec/engine.hpp"
#include "asec/pointcloud.hpp"
#include "common.hpp"
#include "synth.hpp"

using namespace asec;

namespace {
struct Merged { fs::path file, dir; SrsInfo srs; fs::path rootA; };
// 레이어 A = 합성 지형(0..16 × 0..12), 레이어 B = 옆 격자(20..30 × 0..10, 잎 1개)
Merged makeMerged(const std::string& name, bool addForeign, bool addDup) {
    Merged m;
    m.dir = tmpDir(name);
    synth::Params p; p.dir = m.dir / "A"; p.leafSpacing = 0.1; p.texSize = 16;
    fs::path a3mx; std::string err;
    REQUIRE(synth::write(p, &a3mx, &err));
    TmxScene sa; REQUIRE(readTmxScene(a3mx, sa, &err));
    m.srs = sa.srs; m.rootA = sa.rootFile;
    fs::create_directories(m.dir / "B" / "Data");
    TmxWriteNode n; n.id = "b"; n.meshes.push_back(synth::gridMesh(20, 0, 30, 10, 0.5));
    REQUIRE(writeTmxTile(m.dir / "B" / "Data" / "root.3mxb", {n}, &err));
    REQUIRE(writeTmxTile(m.dir / "B" / "Data" / "other.3mxb", {n}, &err));
    std::vector<TmxWriteLayer> L;
    L.push_back({"A", sa.srs, fs::relative(sa.rootFile, m.dir).generic_string()});
    L.push_back({"B", sa.srs, "B/Data/root.3mxb"});
    if (addForeign) { SrsInfo o = sa.srs; o.origin.x += 1000; L.push_back({"Foreign", o, "B/Data/other.3mxb"}); }
    if (addDup) L.push_back({"A again", sa.srs, fs::relative(sa.rootFile, m.dir).generic_string()});
    m.file = m.dir / "merged.3mx";
    REQUIRE(writeTmxSceneLayers(m.file, "merged", L, &err));
    return m;
}
}  // namespace

TEST_CASE("병합 3MX: 레이어 2개를 모두 읽고 단면·범위·점군에 둘 다 들어간다") {
    Merged m = makeMerged("layers2", false, false);
    TmxSource src; std::string err;
    REQUIRE(src.open(m.file, &err));
    REQUIRE(src.scene.layers.size() == 2);
    CHECK(src.scene.skippedLayers == 0);
    CHECK(src.scene.warnings.empty());
    CHECK(src.bounds.mx.x >= 29.9);   // 레이어 B 까지
    CHECK(src.bounds.mn.x <= 0.1);
    CHECK(src.scene.roots().size() == 2);

    // A 만 지나는 선 / A·B 둘 다 지나는 선
    SectionRequest rq; rq.wantImage = false; rq.line.back = 0.3;
    rq.line.a = {1, 5}; rq.line.b = {29, 5};
    SectionOutput both; REQUIRE(computeSection(src, rq, both, &err));
    TmxSource only; REQUIRE(only.open(m.dir / "A" / "Synthetic.3mx", &err));
    SectionOutput a; REQUIRE(computeSection(only, rq, a, &err));
    CHECK(both.stats.leafNodes == a.stats.leafNodes + 1);
    CHECK(both.stats.triangles > a.stats.triangles);
    double sMax = 0;
    for (auto& pl : both.result.profile) for (auto& q : pl) sMax = std::max(sMax, q.x);
    CHECK(sMax > 27.5);   // 레이어 B 구간(x=20..29)까지 단면선이 이어짐

    // 미리보기(거친 LOD)도 두 레이어
    rq.meshRes = 0.05;
    SectionOutput pv; REQUIRE(computeSection(src, rq, pv, &err));
    double sMax2 = 0;
    for (auto& pl : pv.result.profile) for (auto& q : pl) sMax2 = std::max(sMax2, q.x);
    CHECK(sMax2 > 27.5);

    // 정사영상용 영역 수집
    std::vector<MeshPtr> ms; LeafStats st;
    Box3 area; area.add(Vec3(21, 1, -100)); area.add(Vec3(22, 2, 100));
    REQUIRE(src.areaMeshes(area, 0.01, ms, &st, &err));
    CHECK(ms.size() == 1);

    // 점군: 두 레이어 꼭짓점 합
    PointExportOptions o; o.format = PointFormat::XYZ; o.rgb = false;
    PointExportStats s2, s1;
    REQUIRE(exportPointsTmx(*src.cache, src.scene.roots(), src.srs, o, m.dir / "all.xyz", &s2, &err));
    REQUIRE(exportPointsTmx(*only.cache, only.scene.rootFile, only.srs, o, m.dir / "a.xyz", &s1, &err));
    CHECK(s2.points > s1.points);
}

TEST_CASE("병합 3MX: 원점이 다른 레이어는 빼고 경고, 같은 루트 중복은 한 번만") {
    Merged m = makeMerged("layers_foreign", true, true);
    TmxScene sc; std::string err;
    REQUIRE(readTmxScene(m.file, sc, &err));
    CHECK(sc.layers.size() == 2);
    CHECK(sc.skippedLayers == 1);
    REQUIRE(sc.warnings.size() == 1);
    CHECK(sc.warnings[0].find("Foreign") != std::string::npos);
    CHECK(sc.warnings[0].find("SRSOrigin") != std::string::npos);
}

TEST_CASE("병합 3MX: SRS 문자열이 달라도 같은 좌표계(EPSG:5186 vs 같은 WKT)면 함께 연다, 다른 EPSG 는 뺀다") {
    auto dir = tmpDir("layers_srs");
    fs::create_directories(dir / "Data");
    TmxWriteNode n; n.id = "n"; n.meshes.push_back(synth::gridMesh(0, 0, 4, 4, 0.5));
    std::string err;
    REQUIRE(writeTmxTile(dir / "Data" / "a.3mxb", {n}, &err));
    REQUIRE(writeTmxTile(dir / "Data" / "b.3mxb", {n}, &err));
    REQUIRE(writeTmxTile(dir / "Data" / "c.3mxb", {n}, &err));
    SrsInfo s1; s1.srs = "EPSG:5186"; s1.origin = Vec3(1, 2, 3); s1.hasOrigin = true;
    SrsInfo s2 = s1; s2.srs = "PROJCS[\"KGD2002 / Central Belt 2010\",AUTHORITY[\"EPSG\",\"5186\"]]";
    SrsInfo s3 = s1; s3.srs = "EPSG:5187";
    REQUIRE(writeTmxSceneLayers(dir / "m.3mx", "m", {{"a", s1, "Data/a.3mxb"}, {"b", s2, "Data/b.3mxb"}, {"c", s3, "Data/c.3mxb"}}, &err));
    TmxScene sc; REQUIRE(readTmxScene(dir / "m.3mx", sc, &err));
    CHECK(sc.layers.size() == 2);
    CHECK(sc.skippedLayers == 1);
    REQUIRE(sc.warnings.size() == 1);
    CHECK(sc.warnings[0].find("EPSG:5187") != std::string::npos);
}
