#include "asec/raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace asec {
namespace {
struct V { double s, z, d, u, v; };   // 단면 좌표 꼭짓점
struct PV { double X, Y, dep, u, v; }; // 화면 좌표(픽셀) + 깊이(작을수록 앞)

int clipD(const V* in, int n, V* out, double lim, bool keepGreater) {
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const V& a = in[i];
        const V& b = in[(i + 1) % n];
        double fa = keepGreater ? a.d - lim : lim - a.d, fb = keepGreater ? b.d - lim : lim - b.d;
        if (fa >= 0) out[m++] = a;
        if ((fa >= 0) != (fb >= 0)) {
            double t = fa / (fa - fb);
            out[m++] = {a.s + t * (b.s - a.s), a.z + t * (b.z - a.z), a.d + t * (b.d - a.d), a.u + t * (b.u - a.u), a.v + t * (b.v - a.v)};
        }
    }
    return m;
}

inline void sampleBilinear(const RgbaImage& im, double u, double v, uint8_t* o) {
    // v=0 이 이미지 아래(OpenGL 규약) → 행 = (1-v)*h
    u -= std::floor(u); v -= std::floor(v);
    double x = u * im.w - 0.5, y = (1.0 - v) * im.h - 0.5;
    int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    double fx = x - x0, fy = y - y0;
    auto px = [&](int xx, int yy) {
        xx = std::clamp(xx, 0, im.w - 1); yy = std::clamp(yy, 0, im.h - 1);
        return &im.px[(size_t(yy) * im.w + xx) * 4];
    };
    const uint8_t *a = px(x0, y0), *b = px(x0 + 1, y0), *c = px(x0, y0 + 1), *d = px(x0 + 1, y0 + 1);
    for (int k = 0; k < 3; ++k) {
        double t = (a[k] * (1 - fx) + b[k] * fx) * (1 - fy) + (c[k] * (1 - fx) + d[k] * fx) * fy;
        o[k] = uint8_t(std::clamp(t + 0.5, 0.0, 255.0));
    }
    o[3] = 255;
}

