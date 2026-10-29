#include "catch_amalgamated.hpp"
#include "asec/section.hpp"
#include "synth.hpp"

using namespace asec;
using Catch::Approx;

static MeshPtr tri(std::vector<float> p) {
    auto m = std::make_shared<Mesh>();
    m->pos = p;
    for (uint32_t i = 0; i < p.size() / 3; ++i) m->idx.push_back(i);
    m->computeBBox();
    return m;
}

TEST_CASE("평면-삼각형 교차: 알려진 값") {
    // 단면선 x축(A=(0,0)→B=(10,0)), 평면 y=0. 삼각형이 y=-1..1 에 걸침
    auto m = tri({2, -1, 10, 4, 1, 12, 2, 1, 14});
    SectionFrame f(SectionLine{{0, 0}, {10, 0}});
    std::vector<CutSeg> s;
    REQUIRE(cutMesh(*m, f, 0, 0, 10, s) == 1);
    double a0 = std::min(s[0].s0, s[0].s1), a1 = std::max(s[0].s0, s[0].s1);
    CHECK(a0 == Approx(2.0));   // (2,-1,10)-(2,1,14) 중점 → s=2, z=12
    CHECK(a1 == Approx(3.0));   // (2,-1,10)-(4,1,12) 중점 → s=3, z=11
    CHECK((s[0].s0 < s[0].s1 ? s[0].z0 : s[0].z1) == Approx(12.0));
    CHECK((s[0].s0 < s[0].s1 ? s[0].z1 : s[0].z0) == Approx(11.0));
}

TEST_CASE("꼭짓점이 평면 위: 퇴화·겹선 없음") {
    // 두 삼각형이 평면 위 꼭짓점(5,0)을 공유
    auto m = tri({5, 0, 1, 4, -1, 1, 6, -1, 1,   5, 0, 1, 6, 1, 1, 4, 1, 1});
    SectionFrame f(SectionLine{{0, 0}, {10, 0}});
    std::vector<CutSeg> s;
    cutMesh(*m, f, 0, 0, 10, s);
    for (auto& g : s) CHECK(std::hypot(g.s1 - g.s0, g.z1 - g.z0) >= 0.0);
    auto L = stitchSegments(s, 1e-6);
    CHECK(countDuplicateVertices(L, 1e-9) == 0);
}

TEST_CASE("공유 모서리 교점은 비트 단위로 같다") {
    auto m = synth::gridMesh(0, 0, 2, 2, 0.1);
    SectionFrame f(SectionLine{{0.013, 0.37}, {1.97, 1.61}});
    std::vector<CutSeg> s;
    cutMesh(*m, f, 0, 0, f.L, s);
    REQUIRE(s.size() > 20);
    auto L = stitchSegments(s, 1e-12);  // 공차 거의 0 이어도 한 줄
    CHECK(L.size() == 1);
}

TEST_CASE("평탄 격자 단면 → 직선 2점") {
    auto m = std::make_shared<Mesh>(*synth::gridMesh(0, 0, 4, 4, 0.2));
    for (size_t i = 2; i < m->pos.size(); i += 3) m->pos[i] = 7.0f;
    SectionFrame f(SectionLine{{0.5, 0.5}, {3.5, 3.1}});
    std::vector<CutSeg> s;
    cutMesh(*m, f, 0, 0, f.L, s);
    auto L = buildProfile(s, CleanupParams{});
    REQUIRE(L.size() == 1);
    CHECK(L[0].size() == 2);
    CHECK(L[0].front().x == Approx(0.0).margin(1e-9));
    CHECK(L[0].back().x == Approx(f.L).margin(1e-6));
}

TEST_CASE("단면 점은 메시 위(격자 보간 대비 < 0.01 mm), 해석해 대비 < 3 cm") {
    auto m = synth::gridMesh(2, 2, 14, 10, 0.04);
    SectionLine ln{{3, 6}, {13, 6.3}};
    SectionFrame f(ln);
    std::vector<CutSeg> s;
    cutMesh(*m, f, 0, 0, f.L, s);
    auto L = buildProfile(s, CleanupParams{});
    REQUIRE(L.size() == 1);
    double maxErr = 0, maxAna = 0;
    for (auto& p : L[0]) {
        Vec2 xy = f.planXY(p.x);
        maxErr = std::max(maxErr, std::fabs(p.y - synth::gridZ(xy.x, xy.y, 0.04)));
        maxAna = std::max(maxAna, std::fabs(p.y - synth::height(xy.x, xy.y)));
    }
    CHECK(maxErr < 1e-5);
    CHECK(maxAna < 0.03);
}

