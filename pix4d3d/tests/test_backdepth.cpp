// 뒤 깊이(입면 배경) 기본 3 m(1.2.1, 「뒤로 3 m 이상 보여야 입면도를 그릴 수 있다」) + 「잘린 면은 정확해야 한다」:
// 옛 설정(0.5 기본) 이관, 3 m 뒤 물체가 입면 영상에 실제로 그려짐, 배경 깊이와 무관하게 단면선은 같고 잎 기준,
// 미리보기(배경 거친 LOD)에서도 단면선은 잎으로 잘라 최종과 같음.
#include "catch_amalgamated.hpp"
#include "asec/engine.hpp"
#include "asec/pick.hpp"
#include "asec/sheet.hpp"
#include "common.hpp"
#include "synth.hpp"
#include <algorithm>
#include <cmath>

using namespace asec;

TEST_CASE("뒤 깊이 기본 3 m, 옛 기본값 0.5 는 사용자가 바꾼 표시가 없으면 3 m 로", "[backdepth]") {
    CHECK(kDefaultBackDepth == 3.0);
    CHECK(SectionLine().back == kDefaultBackDepth);
    CHECK(SavedSection().back == kDefaultBackDepth);
    CHECK(resolveBackDepth(false, 0, false) == 3.0);       // 저장 없음
    CHECK(resolveBackDepth(true, 0.5, false) == 3.0);      // 1.2.0 이 저장한 옛 기본값
    CHECK(resolveBackDepth(true, 0.5, true) == 0.5);       // 1.2.1 에서 사용자가 0.5 를 고름
    CHECK(resolveBackDepth(true, 2.0, false) == 2.0);      // 옛 버전에서 사용자가 바꾼 값
    CHECK(resolveBackDepth(true, 1.0, true) == 1.0);
    CHECK(resolveBackDepth(true, 9.0, true) == kMaxBackDepth);
    CHECK(resolveBackDepth(true, -1.0, false) == 3.0);
}

TEST_CASE("단면 목록 JSON: 옛 기록(backUserSet 없음)의 0.5 → 3 m, 다른 값·사용자 표시는 유지, 왕복 보존", "[backdepth]") {
    const std::string old = R"({"format":"ExcavSection.sections","version":1,"current":0,"sections":[
        {"name":"A–A′","a":[1,2],"b":[3,4],"front":0,"back":0.5},
        {"name":"B–B′","a":[1,2],"b":[3,4],"front":0,"back":2.0},
        {"name":"C–C′","a":[1,2],"b":[3,4],"front":0},
        {"name":"D–D′","a":[1,2],"b":[3,4],"front":0,"back":0.5,"backUserSet":true}]})";
    std::vector<SavedSection> v; int cur = -1; std::string err;
    REQUIRE(sectionsFromJson(old, v, &cur, &err));
    REQUIRE(v.size() == 4);
    CHECK(v[0].back == 3.0); CHECK_FALSE(v[0].backUserSet);
    CHECK(v[1].back == 2.0);
    CHECK(v[2].back == 3.0);
    CHECK(v[3].back == 0.5); CHECK(v[3].backUserSet);
    std::vector<SavedSection> w;
    REQUIRE(sectionsFromJson(sectionsToJson(v, "m.3mx", "EPSG:5186", 0), w, &cur, &err));
    REQUIRE(w.size() == 4);
    for (size_t i = 0; i < 4; ++i) { CHECK(w[i].back == v[i].back); CHECK(w[i].backUserSet == v[i].backUserSet); }
}

