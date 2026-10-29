// joinGaps: 실제 현장 모델에서 수천 조각일 때 O(n³) 로 수 분 멈추던 문제(1.0) 회귀 시험 + 이전 알고리즘과 같은 결과
#include "catch_amalgamated.hpp"
#include "asec/section.hpp"
#include <algorithm>
#include <chrono>
#include <random>

using namespace asec;

namespace {
bool closedRef(const Polyline& pl) { return pl.size() > 3 && (pl.front() - pl.back()).len() < 1e-12; }
// 1.0 의 원래 알고리즘(매번 전체에서 가장 가까운 끝점 쌍을 찾아 잇기) — 기준값
void joinGapsRef(std::vector<Polyline>& L, double gapTol) {
    while (true) {
        double best = gapTol; int bi = -1, bj = -1, mode = 0;
        for (size_t i = 0; i < L.size(); ++i) {
            if (L[i].size() < 2 || closedRef(L[i])) continue;
            for (size_t j = i + 1; j < L.size(); ++j) {
                if (L[j].size() < 2 || closedRef(L[j])) continue;
                const Vec2 e[4][2] = {{L[i].back(), L[j].front()}, {L[i].back(), L[j].back()}, {L[i].front(), L[j].front()}, {L[i].front(), L[j].back()}};
                for (int k = 0; k < 4; ++k) { double d = (e[k][0] - e[k][1]).len(); if (d < best) { best = d; bi = int(i); bj = int(j); mode = k; } }
            }
        }
        if (bi < 0) break;
        Polyline& A = L[size_t(bi)]; Polyline B = std::move(L[size_t(bj)]); L.erase(L.begin() + bj);
        if (mode == 1) std::reverse(B.begin(), B.end());
        if (mode == 2) std::reverse(A.begin(), A.end());
        if (mode == 3) { std::reverse(A.begin(), A.end()); std::reverse(B.begin(), B.end()); }
        size_t sB = ((A.back() - B.front()).len() <= 1e-12) ? 1 : 0;
        A.insert(A.end(), B.begin() + long(sB), B.end());
    }
}
// 비교용 정규형: 방향 무관(사전순 작은 쪽), 선 순서 무관
std::vector<Polyline> canon(std::vector<Polyline> L) {
    for (auto& p : L) {
        Polyline r(p.rbegin(), p.rend());
        auto lt = [](const Polyline& a, const Polyline& b) { return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](const Vec2& x, const Vec2& y) { return x.x < y.x || (x.x == y.x && x.y < y.y); }); };
        if (lt(r, p)) p = r;
    }
    std::sort(L.begin(), L.end(), [](const Polyline& a, const Polyline& b) { return a.front().x < b.front().x || (a.front().x == b.front().x && a.front().y < b.front().y); });
    return L;
}
}  // namespace

TEST_CASE("joinGaps: 무작위 조각에서 이전(O(n³)) 알고리즘과 같은 결과") {
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> U(0, 3), G(0, 0.06);
    for (int trial = 0; trial < 40; ++trial) {
        std::vector<Polyline> L;
        int n = 5 + trial * 3;
        for (int i = 0; i < n; ++i) {
            Vec2 a(U(rng), U(rng)), b = a + Vec2(G(rng) * 5, G(rng) * 5);
            Polyline p{a, (a + b) * 0.5, b};
            if (rng() & 1) std::reverse(p.begin(), p.end());
            L.push_back(p);
        }
        auto A = L, B = L;
        joinGaps(A, 0.03);
        joinGapsRef(B, 0.03);
        auto ca = canon(A), cb = canon(B);
        INFO("trial " << trial);
        REQUIRE(ca.size() == cb.size());
        size_t va = 0, vb = 0;
        for (auto& p : ca) va += p.size();
        for (auto& p : cb) vb += p.size();
        CHECK(va == vb);
        // 같은 거리 동점이 없으면 같은 사슬
        for (size_t i = 0; i < ca.size(); ++i) CHECK(ca[i].size() == cb[i].size());
    }
}

TEST_CASE("joinGaps: 조각 6000개(뒤섞임·방향 섞임)도 1초 안에 한 줄로") {
    std::vector<Polyline> L;
    const int n = 6000;
    for (int i = 0; i < n; ++i) {
        double x0 = i * 0.01;
        Polyline p{Vec2(x0 + 0.0005, std::sin(x0)), Vec2(x0 + 0.009, std::sin(x0 + 0.009))};  // 조각 사이 틈 1.5 mm
        if (i % 3 == 0) std::reverse(p.begin(), p.end());
        L.push_back(p);
    }
    std::shuffle(L.begin(), L.end(), std::mt19937(3));
    auto t0 = std::chrono::steady_clock::now();
    joinGaps(L, 0.03);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 1000);
    REQUIRE(L.size() == 1);
    CHECK(L[0].size() == size_t(2 * n));
}