/// Z 버퍼 래스터라이저(부채꼴 다각형). 납작한 삼각형은 모서리를 1px 선으로도 그려 사라지지 않게 한다.
struct Raster {
    int W, H;
    RgbaImage& img;
    std::vector<float> ownDepth;
    float* depth;
    int yLo = 0, yHi = 0;   // 이 래스터가 쓰는 행 [yLo, yHi) — 여러 스레드가 행을 나눠 같은 영상에 그림
    Raster(int w, int h, RgbaImage& im) : W(w), H(h), img(im), ownDepth(size_t(w) * h, 1e30f), depth(ownDepth.data()), yHi(h) {
        img.w = w; img.h = h; img.px.assign(size_t(w) * h * 4, 0);
    }
    // 공유 버퍼(영상·깊이는 호출자가 준비) 위의 행 구간
    Raster(int w, int h, RgbaImage& im, float* sharedDepth, int y0, int y1) : W(w), H(h), img(im), depth(sharedDepth), yLo(y0), yHi(y1) {}
    const RgbaImage* tex = nullptr;
    uint8_t shade = 180;
    inline void plot(size_t pi, double dd, double u, double v) {
        if (dd >= depth[pi]) return;
        depth[pi] = float(dd);
        uint8_t* o = &img.px[pi * 4];
        if (tex) sampleBilinear(*tex, u, v, o);
        else { o[0] = o[1] = o[2] = shade; o[3] = 255; }
    }
    void poly(const PV* P, int n) {
        for (int k = 1; k + 1 < n; ++k) tri(P[0], P[k], P[k + 1]);
    }
    void tri(const PV& a, const PV& b, const PV& c) {
        const PV* T[3] = {&a, &b, &c};
        double area = (b.X - a.X) * (c.Y - a.Y) - (c.X - a.X) * (b.Y - a.Y);
        double minX = std::min({a.X, b.X, c.X}), maxX = std::max({a.X, b.X, c.X});
        double minY = std::min({a.Y, b.Y, c.Y}), maxY = std::max({a.Y, b.Y, c.Y});
        if (maxX < 0 || maxY < yLo || minX > W || minY > yHi) return;
        if (maxY - minY < 1.5 || maxX - minX < 1.5 || std::fabs(area) < 2.0) {
            for (int e = 0; e < 3; ++e) {
                const PV& p = *T[e]; const PV& q = *T[(e + 1) % 3];
                int steps = std::max(1, int(std::ceil(std::max(std::fabs(q.X - p.X), std::fabs(q.Y - p.Y)) * 1.5)));
                for (int k = 0; k <= steps; ++k) {
                    double t = double(k) / steps;
                    int x = int(std::floor(p.X + t * (q.X - p.X))), y = int(std::floor(p.Y + t * (q.Y - p.Y)));
                    if (x < 0 || y < yLo || x >= W || y >= yHi) continue;
                    plot(size_t(y) * W + x, p.dep + t * (q.dep - p.dep), p.u + t * (q.u - p.u), p.v + t * (q.v - p.v));
                }
            }
        }
        if (std::fabs(area) < 1e-12) return;
        int x0 = std::max(0, int(std::floor(minX))), x1 = std::min(W - 1, int(std::ceil(maxX)));
        int y0 = std::max(yLo, int(std::floor(minY))), y1 = std::min(yHi - 1, int(std::ceil(maxY)));
        double ia = 1.0 / area;
        for (int y = y0; y <= y1; ++y) {
            double py = y + 0.5;
            for (int x = x0; x <= x1; ++x) {
                double px = x + 0.5;
                double w0 = ((b.X - px) * (c.Y - py) - (c.X - px) * (b.Y - py)) * ia;
                double w1 = ((c.X - px) * (a.Y - py) - (a.X - px) * (c.Y - py)) * ia;
                double w2 = 1.0 - w0 - w1;
                if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
                plot(size_t(y) * W + x, w0 * a.dep + w1 * b.dep + w2 * c.dep, w0 * a.u + w1 * b.u + w2 * c.u, w0 * a.v + w1 * b.v + w2 * c.v);
            }
        }
    }
};

uint8_t lambertShade(const float* a, const float* b, const float* c, double vx, double vy, double vz) {
    double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2], wx = c[0] - a[0], wy = c[1] - a[1], wz = c[2] - a[2];
    double nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
    double nl = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (nl <= 0) return 180;
    double lam = std::fabs(nx * vx + ny * vy + nz * vz) / nl;
    return uint8_t(90 + 140 * std::min(1.0, lam));
}
}  // namespace

