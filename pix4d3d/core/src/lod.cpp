#include "asec/lod.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <thread>

namespace asec {

static size_t tileBytes(const TmxTile& t) {
    size_t b = t.bytes.size();
    for (auto& n : t.nodes) for (auto& m : n.meshes) b += m->pos.size() * 4 + m->uv.size() * 4 + m->idx.size() * 4;
    for (auto& tx : t.textures) b += tx.second->rgba.px.size();
    return b;
}

std::shared_ptr<TmxTile> TileCache::get(const fs::path& p, std::string* err) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = tiles_.find(p);
        if (it != tiles_.end()) { lastUse_[p] = ++clock_; return it->second; }
    }
    auto t = std::make_shared<TmxTile>();
    if (!readTmxTile(p, *t, err)) return nullptr;  // 파일 읽기는 잠금 밖
    std::lock_guard<std::mutex> lk(mu_);
    auto it = tiles_.find(p);
    if (it != tiles_.end()) return it->second;
    tiles_[p] = t;
    lastUse_[p] = ++clock_;
    bytes_[p] = t->bytes.size();
    evictLocked();
    return t;
}

bool TileCache::decode(const std::shared_ptr<TmxTile>& t, size_t node, std::string* err) {
    if (!t || node >= t->nodes.size()) return false;
    std::vector<TexturePtr> todo;
    {
        std::lock_guard<std::mutex> lk(*t->mu);  // 이 타일만 잠금
        TmxNode& N = t->nodes[node];
        if (N.ready) return true;
        if (!decodeNode(*t, node, err)) return false;
        if (textureDecoder)
            for (auto& m : N.meshes)
                if (m->texture && m->texture->rgba.empty() && !m->texture->encoded.empty() &&
                    std::find(todo.begin(), todo.end(), m->texture) == todo.end())
                    todo.push_back(m->texture);
        if (todo.empty()) N.ready = true;
    }
    if (!todo.empty()) {
        // JPG 디코드(가장 비싼 부분)는 잠금 없이 사본에 → 잠금 안에서 비어 있을 때만 옮김.
        // 준비된(ready) 노드가 가진 텍스처는 이미 차 있으므로 그 노드를 읽는 스레드와 겹쳐 쓰지 않는다.
        std::vector<RgbaImage> imgs(todo.size());
        for (size_t k = 0; k < todo.size(); ++k) {
            Texture tmp;
            tmp.format = todo[k]->format; tmp.filePath = todo[k]->filePath; tmp.encoded = todo[k]->encoded;
            textureDecoder(tmp);
            imgs[k] = std::move(tmp.rgba);
        }
        std::lock_guard<std::mutex> lk(*t->mu);
        for (size_t k = 0; k < todo.size(); ++k)
            if (todo[k]->rgba.empty() && !imgs[k].empty()) todo[k]->rgba = std::move(imgs[k]);
        t->nodes[node].ready = true;
    }
    size_t b;
    {
        std::lock_guard<std::mutex> lk(*t->mu);
        b = tileBytes(*t);
    }
    std::lock_guard<std::mutex> lk(mu_);
    auto it = tiles_.find(t->path);
    if (it != tiles_.end() && it->second == t) { bytes_[t->path] = b; evictLocked(); }
    return true;
}

bool TileCache::decodeGeometry(const std::shared_ptr<TmxTile>& t, size_t node, std::string* err) {
    if (!t || node >= t->nodes.size()) return false;
    {
        std::lock_guard<std::mutex> lk(*t->mu);
        if (!decodeNode(*t, node, err)) return false;   // 이미 했으면 바로 돌아옴(decoded)
    }
    size_t b;
    { std::lock_guard<std::mutex> lk(*t->mu); b = tileBytes(*t); }
    std::lock_guard<std::mutex> lk(mu_);
    auto it = tiles_.find(t->path);
    if (it != tiles_.end() && it->second == t) { bytes_[t->path] = b; evictLocked(); }
    return true;
}

size_t TileCache::loadedBytes() const {
    std::lock_guard<std::mutex> lk(mu_);
    size_t b = 0;
    for (auto& kv : bytes_) b += kv.second;
    return b;
}

void TileCache::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    tiles_.clear(); lastUse_.clear(); bytes_.clear();
}

void TileCache::evictLocked() {
    size_t total = 0;
    for (auto& kv : bytes_) total += kv.second;
    while (total > budget_ && tiles_.size() > 1) {
        auto victim = tiles_.end(); uint64_t best = UINT64_MAX;
        for (auto it = tiles_.begin(); it != tiles_.end(); ++it) {
            if (it->second.use_count() > 1) continue;  // 사용 중
            uint64_t u = lastUse_[it->first];
            if (u < best) { best = u; victim = it; }
        }
        if (victim == tiles_.end()) break;
        total -= bytes_[victim->first];
        lastUse_.erase(victim->first);
        bytes_.erase(victim->first);
        tiles_.erase(victim);
    }
}

