#include <algorithm>
#include <thread>
#include "asec/engine.hpp"

namespace asec {
using clk = std::chrono::steady_clock;
static double msSince(clk::time_point t) { return std::chrono::duration<double, std::milli>(clk::now() - t).count(); }

bool TmxSource::open(const fs::path& p, std::string* err) {
    if (!readTmxScene(p, scene, err)) return false;
    srs = scene.srs;
    bounds = Box3();
    for (auto& r : scene.roots()) {
        auto t = cache->get(r, err);
        if (!t) return false;
        for (auto& n : t->nodes) bounds.add(n.bb);
    }
    return true;
}

static void addStats(LeafStats& a, const LeafStats& b) {
    a.tilesVisited += b.tilesVisited; a.leafNodes += b.leafNodes; a.meshes += b.meshes; a.triangles += b.triangles;
    a.maxDepth = std::max(a.maxDepth, b.maxDepth); a.fallbackNodes += b.fallbackNodes;
}

// 병합 3MX: 레이어(루트)마다 모아서 합친다
template <class F>
static bool eachRoot(const TmxScene& sc, LeafStats* st, F&& f) {
    LeafStats all;
    for (auto& r : sc.roots()) {
        LeafStats s;
        if (!f(r, &s)) return false;
        addStats(all, s);
    }
    if (st) *st = all;
    return true;
}

bool TmxSource::leafMeshes(const BandQuad& band, std::vector<MeshPtr>& out, LeafStats* st, std::string* err, const std::atomic<bool>* cancel) {
    return eachRoot(scene, st, [&](const fs::path& r, LeafStats* s) { return collectLeafMeshes(*cache, r, band, out, s, err, cancel); });
}

bool TmxSource::cutMeshes(const BandQuad& band, std::vector<MeshPtr>& out, LeafStats* st, std::string* err, const std::atomic<bool>* cancel) {
    // 미리보기 잘린 면: 지오메트리만(사진 디코드 없음), 코어 절반까지(2–4) — 화면 스레드·평면 스트리밍 몫을 남김
    const size_t nth = std::clamp<size_t>(std::thread::hardware_concurrency() / 2, 2, 4);
    return eachRoot(scene, st, [&](const fs::path& r, LeafStats* s) { return collectLeafMeshes(*cache, r, band, out, s, err, cancel, false, nth); });
}

bool TmxSource::bandMeshes(const BandQuad& band, double res, std::vector<MeshPtr>& out, LeafStats* st, std::string* err, const std::atomic<bool>* cancel) {
    return eachRoot(scene, st, [&](const fs::path& r, LeafStats* s) { return collectBandMeshesForResolution(*cache, r, band, res, out, s, err, cancel); });
}

bool TmxSource::areaMeshes(const Box3& areaXY, double res, std::vector<MeshPtr>& out, LeafStats* st, std::string* err, const std::atomic<bool>* cancel) {
    return eachRoot(scene, st, [&](const fs::path& r, LeafStats* s) { return collectMeshesForResolution(*cache, r, areaXY, res, out, s, err, cancel); });
}

bool StaticSource::leafMeshes(const BandQuad& band, std::vector<MeshPtr>& out, LeafStats* st, std::string*, const std::atomic<bool>*) {
    LeafStats s;
    for (auto& m : meshes)
        if (!m->bbox.valid() || bandIntersectsBox(band, m->bbox)) { out.push_back(m); s.meshes++; s.triangles += m->triangleCount(); }
    s.leafNodes = s.meshes;
    if (st) *st = s;
    return true;
}

bool computeSection(MeshSource& src, const SectionRequest& rq, SectionOutput& out, std::string* err, const std::atomic<bool>* cancel) {
    out = SectionOutput();
    SectionFrame f(rq.line);
    if (f.L < 1e-3) { if (err) *err = "단면선이 너무 짧습니다"; return false; }
    auto t0 = clk::now();
    std::vector<MeshPtr> meshes;
    BandQuad band = sectionBand(rq.line, 0.0);
    // 단면 평면 자체(두께 0)도 포함되도록 띠가 비어 있으면 약간 넓힘
    if (rq.line.front + rq.line.back < 1e-6) band = sectionBand(SectionLine{rq.line.a, rq.line.b, 0.001, 0.001}, 0.0);
    out.previewLod = rq.meshRes > 0;
    if (!(out.previewLod ? src.bandMeshes(band, rq.meshRes, meshes, &out.stats, err, cancel) : src.leafMeshes(band, meshes, &out.stats, err, cancel))) return false;
    // 잘린 면은 언제나 최고 해상도 잎: 최종은 위 잎 메시 그대로, 미리보기는 단면 평면 ±1 mm 얇은 띠의 잎을 따로 모음
    std::vector<MeshPtr> cutMeshes;
    const std::vector<MeshPtr>* cutSrc = &meshes;
    out.cutStats = out.stats;
    out.cutFromLeaf = !out.previewLod;
    if (out.previewLod && rq.leafProfileAlways) {
        const BandQuad thin = sectionBand(SectionLine{rq.line.a, rq.line.b, 0.001, 0.001}, 0.0);
        LeafStats cs;
        if (!src.cutMeshes(thin, cutMeshes, &cs, err, cancel)) return false;
        out.cutStats = cs; cutSrc = &cutMeshes; out.cutFromLeaf = true;
    }
    out.msCollect = msSince(t0);
    if (cancel && cancel->load()) return false;

    auto t1 = clk::now();
    std::vector<CutSeg> segs;
    for (auto& m : *cutSrc) cutMesh(*m, f, 0.0, 0.0, f.L, segs);
    out.result.line = rq.line;
    out.result.srs = src.srs;
    out.result.rawSegments = segs.size();
    out.result.triangles = out.stats.triangles;
    out.result.tiles = out.stats.leafNodes;
    out.result.profile = buildProfile(segs, rq.cleanup);
    snapEnds(out.result.profile, 0.0, f.L, rq.cleanup.weldTol * 2.5);
    out.msCut = msSince(t1);

    double zmin = 1e300, zmax = -1e300;
    for (auto& pl : out.result.profile) for (auto& p : pl) { zmin = std::min(zmin, p.y); zmax = std::max(zmax, p.y); }
    double bz0, bz1;
    if (bandZRange(meshes, f, -rq.line.front, rq.line.back, 0.0, f.L, bz0, bz1)) { zmin = std::min(zmin, bz0); zmax = std::max(zmax, bz1); }
    if (zmax < zmin) { if (err) *err = "단면선이 메시와 만나지 않습니다"; return false; }
    out.result.zMin = std::floor((zmin - rq.zMargin) * 10.0) / 10.0;
    out.result.zMax = std::ceil((zmax + rq.zMargin) * 10.0) / 10.0;

    if (rq.wantImage) {
        auto t2 = clk::now();
        RasterRequest rr;
        rr.s0 = 0; rr.s1 = f.L; rr.z0 = out.result.zMin; rr.z1 = out.result.zMax;
        rr.res = rq.imageRes; rr.dNear = -rq.line.front; rr.dFar = rq.line.back; rr.maxPixels = rq.maxImagePixels; rr.depthFade = rq.depthFade;
        if (rr.dFar - rr.dNear < 1e-4) rr.dFar = rr.dNear + 0.01;
        renderElevation(meshes, f, rr, out.image, cancel);
        out.msImage = msSince(t2);
    }
    return !(cancel && cancel->load());
}

}  // namespace asec
