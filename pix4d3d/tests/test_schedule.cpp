// 단면 계산 예약: 끌기 중 미리보기 합치기, 최종 우선 취소
#include "catch_amalgamated.hpp"
#include "asec/schedule.hpp"
#include <chrono>
#include <vector>

using namespace asec;
using namespace std::chrono_literals;

namespace {
struct Log {
    std::mutex mu;
    std::vector<std::pair<uint64_t, bool>> done;   // (gen, 취소됨)
    void add(uint64_t g, bool c) { std::lock_guard<std::mutex> lk(mu); done.push_back({g, c}); }
};
// 취소를 확인하며 ms 동안 일하는 작업
CoalescingWorker::Job work(Log& log, int ms, std::atomic<int>* started = nullptr) {
    return [&log, ms, started](uint64_t g, bool, const std::atomic<bool>* cancel) {
        if (started) ++*started;
        auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(ms)) {
            if (cancel->load()) { log.add(g, true); return; }
            std::this_thread::sleep_for(1ms);
        }
        log.add(g, false);
    };
}
}  // namespace

TEST_CASE("CoalescingWorker: 끄는 동안의 미리보기는 실행 중인 것을 취소하지 않고 마지막 것만 이어서 계산") {
    CoalescingWorker w;
    Log log;
    std::atomic<int> started{0};
    w.submit(false, work(log, 600, &started));
    while (started.load() == 0) std::this_thread::sleep_for(1ms);
    uint64_t last = 0;
    for (int i = 0; i < 50; ++i) { last = w.submit(false, work(log, 10)); std::this_thread::sleep_for(1ms); }
    w.waitIdle();
    REQUIRE(log.done.size() == 2);       // 첫 미리보기 + 마지막 미리보기
    CHECK_FALSE(log.done[0].second);     // 첫 것은 끝까지 계산됨(취소 안 됨)
    CHECK(log.done[1].first == last);    // 중간 49개는 합쳐져 사라짐
    CHECK(w.stats().coalesced == 49);
}

TEST_CASE("CoalescingWorker: 최종 요청은 실행 중인 미리보기를 취소하고 바로 계산") {
    CoalescingWorker w;
    Log log;
    std::atomic<int> started{0};
    uint64_t g1 = w.submit(false, work(log, 2000, &started));
    while (started.load() == 0) std::this_thread::sleep_for(1ms);
    auto t0 = std::chrono::steady_clock::now();
    uint64_t g2 = w.submit(true, work(log, 5));
    w.waitIdle();
    CHECK(std::chrono::steady_clock::now() - t0 < 1000ms);
    REQUIRE(log.done.size() == 2);
    CHECK(log.done[0] == std::make_pair(g1, true));
    CHECK(log.done[1] == std::make_pair(g2, false));
}

TEST_CASE("CoalescingWorker: 최종 계산 중에 선이 다시 움직이면(새 미리보기) 낡은 최종을 취소") {
    CoalescingWorker w;
    Log log;
    std::atomic<int> started{0};
    uint64_t g1 = w.submit(true, work(log, 2000, &started));
    while (started.load() == 0) std::this_thread::sleep_for(1ms);
    uint64_t g2 = w.submit(false, work(log, 5));
    w.waitIdle();
    REQUIRE(log.done.size() == 2);
    CHECK(log.done[0] == std::make_pair(g1, true));
    CHECK(log.done[1] == std::make_pair(g2, false));
}

TEST_CASE("CoalescingWorker: cancelAll 은 대기 요청을 버리고 실행 중인 것을 취소") {
    CoalescingWorker w;
    Log log;
    std::atomic<int> started{0};
    w.submit(false, work(log, 2000, &started));
    while (started.load() == 0) std::this_thread::sleep_for(1ms);
    w.submit(false, work(log, 5));
    uint64_t g = w.cancelAll();
    w.waitIdle();
    REQUIRE(log.done.size() == 1);
    CHECK(log.done[0].second);
    CHECK(g > 2);
}

TEST_CASE("ResultGate: 이미 보여준 것보다 새 세대만 받는다") {
    ResultGate gate;
    CHECK(gate.accept(3));
    CHECK_FALSE(gate.accept(2));   // 늦게 도착한 오래된 미리보기
    CHECK_FALSE(gate.accept(3));
    CHECK(gate.accept(7));
}

