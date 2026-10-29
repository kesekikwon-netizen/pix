// 단면 위치 회귀: 실좌표로 준 단면선이 정확히 그 자리를 자르는가(원점 이동·비스듬한 선·x/y 뒤바뀜·반 픽셀 없음),
// 얕은 접시형 수혈(8 cm)도 정리 후 그대로 남는가, 화면 세로 과장 추천.
// 1.2.0 제주 신고("단면선이 지나가는 곳이 정확히 잘리지 않는다") 조사에서 만든 시험 — 원인은 자르기 오류가 아니라
// 원(수혈 윤곽)이 거의 평평(≈10 cm)했고 1:1 화면에서 평평해 보였던 것. 이 시험은 자르는 위치가 앞으로도 틀리지 않게 지킨다.
#include "catch_amalgamated.hpp"
#include "asec/engine.hpp"
#include "asec/pick.hpp"
#include "common.hpp"
#include "synth.hpp"
#include <cmath>

using namespace asec;

TEST_CASE("단면 위치(3MX): 실좌표 비스듬한 선이 수혈 한가운데를 자른다 — 점마다 Z = 그 XY 의 잎 표면, 바닥 깊이 일치", "[cutplace]") {
    auto dir = tmpDir("cutplace_tmx");
    synth::Params p; p.dir = dir; p.leafSpacing = 0.1; p.texSize = 16;   // 원점 (200000, 450000, 40)
    fs::path f; std::string err;
    REQUIRE(synth::write(p, &f, &err));
    TmxSource src; REQUIRE(src.open(f, &err));
    const Vec3 o = src.srs.origin;
    REQUIRE(o.x == Catch::Approx(200000.0));
    // 실좌표 A,B (앱과 같이: 로컬 = 실좌표 − 원점). 선은 수혈 중심(8,6)을 지나는 비스듬한 선(방위 ≈ 63°), 주혈(9.5,6)은 비껴감
    const double AX = o.x + 4.0, AY = o.y + 4.0, BX = o.x + 12.0, BY = o.y + 8.0;
    SectionRequest rq;
    rq.line.a = Vec2(AX - o.x, AY - o.y); rq.line.b = Vec2(BX - o.x, BY - o.y); rq.line.front = 0; rq.line.back = 0.5;
    rq.wantImage = false;
    SectionOutput so; REQUIRE(computeSection(src, rq, so, &err));
    CHECK_FALSE(so.previewLod);
    CHECK(so.stats.maxDepth == 2);        // 최고 해상도 잎
    CHECK(so.stats.fallbackNodes == 0);
    const SectionFrame fr(rq.line);
    double worst = 0, zMinCut = 1e9; size_t n = 0;
    for (auto& pl : so.result.profile)
        for (auto& q : pl) {
            // 단면 좌표 s → 실좌표(XY 는 정확히 A→B 위)
            Vec3 w = sectionToWorld(so.result, q.x, q.y);
            const double ex = AX + (BX - AX) * q.x / fr.L, ey = AY + (BY - AY) * q.x / fr.L;
            REQUIRE(std::fabs(w.x - ex) < 1e-6);
            REQUIRE(std::fabs(w.y - ey) < 1e-6);
            // 그 XY 의 잎 표면 높이(해석적 격자 보간 + 원점 z)
            const double want = synth::gridZ(w.x - o.x, w.y - o.y, p.leafSpacing) + o.z;
            worst = std::max(worst, std::fabs(w.z - want));
            zMinCut = std::min(zMinCut, w.z); ++n;
        }
    REQUIRE(n > 20);         // 더글라스-포이커(0.5 mm)로 솎인 꼭짓점
    CHECK(worst < 0.0015);   // OpenCTM 양자화 안. x/y 가 바뀌거나 원점·반 픽셀만큼 밀리면 수혈 벽에서 수 cm~60 cm 틀림
    // 꼭짓점 사이도: 1 cm 간격으로 선형 보간한 단면 Z 와 그 XY 의 표면(솎기 0.5 mm + 양자화)
    double worstDense = 0; size_t nd = 0;
    for (auto& pl : so.result.profile)
        for (size_t i = 1; i < pl.size(); ++i) {
            const Vec2 a = pl[i - 1], b = pl[i];
            if (b.x - a.x < 1e-6) continue;
            for (double sx = a.x; sx < b.x; sx += 0.01) {
                const double zl = a.y + (b.y - a.y) * (sx - a.x) / (b.x - a.x);
                const double lx = 4.0 + 8.0 * sx / fr.L, ly = 4.0 + 4.0 * sx / fr.L;
                worstDense = std::max(worstDense, std::fabs(zl - synth::gridZ(lx, ly, p.leafSpacing))); ++nd;
            }
        }
    CHECK(nd > 800);
    CHECK(worstDense < 0.0025);
    // 바닥: 선을 따라 1 cm 간격 해석적 최저와 2 mm 안
    double zMinTrue = 1e9;
    for (double s = 0; s <= fr.L; s += 0.01) zMinTrue = std::min(zMinTrue, synth::gridZ(4.0 + 8.0 * s / fr.L, 4.0 + 4.0 * s / fr.L, p.leafSpacing) + o.z);
    CHECK(std::fabs(zMinCut - zMinTrue) < 0.002);
    // 대조: 같은 선을 30 cm 옆으로 옮기면(잘못된 원점·좌표 뒤바뀜 흉내) 점별 차이가 cm 단위로 커진다 → 위 시험이 위치 오류를 잡을 수 있음
    SectionRequest sh = rq; const Vec2 nn = fr.n * 0.3; sh.line.a = rq.line.a + nn; sh.line.b = rq.line.b + nn;
    SectionOutput so2; REQUIRE(computeSection(src, sh, so2, &err));
    double worstShift = 0;
    for (auto& pl : so2.result.profile)
        for (auto& q : pl) {
            const double ex = AX + (BX - AX) * q.x / fr.L, ey = AY + (BY - AY) * q.x / fr.L;   // 원래 선 위 XY 와 비교
            worstShift = std::max(worstShift, std::fabs(q.y + o.z - (synth::gridZ(ex - o.x, ey - o.y, p.leafSpacing) + o.z)));
        }
    CHECK(worstShift > 0.05);
}

