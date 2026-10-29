#include "asec/vdatum.hpp"
#include <cctype>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <fstream>

namespace asec {
namespace {
const VDatumInfo kTable[] = {
    {VDatum::Unknown, 0, "?", "알 수 없는 높이 기준", ""},
    {VDatum::None, 0, "-", "SRS 에 명시 안 됨", ""},
    {VDatum::Ellipsoidal, 0, "h", "타원체고", ""},
    {VDatum::EGM96, 5773, "EGM96", "EGM96 지오이드 기준 높이", "egm96"},
    {VDatum::EGM2008, 3855, "EGM2008", "EGM2008 지오이드 기준 높이", "egm2008"},
    {VDatum::KVD1964, 5193, "KVD1964", "KVD1964 정표고(인천만 평균해수면)", "kngeoid"},
    {VDatum::KNGeoid, 0, "KNGeoid", "KNGeoid 지오이드 기준 정표고", "kngeoid"},
    {VDatum::LocalEnu, 0, "ENU", "ENU 로컬 높이", ""},
};
std::string norm(const std::string& s) {
    std::string r;
    for (unsigned char c : s) if (std::isalnum(c)) r += char(std::tolower(c));
    return r;
}
bool has(const std::string& n, const char* k) { return n.find(k) != std::string::npos; }
}  // namespace

const VDatumInfo& vdatumInfo(VDatum d) {
    for (auto& e : kTable) if (e.id == d) return e;
    return kTable[0];
}
const char* vdatumKey(VDatum d) {
    switch (d) {
    case VDatum::None: return "none"; case VDatum::Ellipsoidal: return "ellipsoidal"; case VDatum::EGM96: return "egm96";
    case VDatum::EGM2008: return "egm2008"; case VDatum::KVD1964: return "kvd1964"; case VDatum::KNGeoid: return "kngeoid";
    case VDatum::LocalEnu: return "enu"; default: return "unknown";
    }
}
VDatum vdatumFromKey(const std::string& k) {
    for (VDatum d : {VDatum::None, VDatum::Ellipsoidal, VDatum::EGM96, VDatum::EGM2008, VDatum::KVD1964, VDatum::KNGeoid, VDatum::LocalEnu})
        if (k == vdatumKey(d)) return d;
    return VDatum::Unknown;
}
std::vector<VDatum> declarableVDatums() { return {VDatum::EGM96, VDatum::EGM2008, VDatum::KVD1964, VDatum::KNGeoid, VDatum::Ellipsoidal}; }

VDatum vdatumFromEpsg(int epsg) {
    switch (epsg) {
    case 5773: case 5171: return VDatum::EGM96;     // 5171 = EGM96 geoid (datum)
    case 3855: case 1027: return VDatum::EGM2008;   // 1027 = EGM2008 geoid (datum)
    case 5193: case 1049: return VDatum::KVD1964;   // 1049 = Korean Vertical Datum 1964 (datum)
    default: return VDatum::Unknown;
    }
}
VDatum vdatumFromText(const std::string& text) {
    std::string n = norm(text);
    if (n.empty()) return VDatum::Unknown;
    if (has(n, "kngeoid")) return VDatum::KNGeoid;
    if (has(n, "egm2008") || has(n, "egm08")) return VDatum::EGM2008;
    if (has(n, "egm96")) return VDatum::EGM96;
    if (has(n, "kvd1964") || has(n, "koreanverticaldatum") || has(n, "incheon")) return VDatum::KVD1964;
    if (has(n, "ellipsoidalheight") || n == "ellipsoidal" || n == "h") return VDatum::Ellipsoidal;
    return VDatum::Unknown;
}

// ---------------------------------------------------------------- 격자
bool GridGeoidModel::undulation(double lat, double lon, double& out) const {
    if (rows < 2 || cols < 2 || N.size() != size_t(rows) * size_t(cols)) return false;
    double fy = (lat - lat0) / dLat;
    double l = lon - lon0;
    l = std::fmod(l, 360.0); if (l < 0) l += 360;  // 경도 0~360 / −180~180 격자 모두
    double fx = l / dLon;
    if (fy < -1e-9 || fx < -1e-9 || fy > rows - 1 + 1e-9 || fx > cols - 1 + 1e-9) return false;
    int y0 = std::min(int(std::floor(fy)), rows - 2), x0 = std::min(int(std::floor(fx)), cols - 2);
    y0 = std::max(y0, 0); x0 = std::max(x0, 0);
    double ty = fy - y0, tx = fx - x0;
    auto at = [&](int y, int x) { return N[size_t(y) * size_t(cols) + size_t(x)]; };
    float v[4] = {at(y0, x0), at(y0, x0 + 1), at(y0 + 1, x0), at(y0 + 1, x0 + 1)};
    for (float f : v) if (std::fabs(f - nodata) < 1e-3f || !std::isfinite(f)) return false;
    out = (v[0] * (1 - tx) + v[1] * tx) * (1 - ty) + (v[2] * (1 - tx) + v[3] * tx) * ty;
    return true;
}

static uint64_t be64(const unsigned char* p) { uint64_t v = 0; for (int i = 0; i < 8; ++i) v = (v << 8) | p[i]; return v; }
static uint32_t be32(const unsigned char* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

bool GridGeoidModel::loadGtx(const std::string& path, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "열 수 없음: " + path; return false; }
    unsigned char h[40];
    if (!f.read(reinterpret_cast<char*>(h), 40)) { if (err) *err = "GTX 머리가 짧음"; return false; }
    double d[4];
    for (int i = 0; i < 4; ++i) { uint64_t u = be64(h + 8 * i); std::memcpy(&d[i], &u, 8); }
    int r = int(be32(h + 32)), c = int(be32(h + 36));
    if (r < 2 || c < 2 || r > 100000 || c > 100000 || !(d[2] > 0) || !(d[3] > 0)) { if (err) *err = "GTX 머리 값이 이상함"; return false; }
    std::vector<unsigned char> buf(size_t(r) * size_t(c) * 4);
    if (!f.read(reinterpret_cast<char*>(buf.data()), std::streamsize(buf.size()))) { if (err) *err = "GTX 격자가 짧음"; return false; }
    lat0 = d[0]; lon0 = d[1]; dLat = d[2]; dLon = d[3]; rows = r; cols = c;
    N.resize(size_t(r) * size_t(c));
    for (size_t i = 0; i < N.size(); ++i) { uint32_t u = be32(&buf[i * 4]); std::memcpy(&N[i], &u, 4); }
    if (label.empty()) label = path;
    return true;
}

// ---------------------------------------------------------------- 변환
bool convertHeight(VDatum from, VDatum to, double lat, double lon, double z, double& out, const GeoidRegistry& reg, std::string* err) {
    out = z;
    if (from == to) return true;
    auto geoidBased = [](VDatum d) { return *vdatumInfo(d).geoidKey != 0; };
    auto fail = [&](const std::string& m) { if (err) *err = m; out = z; return false; };
    if (!(from == VDatum::Ellipsoidal || geoidBased(from)) || !(to == VDatum::Ellipsoidal || geoidBased(to)))
        return fail(std::string("지원하지 않는 높이 변환: ") + vdatumInfo(from).nameKo + " → " + vdatumInfo(to).nameKo + " (변환하지 않음)");
    double h = z;  // 타원체고로
    if (from != VDatum::Ellipsoidal) {
        const GeoidModel* g = reg.find(vdatumInfo(from).geoidKey);
        if (!g) return fail(std::string("지오이드 격자가 없습니다(") + vdatumInfo(from).geoidKey + "): 변환하지 않음");
        double N;
        if (!g->undulation(lat, lon, N)) return fail("지오이드 격자 범위 밖: 변환하지 않음");
        h = z + N;
    }
    if (to == VDatum::Ellipsoidal) { out = h; return true; }
    const GeoidModel* g = reg.find(vdatumInfo(to).geoidKey);
    if (!g) return fail(std::string("지오이드 격자가 없습니다(") + vdatumInfo(to).geoidKey + "): 변환하지 않음");
    double N;
    if (!g->undulation(lat, lon, N)) return fail("지오이드 격자 범위 밖: 변환하지 않음");
    out = h - N;
    return true;
}

}  // namespace asec
