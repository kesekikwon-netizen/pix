#include "asec/stream.hpp"
#include <algorithm>

namespace asec {

LodStreamer::LodStreamer(Config cfg, PrepareFn prepare) : cfg_(cfg), prepare_(std::move(prepare)) {
    for (int i = 0; i < cfg_.threads; ++i) workers_.emplace_back([this] { workerLoop(); });
}

LodStreamer::~LodStreamer() { stop(); }

void LodStreamer::stop() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : workers_) if (t.joinable()) t.join();
    workers_.clear();
}

int LodStreamer::tileFor(const fs::path& p, int depth) {
    auto it = tileIndex_.find(p);
    if (it != tileIndex_.end()) return it->second;
    int i = int(tiles_.size());
    Tile t; t.path = p; t.depth = depth;
    tiles_.push_back(std::move(t));
    tileIndex_[p] = i;
    return i;
}

void LodStreamer::setRoots(const std::vector<fs::path>& roots) {
    std::lock_guard<std::mutex> lk(mu_);
    roots_.clear();
    for (auto& r : roots) roots_.push_back(tileFor(r, 0));
}

LodStreamer::Stats LodStreamer::stats() const {
    std::lock_guard<std::mutex> lk(mu_);
    return stats_;
}

bool LodStreamer::nodeBox(Key k, Box3& out) const {
    std::lock_guard<std::mutex> lk(mu_);
    size_t t = size_t(k >> 20), n = size_t(k & 0xFFFFF);
    if (t >= tiles_.size() || n >= tiles_[t].nodes.size()) return false;
    out = tiles_[t].nodes[n].bb;
    return true;
}

int LodStreamer::nodeDepth(Key k) const {
    std::lock_guard<std::mutex> lk(mu_);
    size_t t = size_t(k >> 20);
    return t < tiles_.size() ? tiles_[t].depth : -1;
}

static double prioOf(int depth, double diam) { return depth - std::min(diam, 1e6) * 1e-7; }  // 얕은 것 먼저, 같은 깊이면 화면에 큰 것 먼저

bool LodStreamer::traverse(int ti, size_t ni, const View& v, Frame& f, std::vector<std::pair<double, int>>& want, std::vector<std::pair<double, Key>>& up) {
    Tile& T = tiles_[size_t(ti)];
    Node& n = T.nodes[ni];
    if (n.bb.valid() && v.visible && !v.visible(n.bb)) return true;  // 절두체 밖: 필요 없음
    double d = v.screenDiameter ? v.screenDiameter(n.bb) : 0.0;
    double pr = prioOf(T.depth, d);
    const Key k = key(ti, ni);

    auto selfCover = [&]() -> bool {
        if (!n.hasGeom) return true;  // 빈 노드: 그릴 것 없음
        n.lastWanted = frame_;
        if (n.st == NS::Resident) { f.draw.push_back(k); n.lastDrawn = frame_; f.maxDepthDrawn = std::max(f.maxDepthDrawn, T.depth); return true; }
        f.wanted++;
        if (n.st == NS::Ready) up.push_back({pr, k});
        else want.push_back({pr, ti});  // 노드 데이터를 버렸음 → 타일 다시 읽기
        return false;
    };

    bool refine = !n.childTiles.empty() && (n.msd <= 0 || d > n.msd);
    if (refine) {
        bool allLoaded = true, failed = false;
        for (int ct : n.childTiles) {
            Tile& C = tiles_[size_t(ct)];
            if (C.st == TS::Loaded) continue;
            if (C.st == TS::Failed) { failed = true; continue; }
            allLoaded = false;
            want.push_back({prioOf(C.depth, d), ct});
        }
        if (failed) return selfCover();  // 자식 파일 없음 → 이 노드가 최고 해상도
        if (!allLoaded) { if (!selfCover()) { f.wanted++; return false; } return true; }
        size_t mark = f.draw.size();
        int depthMark = f.maxDepthDrawn;
        bool all = true;
        for (int ct : n.childTiles)
            for (size_t j = 0; j < tiles_[size_t(ct)].nodes.size(); ++j) all = traverse(ct, j, v, f, want, up) && all;
        if (all) return true;
        // 자식이 덜 준비됨 → 부모가 상주면 부모로 덮는다(겹침 방지로 자식 그리기는 취소)
        Node& self = tiles_[size_t(ti)].nodes[ni];  // (traverse 중 tiles_ 는 바뀌지 않지만 명시)
        if (self.hasGeom && self.st == NS::Resident) {
            f.draw.resize(mark);
            f.maxDepthDrawn = depthMark;
            f.draw.push_back(k); self.lastDrawn = frame_; self.lastWanted = frame_;
            f.maxDepthDrawn = std::max(f.maxDepthDrawn, T.depth);
            return true;
        }
        selfCover();  // 부모도 요청(거친 것이 먼저 보이도록) — 자식 일부는 그대로 그림
        return false;
    }
    return selfCover();
}

