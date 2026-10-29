// 시점 의존 LOD 스트리밍(LodStreamer): 거친 것 먼저, 확대하면 세부, 화면 밖은 안 읽음, GPU 예산 LRU
#include "catch_amalgamated.hpp"
#include "asec/stream.hpp"
#include "common.hpp"
#include "synth.hpp"
#include <set>

using namespace asec;

namespace {
struct Ortho {  // 위에서 본 정사 화면: 로컬 XY 사각형 + m/px
    double x0, y0, x1, y1, mpp;
    LodStreamer::View view() const {
        LodStreamer::View v;
        v.visible = [=](const Box3& b) { return !(b.mx.x < x0 || b.mn.x > x1 || b.mx.y < y0 || b.mn.y > y1); };
        v.screenDiameter = [=](const Box3& b) { return b.diag() / mpp; };
        return v;
    }
};

std::unique_ptr<LodStreamer> makeStreamer(size_t gpuBudget = size_t(1) << 30, std::atomic<int>* prepared = nullptr) {
    LodStreamer::Config c; c.threads = 0; c.gpuBudgetBytes = gpuBudget;
    return std::make_unique<LodStreamer>(c, [prepared](const TmxNode& n, size_t* bytes) -> LodStreamer::Payload {
        size_t tris = 0;
        for (auto& m : n.meshes) tris += m->triangleCount();
        *bytes = 1000;  // 노드당 고정 크기(예산 시험을 단순하게)
        if (prepared) ++*prepared;
        return std::make_shared<size_t>(tris);
    });
}

/// 끝날 때까지 돌림: 매 프레임 올릴 것을 받고 대기열 하나 처리
LodStreamer::Frame converge(LodStreamer& s, const Ortho& o, int* frames = nullptr) {
    LodStreamer::Frame f;
    int i = 0;
    for (; i < 500; ++i) {
        f = s.update(o.view());
        for (auto k : f.upload) REQUIRE(s.take(k));
        if (f.idle()) break;
        s.processOne();
    }
    if (frames) *frames = i;
    return f;
}

std::set<int> depths(LodStreamer& s, const std::vector<LodStreamer::Key>& ks) {
    std::set<int> d;
    for (auto k : ks) d.insert(s.nodeDepth(k));
    return d;
}

fs::path synthScene(const char* name) {
    auto dir = tmpDir(name);
    synth::Params p; p.dir = dir; p.leafSpacing = 0.2; p.texSize = 16;
    fs::path f; std::string err;
    REQUIRE(synth::write(p, &f, &err));
    TmxScene sc; REQUIRE(readTmxScene(f, sc, &err));
    return sc.rootFile;
}
}  // namespace

TEST_CASE("LodStreamer: 멀리서 보면 거친 루트만, 확대하면 잎(L2)까지 내려간다") {
    auto root = synthScene("stream1");
    auto s = makeStreamer();
    s->setRoots({root});
    // 모델 16x12 m, 대각선 ~20 m. mpp=1 → 화면 지름 ~20 px < 200 → 루트만
    Ortho far{-10, -10, 30, 30, 1.0};
    auto f = converge(*s, far);
    REQUIRE(f.draw.size() == 1);
    CHECK(depths(*s, f.draw) == std::set<int>{0});
    CHECK(s->stats().tilesLoaded == 1);  // 자식 타일은 읽지 않음

    // 확대: mpp=0.005 → L2 지름(~5m)/0.005 = 1000 px > 800? 잎은 항상 그 자체로 그림 → L2
    Ortho near{1, 1, 3, 3, 0.005};
    f = converge(*s, near);
    REQUIRE(!f.draw.empty());
    CHECK(depths(*s, f.draw) == std::set<int>{2});
    CHECK(f.maxDepthDrawn == 2);
}

TEST_CASE("LodStreamer: 열자마자 거친 모델이 먼저 그려지고(부모 대체), 그 뒤 세부로 바뀐다") {
    auto root = synthScene("stream2");
    auto s = makeStreamer();
    s->setRoots({root});
    Ortho near{0, 0, 16, 12, 0.004};  // 처음부터 세부가 필요한 시점
    std::vector<std::set<int>> history;
    for (int i = 0; i < 100; ++i) {
        auto f = s->update(near.view());
        for (auto k : f.upload) REQUIRE(s->take(k));
        if (!f.draw.empty()) history.push_back(depths(*s, f.draw));
        if (f.idle()) break;
        s->processOne();
    }
    REQUIRE(!history.empty());
    CHECK(history.front() == std::set<int>{0});  // 첫 그림은 루트(거친 것)
    CHECK(history.back() == std::set<int>{2});   // 마지막은 잎
    // 중간에 아무것도 안 그린 프레임이 생기지 않음(구멍 없음): 첫 그림 이후 모든 프레임에 그릴 것이 있음
    for (auto& h : history) CHECK(!h.empty());
}

TEST_CASE("LodStreamer: 화면 밖(절두체 밖) 타일은 읽지 않는다") {
    auto root = synthScene("stream3");
    auto s = makeStreamer();
    s->setRoots({root});
    Ortho corner{0.2, 0.2, 2.0, 2.0, 0.003};  // 왼쪽 아래 귀퉁이만
    auto f = converge(*s, corner);
    CHECK(depths(*s, f.draw) == std::set<int>{2});
    // Root + L1 + L2_0(귀퉁이를 덮는 L1 노드의 자식 타일) = 3 타일만. 전체는 6
    CHECK(s->stats().tilesLoaded == 3);
    for (auto k : f.draw) {
        Box3 b; REQUIRE(s->nodeBox(k, b));
        CHECK(b.mn.x < 2.0); CHECK(b.mn.y < 2.0);
    }
}