#include "asec/engine.hpp"
#include "common.hpp"
#include "synth.hpp"

TEST_CASE("computeSection: 미리보기(meshRes>0)는 거친 LOD, 최종은 잎 — 같은 선에서 단면 위치가 비슷") {
    auto dir = tmpDir("preview_lod");
    synth::Params p; p.dir = dir; p.leafSpacing = 0.1; p.texSize = 16;
    fs::path f; std::string err;
    REQUIRE(synth::write(p, &f, &err));
    TmxSource src; REQUIRE(src.open(f, &err));
    SectionRequest rq;
    rq.line.a = {1.0, 6.1}; rq.line.b = {15.0, 6.1}; rq.line.front = 0; rq.line.back = 0.5;
    rq.wantImage = false;
    SectionOutput fin; REQUIRE(computeSection(src, rq, fin, &err));
    CHECK_FALSE(fin.previewLod);
    CHECK(fin.stats.maxDepth == 2);

    rq.meshRes = 0.05;  // L1(대각선 ~10 m / 0.05 = 200 px ≤ 400) 이면 충분
    SectionOutput pre; REQUIRE(computeSection(src, rq, pre, &err));
    CHECK(pre.previewLod);
    CHECK(pre.stats.maxDepth == 1);
    CHECK(pre.stats.triangles * 4 < fin.stats.triangles);
    // 단면 Z 범위는 거의 같다(거친 격자 보간 차이만)
    CHECK(std::fabs(pre.result.zMin - fin.result.zMin) <= 0.31);
    CHECK(std::fabs(pre.result.zMax - fin.result.zMax) <= 0.31);

    rq.meshRes = 1e-4;  // 아주 세밀 → 잎과 같음
    SectionOutput fine; REQUIRE(computeSection(src, rq, fine, &err));
    CHECK(fine.stats.triangles == fin.stats.triangles);
}

TEST_CASE("끌기 흉내: 미리보기 30개 + 최종 1개 → 화면에 남는 결과는 최종(잎)이고 직접 계산과 같다") {
    auto dir = tmpDir("drag_sim");
    synth::Params p; p.dir = dir; p.leafSpacing = 0.1; p.texSize = 16;
    fs::path f; std::string err;
    REQUIRE(synth::write(p, &f, &err));
    TmxSource src; REQUIRE(src.open(f, &err));
    CoalescingWorker w;
    ResultGate gate;
    std::mutex mu;
    SectionOutput shown; bool shownFinal = false; int accepted = 0;
    auto submit = [&](double y, bool fin) {
        SectionRequest rq;
        rq.line.a = {1.0, y}; rq.line.b = {15.0, y}; rq.line.back = 0.5; rq.wantImage = false;
        rq.meshRes = fin ? 0.0 : 0.05;
        w.submit(fin, [&, rq](uint64_t g, bool fn, const std::atomic<bool>* c) {
            SectionOutput o; std::string e;
            if (!computeSection(src, rq, o, &e, c) || c->load()) return;
            std::lock_guard<std::mutex> lk(mu);
            if (gate.accept(g)) { shown = std::move(o); shownFinal = fn; ++accepted; }
        });
    };
    for (int i = 0; i < 30; ++i) submit(3.0 + i * 0.1, false);
    submit(6.1, true);
    w.waitIdle();
    REQUIRE(shownFinal);
    CHECK_FALSE(shown.previewLod);
    CHECK(accepted >= 1);
    auto stt = w.stats();
    CHECK(stt.submitted == 31);
    CHECK(stt.started + stt.coalesced == 31);   // 시작 안 된 것은 모두 합쳐짐(사라진 요청 없음)
    SectionRequest rq; rq.line.a = {1.0, 6.1}; rq.line.b = {15.0, 6.1}; rq.line.back = 0.5; rq.wantImage = false;
    SectionOutput direct; REQUIRE(computeSection(src, rq, direct, &err));
    CHECK(direct.stats.triangles == shown.stats.triangles);
    CHECK(direct.result.zMin == shown.result.zMin);
    CHECK(direct.result.zMax == shown.result.zMax);
}