LodStreamer::Frame LodStreamer::update(const View& v) {
    Frame f;
    std::vector<std::pair<double, int>> want;
    std::vector<std::pair<double, Key>> up;
    {
        std::lock_guard<std::mutex> lk(mu_);
        ++frame_;
        for (int r : roots_) {
            Tile& R = tiles_[size_t(r)];
            if (R.st == TS::Loaded) {
                for (size_t j = 0; j < R.nodes.size(); ++j) traverse(r, j, v, f, want, up);
            } else if (R.st == TS::None) {
                want.push_back({-1.0, r}); f.wanted++;
            }
        }
        // 대기열 다시 만들기(이번 프레임에 필요 없는 타일은 빠짐)
        for (int q : queue_) tiles_[size_t(q)].queued = false;
        queue_.clear();
        std::sort(want.begin(), want.end());
        for (auto& w : want) {
            Tile& t = tiles_[size_t(w.second)];
            if (t.queued || t.loading || t.st == TS::Failed) continue;
            t.queued = true; t.prio = w.first;
            queue_.push_back(w.second);
        }
        std::stable_sort(up.begin(), up.end(), [](auto& a, auto& b) { return a.first < b.first; });
        for (auto& u : up) f.upload.push_back(u.second);

        // GPU 예산: 이번 프레임에 안 그린 상주 노드부터 오래된 순으로 뺀다
        if (residentBytes_ > cfg_.gpuBudgetBytes || readyBytes_ > cfg_.cpuBudgetBytes) {
            std::vector<std::pair<uint64_t, Key>> cand, rcand;
            for (size_t t = 0; t < tiles_.size(); ++t)
                for (size_t n = 0; n < tiles_[t].nodes.size(); ++n) {
                    auto& N = tiles_[t].nodes[n];
                    if (N.st == NS::Resident && N.lastDrawn < frame_) cand.push_back({N.lastDrawn, key(int(t), n)});
                    if (N.st == NS::Ready && N.lastWanted < frame_) rcand.push_back({N.lastWanted, key(int(t), n)});
                }
            std::sort(cand.begin(), cand.end());
            std::sort(rcand.begin(), rcand.end());
            for (auto& c : cand) {
                if (residentBytes_ <= cfg_.gpuBudgetBytes) break;
                auto& N = tiles_[size_t(c.second >> 20)].nodes[size_t(c.second & 0xFFFFF)];
                residentBytes_ -= N.bytes; N.st = NS::None; N.bytes = 0;
                f.evict.push_back(c.second); stats_.evictions++;
            }
            for (auto& c : rcand) {
                if (readyBytes_ <= cfg_.cpuBudgetBytes) break;
                auto& N = tiles_[size_t(c.second >> 20)].nodes[size_t(c.second & 0xFFFFF)];
                readyBytes_ -= N.bytes; N.st = NS::None; N.bytes = 0; N.payload.reset();
            }
        }
        f.queued = queue_.size();
        f.loading = size_t(loading_);
        f.residentBytes = residentBytes_;
        f.readyBytes = readyBytes_;
    }
    cv_.notify_all();
    return f;
}

LodStreamer::Payload LodStreamer::peek(Key k) const {
    std::lock_guard<std::mutex> lk(mu_);
    size_t t = size_t(k >> 20), n = size_t(k & 0xFFFFF);
    if (t >= tiles_.size() || n >= tiles_[t].nodes.size()) return nullptr;
    auto& N = tiles_[t].nodes[n];
    return N.st == NS::Ready ? N.payload : nullptr;
}

LodStreamer::Payload LodStreamer::take(Key k) {
    std::lock_guard<std::mutex> lk(mu_);
    size_t t = size_t(k >> 20), n = size_t(k & 0xFFFFF);
    if (t >= tiles_.size() || n >= tiles_[t].nodes.size()) return nullptr;
    auto& N = tiles_[t].nodes[n];
    if (N.st != NS::Ready) return nullptr;
    N.st = NS::Resident;
    readyBytes_ -= N.bytes;
    residentBytes_ += N.bytes;
    Payload p = std::move(N.payload);
    N.payload.reset();
    return p;
}

