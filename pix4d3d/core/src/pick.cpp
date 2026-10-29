#include "asec/pick.hpp"

#include <chrono>
#include <cmath>

namespace asec {

const char* zSourceKo(ZSource s) {
    switch (s) {
    case ZSource::LeafSurface: return "잎 표면(최고 해상도)";
    case ZSource::LeafWithFallback: return "표면(일부 상위 LOD 대체)";
    case ZSource::Coarse: return "대략(화면 LOD)";
    case ZSource::SectionCursor: return "단면 커서 위치";
    default: return "—";
    }
}

static bool rayBox(const Vec3& o, const Vec3& d, const Box3& b, double tMin, double tMax) {
    const double oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z}, mn[3] = {b.mn.x, b.mn.y, b.mn.z}, mx[3] = {b.mx.x, b.mx.y, b.mx.z};
    for (int k = 0; k < 3; ++k) {
        const double pad = 1e-6;
        if (std::fabs(dd[k]) < 1e-300) {
            if (oo[k] < mn[k] - pad || oo[k] > mx[k] + pad) return false;
            continue;
        }
        double t0 = (mn[k] - pad - oo[k]) / dd[k], t1 = (mx[k] + pad - oo[k]) / dd[k];
        if (t0 > t1) std::swap(t0, t1);
        tMin = std::max(tMin, t0); tMax = std::min(tMax, t1);
        if (tMin > tMax) return false;
    }
    return true;
}

bool rayMeshes(const std::vector<MeshPtr>& meshes, const Vec3& o, const Vec3& d, double tMin, double& tBest, size_t* tested) {
    bool hit = false;
    size_t n = 0;
    for (auto& mp : meshes) {
        const Mesh& m = *mp;
        if (m.bbox.valid() && !rayBox(o, d, m.bbox, tMin, tBest)) continue;
        const float* P = m.pos.data();
        const size_t nt = m.triangleCount();
        for (size_t i = 0; i < nt; ++i) {
            const uint32_t* t = &m.idx[3 * i];
            const float* a = P + 3 * size_t(t[0]); const float* b = P + 3 * size_t(t[1]); const float* c = P + 3 * size_t(t[2]);
            // 빠른 기각: 연직 광선이면 XY 상자 밖 삼각형 제외(float 비교, 넉넉하게)
            double ax = a[0], ay = a[1], az = a[2];
            double e1x = double(b[0]) - ax, e1y = double(b[1]) - ay, e1z = double(b[2]) - az;
            double e2x = double(c[0]) - ax, e2y = double(c[1]) - ay, e2z = double(c[2]) - az;
            double px = d.y * e2z - d.z * e2y, py = d.z * e2x - d.x * e2z, pz = d.x * e2y - d.y * e2x;
            double det = e1x * px + e1y * py + e1z * pz;
            ++n;
            if (std::fabs(det) < 1e-18) continue;
            double inv = 1.0 / det;
            double sx = o.x - ax, sy = o.y - ay, sz = o.z - az;
            double u = (sx * px + sy * py + sz * pz) * inv;
            if (u < -1e-12 || u > 1 + 1e-12) continue;
            double qx = sy * e1z - sz * e1y, qy = sz * e1x - sx * e1z, qz = sx * e1y - sy * e1x;
            double v = (d.x * qx + d.y * qy + d.z * qz) * inv;
            if (v < -1e-12 || u + v > 1 + 1e-12) continue;
            double tt = (e2x * qx + e2y * qy + e2z * qz) * inv;
            if (tt >= tMin && tt < tBest) { tBest = tt; hit = true; }
        }
    }
    if (tested) *tested += n;
    return hit;
}

bool pickRay(MeshSource& src, const Vec3& o, const Vec3& d0, PickResult& out, std::string* err, const std::atomic<bool>* cancel) {
    auto t0 = std::chrono::steady_clock::now();
    out = PickResult();
    double L = std::sqrt(d0.x * d0.x + d0.y * d0.y + d0.z * d0.z);
    if (!(L > 0)) { if (err) *err = "광선 방향이 0"; return false; }
    Vec3 d = d0 * (1.0 / L);
    Box3 bb = src.bounds;
    if (!bb.valid()) { if (err) *err = "모델 범위 없음"; return false; }
    // 광선이 모델 상자(z 여유 포함)를 지나는 구간 → 그 XY 투영을 띠로
    Box3 big = bb; big.mn.z -= 1; big.mx.z += 1; big.mn.x -= 0.01; big.mn.y -= 0.01; big.mx.x += 0.01; big.mx.y += 0.01;
    double ta = -1e300, tb = 1e300;
    {
        const double oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z}, mn[3] = {big.mn.x, big.mn.y, big.mn.z}, mx[3] = {big.mx.x, big.mx.y, big.mx.z};
        for (int k = 0; k < 3; ++k) {
            if (std::fabs(dd[k]) < 1e-15) { if (oo[k] < mn[k] || oo[k] > mx[k]) { out.ms = 0; return true; } continue; }
            double a = (mn[k] - oo[k]) / dd[k], b = (mx[k] - oo[k]) / dd[k];
            if (a > b) std::swap(a, b);
            ta = std::max(ta, a); tb = std::min(tb, b);
        }
        if (ta > tb) return true;  // 모델 밖: 맞지 않음(오류 아님)
    }
    Vec3 A = o + d * ta, B = o + d * tb;
    const double w = 0.02;  // 띠 반폭(m) — 타일 경계 걸침 대비
    SectionLine sl;
    Vec2 a2(A.x, A.y), b2(B.x, B.y);
    if ((b2 - a2).len() < 1e-3) { a2 = a2 - Vec2(w, 0); b2 = b2 + Vec2(w, 0); }
    sl.a = a2; sl.b = b2; sl.front = w; sl.back = w;
    BandQuad band = sectionBand(sl, w);
    std::vector<MeshPtr> meshes;
    if (!src.leafMeshes(band, meshes, &out.stats, err, cancel)) return false;
    if (cancel && cancel->load()) return false;
    out.meshes = meshes.size();
    double tBest = tb + 1e-9;
    if (rayMeshes(meshes, o, d, ta - 1e-9, tBest, &out.trianglesTested)) {
        out.hit = true;
        out.t = tBest;
        out.local = o + d * tBest;
        out.world = src.srs.toWorld(out.local);
        out.source = out.stats.fallbackNodes ? ZSource::LeafWithFallback : ZSource::LeafSurface;
    }
    out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return true;
}

bool pickVertical(MeshSource& src, double x, double y, PickResult& out, std::string* err, const std::atomic<bool>* cancel) {
    double top = src.bounds.valid() ? src.bounds.mx.z + 10.0 : 1e4;
    return pickRay(src, Vec3(x, y, top), Vec3(0, 0, -1), out, err, cancel);
}

}  // namespace asec