TEST_CASE("LodStreamer: GPU 예산을 넘으면 안 보이는 노드부터 LRU 로 뺀다") {
    auto root = synthScene("stream4");
    // 노드당 1000 바이트 — 예산 6 노드
    auto s = makeStreamer(6000);
    s->setRoots({root});
    std::set<LodStreamer::Key> resident;
    auto run = [&](const Ortho& o) {
        LodStreamer::Frame f;
        for (int i = 0; i < 500; ++i) {
            f = s->update(o.view());
            for (auto k : f.evict) { CHECK(resident.erase(k) == 1); }
            for (auto k : f.upload) { REQUIRE(s->take(k)); resident.insert(k); }
            if (f.idle()) break;
            s->processOne();
        }
        return f;
    };
    auto f1 = run({0.2, 0.2, 3.0, 3.0, 0.003});   // 왼쪽 아래
    auto f2 = run({13.0, 9.0, 15.8, 11.8, 0.003}); // 오른쪽 위로 이동
    CHECK(depths(*s, f2.draw) == std::set<int>{2});
    CHECK(s->stats().evictions > 0);
    CHECK(f2.residentBytes <= 6000 + 1000 * f2.draw.size());
    // 이번 프레임에 그리는 노드는 절대 빠지지 않는다
    for (auto k : f2.draw) CHECK(resident.count(k) == 1);
    // 처음 시점으로 돌아가면 빠졌던 노드를 다시 읽어 그린다
    auto f3 = run({0.2, 0.2, 3.0, 3.0, 0.003});
    CHECK(depths(*s, f3.draw) == std::set<int>{2});
    CHECK(f3.draw.size() == f1.draw.size());
}

TEST_CASE("LodStreamer: 자식 파일이 없으면 부모를 잎으로 그리고 계속 요청하지 않는다") {
    auto root = synthScene("stream5");
    fs::remove(root.parent_path() / "L1" / "L2_0.3mxb");
    auto s = makeStreamer();
    s->setRoots({root});
    int frames = 0;
    auto f = converge(*s, {0, 0, 16, 12, 0.003}, &frames);
    CHECK(f.idle());
    CHECK(frames < 200);
    CHECK(s->stats().tileFailures == 1);
    auto d = depths(*s, f.draw);
    CHECK(d.count(1) == 1);  // L2_0 이 없는 사분면은 L1 노드
    CHECK(d.count(2) == 1);
}

TEST_CASE("LodStreamer: 작업 스레드로 실제 비동기 로드(여러 루트)") {
    auto r1 = synthScene("stream6a");
    auto r2 = synthScene("stream6b");
    LodStreamer::Config c; c.threads = 3;
    std::atomic<int> ready{0};
    LodStreamer s(c, [](const TmxNode& n, size_t* b) -> LodStreamer::Payload { *b = 100; return std::make_shared<int>(int(n.meshes.size())); });
    s.onReady = [&] { ++ready; };
    s.setRoots({r1, r2});
    Ortho o{0, 0, 16, 12, 0.003};
    LodStreamer::Frame f;
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(20)) {
        f = s.update(o.view());
        for (auto k : f.upload) s.take(k);
        if (f.idle()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(f.idle());
    CHECK(ready.load() > 0);
    CHECK(f.draw.size() == 32);  // 루트 둘 × L2 16
    CHECK(depths(s, f.draw) == std::set<int>{2});
}

TEST_CASE("LodStreamer: peek 은 상태를 바꾸지 않아 여러 프레임 나눠 올리는 동안 부모가 그려진다(구멍 없음)") {
    auto root = synthScene("stream_peek");
    auto s = makeStreamer();
    s->setRoots({root});
    Ortho near{0, 0, 16, 12, 0.004};
    // 루트가 상주할 때까지
    for (int i = 0; i < 50; ++i) {
        auto f = s->update(near.view());
        for (auto k : f.upload) REQUIRE(s->take(k));
        if (!f.draw.empty()) break;
        s->processOne();
    }
    // 자식이 준비되면 peek 만 하고(나눠 올리는 중) take 는 미룸 → 계속 부모가 그려짐(빈 프레임 없음)
    std::set<LodStreamer::Key> peeked;
    for (int i = 0; i < 200; ++i) {
        auto f = s->update(near.view());
        REQUIRE(!f.draw.empty());
        for (auto k : f.upload) { auto p = s->peek(k); REQUIRE(p); CHECK(p.use_count() >= 2); peeked.insert(k); }
        CHECK(depths(*s, f.draw).count(2) == 0);   // 잎은 아직 상주 확정 전이라 그리지 않음(부모가 덮음)
        if (f.queued == 0 && f.loading == 0 && !s->processOne()) break;
        s->processOne();
    }
    REQUIRE(!peeked.empty());
    // 이제 take(확정) → 다음 프레임부터 세부가 그려짐
    for (int i = 0; i < 200; ++i) {
        auto f = s->update(near.view());
        for (auto k : f.upload) REQUIRE(s->take(k));
        if (f.idle()) { CHECK(depths(*s, f.draw) == std::set<int>{2}); break; }
        s->processOne();
    }
    CHECK(s->peek(*peeked.begin()) == nullptr);  // 상주한 노드는 peek 불가
}