namespace {
struct Job { std::shared_ptr<TmxTile> tile; size_t node; };

/// 모은 노드를 여러 스레드로 디코드(타일 단위 잠금이라 서로 다른 타일·노드가 동시에 진행).
bool decodeJobs(TileCache& c, const std::vector<Job>& jobs, std::string* err, const std::atomic<bool>* cancel, bool textures = true, size_t maxThreads = 8) {
    if (jobs.empty()) return true;
    unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    size_t nth = std::min<size_t>({jobs.size(), size_t(hw), std::max<size_t>(1, maxThreads)});
    std::atomic<size_t> next{0};
    std::atomic<bool> failed{false};
    std::mutex emu; std::string firstErr;
    auto run = [&] {
        for (;;) {
            if (failed.load() || (cancel && cancel->load())) return;
            size_t k = next++;
            if (k >= jobs.size()) return;
            std::string e;
            if (!(textures ? c.decode(jobs[k].tile, jobs[k].node, &e) : c.decodeGeometry(jobs[k].tile, jobs[k].node, &e))) {
                std::lock_guard<std::mutex> lk(emu);
                if (!failed.exchange(true)) firstErr = e;
            }
        }
    };
    if (nth <= 1) run();
    else {
        std::vector<std::thread> th;
        for (size_t i = 1; i < nth; ++i) th.emplace_back(run);
        run();
        for (auto& t : th) t.join();
    }
    if (failed) { if (err) *err = firstErr; return false; }
    if (cancel && cancel->load()) return false;
    return true;
}
}  // namespace

static bool collectRec(TileCache& c, const fs::path& p, const BandQuad& band, std::vector<Job>& out, LeafStats& st, std::string* err, int depth,
                       const std::atomic<bool>* cancel) {
    if (cancel && cancel->load()) return false;
    auto t = c.get(p, err);
    if (!t) return false;
    st.tilesVisited++;
    st.maxDepth = std::max(st.maxDepth, depth);
    for (size_t i = 0; i < t->nodes.size(); ++i) {
        const TmxNode& n = t->nodes[i];
        if (n.bb.valid() && !bandIntersectsBox(band, n.bb)) continue;
        bool useSelf = n.isLeaf();
        if (!n.isLeaf()) {
            for (auto& ch : n.children) {
                std::string e2;
                if (!collectRec(c, ch, band, out, st, &e2, depth + 1, cancel)) {
                    if (cancel && cancel->load()) return false;
                    useSelf = true;  // 자식 파일 없음 → 이 노드가 가장 고해상도
                    st.fallbackNodes++;
                    if (err) *err = e2;
                }
            }
        }
        if (useSelf) { out.push_back({t, i}); st.leafNodes++; }
    }
    return true;
}

bool collectLeafMeshes(TileCache& c, const fs::path& root, const BandQuad& band, std::vector<MeshPtr>& out, LeafStats* st, std::string* err,
                       const std::atomic<bool>* cancel, bool textures, size_t maxThreads) {
    LeafStats s;
    std::vector<Job> jobs;
    bool ok = collectRec(c, root, band, jobs, s, err, 0, cancel);
    if (ok) ok = decodeJobs(c, jobs, err, cancel, textures, maxThreads);
    if (ok)
        for (auto& j : jobs)
            for (auto& m : j.tile->nodes[j.node].meshes) {
                if (m->bbox.valid() && !bandIntersectsBox(band, m->bbox)) continue;
                out.push_back(m); s.meshes++; s.triangles += m->triangleCount();
            }
    if (st) *st = s;
    return ok;
}

static bool boxOverlapXY(const Box3& a, const Box3& b) {
    return a.valid() && b.valid() && !(a.mx.x < b.mn.x || a.mn.x > b.mx.x || a.mx.y < b.mn.y || a.mn.y > b.mx.y);
}

using OverlapFn = std::function<bool(const Box3&)>;

