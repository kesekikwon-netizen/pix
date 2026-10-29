#include "asec/sheet.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace asec {

void paperSizeMm(Paper p, bool landscape, double& w, double& h) {
    double a = p == Paper::A3 ? 420 : 297, b = p == Paper::A3 ? 297 : 210;
    w = landscape ? a : b;
    h = landscape ? b : a;
}

const char* paperName(Paper p) { return p == Paper::A3 ? "A3" : "A4"; }

SheetLayout layoutSheet(const SheetSpec& s, double lenM, double heightM) {
    SheetLayout L;
    paperSizeMm(s.paper, s.landscape, L.paperW, L.paperH);
    L.frameX = s.marginMm; L.frameY = s.marginMm;
    L.frameW = L.paperW - 2 * s.marginMm; L.frameH = L.paperH - 2 * s.marginMm;
    L.titleY = L.frameY + L.frameH - s.titleHmm;
    L.plotX = L.frameX + s.gutterLeftMm;
    L.plotY = L.frameY + s.gutterTopMm;
    L.plotW = std::max(1.0, L.frameW - s.gutterLeftMm - s.gutterRightMm);
    L.plotH = std::max(1.0, L.titleY - s.gutterBottomMm - L.plotY);
    const double k = 1000.0 / std::max(1e-9, s.denom);   // m → mm
    L.contentW = std::max(0.0, lenM) * k;
    L.contentH = std::max(0.0, heightM) * k;
    L.fitLenM = L.plotW / k;
    L.fitHeightM = L.plotH / k;
    const double tol = 1e-6;
    L.cols = std::max(1, int(std::ceil(L.contentW / L.plotW - tol)));
    L.rows = std::max(1, int(std::ceil(L.contentH / L.plotH - tol)));
    L.fits = L.cols == 1 && L.rows == 1;
    return L;
}

double fitDenominator(const SheetSpec& s, double lenM, double heightM) {
    SheetSpec t = s; t.denom = 1000;
    SheetLayout L = layoutSheet(t, 1, 1);
    double d = std::max(lenM * 1000.0 / L.plotW, heightM * 1000.0 / L.plotH);
    return std::max(1.0, std::ceil(d - 1e-9));
}

SheetAdvice adviseSheet(const SheetSpec& s, double lenM, double heightM, const std::vector<double>& stdDenoms) {
    SheetAdvice a;
    SheetLayout L = layoutSheet(s, lenM, heightM);
    a.overflow = !L.fits;
    a.fitLenM = L.fitLenM;
    a.splitCols = L.cols; a.splitRows = L.rows; a.splitSheets = L.cols * L.rows;
    if (!a.overflow) return a;
    std::vector<double> ds = stdDenoms;
    std::sort(ds.begin(), ds.end());
    for (double d : ds) {
        if (d <= s.denom) continue;
        SheetSpec t = s; t.denom = d;
        if (layoutSheet(t, lenM, heightM).fits) { a.smallerDenom = d; break; }
    }
    if (s.paper == Paper::A4) { SheetSpec t = s; t.paper = Paper::A3; a.a3Fits = layoutSheet(t, lenM, heightM).fits; }
    { SheetSpec t = s; t.landscape = !s.landscape; a.rotateFits = layoutSheet(t, lenM, heightM).fits; }
    return a;
}

std::string formatAzimuth(double az) {
    az = std::fmod(az, 360.0); if (az < 0) az += 360.0;
    char b[48];
    if (az <= 180.0) std::snprintf(b, sizeof b, "N %.1f\xC2\xB0 E", az);
    else std::snprintf(b, sizeof b, "N %.1f\xC2\xB0 W", 360.0 - az);
    return b;
}

std::string facingKo(double lineAz) {
    double f = std::fmod(lineAz - 90.0, 360.0); if (f < 0) f += 360.0;
    static const char* names[8] = {"북쪽", "북동쪽", "동쪽", "남동쪽", "남쪽", "남서쪽", "서쪽", "북서쪽"};
    int k = int(std::floor((f + 22.5) / 45.0)) % 8;
    return std::string(names[k]) + "을 봄";
}

std::string sectionLetterName(int i) {
    if (i < 0) i = 0;
    std::string base(1, char('A' + i % 26));
    if (i >= 26) base += std::to_string(i / 26);
    return base + "\xE2\x80\x93" + base + "\xE2\x80\xB2";   // – , ′
}

static std::string normPath(const std::string& p) {
    std::string r = p;
    for (auto& c : r) { c = char(std::tolower(static_cast<unsigned char>(c))); if (c == '\\') c = '/'; }
    return r;
}

std::vector<std::string> pushRecent(const std::vector<std::string>& list, const std::string& item, size_t maxN) {
    std::vector<std::string> out;
    if (!item.empty()) out.push_back(item);
    const std::string key = normPath(item);
    for (auto& x : list) {
        if (out.size() >= maxN) break;
        if (x.empty() || normPath(x) == key) continue;
        out.push_back(x);
    }
    if (out.size() > maxN) out.resize(maxN);
    return out;
}

}  // namespace asec

#include "json.hpp"
namespace asec {
double resolveBackDepth(bool hasStored, double stored, bool userSet) {
    if (!hasStored || !(stored >= 0)) return kDefaultBackDepth;
    stored = std::min(stored, kMaxBackDepth);
    if (userSet) return stored;
    if (std::fabs(stored - kLegacyDefaultBackDepth) < 1e-6) return kDefaultBackDepth;   // 옛 기본값 → 새 기본값
    return stored;
}

std::string sectionsToJson(const std::vector<SavedSection>& v, const std::string& model, const std::string& srsLabel, int current) {
    nlohmann::json j;
    j["format"] = "ExcavSection.sections";
    j["version"] = 1;
    j["model"] = model;
    j["srs"] = srsLabel;
    j["current"] = current;
    j["coords"] = "world (SRSOrigin applied), metres";
    auto& a = j["sections"] = nlohmann::json::array();
    for (auto& s : v)
        a.push_back({{"name", s.name}, {"a", {s.ax, s.ay}}, {"b", {s.bx, s.by}}, {"front", s.front}, {"back", s.back}, {"backUserSet", s.backUserSet}, {"note", s.note}});
    return j.dump(2);
}

bool sectionsFromJson(const std::string& text, std::vector<SavedSection>& out, int* current, std::string* err) {
    out.clear();
    try {
        auto j = nlohmann::json::parse(text);
        if (!j.is_object() || !j.contains("sections") || !j["sections"].is_array()) { if (err) *err = "sections 배열이 없습니다"; return false; }
        for (auto& e : j["sections"]) {
            SavedSection s;
            s.name = e.value("name", std::string());
            auto A = e.at("a"), B = e.at("b");
            s.ax = A.at(0).get<double>(); s.ay = A.at(1).get<double>();
            s.bx = B.at(0).get<double>(); s.by = B.at(1).get<double>();
            s.front = e.value("front", 0.0);
            s.backUserSet = e.value("backUserSet", false);
            s.back = resolveBackDepth(e.contains("back") && e["back"].is_number(), e.value("back", kDefaultBackDepth), s.backUserSet);
            s.note = e.value("note", std::string());
            out.push_back(s);
        }
        if (current) *current = j.value("current", out.empty() ? -1 : 0);
        return true;
    } catch (const std::exception& ex) {
        if (err) *err = ex.what();
        return false;
    }
}
}  // namespace asec
