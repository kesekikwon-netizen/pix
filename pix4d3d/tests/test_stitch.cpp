// 폴리라인 이어붙이기·정리 품질 시험
#include <algorithm>
#include <random>
#include "catch_amalgamated.hpp"
#include "asec/section.hpp"

using namespace asec;
using Catch::Approx;

static std::vector<CutSeg> segsOf(const Polyline& p) {
    std::vector<CutSeg> s;
    for (size_t i = 1; i < p.size(); ++i) s.push_back({p[i - 1].x, p[i - 1].y, p[i].x, p[i].y});
    return s;
}
static Polyline profileCurve(int n, double L) {
    Polyline p;
    for (int i = 0; i <= n; ++i) { double s = L * i / n; p.emplace_back(s, 45 + 0.3 * std::sin(s) - (s > 4 && s < 6 ? 0.6 : 0)); }
    return p;
}

TEST_CASE("섞이고 뒤집힌 선분 → 순서대로 한 줄") {
    auto ref = profileCurve(400, 10);
    auto s = segsOf(ref);
    std::mt19937 rng(7);
    std::shuffle(s.begin(), s.end(), rng);
    for (size_t i = 0; i < s.size(); i += 2) { std::swap(s[i].s0, s[i].s1); std::swap(s[i].z0, s[i].z1); }
    CleanupParams cp; cp.simplifyTol = 0;
    auto L = buildProfile(s, cp);
    REQUIRE(L.size() == 1);
    CHECK(L[0].size() == ref.size());
    CHECK(L[0].front().x == Approx(0.0));
    CHECK(L[0].back().x == Approx(10.0));
    for (size_t i = 1; i < L[0].size(); ++i) CHECK(L[0][i].x > L[0][i - 1].x);
}

TEST_CASE("끝점 미세 어긋남(타일 경계) 용접") {
    auto ref = profileCurve(200, 10);
    auto s = segsOf(ref);
    std::mt19937 rng(3);
    std::uniform_real_distribution<double> j(-0.0002, 0.0002);  // ±0.2 mm
    for (auto& g : s) { g.s0 += j(rng); g.z0 += j(rng); g.s1 += j(rng); g.z1 += j(rng); }
    auto L = buildProfile(s, CleanupParams{});
    REQUIRE(L.size() == 1);
    CHECK(countDuplicateVertices(L, 0.0005) == 0);
}

TEST_CASE("겹선·퇴화 선분 제거, 중복 꼭짓점 0") {
    auto ref = profileCurve(100, 5);
    auto s = segsOf(ref);
    auto dup = s;
    s.insert(s.end(), dup.begin(), dup.end());                 // 전부 두 번
    s.push_back({1.0, 45.0, 1.0, 45.0});                         // 퇴화
    s.push_back({s[10].s1, s[10].z1, s[10].s0, s[10].z0});        // 역방향 겹선
    CleanupParams cp; cp.simplifyTol = 0;
    auto L = buildProfile(s, cp);
    REQUIRE(L.size() == 1);
    CHECK(L[0].size() == ref.size());
    CHECK(countDuplicateVertices(L, 1e-9) == 0);
}

TEST_CASE("작은 끊김 잇기(3 cm 이내), 큰 끊김은 유지") {
    auto lin = [](double s0, double z0, double s1, double z1) { Polyline p; for (int i = 0; i <= 100; ++i) p.emplace_back(s0 + (s1 - s0) * i / 100, z0 + (z1 - z0) * i / 100); return p; };
    auto a = lin(0, 45.0, 4, 45.1);
    auto b = lin(4.02, 45.1, 8.02, 45.0);   // 2 cm 틈
    auto c = lin(8.5, 45.0, 12.5, 45.2);    // 48 cm 틈
    auto s = segsOf(a); auto sb = segsOf(b); auto sc = segsOf(c);
    s.insert(s.end(), sb.begin(), sb.end()); s.insert(s.end(), sc.begin(), sc.end());
    auto L = buildProfile(s, CleanupParams{});
    REQUIRE(L.size() == 2);
    CHECK(L[0].front().x == Approx(0.0));
    CHECK(L[0].back().x == Approx(8.02));
    CHECK(L[1].front().x == Approx(8.5));
}

TEST_CASE("가시(spike) 제거, 실제 모서리(수혈 벽 90도)는 보존") {
    Polyline p{{0, 45}, {1, 45}, {1.003, 45.015}, {1.006, 45}, {2, 45}, {2, 44.4}, {3, 44.4}};
    //           가시: 1→1.005(위로 1.5cm)→1.01 되돌아옴        벽: (2,45)→(2,44.4) 수직
    int r = removeSpikes(p, 0.02, 150);
    CHECK(r == 1);
    bool hasWallTop = false, hasWallBottom = false;
    for (auto& v : p) { if (v.x == 2 && v.y == 45) hasWallTop = true; if (v.x == 2 && v.y == 44.4) hasWallBottom = true; }
    CHECK(hasWallTop); CHECK(hasWallBottom);
    for (auto& v : p) CHECK(v.y <= 45.0 + 1e-12);
}

