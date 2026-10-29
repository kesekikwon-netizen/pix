// TileCache 병렬 디코드: 전역 잠금 없이 서로 다른 타일을 동시에 디코드하고, 같은 노드는 한 번만 완성된 상태로 보인다.
#include "catch_amalgamated.hpp"
#include "asec/lod.hpp"
#include "asec/section.hpp"
#include "common.hpp"
#include "synth.hpp"
#include <atomic>
#include <chrono>
#include <thread>

using namespace asec;

namespace {
std::vector<fs::path> allTiles(const fs::path& root) {
    std::vector<fs::path> out{root}, todo{root};
    while (!todo.empty()) {
        TmxTile t; std::string e; REQUIRE(readTmxTile(todo.back(), t, &e)); todo.pop_back();
        for (auto& n : t.nodes) for (auto& c : n.children) { out.push_back(c); todo.push_back(c); }
    }
    return out;
}
}  // namespace

TEST_CASE("TileCache: 서로 다른 타일의 텍스처 디코드가 동시에 진행된다(전역 잠금 없음)") {
    auto dir = tmpDir("cachepar");
    synth::Params p; p.dir = dir; p.leafSpacing = 0.2; p.texSize = 32;
    fs::path f; std::string err;
    REQUIRE(synth::write(p, &f, &err));
    TmxScene sc; REQUIRE(readTmxScene(f, sc, &err));
    auto tiles = allTiles(sc.rootFile);
    REQUIRE(tiles.size() == 6);

    TileCache cache;
    std::atomic<int> now{0}, peak{0}, calls{0};
    cache.textureDecoder = [&](Texture& t) {
        int c = ++now;
        int pk = peak.load();
        while (c > pk && !peak.compare_exchange_weak(pk, c)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        decodeTexStb(t);
        ++calls; --now;
    };
    std::vector<std::thread> th;
    std::atomic<bool> ok{true};
    for (size_t k = 0; k < 4; ++k)
        th.emplace_back([&, k] {
            // 각 스레드는 서로 다른 L2 타일(인덱스 2..5)을 디코드
            auto t = cache.get(tiles[2 + k], nullptr);
            if (!t) { ok = false; return; }
            for (size_t i = 0; i < t->nodes.size(); ++i) if (!cache.decode(t, i, nullptr)) ok = false;
        });
    for (auto& t : th) t.join();
    REQUIRE(ok);
    CHECK(peak.load() >= 2);  // 전역 잠금이면 1
    CHECK(calls.load() == 16);
}

TEST_CASE("TileCache: 같은 타일을 여러 스레드가 동시에 디코드해도 결과가 완전하다") {
    auto dir = tmpDir("cachepar2");
    synth::Params p; p.dir = dir; p.leafSpacing = 0.2; p.texSize = 32;
    fs::path f; std::string err;
    REQUIRE(synth::write(p, &f, &err));
    TmxScene sc; REQUIRE(readTmxScene(f, sc, &err));
    auto tiles = allTiles(sc.rootFile);
    TileCache cache;
    cache.textureDecoder = decodeTexStb;
    std::atomic<bool> ok{true};
    std::vector<std::thread> th;
    for (int k = 0; k < 8; ++k)
        th.emplace_back([&] {
            for (auto& tp : tiles) {
                auto t = cache.get(tp, nullptr);
                if (!t) { ok = false; return; }
                for (size_t i = 0; i < t->nodes.size(); ++i) {
                    if (!cache.decode(t, i, nullptr)) { ok = false; return; }
                    // decode 가 돌려준 뒤에는 메시·텍스처가 모두 준비되어 있어야 함
                    for (auto& m : t->nodes[i].meshes) if (!m->texture || m->texture->rgba.empty()) ok = false;
                    if (t->nodes[i].meshes.empty()) ok = false;
                }
            }
        });
    for (auto& t : th) t.join();
    CHECK(ok);
    CHECK(cache.loadedBytes() > 0);
}

TEST_CASE("collectLeafMeshes: 잎 노드를 병렬로 디코드하고 결과 순서·내용은 순차와 같다") {
    auto dir = tmpDir("cachepar3");
    synth::Params p; p.dir = dir; p.leafSpacing = 0.2; p.texSize = 32;
    fs::path f; std::string err;
    REQUIRE(synth::write(p, &f, &err));
    TmxScene sc; REQUIRE(readTmxScene(f, sc, &err));
    BandQuad band;  // 모델 전체를 덮는 띠
    band.p[0] = {-100, -100}; band.p[1] = {100, -100}; band.p[2] = {100, 100}; band.p[3] = {-100, 100};

    TileCache ref; ref.textureDecoder = decodeTexStb;
    std::vector<MeshPtr> a; LeafStats sa;
    REQUIRE(collectLeafMeshes(ref, sc.rootFile, band, a, &sa, &err));

    TileCache cache;
    std::atomic<int> now{0}, peak{0};
    cache.textureDecoder = [&](Texture& t) {
        int c = ++now; int pk = peak.load();
        while (c > pk && !peak.compare_exchange_weak(pk, c)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        decodeTexStb(t); --now;
    };
    std::vector<MeshPtr> b; LeafStats sb;
    REQUIRE(collectLeafMeshes(cache, sc.rootFile, band, b, &sb, &err));
    REQUIRE(a.size() == b.size());
    CHECK(sa.leafNodes == 16);
    CHECK(sb.triangles == sa.triangles);
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i]->pos == b[i]->pos);
        CHECK(!b[i]->texture->rgba.empty());
    }
    if (std::thread::hardware_concurrency() >= 2) CHECK(peak.load() >= 2);
}
