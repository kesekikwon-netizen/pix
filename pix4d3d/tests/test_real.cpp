// 실제 3MX 로 확인(선택). ASEC_REAL_3MX=<.3mx 경로> 가 없거나 파일이 없으면 건너뜀.
// 예: ASEC_REAL_3MX=/workspace/section-viewer-samples/jeju1_production2/Scene/Production_2.3mx ./asec_tests "[real]"
// ASEC_REAL_EXPECT_LABEL 이 있으면 좌표계 한 줄 표기가 정확히 같아야 함(예: "수평 EPSG:5186 / 높이 타원체고(GRS80)")
#include "catch_amalgamated.hpp"
#include "asec/engine.hpp"
#include "asec/pick.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>

using namespace asec;

TEST_CASE("실제 3MX(선택): 열기·좌표계·피킹·단면", "[real]") {
    const char* p = std::getenv("ASEC_REAL_3MX");
    if (!p || !*p || !std::filesystem::exists(fs::u8path(p))) SKIP("ASEC_REAL_3MX 미설정/없음 — 건너뜀");
    TmxSource src; std::string err;
    auto t0 = std::chrono::steady_clock::now();
    REQUIRE(src.open(fs::u8path(p), &err));
    double openMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(openMs < 2000);
    REQUIRE(!src.scene.layers.empty());
    SrsReport rep = analyzeSrs(src.srs, src.bounds);
    INFO("SRS: " << rep.labelKo);
    CHECK(rep.desc.known());
    CHECK(rep.desc.horizontalEpsg > 0);
    if (const char* want = std::getenv("ASEC_REAL_EXPECT_LABEL")) CHECK(rep.labelKo == std::string(want));
    // 모델 중앙에서 잎 메시 수직 피킹 → 루트 상자 Z 범위 안
    Vec3 c = src.bounds.center();
    PickResult r;
    REQUIRE(pickVertical(src, c.x, c.y, r, &err));
    if (r.hit) {
        CHECK(r.source == ZSource::LeafSurface);
        CHECK(r.world.z >= src.bounds.mn.z + src.srs.origin.z - 0.01);
        CHECK(r.world.z <= src.bounds.mx.z + src.srs.origin.z + 0.01);
    }
    // 중앙을 지나는 동-서 단면(최종 = 잎) 이 비어 있지 않고 10초 안에
    SectionRequest rq;
    rq.line.a = Vec2(src.bounds.mn.x + 0.5, c.y); rq.line.b = Vec2(src.bounds.mx.x - 0.5, c.y); rq.line.front = 0; rq.line.back = 0.5;
    rq.imageRes = 0.01; rq.maxImagePixels = size_t(8) << 20;
    SectionOutput out;
    auto t1 = std::chrono::steady_clock::now();
    REQUIRE(computeSection(src, rq, out, &err));
    double secMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    CHECK(secMs < 10000);
    CHECK(!out.result.profile.empty());
}