bool LodStreamer::runJob(std::unique_lock<std::mutex>& lk) {
    int ti = -1;
    for (size_t i = 0; i < queue_.size(); ++i) {
        Tile& t = tiles_[size_t(queue_[i])];
        if (!t.loading) { ti = queue_[i]; queue_.erase(queue_.begin() + long(i)); break; }
    }
    if (ti < 0) return false;
    Tile& T0 = tiles_[size_t(ti)];
    T0.queued = false; T0.loading = true; loading_++;
    const bool first = T0.st == TS::None;
    const fs::path path = T0.path;
    std::vector<size_t> needed;
    if (!first)
        for (size_t n = 0; n < T0.nodes.size(); ++n)
            if (T0.nodes[n].hasGeom && T0.nodes[n].st == NS::None) needed.push_back(n);
    lk.unlock();

    // ---- 잠금 밖: 파일 읽기 + 디코드 + prepare
    TmxTile t; std::string err;
    bool ok = readTmxTile(path, t, &err);
    std::vector<Payload> pay(ok ? t.nodes.size() : 0);
    std::vector<size_t> bytes(pay.size(), 0);
    std::vector<char> tried(pay.size(), 0);
    if (ok) {
        if (first) for (size_t n = 0; n < t.nodes.size(); ++n) needed.push_back(n);
        for (size_t n : needed) {
            if (n >= t.nodes.size()) continue;
            tried[n] = 1;
            std::string e2;
            if (!decodeNode(t, n, &e2)) continue;
            if (!t.nodes[n].meshes.empty() && prepare_) pay[n] = prepare_(t.nodes[n], &bytes[n]);
            t.nodes[n].meshes.clear();  // 원본 메시는 바로 버림
        }
    }

    lk.lock();
    Tile& T = tiles_[size_t(ti)];
    T.loading = false; loading_--;
    stats_.tileLoads++;
    if (!ok) {
        if (first) { T.st = TS::Failed; stats_.tileFailures++; }
    } else {
        if (first) {
            T.nodes.resize(t.nodes.size());
            for (size_t n = 0; n < t.nodes.size(); ++n) {
                Node& N = T.nodes[n];
                N.bb = t.nodes[n].bb; N.msd = t.nodes[n].maxScreenDiameter;
                N.hasGeom = !t.nodes[n].resourceIds.empty();
                for (auto& c : t.nodes[n].children) N.childTiles.push_back(-1);
            }
            // tileFor 가 tiles_ 를 늘릴 수 있으므로 참조를 다시 잡는다
            for (size_t n = 0; n < t.nodes.size(); ++n)
                for (size_t c = 0; c < t.nodes[n].children.size(); ++c) {
                    int ci = tileFor(t.nodes[n].children[c], tiles_[size_t(ti)].depth + 1);
                    tiles_[size_t(ti)].nodes[n].childTiles[c] = ci;
                }
            tiles_[size_t(ti)].st = TS::Loaded;
            stats_.tilesLoaded++;
        }
        Tile& T2 = tiles_[size_t(ti)];
        for (size_t n = 0; n < pay.size() && n < T2.nodes.size(); ++n) {
            Node& N = T2.nodes[n];
            if (!tried[n]) continue;
            if (!pay[n]) { if (first) N.hasGeom = false; continue; }  // 빈 메시 → 그릴 것 없음
            if (N.st != NS::None) continue;
            N.st = NS::Ready; N.payload = std::move(pay[n]); N.bytes = std::max<size_t>(bytes[n], 1);
            readyBytes_ += N.bytes;
            stats_.nodesPrepared++;
        }
    }
    auto cb = onReady;
    lk.unlock();
    if (cb) cb();
    lk.lock();
    return true;
}

void LodStreamer::workerLoop() {
    std::unique_lock<std::mutex> lk(mu_);
    for (;;) {
        cv_.wait(lk, [&] {
            if (stop_) return true;
            if (readyBytes_ > cfg_.cpuBudgetBytes) return false;  // 앱이 올릴 때까지 잠시 쉼
            for (int q : queue_) if (!tiles_[size_t(q)].loading) return true;
            return false;
        });
        if (stop_) return;
        runJob(lk);
    }
}

bool LodStreamer::processOne() {
    std::unique_lock<std::mutex> lk(mu_);
    return runJob(lk);
}

}  // namespace asec
