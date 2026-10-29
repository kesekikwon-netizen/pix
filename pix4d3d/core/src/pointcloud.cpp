#include "asec/pointcloud.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_set>

namespace asec {

static bool visitRec(TileCache& c, const fs::path& p, const std::function<bool(const Box3&)>& filt, const std::function<bool(const MeshPtr&)>& cb,
                     std::string* err, const std::atomic<bool>* cancel, bool& stop) {
    if (stop || (cancel && cancel->load())) return false;
    auto t = c.get(p, err);
    if (!t) return false;
    for (size_t i = 0; i < t->nodes.size() && !stop; ++i) {
        const TmxNode& n = t->nodes[i];
        if (n.bb.valid() && filt && !filt(n.bb)) continue;
        bool useSelf = n.isLeaf();
        for (auto& ch : n.children) {
            std::string e2;
            if (!visitRec(c, ch, filt, cb, &e2, cancel, stop)) { if (stop || (cancel && cancel->load())) return false; useSelf = true; }
        }
        if (useSelf) {
            if (!c.decode(t, i, err)) return false;
            for (auto& m : t->nodes[i].meshes) {
                if (m->bbox.valid() && filt && !filt(m->bbox)) continue;
                if (!cb(m)) { stop = true; break; }
            }
        }
    }
    return !stop;
}

bool visitLeafMeshes(TileCache& c, const fs::path& root, const std::function<bool(const Box3&)>& filt, const std::function<bool(const MeshPtr&)>& cb,
                     std::string* err, const std::atomic<bool>* cancel) {
    bool stop = false;
    bool ok = visitRec(c, root, filt, cb, err, cancel, stop);
    return ok || stop;
}

namespace {
inline bool pointInQuad(const BandQuad& q, double x, double y) {
    bool pos = false, neg = false;
    for (int i = 0; i < 4; ++i) {
        const Vec2& a = q.p[i]; const Vec2& b = q.p[(i + 1) % 4];
        double c = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
        if (c > 0) pos = true; else if (c < 0) neg = true;
    }
    return !(pos && neg);
}
struct VK { int64_t x, y, z; bool operator==(const VK& o) const { return x == o.x && y == o.y && z == o.z; } };
struct VKH { size_t operator()(const VK& k) const {
    uint64_t h = uint64_t(k.x) * 0x9E3779B97F4A7C15ULL; h ^= uint64_t(k.y) + 0xC2B2AE3D27D4EB4FULL + (h << 6) + (h >> 2);
    h ^= uint64_t(k.z) * 0x165667B19E3779F9ULL + (h << 6) + (h >> 2); return size_t(h); } };
void sampleNearestBilinear(const RgbaImage& im, double u, double v, uint8_t* o) {
    u -= std::floor(u); v -= std::floor(v);
    double x = u * im.w - 0.5, y = (1.0 - v) * im.h - 0.5;
    int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    double fx = x - x0, fy = y - y0;
    auto px = [&](int xx, int yy) { xx = std::clamp(xx, 0, im.w - 1); yy = std::clamp(yy, 0, im.h - 1); return &im.px[(size_t(yy) * im.w + xx) * 4]; };
    const uint8_t *a = px(x0, y0), *b = px(x0 + 1, y0), *c = px(x0, y0 + 1), *d = px(x0 + 1, y0 + 1);
    for (int k = 0; k < 3; ++k) o[k] = uint8_t(std::clamp((a[k] * (1 - fx) + b[k] * fx) * (1 - fy) + (c[k] * (1 - fx) + d[k] * fx) * fy + 0.5, 0.0, 255.0));
}

#pragma pack(push, 1)
struct LasHeader {
    char sig[4] = {'L', 'A', 'S', 'F'};
    uint16_t fileSource = 0, globalEnc = 0;
    uint32_t guid1 = 0; uint16_t guid2 = 0, guid3 = 0; uint8_t guid4[8] = {};
    uint8_t vMaj = 1, vMin = 2;
    char sysId[32] = {}, software[32] = {};
    uint16_t doy = 1, year = 2026, headerSize = 227;
    uint32_t offsetToData = 227, nVlr = 0;
    uint8_t format = 2; uint16_t recLen = 26;
    uint32_t nPoints = 0, byReturn[5] = {};
    double sx = 0.001, sy = 0.001, sz = 0.001, ox = 0, oy = 0, oz = 0;
    double maxX = 0, minX = 0, maxY = 0, minY = 0, maxZ = 0, minZ = 0;
};
struct LasPoint2 { int32_t x, y, z; uint16_t intensity; uint8_t flags, cls; int8_t scan; uint8_t user; uint16_t src; uint16_t r, g, b; };
#pragma pack(pop)
static_assert(sizeof(LasHeader) == 227, "LAS header");
static_assert(sizeof(LasPoint2) == 26, "LAS point 2");
}  // namespace

bool exportPoints(const std::function<bool(const std::function<bool(const MeshPtr&)>&)>& visitor, const SrsInfo& srs, const PointExportOptions& o,
                  const fs::path& out, PointExportStats* st, std::string* err, const std::atomic<bool>* cancel, const std::function<void(size_t)>& progress) {
    std::ofstream f(out, std::ios::binary);
    if (!f) { if (err) *err = "쓸 수 없음: " + out.u8string(); return false; }
    PointExportStats S;
    std::unordered_set<VK, VKH> seen;
    const bool nearDup = !(o.spacing > 0);
    const double vox = nearDup ? 0.001 : o.spacing;  // 0 이면: 1 mm 이웃 안의 점은 같은 점(타일 경계 공유점, MG2 양자화 차이 흡수)
    LasHeader H;
    if (o.format == PointFormat::LAS) {
        std::snprintf(H.software, sizeof H.software, "Excavation Section Viewer");
        std::snprintf(H.sysId, sizeof H.sysId, "%s", srs.srs.empty() ? "ExcavSection" : srs.shortLabel().c_str());
        // 오프셋 = 원점(정수 m) → int32 범위·mm 정밀도 유지
        H.ox = std::floor(srs.origin.x); H.oy = std::floor(srs.origin.y); H.oz = std::floor(srs.origin.z);
        // GeoKeyDirectory VLR(LASF_Projection 34735): 수평 EPSG + (있으면) 수직 EPSG — 높이 값은 그대로
        std::vector<uint16_t> gk;
        if (srs.epsg() > 0 && srs.epsg() < 32767) {
            gk = {1, 1, 0, 0};
            auto key = [&](uint16_t id, uint16_t v) { gk.insert(gk.end(), {id, 0, 1, v}); gk[3]++; };
            key(1024, 1); key(3072, uint16_t(srs.epsg())); key(3076, 9001);
            if (srs.verticalEpsg() > 0 && srs.verticalEpsg() < 32767) { key(4096, uint16_t(srs.verticalEpsg())); key(4099, 9001); }
        }
        if (!gk.empty()) { H.nVlr = 1; H.offsetToData = uint32_t(227 + 54 + gk.size() * 2); }
        f.write(reinterpret_cast<char*>(&H), sizeof H);
        if (!gk.empty()) {
            uint8_t vh[54] = {};
            std::memcpy(vh + 2, "LASF_Projection", 15);
            uint16_t rid = 34735, len = uint16_t(gk.size() * 2);
            std::memcpy(vh + 18, &rid, 2); std::memcpy(vh + 20, &len, 2);
            std::snprintf(reinterpret_cast<char*>(vh + 22), 32, "GeoKeyDirectoryTag");
            f.write(reinterpret_cast<char*>(vh), 54);
            f.write(reinterpret_cast<const char*>(gk.data()), std::streamsize(len));
        }
    }
    char line[160];
    std::string buf;
    buf.reserve(1 << 20);
    bool ok = visitor([&](const MeshPtr& mp) -> bool {
        if (cancel && cancel->load()) return false;
        const Mesh& m = *mp;
        S.meshes++;
        const bool tex = o.rgb && m.texture && !m.texture->rgba.empty() && m.uv.size() == m.pos.size() / 3 * 2;
        for (size_t i = 0; i < m.vertexCount(); ++i) {
            double x = m.pos[3 * i], y = m.pos[3 * i + 1], z = m.pos[3 * i + 2];
            S.vertices++;
            if (o.area == PointArea::Band && !pointInQuad(o.band, x, y)) continue;
            if (o.area == PointArea::Box && (x < o.box.mn.x || x > o.box.mx.x || y < o.box.mn.y || y > o.box.mx.y)) continue;
            Vec3 w = srs.toWorld(Vec3(x, y, z));
            // 복셀 키는 실좌표 기준(타일이 달라도 같은 격자)
            VK k{int64_t(std::floor(w.x / vox)), int64_t(std::floor(w.y / vox)), int64_t(std::floor(w.z / vox))};
            if (nearDup) {
                bool dup = false;
                for (int dx = -1; dx <= 1 && !dup; ++dx)
                    for (int dy = -1; dy <= 1 && !dup; ++dy)
                        for (int dz = -1; dz <= 1 && !dup; ++dz)
                            if (seen.count(VK{k.x + dx, k.y + dy, k.z + dz})) dup = true;
                if (dup) continue;
                seen.insert(k);
            } else if (!seen.insert(k).second) continue;
            uint8_t c[3] = {180, 180, 180};
            if (tex) sampleNearestBilinear(m.texture->rgba, m.uv[2 * i], m.uv[2 * i + 1], c);
            if (o.format == PointFormat::XYZ) {
                int n = std::snprintf(line, sizeof line, "%.*f %.*f %.*f %d %d %d\n", o.decimals, w.x, o.decimals, w.y, o.decimals, w.z, c[0], c[1], c[2]);
                buf.append(line, size_t(n));
                if (buf.size() > (1u << 20)) { f.write(buf.data(), std::streamsize(buf.size())); buf.clear(); }
            } else {
                LasPoint2 P{};
                P.x = int32_t(std::llround((w.x - H.ox) / H.sx)); P.y = int32_t(std::llround((w.y - H.oy) / H.sy)); P.z = int32_t(std::llround((w.z - H.oz) / H.sz));
                P.flags = 0x09;  // return 1 of 1
                P.cls = 1;
                P.r = uint16_t(c[0] * 257); P.g = uint16_t(c[1] * 257); P.b = uint16_t(c[2] * 257);
                buf.append(reinterpret_cast<char*>(&P), sizeof P);
                if (buf.size() > (1u << 20)) { f.write(buf.data(), std::streamsize(buf.size())); buf.clear(); }
            }
            S.worldBounds.add(w);
            S.points++;
        }
        if (progress) progress(S.points);
        return true;
    });
    f.write(buf.data(), std::streamsize(buf.size()));
    if (!ok || (cancel && cancel->load())) { if (err && err->empty()) *err = "취소됨"; return false; }
    if (o.format == PointFormat::LAS) {
        if (S.points > 0xFFFFFFFFull) { if (err) *err = "LAS 1.2 점 수 한도 초과"; return false; }
        H.nPoints = uint32_t(S.points); H.byReturn[0] = H.nPoints;
        if (S.worldBounds.valid()) {
            H.minX = S.worldBounds.mn.x; H.maxX = S.worldBounds.mx.x; H.minY = S.worldBounds.mn.y; H.maxY = S.worldBounds.mx.y;
            H.minZ = S.worldBounds.mn.z; H.maxZ = S.worldBounds.mx.z;
        }
        f.seekp(0);
        f.write(reinterpret_cast<char*>(&H), sizeof H);
    }
    if (st) *st = S;
    return bool(f);
}

bool exportPointsTmx(TileCache& c, const std::vector<fs::path>& roots, const SrsInfo& srs, const PointExportOptions& o, const fs::path& out, PointExportStats* st,
                     std::string* err, const std::atomic<bool>* cancel, const std::function<void(size_t)>& progress) {
    std::function<bool(const Box3&)> filt;
    if (o.area == PointArea::Band) filt = [&](const Box3& b) { return bandIntersectsBox(o.band, b); };
    else if (o.area == PointArea::Box) filt = [&](const Box3& b) { return !(b.mx.x < o.box.mn.x || b.mn.x > o.box.mx.x || b.mx.y < o.box.mn.y || b.mn.y > o.box.mx.y); };
    auto visitor = [&](const std::function<bool(const MeshPtr&)>& cb) {
        for (auto& root : roots) if (!visitLeafMeshes(c, root, filt, cb, err, cancel)) return false;  // 병합 3MX: 레이어 전부
        return true;
    };
    return exportPoints(visitor, srs, o, out, st, err, cancel, progress);
}

}  // namespace asec