TEST_CASE("작은 조각 버림") {
    auto a = profileCurve(100, 4);
    Polyline crumb{{6, 45}, {6.005, 45.002}, {6.01, 45}};  // 1 cm 부스러기
    auto s = segsOf(a); auto sc = segsOf(crumb);
    s.insert(s.end(), sc.begin(), sc.end());
    auto L = buildProfile(s, CleanupParams{});
    CHECK(L.size() == 1);
}

TEST_CASE("단순화: 공차 안에서 형상 보존, 모서리 유지") {
    Polyline p;
    for (int i = 0; i <= 100; ++i) p.emplace_back(i * 0.02, 45.0);        // 평탄 2 m
    for (int i = 1; i <= 30; ++i) p.emplace_back(2.0, 45.0 - i * 0.02);   // 벽 60 cm
    for (int i = 1; i <= 100; ++i) p.emplace_back(2.0 + i * 0.02, 44.4);
    auto q = simplifyDP(p, 0.0005);
    CHECK(q.size() == 4);
    CHECK(q[1].x == Approx(2.0)); CHECK(q[1].y == Approx(45.0));
    CHECK(q[2].x == Approx(2.0)); CHECK(q[2].y == Approx(44.4));
}

TEST_CASE("평활(선택): 잡음 감소, 끝점 고정, 기본은 꺼짐") {
    std::mt19937 rng(11);
    std::normal_distribution<double> nz(0, 0.004);
    Polyline p;
    for (int i = 0; i <= 300; ++i) p.emplace_back(i * 0.01, 45.0 + nz(rng));
    auto rough = [](const Polyline& q) { double r = 0; for (size_t i = 1; i + 1 < q.size(); ++i) r += std::fabs(q[i + 1].y - 2 * q[i].y + q[i - 1].y); return r; };
    Polyline sm = p;
    smoothTaubin(sm, 3);
    CHECK(rough(sm) < rough(p) * 0.5);
    CHECK(sm.front().y == p.front().y);
    CHECK(sm.back().y == p.back().y);
    CHECK_FALSE(CleanupParams{}.smooth);
}

TEST_CASE("정리 결과: 연속 두 점 사이 최소 거리 >= 용접 공차") {
    auto ref = profileCurve(2000, 10);  // 5 mm 간격 + 잡음
    std::mt19937 rng(5);
    std::uniform_real_distribution<double> j(-0.0003, 0.0003);
    auto s = segsOf(ref);
    for (auto& g : s) { g.z0 += j(rng); g.z1 += j(rng); }
    CleanupParams cp;
    auto L = buildProfile(s, cp);
    REQUIRE(L.size() == 1);
    double mind = 1e9;
    for (size_t i = 1; i < L[0].size(); ++i) mind = std::min(mind, (L[0][i] - L[0][i - 1]).len());
    CHECK(mind >= cp.weldTol);
}

TEST_CASE("닫힌 고리(나무·돌 덩어리) 보존: 지면선과 따로, 처음=끝, 잇기에 끼지 않음") {
    // 지면 + 지면 바로 위(2 cm) 떠 있는 덩어리 단면(원), 원은 섞어서 넣음
    Polyline ground; for (int i = 0; i <= 100; ++i) ground.emplace_back(i * 0.1, 45.0);
    Polyline blob; for (int i = 0; i <= 72; ++i) { double a = i * 2 * 3.14159265358979 / 72; blob.emplace_back(5 + 0.5 * std::cos(a), 45.52 + 0.5 * std::sin(a)); }
    auto s = segsOf(ground); auto b = segsOf(blob);
    std::mt19937 rng(3); std::shuffle(b.begin(), b.end(), rng);
    s.insert(s.end(), b.begin(), b.end());
    CleanupParams cp;
    auto L = buildProfile(s, cp);
    REQUIRE(L.size() == 2);
    int closed = 0;
    for (auto& pl : L) if ((pl.front() - pl.back()).len() < 1e-12 && pl.size() > 3) { ++closed; CHECK(polylineLength(pl) == Approx(2 * 3.14159 * 0.5).epsilon(0.01)); }
    CHECK(closed == 1);
    CHECK(countDuplicateVertices(L, cp.weldTol) == 0);
}

TEST_CASE("거의 닫힌 고리(틈 2.6 cm)는 닫힘") {
    Polyline blob; for (int i = 0; i <= 71; ++i) { double a = i * 2 * 3.14159265358979 / 72; blob.emplace_back(5 + 0.3 * std::cos(a), 45.5 + 0.3 * std::sin(a)); }
    auto L = buildProfile(segsOf(blob), CleanupParams());
    REQUIRE(L.size() == 1);
    CHECK((L[0].front() - L[0].back()).len() < 1e-12);
}