bool renderElevation(const std::vector<MeshPtr>& meshes, const SectionFrame& f, const RasterRequest& rq, ElevationImage& out,
                     const std::atomic<bool>* cancel) {
    out = ElevationImage();
    if (!(rq.s1 > rq.s0) || !(rq.z1 > rq.z0) || rq.res <= 0) return false;
    double res = rq.res;
    int W = int(std::ceil((rq.s1 - rq.s0) / res)), H = int(std::ceil((rq.z1 - rq.z0) / res));
    while (size_t(W) * size_t(H) > rq.maxPixels) { res *= 1.25; W = int(std::ceil((rq.s1 - rq.s0) / res)); H = int(std::ceil((rq.z1 - rq.z0) / res)); }
    if (W <= 0 || H <= 0) return false;
    out.res = res; out.s0 = rq.s0; out.z1 = rq.z1;
    out.img.w = W; out.img.h = H; out.img.px.assign(size_t(W) * H * 4, 0);
    std::vector<float> depth(size_t(W) * H, 1e30f);
    const double inv = 1.0 / res;
    // 메시 단위 빠른 거르기(경계 상자 네 모서리의 s·d)
    std::vector<const Mesh*> list;
    size_t totalTris = 0;
    for (auto& mp : meshes) {
        const Mesh& m = *mp;
        if (m.bbox.valid()) {
            double dmin = 1e300, dmax = -1e300, smin = 1e300, smax = -1e300;
            for (int k = 0; k < 4; ++k) {
                double x = (k & 1) ? m.bbox.mx.x : m.bbox.mn.x, y = (k & 2) ? m.bbox.mx.y : m.bbox.mn.y;
                double ss = f.s(x, y), dd = f.d(x, y);
                dmin = std::min(dmin, dd); dmax = std::max(dmax, dd); smin = std::min(smin, ss); smax = std::max(smax, ss);
            }
            if (dmax < rq.dNear || dmin > rq.dFar || smax < rq.s0 || smin > rq.s1 || m.bbox.mx.z < rq.z0 || m.bbox.mn.z > rq.z1) continue;
        }
        list.push_back(&m);
        totalTris += m.triangleCount();
    }
    // 행 구간으로 나눠 병렬(각 스레드는 자기 행만 씀 → 잠금 없음). 삼각형의 z(행) 범위로 먼저 거름
    unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    int nThreads = int(std::min<unsigned>(std::min(hw, 8u), unsigned(std::max(1, H / 32))));
    if (totalTris < 20000) nThreads = 1;
    if (rq.threads > 0) nThreads = std::min(nThreads, rq.threads);
    std::vector<size_t> drawn(size_t(nThreads), 0);
    auto work = [&](int ti) {
        int y0 = int(int64_t(H) * ti / nThreads), y1 = int(int64_t(H) * (ti + 1) / nThreads);
        // 이 행 구간에 해당하는 z 범위(여유 1px — 납작한 삼각형 모서리 그리기 포함)
        const double zTopBand = rq.z1 - (y0 - 1.0) * res, zBotBand = rq.z1 - (y1 + 1.0) * res;
        Raster R(W, H, out.img, depth.data(), y0, y1);
        V poly[3], t1[8], t2[8];
        PV pv[8];
        for (const Mesh* mp : list) {
            if (cancel && cancel->load()) return;
            const Mesh& m = *mp;
            if (m.bbox.valid() && (m.bbox.mx.z < zBotBand || m.bbox.mn.z > zTopBand)) continue;
            const bool hasTex = m.texture && !m.texture->rgba.empty() && m.uv.size() == m.pos.size() / 3 * 2;
            R.tex = hasTex ? &m.texture->rgba : nullptr;
            const float* P = m.pos.data();
            const size_t nt = m.triangleCount();
            for (size_t t = 0; t < nt; ++t) {
                uint32_t ix[3] = {m.idx[3 * t], m.idx[3 * t + 1], m.idx[3 * t + 2]};
                float za = P[3 * ix[0] + 2], zb = P[3 * ix[1] + 2], zc = P[3 * ix[2] + 2];
                if (std::max({za, zb, zc}) < zBotBand || std::min({za, zb, zc}) > zTopBand) continue;
                double dmin = 1e300, dmax = -1e300, smin = 1e300, smax = -1e300;
                for (int k = 0; k < 3; ++k) {
                    const float* p = P + 3 * ix[k];
                    V& q = poly[k];
                    q.s = f.s(p[0], p[1]); q.d = f.d(p[0], p[1]); q.z = p[2];
                    q.u = hasTex ? m.uv[2 * ix[k]] : 0; q.v = hasTex ? m.uv[2 * ix[k] + 1] : 0;
                    dmin = std::min(dmin, q.d); dmax = std::max(dmax, q.d); smin = std::min(smin, q.s); smax = std::max(smax, q.s);
                }
                if (dmax < rq.dNear || dmin > rq.dFar || smax < rq.s0 || smin > rq.s1) continue;
                if (!hasTex) R.shade = lambertShade(P + 3 * ix[0], P + 3 * ix[1], P + 3 * ix[2], -f.n.x * 0.6, -f.n.y * 0.6, 0.8);
                int n = clipD(poly, 3, t1, rq.dNear, true);
                if (n < 3) continue;
                n = clipD(t1, n, t2, rq.dFar, false);
                if (n < 3) continue;
                double zmax = -1e300;
                for (int k = 0; k < n; ++k) { pv[k] = {(t2[k].s - rq.s0) * inv, (rq.z1 - t2[k].z) * inv, t2[k].d, t2[k].u, t2[k].v}; zmax = std::max(zmax, t2[k].z); }
                // 삼각형 개수는 맨 위 꼭짓점이 속한 구간에서만 센다(중복 방지)
                int topRow = std::clamp(int(std::floor((rq.z1 - zmax) * inv)), 0, H - 1);
                if (topRow >= y0 && topRow < y1) drawn[size_t(ti)]++;
                R.poly(pv, n);
            }
        }
    };
    if (nThreads == 1) work(0);
    else {
        std::vector<std::thread> th;
        for (int i = 0; i < nThreads; ++i) th.emplace_back(work, i);
        for (auto& t : th) t.join();
    }
    if (cancel && cancel->load()) return false;
    for (size_t c : drawn) out.trianglesDrawn += c;
    // 깊이 음영: 멀리(보는 방향 뒤쪽) 있는 면일수록 흰색 쪽으로 옅게 → 단면선(빨강)이 잘 보이게
    if (rq.depthFade > 0) {
        const double ref = std::max(rq.dFar, rq.fadeRef);
        if (ref > 1e-6) {
            for (size_t i = 0; i < depth.size(); ++i) {
                if (depth[i] >= 1e29f) continue;
                double d = depth[i];
                if (d <= 0) continue;
                double k = rq.depthFade * std::min(1.0, d / ref);
                uint8_t* o = &out.img.px[i * 4];
                for (int c = 0; c < 3; ++c) o[c] = uint8_t(std::lround(o[c] + (255.0 - o[c]) * k));
            }
        }
    }
    return true;
}

