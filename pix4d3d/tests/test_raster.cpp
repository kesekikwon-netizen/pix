#include "catch_amalgamated.hpp"
#include "asec/raster.hpp"
#include "synth.hpp"

using namespace asec;
using Catch::Approx;

static MeshPtr wall(double x, double y0, double y1, double z0, double z1, uint8_t r, uint8_t g, uint8_t b) {
    // 수직 벽(평면 x=const), 단색 텍스처
    auto m = std::make_shared<Mesh>();
    m->pos = {float(x), float(y0), float(z0), float(x), float(y1), float(z0), float(x), float(y1), float(z1), float(x), float(y0), float(z1)};
    m->uv = {0, 0, 1, 0, 1, 1, 0, 1};
    m->idx = {0, 1, 2, 0, 2, 3};
    m->texture = std::make_shared<Texture>();
    m->texture->rgba.w = 2; m->texture->rgba.h = 2; m->texture->rgba.px.assign(16, 255);
    for (int i = 0; i < 4; ++i) { m->texture->rgba.px[i * 4] = r; m->texture->rgba.px[i * 4 + 1] = g; m->texture->rgba.px[i * 4 + 2] = b; }
    m->computeBBox();
    return m;
}

TEST_CASE("입면 영상: 가까운 면이 앞, 두께 띠 밖은 제외, 좌표 정렬") {
    // 단면선 y=0, x 0→4. 보는 방향 +y(n). 벽 A: y=0.2 빨강(x=1..3), 벽 B: y=0.4 파랑(x=0..4), 벽 C: y=2.0 초록(띠 밖)
    auto A = std::make_shared<Mesh>(); // x=const 벽이 아니라 y=const 면이 필요 → 직접 구성
    auto face = [](double y, double x0, double x1, double z0, double z1, uint8_t r, uint8_t g, uint8_t b) {
        auto m = std::make_shared<Mesh>();
        m->pos = {float(x0), float(y), float(z0), float(x1), float(y), float(z0), float(x1), float(y), float(z1), float(x0), float(y), float(z1)};
        m->uv = {0, 0, 1, 0, 1, 1, 0, 1};
        m->idx = {0, 1, 2, 0, 2, 3};
        m->texture = std::make_shared<Texture>();
        m->texture->rgba.w = 1; m->texture->rgba.h = 1; m->texture->rgba.px = {r, g, b, 255};
        m->computeBBox();
        return m;
    };
    std::vector<MeshPtr> ms{face(0.4, 0, 4, 0, 2, 0, 0, 255), face(0.2, 1, 3, 0, 1, 255, 0, 0), face(2.0, 0, 4, 0, 3, 0, 255, 0)};
    SectionFrame f(SectionLine{{0, 0}, {4, 0}});
    RasterRequest rq; rq.s0 = 0; rq.s1 = 4; rq.z0 = 0; rq.z1 = 3; rq.res = 0.01; rq.dNear = 0; rq.dFar = 1.0;
    ElevationImage im;
    REQUIRE(renderElevation(ms, f, rq, im));
    REQUIRE(im.img.w == 400); REQUIRE(im.img.h == 300);
    auto px = [&](double s, double z) { int c = int((s - im.s0) / im.res), r = int((im.z1 - z) / im.res); return &im.img.px[(size_t(r) * im.img.w + c) * 4]; };
    CHECK(int(px(2.0, 0.5)[0]) == 255);  // 빨강이 앞
    CHECK(int(px(0.5, 0.5)[2]) == 255);  // 빨강 밖은 파랑
    CHECK(int(px(2.0, 1.5)[2]) == 255);
    CHECK(int(px(2.0, 2.5)[3]) == 0);    // 초록(띠 밖) 안 보임 → 투명
    // 경계 정렬: 빨강 시작 s=1.0 → 열 100
    CHECK(int(im.img.px[(size_t(250) * 400 + 99) * 4 + 2]) == 255);
    CHECK(int(im.img.px[(size_t(250) * 400 + 100) * 4 + 0]) == 255);
    (void)wall;
}

TEST_CASE("입면 영상 위쪽 경계 = 단면선 높이(정렬 확인)") {
    auto m = synth::gridMesh(0, -1, 10, 1, 0.05);
    // 텍스처 없는 메시 → 회색 음영
    SectionLine ln{{1, 0}, {9, 0}, 0.0, 0.01};
    SectionFrame f(ln);
    RasterRequest rq; rq.s0 = 0; rq.s1 = f.L; rq.z0 = 4; rq.z1 = 6; rq.res = 0.005; rq.dNear = 0; rq.dFar = 0.01;
    ElevationImage im;
    REQUIRE(renderElevation({m}, f, rq, im));
    int bad = 0;
    for (int c = 5; c < im.img.w - 5; c += 7) {
        double s = im.s0 + (c + 0.5) * im.res;
        Vec2 xy = f.planXY(s);
        double zt = synth::height(xy.x, xy.y);
        int top = -1;
        for (int r = 0; r < im.img.h; ++r) if (im.img.px[(size_t(r) * im.img.w + c) * 4 + 3]) { top = r; break; }
        double ztop = im.z1 - top * im.res;
        if (std::fabs(ztop - zt) > 0.02) ++bad;  // 두께 1 cm 안의 경사 + 1 px
    }
    CHECK(bad == 0);
}