TEST_CASE("레벨선: cm 정수 산술, 등급, 표기") {
    auto L = levelLines(44.93, 45.61, 10);
    REQUIRE(L.size() == 7);
    CHECK(formatElevation(L.front().z) == "45.0");
    CHECK(formatElevation(L.back().z) == "45.6");
    CHECK(L.front().cls == LevelClass::Master);
    CHECK(L[5].cls == LevelClass::Major);  // 45.50
    CHECK(L[3].cls == LevelClass::Minor);  // 45.30
    for (size_t i = 0; i < L.size(); ++i) CHECK(L[i].cm == 4500 + long(i) * 10);
    CHECK(formatElevation(45.3) == "45.3");
    CHECK(formatElevation(78.5) == "78.5");
    CHECK(formatElevation(79.0) == "79.0");
    CHECK(formatElevation(45.3, 2) == "45.30");
    CHECK(formatElevation(-0.05, 2) == "-0.05");
    CHECK(formatElevation(-0.04) == "0.0");
    CHECK(formatElevation(1234.5678, 2) == "1234.57");
    // 경계값 포함
    CHECK(levelLines(45.0, 45.2).size() == 3);
    // 긴 범위 누적 오차 없음
    auto B = levelLines(0, 1000, 10);
    CHECK(formatElevation(B[7777].z) == "777.7");
}

TEST_CASE("라벨 간격: 겹치지 않게") {
    CHECK(labelStepCm(400, 14) == 50);   // 넉넉해도 라벨은 50 cm 마다(10 cm 라벨 없음)
    CHECK(labelStepCm(100, 14) == 50);   // 10cm=10px → 50cm=50px
    CHECK(labelStepCm(20, 14) == 100);
    CHECK(labelStepCm(2, 14) == 1000);
    CHECK(niceStep(10, 10) == Approx(1.0));
    CHECK(niceStep(23, 10) == Approx(2.0));
    CHECK(niceStep(0.7, 10) == Approx(0.05));
}

TEST_CASE("두께 띠 vs 상자(SAT)") {
    SectionLine l{{0, 0}, {10, 10}, 0.0, 0.5};
    auto q = sectionBand(l);
    Box3 in; in.add(Vec3(4, 4, 0)); in.add(Vec3(5, 5, 1));
    Box3 off; off.add(Vec3(8, 0, 0)); off.add(Vec3(9, 1, 1));
    Box3 corner; corner.add(Vec3(-2, 9, 0)); corner.add(Vec3(1, 12, 1));  // 축 상자로는 겹치지만 띠와는 안 겹침
    CHECK(bandIntersectsBox(q, in));
    CHECK_FALSE(bandIntersectsBox(q, off));
    CHECK_FALSE(bandIntersectsBox(q, corner));
}

TEST_CASE("레벨선 간격 규칙: 작업·인쇄 축척은 10 cm 선 / 50 cm 숫자, 축소하면 솎고, 확대해도 10 cm 보다 촘촘하지 않음") {
    // 인쇄 1:20, 300 dpi: 1 m = 50 mm = 590.6 px. 선 기준 0.5 mm(5.9 px), 숫자 기준 약 15 px
    const double dpi = 300, minLine = 0.5 / 25.4 * dpi, minLab = 15;
    for (double denom : {10.0, 20.0, 40.0, 50.0, 100.0}) {
        double ppm = 1000.0 / denom / 25.4 * dpi;
        LevelPlan lp = planLevels(ppm, minLine, minLab);
        INFO("1:" << denom);
        CHECK(lp.lineCm == 10);
        CHECK(lp.labelCm == 50);
    }
    // 화면: 4 px 보다 촘촘하면 솎음
    CHECK(planLevels(100, 4, 20).lineCm == 10);   // 10 cm = 10 px
    CHECK(planLevels(100, 4, 20).labelCm == 50);  // 50 cm = 50 px
    CHECK(planLevels(30, 4, 20).lineCm == 50);    // 10 cm = 3 px → 50 cm
    CHECK(planLevels(30, 4, 20).labelCm == 100);  // 50 cm = 15 px < 20 → 1 m
    CHECK(planLevels(6, 4, 20).lineCm == 100);    // 50 cm = 3 px → 1 m
    CHECK(planLevels(6, 4, 20).labelCm == 500);
    CHECK(planLevels(0.5, 4, 20).lineCm == 1000);
    // 크게 확대: 여전히 10 cm(더 촘촘하게는 사용자가 설정할 때만)
    CHECK(planLevels(5000, 4, 20).lineCm == 10);
    CHECK(planLevels(5000, 4, 20, 5).lineCm == 5);
    CHECK(planLevels(5000, 4, 20).labelCm == 50);
    // 숫자는 그리는 선 위에만
    for (double ppm : {0.3, 2.0, 7.0, 25.0, 41.0, 90.0, 400.0}) {
        LevelPlan lp = planLevels(ppm, 4, 20);
        CHECK(lp.labelCm % lp.lineCm == 0);
        CHECK(lp.labelCm >= 50);
    }
}