static bool resRec(TileCache& c, const fs::path& p, const OverlapFn& overlap, double res, std::vector<Job>& out, LeafStats& st, std::string* err, int depth,
                   const std::atomic<bool>* cancel) {
    if (cancel && cancel->load()) return false;
    auto t = c.get(p, err);
    if (!t) return false;
    st.tilesVisited++;
    st.maxDepth = std::max(st.maxDepth, depth);
    for (size_t i = 0; i < t->nodes.size(); ++i) {
        const TmxNode& n = t->nodes[i];
        if (n.bb.valid() && !overlap(n.bb)) continue;
        bool enough = n.isLeaf() || (n.maxScreenDiameter > 0 && n.bb.diag() / res <= n.maxScreenDiameter);
        bool useSelf = enough;
        if (!enough) {
            for (auto& ch : n.children) {
                std::string e2;
                if (!resRec(c, ch, overlap, res, out, st, &e2, depth + 1, cancel)) {
                    if (cancel && cancel->load()) return false;
                    useSelf = true; st.fallbackNodes++;
                }
            }
        }
        if (useSelf) { out.push_back({t, i}); st.leafNodes++; }
    }
    return true;
}

static bool collectRes(TileCache& c, const fs::path& root, const OverlapFn& overlap, double res, std::vector<MeshPtr>& out, LeafStats* st, std::string* err,
                       const std::atomic<bool>* cancel) {
    LeafStats s;
    std::vector<Job> jobs;
    bool ok = resRec(c, root, overlap, res, jobs, s, err, 0, cancel);
    if (ok) ok = decodeJobs(c, jobs, err, cancel);
    if (ok)
        for (auto& j : jobs)
            for (auto& m : j.tile->nodes[j.node].meshes) {
                if (m->bbox.valid() && !overlap(m->bbox)) continue;
                out.push_back(m); s.meshes++; s.triangles += m->triangleCount();
            }
    if (st) *st = s;
    return ok;
}

bool collectMeshesForResolution(TileCache& c, const fs::path& root, const Box3& area, double res, std::vector<MeshPtr>& out, LeafStats* st, std::string* err,
                                const std::atomic<bool>* cancel) {
    return collectRes(c, root, [&](const Box3& b) { return boxOverlapXY(b, area); }, res, out, st, err, cancel);
}

bool collectBandMeshesForResolution(TileCache& c, const fs::path& root, const BandQuad& band, double res, std::vector<MeshPtr>& out, LeafStats* st,
                                    std::string* err, const std::atomic<bool>* cancel) {
    if (res <= 0) return collectLeafMeshes(c, root, band, out, st, err, cancel);
    return collectRes(c, root, [&](const Box3& b) { return bandIntersectsBox(band, b); }, res, out, st, err, cancel);
}

namespace {
struct DNode {
    std::shared_ptr<TmxTile> tile; size_t idx; double diag; size_t tris;
};
}

bool selectDisplayMeshes(TileCache& c, const fs::path& root, size_t budget, std::vector<MeshPtr>& out, Box3* bbox, std::string* err,
                         const std::function<void(double)>& progress) {
    auto rt = c.get(root, err);
    if (!rt) return false;
    auto cmp = [](const DNode& a, const DNode& b) { return a.diag < b.diag; };
    std::priority_queue<DNode, std::vector<DNode>, decltype(cmp)> open(cmp);  // 세분 후보(큰 것 먼저)
    std::vector<DNode> final;
    size_t total = 0;
    auto triCount = [&](const std::shared_ptr<TmxTile>& t, size_t i) {
        size_t n = 0; for (auto& m : t->nodes[i].meshes) n += m->triangleCount(); return n;
    };
    for (size_t i = 0; i < rt->nodes.size(); ++i) {
        if (!c.decode(rt, i, err)) return false;
        DNode d{rt, i, rt->nodes[i].bb.diag(), triCount(rt, i)};
        total += d.tris;
        open.push(d);
    }
    int steps = 0;
    while (!open.empty()) {
        DNode d = open.top(); open.pop();
        const TmxNode& n = d.tile->nodes[d.idx];
        if (n.isLeaf()) { final.push_back(d); continue; }
        std::vector<DNode> kids; size_t kt = 0; bool ok = true;
        for (auto& ch : n.children) {
            std::string e2;
            auto t = c.get(ch, &e2);
            if (!t) { ok = false; break; }
            for (size_t i = 0; i < t->nodes.size(); ++i) {
                if (!c.decode(t, i, &e2)) { ok = false; break; }
                DNode k{t, i, t->nodes[i].bb.diag(), triCount(t, i)};
                kt += k.tris; kids.push_back(k);
            }
            if (!ok) break;
        }
        if (!ok || total - d.tris + kt > budget) { final.push_back(d); continue; }
        total = total - d.tris + kt;
        for (auto& k : kids) open.push(k);
        if (progress && (++steps % 4) == 0) progress(std::min(1.0, double(total) / double(budget)));
    }
    Box3 bb;
    for (auto& d : final)
        for (auto& m : d.tile->nodes[d.idx].meshes) { out.push_back(m); bb.add(m->bbox); }
    if (bbox) *bbox = bb;
    return true;
}

}  // namespace asec
