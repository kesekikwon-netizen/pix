// 평면 보기용 시점 의존 LOD 스트리밍(Qt 없음).
// 3MX 규칙: 노드 상자의 화면 지름(px) > maxScreenDiameter 이면 자식으로 세분, 아니면 그 노드를 그린다.
// - 타일(.3mxb)을 작업 스레드가 읽고 디코드 → 앱의 prepare(작업 스레드)가 GPU 용 데이터를 만든다 → 원본 메시는 버림
// - update()(GUI 스레드)는 매 프레임 그릴 노드, 올릴 노드, GPU 에서 뺄 노드(예산 초과 LRU)를 돌려준다
// - 자식이 모두 준비될 때까지 부모를 그린다(구멍 없음) → 열자마자 거친 모델이 먼저 보이고 확대하면 세부가 채워짐
// - 화면 밖(절두체 밖) 노드는 읽지 않는다
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "asec/tmx.hpp"

namespace asec {

class LodStreamer {
public:
    using Key = uint64_t;                    // (타일 번호 << 20) | 노드 번호
    using Payload = std::shared_ptr<void>;   // 앱이 만든 GPU 업로드용 데이터(불투명)

    struct Config {
        size_t gpuBudgetBytes = size_t(768) << 20;  // 상주(GPU) 예산 — 넘으면 이번 프레임에 안 그린 노드부터 LRU 로 뺌
        size_t cpuBudgetBytes = size_t(256) << 20;  // 준비됐지만 아직 안 올린 데이터 예산
        int threads = 3;                            // 0 이면 작업 스레드 없음(processOne() 으로 직접 처리 — 시험용)
    };
    /// 작업 스레드에서 호출: 노드 메시(텍스처는 encoded 상태) → 업로드용 데이터, bytes 에 크기(대략 GPU 메모리)
    using PrepareFn = std::function<Payload(const TmxNode& node, size_t* bytes)>;

    struct View {
        std::function<bool(const Box3&)> visible;          // 절두체 안(일부라도)
        std::function<double(const Box3&)> screenDiameter; // 상자의 화면 지름(px)
    };
    struct Frame {
        std::vector<Key> draw;     // 이번 프레임에 그릴 상주 노드
        std::vector<Key> upload;   // 준비됨 → take() 로 받아 GPU 에 올릴 것(우선순위 순)
        std::vector<Key> evict;    // GPU 에서 지울 것(이미 상주 해제됨)
        size_t wanted = 0;         // 필요하지만 아직 상주하지 않은 노드 수(0 이면 이 시점에서 완료)
        size_t queued = 0, loading = 0;
        size_t residentBytes = 0, readyBytes = 0;
        int maxDepthDrawn = -1;
        bool idle() const { return wanted == 0 && upload.empty() && queued == 0 && loading == 0; }
    };
    struct Stats { size_t tilesLoaded = 0, tileLoads = 0, tileFailures = 0, nodesPrepared = 0, evictions = 0; };

    LodStreamer(Config cfg, PrepareFn prepare);
    ~LodStreamer();
    LodStreamer(const LodStreamer&) = delete;
    LodStreamer& operator=(const LodStreamer&) = delete;

    /// 루트 타일 경로(여러 개 가능: 병합 3MX 의 여러 layer). 읽기는 작업 스레드가 한다.
    void setRoots(const std::vector<fs::path>& roots);
    /// 매 프레임(GUI 스레드). 순회는 잠금 안에서 빠르게, 파일 읽기는 하지 않음.
    Frame update(const View& v);
    /// upload 목록의 노드 데이터를 넘겨받는다(이후 상주로 간주). 없으면 nullptr
    Payload take(Key k);
    /// upload 목록의 노드 데이터를 상태는 그대로(준비됨) 두고 빌려 본다. 앱이 여러 프레임에 나눠 GPU 에 올린 뒤
    /// 다 올렸을 때 take() 로 상주를 확정하는 데 씀(그동안 부모가 계속 그려져 구멍 없음). 없으면 nullptr
    Payload peek(Key k) const;
    /// 작업 스레드가 새 노드를 준비했을 때(작업 스레드에서 호출됨) — 앱은 다시 그리기를 예약
    std::function<void()> onReady;
    /// threads==0 일 때 대기열 하나 처리(시험용). 처리했으면 true
    bool processOne();
    void stop();
    Stats stats() const;
    /// 노드 상자(로컬). 앱 그리기/디버그용
    bool nodeBox(Key k, Box3& out) const;
    int nodeDepth(Key k) const;

private:
    enum class TS { None, Loaded, Failed };
    enum class NS { None, Ready, Resident };
    struct Node {
        Box3 bb; double msd = 0; bool hasGeom = false;
        std::vector<int> childTiles;
        NS st = NS::None; Payload payload; size_t bytes = 0;
        uint64_t lastDrawn = 0, lastWanted = 0;
    };
    struct Tile {
        fs::path path; int depth = 0;
        TS st = TS::None;
        bool queued = false, loading = false;  // 대기열에 있음 / 작업 스레드가 읽는 중
        std::vector<Node> nodes;
        double prio = 0;
    };
    Config cfg_;
    PrepareFn prepare_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::vector<Tile> tiles_;
    std::map<fs::path, int> tileIndex_;
    std::vector<int> roots_;
    std::vector<int> queue_;  // 우선순위 순 타일 번호
    std::vector<std::thread> workers_;
    bool stop_ = false;
    uint64_t frame_ = 0;
    size_t residentBytes_ = 0, readyBytes_ = 0;
    int loading_ = 0;
    Stats stats_;

    int tileFor(const fs::path& p, int depth);
    void workerLoop();
    bool runJob(std::unique_lock<std::mutex>& lk);
    bool traverse(int ti, size_t ni, const View& v, Frame& f, std::vector<std::pair<double, int>>& want, std::vector<std::pair<double, Key>>& up);
    static Key key(int t, size_t n) { return (Key(t) << 20) | Key(n); }
};

}  // namespace asec