namespace {
void addBox(Mesh& m, double x0, double y0, double z0, double x1, double y1, double z1) {
    const uint32_t b = uint32_t(m.pos.size() / 3);
    const double P[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
    for (auto& p : P) m.pos.insert(m.pos.end(), {float(p[0]), float(p[1]), float(p[2])});
    const uint32_t F[12][3] = {{0, 1, 5}, {0, 5, 4}, {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}, {4, 5, 6}, {4, 6, 7}, {0, 2, 1}, {0, 3, 2}};
    for (auto& f : F) m.idx.insert(m.idx.end(), {b + f[0], b + f[1], b + f[2]});
}
// 평평한 땅(z=0, 10×6 m, 10 cm 격자, 작은 굴곡) + 단면선 뒤 2.8 m 의 기둥(0.2×0.2×1.5 m)
std::shared_ptr<StaticSource> postScene() {
    auto s = std::make_shared<StaticSource>();
    auto g = std::make_shared<Mesh>();
    const int nx = 100, ny = 60;
    for (int j = 0; j <= ny; ++j) for (int i = 0; i <= nx; ++i) { double x = i * 0.1, y = j * 0.1; g->pos.insert(g->pos.end(), {float(x), float(y), float(0.03 * std::sin(1.3 * x) * std::cos(0.7 * y))}); }
    for (int j = 0; j < ny; ++j) for (int i = 0; i < nx; ++i) { uint32_t a = uint32_t(j * (nx + 1) + i), b = a + 1, c = a + uint32_t(nx + 1), d = c + 1; g->idx.insert(g->idx.end(), {a, b, d, a, d, c}); }
    g->computeBBox();
    auto post = std::make_shared<Mesh>();
    addBox(*post, 4.9, 3.7, -0.05, 5.1, 3.9, 1.5);   // 단면선 y=1 에서 뒤로 2.7–2.9 m
    post->computeBBox();
    s->meshes = {g, post};
    s->bounds = g->bbox; s->bounds.add(post->bbox);
    s->srs.srs = "EPSG:5186"; s->srs.origin = Vec3(148093, 98119, 0); s->srs.hasOrigin = true;
    return s;
}
bool opaqueAt(const ElevationImage& im, double s, double z) {
    if (im.img.empty()) return false;
    const int c = int((s - im.s0) / im.res), r = int((im.z1 - z) / im.res);
    if (c < 0 || r < 0 || c >= im.img.w || r >= im.img.h) return false;
    return im.img.px[(size_t(r) * im.img.w + c) * 4 + 3] > 0;
}
}  // namespace

TEST_CASE("입면 배경 3 m: 뒤 2.8 m 기둥이 영상에 그려지고(0.5 m 이면 안 보임), 단면선은 배경 깊이와 무관하게 같다", "[backdepth]") {
    auto s = postScene();
    SectionRequest rq; rq.line.a = {1, 1}; rq.line.b = {9, 1}; rq.line.front = 0; rq.imageRes = 0.01; rq.depthFade = 0.6;   // 보는 방향 +y
    rq.line.back = 3.0;
    SectionOutput far; std::string err; REQUIRE(computeSection(*s, rq, far, &err));
    rq.line.back = 0.5;
    SectionOutput nearO; REQUIRE(computeSection(*s, rq, nearO, &err));
    CHECK(opaqueAt(far.image, 4.0, 1.0));      // s = 5−1 = 4 m, z = 1 m: 기둥
    CHECK(opaqueAt(far.image, 4.0, 1.45));
    CHECK_FALSE(opaqueAt(nearO.image, 4.0, 1.0));
    CHECK(far.result.zMax >= 1.5);            // 표시 범위가 배경 물체까지
    // 단면선(잘린 면)은 뒤 깊이와 무관: 꼭짓점이 비트 단위로 같다
    REQUIRE(far.result.profile.size() == nearO.result.profile.size());
    for (size_t i = 0; i < far.result.profile.size(); ++i) {
        REQUIRE(far.result.profile[i].size() == nearO.result.profile[i].size());
        for (size_t k = 0; k < far.result.profile[i].size(); ++k) {
            CHECK(far.result.profile[i][k].x == nearO.result.profile[i][k].x);
            CHECK(far.result.profile[i][k].y == nearO.result.profile[i][k].y);
        }
    }
    // 단면선 점마다 연직 피킹과 1 mm 안(땅 위 — 기둥은 단면선에 없음)
    double worst = 0;
    for (auto& pl : far.result.profile) for (auto& q : pl) {
        Vec3 w = sectionToWorld(far.result, q.x, q.y);
        PickResult pr; REQUIRE(pickVerticalWorld(*s, w.x, w.y, pr, &err)); REQUIRE(pr.hit);
        worst = std::max(worst, std::fabs(pr.world.z - w.z));
    }
    CHECK(worst < 0.001);
}

TEST_CASE("미리보기(배경 거친 LOD, 뒤 3 m)에서도 단면선은 잎으로 잘라 최종과 똑같다; 잎 피킹과 1 mm 안", "[backdepth]") {
    auto dir = tmpDir("backdepth_preview");
    synth::Params p; p.dir = dir; p.leafSpacing = 0.1; p.texSize = 16;
    fs::path f; std::string err;
    REQUIRE(synth::write(p, &f, &err));
    TmxSource src; REQUIRE(src.open(f, &err));
    SectionRequest rq; rq.line.a = {1.0, 6.1}; rq.line.b = {15.0, 6.3}; rq.line.front = 0; rq.line.back = 3.0; rq.imageRes = 0.02;
    SectionOutput fin; REQUIRE(computeSection(src, rq, fin, &err));
    CHECK(fin.cutFromLeaf); CHECK(fin.cutStats.maxDepth == 2);
    rq.meshRes = 0.05;
    SectionOutput pre; REQUIRE(computeSection(src, rq, pre, &err));
    CHECK(pre.previewLod);
    CHECK(pre.stats.maxDepth == 1);           // 배경 영상: 거친 LOD
    CHECK(pre.cutFromLeaf);
    CHECK(pre.cutStats.maxDepth == 2);        // 단면선: 잎
    REQUIRE(pre.result.profile.size() == fin.result.profile.size());
    double dmax = 0;
    for (size_t i = 0; i < fin.result.profile.size(); ++i) {
        REQUIRE(pre.result.profile[i].size() == fin.result.profile[i].size());
        for (size_t k = 0; k < fin.result.profile[i].size(); ++k) dmax = std::max(dmax, (pre.result.profile[i][k] - fin.result.profile[i][k]).len());
    }
    CHECK(dmax == 0.0);
    // 잎 표면(해석적 격자 보간)과 점마다
    double worst = 0;
    for (auto& pl : fin.result.profile) for (auto& q : pl) {
        Vec3 w = sectionToWorld(fin.result, q.x, q.y);
        worst = std::max(worst, std::fabs(w.z - (synth::gridZ(w.x - p.origin.x, w.y - p.origin.y, p.leafSpacing) + p.origin.z)));
    }
    CHECK(worst < 0.0015);
    // 대조(옛 동작): 미리보기를 거친 LOD 로 자르면 cm 단위로 틀림 → 위 시험이 의미 있음
    rq.leafProfileAlways = false;
    SectionOutput oldPre; REQUIRE(computeSection(src, rq, oldPre, &err));
    CHECK_FALSE(oldPre.cutFromLeaf);
    double worstOld = 0;
    for (auto& pl : oldPre.result.profile) for (auto& q : pl) {
        Vec3 w = sectionToWorld(oldPre.result, q.x, q.y);
        worstOld = std::max(worstOld, std::fabs(w.z - (synth::gridZ(w.x - p.origin.x, w.y - p.origin.y, p.leafSpacing) + p.origin.z)));
    }
    CHECK(worstOld > 0.01);
}

TEST_CASE("화면 단면선 솎기(0.1 px 허용)는 모든 꼭짓점을 그려진 선 0.1 px 안에 둔다(세로 과장 포함)", "[backdepth]") {
    // 잎 단면처럼 mm 단위 굴곡이 많은 선. 화면 그리기는 tol = 0.1 px / max(가로, 세로 px/m) 로 솎음(sectionview.cpp)
    Polyline pl;
    uint32_t seed = 7;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / double(1 << 24) - 0.5; };
    for (int i = 0; i <= 4000; ++i) { double s = i * 0.0095; pl.push_back(Vec2(s, 0.3 * std::sin(s * 0.7) + 0.004 * rnd())); }
    for (double vex : {1.0, 5.0}) {
        for (double ppm : {40.0, 400.0, 4000.0}) {
            const double ppmZ = ppm * vex, tolM = 0.1 / std::max(ppm, ppmZ);
            Polyline q = simplifyDP(pl, tolM);
            REQUIRE(q.size() >= 2);
            CHECK(q.front().x == pl.front().x); CHECK(q.back().x == pl.back().x);
            // 화면 좌표(가로 ppm, 세로 ppmZ)에서 원래 꼭짓점 ↔ 솎은 선 최소 거리
            double worstPx = 0;
            size_t j = 0;
            for (auto& p : pl) {
                while (j + 1 < q.size() - 1 && q[j + 1].x < p.x) ++j;
                double best = 1e300;
                for (size_t k = (j > 0 ? j - 1 : 0); k + 1 < q.size() && k <= j + 1; ++k) {
                    Vec2 a(q[k].x * ppm, q[k].y * ppmZ), b(q[k + 1].x * ppm, q[k + 1].y * ppmZ), P(p.x * ppm, p.y * ppmZ);
                    Vec2 ab = b - a; double L2 = ab.dot(ab);
                    double t = L2 > 0 ? std::clamp((P - a).dot(ab) / L2, 0.0, 1.0) : 0.0;
                    best = std::min(best, (P - (a + ab * t)).len());
                }
                worstPx = std::max(worstPx, best);
            }
            INFO("vex " << vex << " ppm " << ppm << " pts " << pl.size() << "->" << q.size());
            CHECK(worstPx <= 0.1 + 1e-9);
        }
    }
}