namespace {
MeshPtr faceAt(double y, double x0, double x1, double z0, double z1, uint8_t r, uint8_t g, uint8_t b) {
    auto m = std::make_shared<Mesh>();
    m->pos = {float(x0), float(y), float(z0), float(x1), float(y), float(z0), float(x1), float(y), float(z1), float(x0), float(y), float(z1)};
    m->uv = {0, 0, 1, 0, 1, 1, 0, 1};
    m->idx = {0, 1, 2, 0, 2, 3};
    m->texture = std::make_shared<Texture>();
    m->texture->rgba.w = 1; m->texture->rgba.h = 1; m->texture->rgba.px = {r, g, b, 255};
    m->computeBBox();
    return m;
}
// 울퉁불퉁한 비탈(삼각형 많음): 보는 방향(+y)으로 0~5 m, 높이가 사인파
MeshPtr bumpySlope(int n) {
    auto m = std::make_shared<Mesh>();
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            double x = 10.0 * i / n, y = 5.0 * j / n;
            m->pos.insert(m->pos.end(), {float(x), float(y), float(0.4 * y + 0.3 * std::sin(x * 3) * std::cos(y * 2))});
        }
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            uint32_t a = uint32_t(j * (n + 1) + i), b = a + 1, c = a + uint32_t(n + 1), d = c + 1;
            m->idx.insert(m->idx.end(), {a, b, d, a, d, c});
        }
    m->computeBBox();
    return m;
}
}  // namespace

TEST_CASE("입면 영상: 행 나눔 병렬 결과 = 단일 스레드 결과(픽셀 단위 동일), 5 m 깊이") {
    std::vector<MeshPtr> ms{bumpySlope(160)};  // 51,200 삼각형 → 병렬 경로
    SectionFrame f(SectionLine{{0, -0.01}, {10, -0.01}});
    RasterRequest rq; rq.s0 = 0; rq.s1 = 10; rq.z0 = -0.5; rq.z1 = 2.5; rq.res = 0.01; rq.dNear = 0; rq.dFar = 5.0;
    ElevationImage a, b;
    rq.threads = 1; REQUIRE(renderElevation(ms, f, rq, a));
    rq.threads = 0; REQUIRE(renderElevation(ms, f, rq, b));
    REQUIRE(a.img.w == b.img.w); REQUIRE(a.img.h == b.img.h);
    CHECK(a.img.px == b.img.px);
    CHECK(a.trianglesDrawn == b.trianglesDrawn);
    CHECK(a.trianglesDrawn > 40000);
    size_t filled = 0; for (size_t i = 3; i < b.img.px.size(); i += 4) filled += b.img.px[i] == 255;
    CHECK(filled > size_t(b.img.w) * 50);
}

TEST_CASE("입면 영상: 깊이 음영 — 먼 면일수록 흰색 쪽으로, 단면 평면 바로 뒤는 거의 그대로") {
    // 빨강 면 y=0.05(가까움, 위쪽 z 1~2), 빨강 면 y=4.5(멂, 아래쪽 z 0~1)
    std::vector<MeshPtr> ms{faceAt(0.05, 0, 4, 1, 2, 200, 0, 0), faceAt(4.5, 0, 4, 0, 1, 200, 0, 0)};
    SectionFrame f(SectionLine{{0, 0}, {4, 0}});
    RasterRequest rq; rq.s0 = 0; rq.s1 = 4; rq.z0 = 0; rq.z1 = 2; rq.res = 0.02; rq.dNear = 0; rq.dFar = 5.0;
    ElevationImage plain, faded;
    REQUIRE(renderElevation(ms, f, rq, plain));
    rq.depthFade = 0.6;
    REQUIRE(renderElevation(ms, f, rq, faded));
    auto px = [](const ElevationImage& im, double s, double z) { int c = int((s - im.s0) / im.res), r = int((im.z1 - z) / im.res); return &im.img.px[(size_t(r) * im.img.w + c) * 4]; };
    CHECK(int(px(plain, 2, 1.5)[1]) == 0);
    CHECK(int(px(faded, 2, 1.5)[1]) <= 3);                       // 가까운 면: 0.05/5*0.6 → 거의 그대로
    CHECK(int(px(faded, 2, 0.5)[1]) == Approx(255 * 0.6 * 0.9).margin(3));  // 먼 면: 4.5/5*0.6 = 54% 흰색 쪽
    CHECK(int(px(faded, 2, 0.5)[3]) == 255);                     // 불투명 유지(DXF·PNG 에서 같은 모습)
}
