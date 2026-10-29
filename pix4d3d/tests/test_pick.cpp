// 정밀 높이 피킹: 잎 메시·double·SRSOrigin. 화면 LOD 와의 차이, 경사 평면 해석해, 큰 원점
#include "catch_amalgamated.hpp"
#include "asec/pick.hpp"
#include "common.hpp"
#include "synth.hpp"
#include <random>

using namespace asec;

namespace {
// z = 0.1x + 0.05y + 30 (로컬) 평면 격자, 원점 (200000, 550000, 40)
std::shared_ptr<StaticSource> planeSource() {
    auto s = std::make_shared<StaticSource>();
    auto m = std::make_shared<Mesh>();
    const int n = 20; const double h = 0.5;
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) { double x = i * h, y = j * h; m->pos.insert(m->pos.end(), {float(x), float(y), float(0.1 * x + 0.05 * y + 30)}); }
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) { uint32_t a = j * (n + 1) + i, b = a + 1, c = a + n + 1, d = c + 1; m->idx.insert(m->idx.end(), {a, b, d, a, d, c}); }
    m->computeBBox();
    s->meshes.push_back(m);
    s->bounds = m->bbox;
    s->srs.srs = "EPSG:5186"; s->srs.origin = Vec3(200000, 550000, 40); s->srs.hasOrigin = true;
    return s;
}
}  // namespace

TEST_CASE("pickVertical: 경사 평면에서 실좌표 Z 오차 < 0.1 mm, X/Y 는 로컬 + 원점(double)") {
    auto s = planeSource();
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> U(0.05, 9.95);
    for (int k = 0; k < 200; ++k) {
        double x = U(rng), y = U(rng);
        PickResult r; std::string err;
        REQUIRE(pickVertical(*s, x, y, r, &err));
        REQUIRE(r.hit);
        CHECK(r.source == ZSource::LeafSurface);
        CHECK(std::fabs(r.world.z - (0.1 * x + 0.05 * y + 30 + 40)) < 1e-4);
        CHECK(std::fabs(r.world.x - (200000 + x)) < 1e-9);
        CHECK(std::fabs(r.world.y - (550000 + y)) < 1e-9);
    }
    // 실좌표로 묻기
    PickResult w; REQUIRE(pickVerticalWorld(*s, 200003.25, 550004.5, w));
    REQUIRE(w.hit);
    CHECK(std::fabs(w.world.z - (0.1 * 3.25 + 0.05 * 4.5 + 70)) < 1e-4);
    // 모델 밖: 맞지 않음(오류 아님)
    PickResult miss; REQUIRE(pickVertical(*s, 50, 50, miss)); CHECK_FALSE(miss.hit);
}

TEST_CASE("pickRay: 비스듬한 광선이 평면과 만나는 점(해석해)과 일치") {
    auto s = planeSource();
    Vec3 o(2, 3, 100), d(0.03, 0.02, -1);
    PickResult r; REQUIRE(pickRay(*s, o, d, r)); REQUIRE(r.hit);
    // o + t·d̂ 가 z = 0.1x + 0.05y + 30 위
    double L = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    Vec3 dh = d * (1 / L);
    double t = (30 + 0.1 * o.x + 0.05 * o.y - o.z) / (dh.z - 0.1 * dh.x - 0.05 * dh.y);
    Vec3 p = o + dh * t;
    CHECK(std::fabs(r.local.x - p.x) < 1e-6);
    CHECK(std::fabs(r.local.y - p.y) < 1e-6);
    CHECK(std::fabs(r.local.z - p.z) < 1e-4);
    CHECK(std::fabs(r.world.z - (p.z + 40)) < 1e-4);
}

TEST_CASE("pickVertical(3MX): 잎(최고 해상도) 표면을 쓰고 1 mm 안, 화면용 거친 LOD 는 cm 단위로 어긋남") {
    auto dir = tmpDir("pick_tmx");
    synth::Params p; p.dir = dir; p.leafSpacing = 0.1; p.texSize = 16;
    fs::path f; std::string err;
    REQUIRE(synth::write(p, &f, &err));
    TmxSource src; REQUIRE(src.open(f, &err));
    double worstLeaf = 0, worstCoarse = 0;
    const double pts[][2] = {{5.03, 6.07}, {7.41, 5.55}, {8.0, 6.0}, {2.2, 2.9}, {12.35, 9.15}, {6.3, 6.8}, {4.55, 4.45}};
    for (auto& q : pts) {
        PickResult r; REQUIRE(pickVertical(src, q[0], q[1], r, &err));
        REQUIRE(r.hit);
        CHECK(r.source == ZSource::LeafSurface);
        CHECK(r.stats.maxDepth == 2);   // 잎 단계
        double want = synth::gridZ(q[0], q[1], p.leafSpacing) + p.origin.z;
        worstLeaf = std::max(worstLeaf, std::fabs(r.world.z - want));
        worstCoarse = std::max(worstCoarse, std::fabs(synth::gridZ(q[0], q[1], p.leafSpacing * 20) - synth::gridZ(q[0], q[1], p.leafSpacing)));
        CHECK(std::fabs(r.world.x - (q[0] + p.origin.x)) < 1e-9);
    }
    CHECK(worstLeaf < 0.001);        // OpenCTM 꼭짓점 양자화(0.5 mm) 안
    CHECK(worstCoarse > 0.02);       // 화면용 L0(2 m 격자)로 읽으면 수 cm 이상 틀림 → 잎 피킹이 필요한 이유
}

TEST_CASE("rayMeshes: 여러 겹이면 가장 가까운 면(위에서 보면 맨 위)") {
    auto a = synth::gridMesh(0, 0, 2, 2, 0.5);
    auto b = std::make_shared<Mesh>(*a);
    for (size_t i = 2; i < b->pos.size(); i += 3) b->pos[i] += 1.5f;
    a->computeBBox(); b->computeBBox();
    double t = 1e9; size_t n = 0;
    REQUIRE(rayMeshes({a, b}, Vec3(1.1, 0.7, 100), Vec3(0, 0, -1), 0, t, &n));
    CHECK(std::fabs((100 - t) - (synth::gridZ(1.1, 0.7, 0.5) + 1.5)) < 1e-5);
    CHECK(n > 0);
}
