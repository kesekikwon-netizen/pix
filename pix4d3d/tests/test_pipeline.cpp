#include "catch_amalgamated.hpp"
#include "asec/engine.hpp"
#include "common.hpp"
#include "synth.hpp"

using namespace asec;
using Catch::Approx;

TEST_CASE("합성 3MX 전 과정: 잎 타일, 한 줄, 해석해 대비 오차, 영상") {
    auto dir = tmpDir("pipe");
    synth::Params p; p.dir = dir;  // 잎 4 cm
    fs::path f; std::string err;
    REQUIRE(synth::write(p, &f, &err));
    TmxSource src;
    src.cache->textureDecoder = decodeTexStb;
    REQUIRE(src.open(f, &err));
    SectionRequest rq;
    // 실좌표로 준 선(수혈 중심 관통, 타일 경계 x=4,8,12 / y=6 근처 통과)
    Vec3 o = src.srs.origin;
    rq.line = SectionLine{{200002.0 - o.x, 450005.2 - o.y}, {200014.5 - o.x, 450006.9 - o.y}, 0.0, 0.3};
    SectionOutput so;
    REQUIRE(computeSection(src, rq, so, &err));
    CHECK(so.stats.fallbackNodes == 0);
    CHECK(so.stats.maxDepth == 2);
    REQUIRE(so.result.profile.size() == 1);  // 타일 경계에서도 끊기지 않음
    CHECK(countDuplicateVertices(so.result.profile, rq.cleanup.weldTol) == 0);
    SectionFrame fr(rq.line);
    // 최고 해상도(4 cm) 격자의 선형 보간과 일치(MG2 양자화 0.5 mm 이내) → 거친 LOD(20·80 cm)가 아님
    double maxErr = 0, maxL1 = 0;
    for (auto& q : so.result.profile[0]) {
        Vec2 xy = fr.planXY(q.x);
        maxErr = std::max(maxErr, std::fabs(q.y - synth::gridZ(xy.x, xy.y, 0.04)));
        maxL1 = std::max(maxL1, std::fabs(q.y - synth::gridZ(xy.x, xy.y, 0.20)));
    }
    CHECK(maxErr < 0.0015);
    CHECK(maxL1 > 0.05);  // 거친 단계였다면 이만큼 달랐을 것
    // 시작·끝이 A, A'
    CHECK(so.result.profile[0].front().x == Approx(0).margin(1e-6));
    CHECK(so.result.profile[0].back().x == Approx(fr.L).margin(1e-6));
    // 수혈 바닥 절대표고 ~ 45.x - 0.6
    double zmin = 1e9; for (auto& q : so.result.profile[0]) zmin = std::min(zmin, q.y + o.z);
    CHECK(zmin < 44.9);
    REQUIRE_FALSE(so.image.img.empty());
    CHECK(so.image.trianglesDrawn > 1000);
    // 영상 범위 = 레벨 0.1 m 단위로 맞춘 z
    CHECK(std::fabs(so.result.zMin * 10 - std::round(so.result.zMin * 10)) < 1e-9);
    // 영상은 실제 텍스처 색(수혈 내부 암갈색)을 담는다
    int dark = 0;
    for (size_t i = 0; i < so.image.img.px.size(); i += 4) if (so.image.img.px[i + 3] && so.image.img.px[i] < 130 && so.image.img.px[i] > 80) ++dark;
    CHECK(dark > 100);
}

TEST_CASE("덤불(떠 있는 닫힌 메시) 단면 → 닫힌 빨강 고리 + 지면선 따로") {
    auto dir = tmpDir("bush");
    synth::Params p; p.dir = dir; p.bushes = true; p.leafSpacing = 0.08;
    fs::path f; std::string err;
    REQUIRE(synth::write(p, &f, &err));
    TmxSource src;
    REQUIRE(src.open(f, &err));
    SectionRequest rq;
    rq.line = SectionLine{{9.0, 3.2}, {15.5, 3.2}, 0, 0.3};  // 덤불 중심(13.0, 3.2) 통과
    rq.wantImage = false;
    SectionOutput so;
    REQUIRE(computeSection(src, rq, so, &err));
    int closed = 0, open = 0;
    for (auto& pl : so.result.profile) {
        bool c = pl.size() > 3 && (pl.front() - pl.back()).len() < 1e-12;
        if (c) {
            ++closed;
            double smin = 1e9, smax = -1e9;
            for (auto& q : pl) { smin = std::min(smin, q.x); smax = std::max(smax, q.x); }
            CHECK(smin > 2.8); CHECK(smax < 5.3);  // 13±~1.1 → s = 4 ± 1.1
        } else ++open;
    }
    CHECK(closed == 1);
    CHECK(open == 1);
}
