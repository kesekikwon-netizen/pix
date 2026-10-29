#include "asec/export.hpp"

#include <cstdio>
#include <fstream>
#include "json.hpp"

namespace asec {

Vec3 sectionToWorld(const SectionResult& r, double s, double z) {
    SectionFrame f(r.line);
    Vec2 p = f.planXY(s, 0);
    return r.srs.toWorld(Vec3(p.x, p.y, z));
}

bool exportSectionDxf(const SectionResult& r, const DxfExportOptions& o, const fs::path& out, std::string* err, std::string* dxfText) {
    SectionFrame f(r.line);
    const double oz = r.srs.origin.z;
    const bool w3 = o.mode == DxfCoordMode::World3D;
    const double th = o.textMm / 1000.0 * o.scaleDenom;
    const Vec3 ext(f.u.y, -f.u.x, 0);  // 관찰자 쪽 법선(−n) → OCS X축 = A→B
    auto P = [&](double s, double zAbs) -> Vec3 {
        if (!w3) return Vec3(s, zAbs, 0);
        Vec2 p = f.planXY(s, 0);
        return Vec3(p.x + r.srs.origin.x, p.y + r.srs.origin.y, zAbs);
    };
    auto T = [&](DxfWriter& d, const std::string& layer, double s, double zAbs, const std::string& txt, int ha, int va) {
        if (w3) d.textOnPlane(layer, P(s, zAbs), ext, th, txt, ha, va);
        else d.text(layer, P(s, zAbs), th, txt, ha, va);
    };
    DxfWriter d;
    d.addLayer("LEVEL_10CM", 9, 9);
    d.addLayer("LEVEL_50CM", 8, 18);
    d.addLayer("LEVEL_1M", 8, 25);
    d.addLayer("SECTION_IMAGE", 7, -3);
    d.addLayer("SECTION_PROFILE", 1, 50);
    d.addLayer("LEVEL_TEXT", 7, 18);
    d.addLayer("SECTION_AXIS", 7, 18);
    d.addLayer("SECTION_TITLE", 7, 25);
    if (w3) d.addLayer("SECTION_LINE_PLAN", 1, 35);

    const double s0 = 0, s1 = f.L;
    const double zlo = r.zMin + oz, zhi = r.zMax + oz;
    // 그리기 순서 = 개체 순서: ① 레벨선(맨 아래) → ② 입면 영상 → ③ 빨간 단면선 → ④ 라벨·축
    const int lab = std::max(labelStepCm(1.0, th * 1.8), std::max(50, o.levelStepCm));  // 숫자 라벨 50 cm 마다
    const auto levels = o.levels ? levelLines(zlo, zhi, o.levelStepCm) : std::vector<LevelLine>{};
    for (auto& lv : levels) {
        const char* layer = lv.cls == LevelClass::Master ? "LEVEL_1M" : lv.cls == LevelClass::Major ? "LEVEL_50CM" : "LEVEL_10CM";
        d.line(layer, P(s0, lv.z), P(s1, lv.z));
    }
    if (o.image && !o.imageFile.empty() && o.imageW > 0 && o.imageH > 0) {
        if (!w3) d.image("SECTION_IMAGE", o.imageFile, o.imageW, o.imageH, Vec3(o.imageS0, o.imageZ0 + oz, 0), Vec3(o.imageRes, 0, 0), Vec3(0, o.imageRes, 0));
        else {
            Vec3 ins = P(o.imageS0, o.imageZ0 + oz);
            d.image("SECTION_IMAGE", o.imageFile, o.imageW, o.imageH, ins, Vec3(f.u.x * o.imageRes, f.u.y * o.imageRes, 0), Vec3(0, 0, o.imageRes));
        }
    }
    for (auto& pl : r.profile) {
        // 닫힌 고리(처음 = 끝)는 마지막 점을 빼고 닫힘 플래그로(DXF 에서 중복 꼭짓점 없음)
        const bool closed = pl.size() > 3 && (pl.front() - pl.back()).len() < 1e-9;
        const size_t n = closed ? pl.size() - 1 : pl.size();
        if (!w3) {
            std::vector<Vec2> v;
            for (size_t i = 0; i < n; ++i) v.emplace_back(pl[i].x, pl[i].y + oz);
            d.polyline2d("SECTION_PROFILE", v, closed);
        } else {
            std::vector<Vec3> v;
            for (size_t i = 0; i < n; ++i) v.push_back(P(pl[i].x, pl[i].y + oz));
            d.polyline3d("SECTION_PROFILE", v, closed);
        }
    }
    for (auto& lv : levels)
        if (lv.cm % lab == 0) {
            T(d, "LEVEL_TEXT", s0 - th * 0.6, lv.z, formatElevation(lv.z), 2, 2);
            T(d, "LEVEL_TEXT", s1 + th * 0.6, lv.z, formatElevation(lv.z), 0, 2);
        }
    // 거리 축
    const double zb = zlo - th * 1.5;
    d.line("SECTION_AXIS", P(s0, zb), P(s1, zb));
    double st = niceStep(f.L, 10);
    for (long k = 0; k * st <= f.L + 1e-9; ++k) {
        double s = k * st;
        d.line("SECTION_AXIS", P(s, zb), P(s, zb - th * 0.5));
        char b[32]; std::snprintf(b, sizeof b, "%.*f", st < 1 ? 1 : 0, s);
        T(d, "SECTION_AXIS", s, zb - th * 0.8, b, 1, 3);
    }
    T(d, "SECTION_TITLE", s0, zhi + th * 1.2, "A", 1, 1);
    T(d, "SECTION_TITLE", s1, zhi + th * 1.2, "A'", 1, 1);
    char tb[256];
    std::snprintf(tb, sizeof tb, "%s  L=%.2fm  %s  1:%g", o.title.c_str(), f.L, r.srs.shortLabel().c_str(), o.scaleDenom);
    T(d, "SECTION_TITLE", (s0 + s1) / 2, zhi + th * 3.2, tb, 1, 1);
    if (w3) {
        Vec3 a = r.srs.toWorld(Vec3(r.line.a.x, r.line.a.y, r.zMin)), b = r.srs.toWorld(Vec3(r.line.b.x, r.line.b.y, r.zMin));
        d.line("SECTION_LINE_PLAN", a, b);
    }
    if (dxfText) *dxfText = d.str();
    if (out.empty()) return true;
    return d.save(out, err);
}

GeoRef planGeoRef(const SrsInfo& srs, double x0, double y1, double res) {
    GeoRef g;
    g.enabled = true;
    g.tieX = x0 + srs.origin.x; g.tieY = y1 + srs.origin.y;
    g.scaleX = g.scaleY = res;
    g.epsg = srs.epsg();
    g.vertEpsg = srs.verticalEpsg();
    g.citation = "ExcavSection plan orthophoto " + srs.shortLabel();
    return g;
}

double sectionAzimuthDeg(const SectionLine& l) {
    double a = std::atan2(l.b.x - l.a.x, l.b.y - l.a.y) * 180.0 / 3.14159265358979323846;
    return a < 0 ? a + 360 : a;
}

GeoRef sectionGeoRef(const SectionResult& r, double s0, double zTop, double res) {
    GeoRef g;
    g.enabled = true;
    g.tieX = s0; g.tieY = zTop;
    g.scaleX = g.scaleY = res;
    g.epsg = 0;
    Vec3 A = r.srs.toWorld(Vec3(r.line.a.x, r.line.a.y, 0)), B = r.srs.toWorld(Vec3(r.line.b.x, r.line.b.y, 0));
    char b[512];
    std::snprintf(b, sizeof b, "ExcavSection local section: X=distance from A (m), Y=elevation (m). %s A=(%.3f,%.3f) A'=(%.3f,%.3f) az=%.2f deg",
                  r.srs.shortLabel().c_str(), A.x, A.y, B.x, B.y, sectionAzimuthDeg(r.line));
    g.citation = b;
    return g;
}

bool writeSectionSidecar(const fs::path& p, const SectionResult& r, const GeoRef& g, double denom, double dpi, int w, int h, std::string* err) {
    using json = nlohmann::json;
    SectionFrame f(r.line);
    Vec3 A = r.srs.toWorld(Vec3(r.line.a.x, r.line.a.y, 0)), B = r.srs.toWorld(Vec3(r.line.b.x, r.line.b.y, 0));
    json j;
    j["type"] = "ExcavSection section image";
    j["srs"] = r.srs.srs;
    {
        SrsDesc sd = r.srs.describe();
        j["srs_label"] = sd.shortAscii();
        j["horizontal_epsg"] = sd.horizontalEpsg;
        j["vertical_reference"] = sd.vertKind == VertKind::Ellipsoidal ? "ellipsoidal" : sd.vertKind == VertKind::Gravity ? "gravity-related" : sd.vertKind == VertKind::LocalEnu ? "local-enu" : "unspecified";
        if (sd.verticalEpsg) j["vertical_epsg"] = sd.verticalEpsg;
        j["vertical_datum"] = vdatumKey(sd.vdatum);
        if (sd.heightDeclared) {
            j["vertical_declared_by_user"] = true;
            j["vertical_srs_original"] = sd.srsVerticalAscii;
            j["height_note"] = "Z = model values as-is (no conversion); height datum label declared by user";
        } else {
            j["height_note"] = "Z = model SRS height as-is (no geoid conversion)";
        }
    }
    j["A"] = {{"X", A.x}, {"Y", A.y}};
    j["A_prime"] = {{"X", B.x}, {"Y", B.y}};
    j["length_m"] = f.L;
    j["azimuth_deg"] = sectionAzimuthDeg(r.line);
    j["thickness_front_m"] = r.line.front;
    j["thickness_back_m"] = r.line.back;
    j["view_direction"] = "viewer looks along the left normal of A->A' (into the back band)";
    j["scale"] = "1:" + std::to_string(int(std::lround(denom)));
    j["dpi"] = dpi;
    j["pixel_size_m"] = g.scaleX;
    j["image_px"] = {w, h};
    j["pixel_to_section"] = {{"distance_m", "s = " + dxfNum(g.tieX) + " + (col + 0.5) * " + dxfNum(g.scaleX)},
                             {"elevation_m", "z = " + dxfNum(g.tieY) + " - (row + 0.5) * " + dxfNum(g.scaleY)}};
    j["section_to_world"] = "X = A.X + s*sin(az), Y = A.Y + s*cos(az), Z = z";
    std::ofstream o(p);
    if (!o) { if (err) *err = "쓸 수 없음: " + p.u8string(); return false; }
    o << j.dump(2);
    return bool(o);
}

}  // namespace asec