namespace {
double dishZ(double x, double y) {   // 로컬: 57.2 m 지면 + 중심(3,3) 반지름 1.4 m · 깊이 8 cm 접시(제주 원형 윤곽과 비슷)
    auto ss = [](double a, double b, double t) { t = std::clamp((t - a) / (b - a), 0.0, 1.0); return t * t * (3 - 2 * t); };
    return 57.2 - 0.08 * (1.0 - ss(1.25, 1.45, std::hypot(x - 3.0, y - 3.0))) + 0.004 * std::sin(7 * x) * std::cos(5 * y);
}
std::shared_ptr<StaticSource> dishSource() {
    auto s = std::make_shared<StaticSource>();
    auto m = std::make_shared<Mesh>();
    const int n = 300; const double h = 0.02;   // 6 m × 6 m, 2 cm 격자
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) { double x = i * h, y = j * h; m->pos.insert(m->pos.end(), {float(x), float(y), float(dishZ(x, y))}); }
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) { uint32_t a = uint32_t(j * (n + 1) + i), b = a + 1, c = a + uint32_t(n + 1), d = c + 1; m->idx.insert(m->idx.end(), {a, b, d, a, d, c}); }
    m->computeBBox();
    s->meshes.push_back(m);
    s->bounds = m->bbox;
    s->srs.srs = "EPSG:5186"; s->srs.origin = Vec3(148093, 98119, 0); s->srs.hasOrigin = true;   // 제주 모델과 같은 원점 꼴(z 0)
    return s;
}
}  // namespace

TEST_CASE("얕은 접시형 수혈(8 cm): 정리(이어붙이기·솎기·평활) 후에도 깊이가 남고, 점마다 연직 피킹과 1 mm 안", "[cutplace]") {
    auto s = dishSource();
    const Vec3 o = s->srs.origin;
    // 중심에서 0.3 m 비낀 동서 선(제주 사례처럼 원을 가로지름), 실좌표로
    const double AX = o.x + 1.2, AY = o.y + 2.7, BX = o.x + 4.82, BY = o.y + 2.7;
    SectionRequest rq; rq.line.a = Vec2(AX - o.x, AY - o.y); rq.line.b = Vec2(BX - o.x, BY - o.y); rq.line.back = 0.5; rq.wantImage = false;
    SectionOutput so; std::string err; REQUIRE(computeSection(*s, rq, so, &err));
    REQUIRE(!so.result.profile.empty());
    double zmin = 1e9, zmax = -1e9, worst = 0;
    for (auto& pl : so.result.profile)
        for (auto& q : pl) {
            Vec3 w = sectionToWorld(so.result, q.x, q.y);
            zmin = std::min(zmin, w.z); zmax = std::max(zmax, w.z);
            PickResult pr; REQUIRE(pickVerticalWorld(*s, w.x, w.y, pr, &err)); REQUIRE(pr.hit);
            worst = std::max(worst, std::fabs(pr.world.z - w.z));
        }
    CHECK(worst < 0.001);
    CHECK(zmax - zmin > 0.075);           // 8 cm 접시가 평활로 지워지지 않음
    CHECK(zmin == Catch::Approx(57.2 - 0.08).margin(0.006));
    const double rel = profileRelief(so.result.profile);
    CHECK(rel > 0.06); CHECK(rel < 0.095);
}

TEST_CASE("화면 세로 과장 추천: 기복이 보이는 높이의 15% 이상이 되는 가장 작은 1·2·5·10", "[cutplace]") {
    CHECK(suggestVerticalExaggeration(1.0, 3.9) == 1);
    CHECK(suggestVerticalExaggeration(0.4, 3.9) == 2);
    CHECK(suggestVerticalExaggeration(0.15, 3.9) == 5);   // 제주 원 안 단면(기복 ≈ 15 cm, 화면 높이 ≈ 3.9 m)
    CHECK(suggestVerticalExaggeration(0.01, 3.9) == 10);
    CHECK(suggestVerticalExaggeration(0.0, 3.9) == 1);
    CHECK(suggestVerticalExaggeration(0.2, 0.0) == 1);
    // 닫힌 고리(덤불)는 기복 계산에서 뺀다
    std::vector<Polyline> pls;
    Polyline ground; for (int i = 0; i <= 100; ++i) ground.push_back(Vec2(i * 0.03, 57.2 + 0.001 * (i % 7)));
    Polyline bush = {Vec2(1, 59), Vec2(1.5, 61), Vec2(2, 59.5), Vec2(1.2, 59.2), Vec2(1, 59)};
    pls.push_back(ground); pls.push_back(bush);
    CHECK(profileRelief(pls) < 0.01);
    CHECK(profileRelief({}) == 0);
}