bool renderPlan(const std::vector<MeshPtr>& meshes, double x0, double y1, double res, int W, int H, RgbaImage& out, const std::atomic<bool>* cancel) {
    if (W <= 0 || H <= 0 || res <= 0) return false;
    Raster R(W, H, out);
    const double inv = 1.0 / res, x1 = x0 + W * res, y0 = y1 - H * res;
    PV pv[3];
    for (auto& mp : meshes) {
        if (cancel && cancel->load()) return false;
        const Mesh& m = *mp;
        if (m.bbox.valid() && (m.bbox.mx.x < x0 || m.bbox.mn.x > x1 || m.bbox.mx.y < y0 || m.bbox.mn.y > y1)) continue;
        const bool hasTex = m.texture && !m.texture->rgba.empty() && m.uv.size() == m.pos.size() / 3 * 2;
        R.tex = hasTex ? &m.texture->rgba : nullptr;
        const float* P = m.pos.data();
        for (size_t t = 0; t < m.triangleCount(); ++t) {
            uint32_t ix[3] = {m.idx[3 * t], m.idx[3 * t + 1], m.idx[3 * t + 2]};
            for (int k = 0; k < 3; ++k) {
                const float* p = P + 3 * ix[k];
                pv[k] = {(p[0] - x0) * inv, (y1 - p[1]) * inv, -double(p[2]), hasTex ? m.uv[2 * ix[k]] : 0.0, hasTex ? m.uv[2 * ix[k] + 1] : 0.0};
            }
            if (!hasTex) R.shade = lambertShade(P + 3 * ix[0], P + 3 * ix[1], P + 3 * ix[2], 0.3, 0.4, 0.85);
            R.tri(pv[0], pv[1], pv[2]);
        }
    }
    return true;
}

bool bandZRange(const std::vector<MeshPtr>& meshes, const SectionFrame& f, double dNear, double dFar, double sMin, double sMax, double& zmin, double& zmax) {
    zmin = 1e300; zmax = -1e300;
    for (auto& mp : meshes) {
        const Mesh& m = *mp;
        const float* P = m.pos.data();
        for (size_t i = 0; i < m.vertexCount(); ++i) {
            double s = f.s(P[3 * i], P[3 * i + 1]), d = f.d(P[3 * i], P[3 * i + 1]);
            if (d < dNear || d > dFar || s < sMin || s > sMax) continue;
            zmin = std::min(zmin, double(P[3 * i + 2])); zmax = std::max(zmax, double(P[3 * i + 2]));
        }
    }
    return zmax >= zmin;
}

}  // namespace asec
