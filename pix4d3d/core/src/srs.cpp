#include "asec/srs.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace asec {

// ---------------------------------------------------------------- WKT 파서
static bool ieq(const std::string& a, const char* b) {
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) if (std::toupper((unsigned char)a[i]) != std::toupper((unsigned char)b[i])) return false;
    return true;
}

const WktNode* WktNode::child(const char* k) const {
    for (auto& c : kids) if (!c.key.empty() && ieq(c.key, k)) return &c;
    return nullptr;
}
std::vector<const WktNode*> WktNode::children(const char* k) const {
    std::vector<const WktNode*> r;
    for (auto& c : kids) if (!c.key.empty() && ieq(c.key, k)) r.push_back(&c);
    return r;
}
std::string WktNode::str(size_t i) const {
    size_t n = 0;
    for (auto& c : kids) if (c.key.empty()) { if (n == i) return c.text; ++n; }
    return std::string();
}
double WktNode::num(size_t i, double def) const {
    std::string s = str(i);
    if (s.empty()) return def;
    char* e = nullptr;
    double v = std::strtod(s.c_str(), &e);
    return e == s.c_str() ? def : v;
}

namespace {
struct WktParser {
    const std::string& s; size_t i = 0; std::string err;
    explicit WktParser(const std::string& str) : s(str) {}
    void ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
    bool fail(const char* m) { char b[96]; std::snprintf(b, sizeof b, "WKT %s (위치 %zu)", m, i); err = b; return false; }
    bool value(WktNode& out, int depth) {
        if (depth > 64) return fail("너무 깊음");
        ws();
        if (i >= s.size()) return fail("끝이 잘림");
        if (s[i] == '"') {  // 따옴표 문자열("" = ")
            ++i;
            std::string t;
            for (;;) {
                if (i >= s.size()) return fail("따옴표가 닫히지 않음");
                if (s[i] == '"') { if (i + 1 < s.size() && s[i + 1] == '"') { t += '"'; i += 2; continue; } ++i; break; }
                t += s[i++];
            }
            out.text = t; out.quoted = true;
            return true;
        }
        size_t b = i;
        while (i < s.size() && s[i] != ',' && s[i] != '[' && s[i] != ']' && s[i] != '(' && s[i] != ')' && !std::isspace((unsigned char)s[i])) ++i;
        std::string tok = s.substr(b, i - b);
        ws();
        if (i < s.size() && (s[i] == '[' || s[i] == '(')) {
            if (tok.empty()) return fail("키워드 없음");
            char close = s[i] == '[' ? ']' : ')';
            ++i;
            out.key = tok;
            for (auto& c : out.key) c = char(std::toupper((unsigned char)c));
            ws();
            if (i < s.size() && s[i] == close) { ++i; return true; }
            for (;;) {
                WktNode k;
                if (!value(k, depth + 1)) return false;
                out.kids.push_back(std::move(k));
                ws();
                if (i >= s.size()) return fail("괄호가 닫히지 않음");
                if (s[i] == ',') { ++i; continue; }
                if (s[i] == close) { ++i; return true; }
                return fail("예상하지 못한 문자");
            }
        }
        if (tok.empty()) return fail("빈 값");
        out.text = tok;
        return true;
    }
};
}  // namespace

bool parseWkt(const std::string& s, WktNode& out, std::string* err) {
    WktParser p(s);
    out = WktNode();
    if (!p.value(out, 0) || out.key.empty()) { if (err) *err = p.err.empty() ? "WKT 아님" : p.err; return false; }
    p.ws();
    if (p.i != s.size()) { if (err) *err = "WKT 뒤에 남은 문자"; return false; }
    return true;
}

// ---------------------------------------------------------------- 내장 표
namespace {
struct HorizEntry { int epsg; const char* name; const char* altName; double lat0, lon0, k0, FE, FN; double a, invF; };
constexpr double GRS80_A = 6378137.0, GRS80_INVF = 298.257222101, WGS84_INVF = 298.257223563;
const HorizEntry kHoriz[] = {
    {5179, "Korea 2000 / Unified CS", "KGD2002 / Unified CS", 38, 127.5, 0.9996, 1000000, 2000000, GRS80_A, GRS80_INVF},
    {5180, "Korea 2000 / West Belt", "KGD2002 / West Belt", 38, 125, 1, 200000, 500000, GRS80_A, GRS80_INVF},
    {5181, "Korea 2000 / Central Belt", "KGD2002 / Central Belt", 38, 127, 1, 200000, 500000, GRS80_A, GRS80_INVF},
    {5182, "Korea 2000 / Central Belt Jeju", "KGD2002 / Central Belt Jeju", 38, 127, 1, 200000, 550000, GRS80_A, GRS80_INVF},
    {5183, "Korea 2000 / East Belt", "KGD2002 / East Belt", 38, 129, 1, 200000, 500000, GRS80_A, GRS80_INVF},
    {5184, "Korea 2000 / East Sea Belt", "KGD2002 / East Sea Belt", 38, 131, 1, 200000, 500000, GRS80_A, GRS80_INVF},
    {5185, "Korea 2000 / West Belt 2010", "KGD2002 / West Belt 2010", 38, 125, 1, 200000, 600000, GRS80_A, GRS80_INVF},
    {5186, "Korea 2000 / Central Belt 2010", "KGD2002 / Central Belt 2010", 38, 127, 1, 200000, 600000, GRS80_A, GRS80_INVF},
    {5187, "Korea 2000 / East Belt 2010", "KGD2002 / East Belt 2010", 38, 129, 1, 200000, 600000, GRS80_A, GRS80_INVF},
    {5188, "Korea 2000 / East Sea Belt 2010", "KGD2002 / East Sea Belt 2010", 38, 131, 1, 200000, 600000, GRS80_A, GRS80_INVF},
    {32651, "WGS 84 / UTM zone 51N", "", 0, 123, 0.9996, 500000, 0, GRS80_A, WGS84_INVF},
    {32652, "WGS 84 / UTM zone 52N", "", 0, 129, 0.9996, 500000, 0, GRS80_A, WGS84_INVF},
};
// TM 매개변수가 없는(또는 이 프로그램이 계산하지 않는) 이름표
struct NameEntry { int epsg; const char* name; };
const NameEntry kOtherHoriz[] = {
    {5174, "Korean 1985 / Modified Central Belt (Bessel)"}, {2097, "Korean 1985 / Central Belt (Bessel)"},
    {4737, "KGD2002 (geographic 2D)"}, {4326, "WGS 84 (geographic 2D)"},
    {4927, "KGD2002 (geographic 3D)"}, {4979, "WGS 84 (geographic 3D)"}, {4937, "ETRS89 (geographic 3D)"},
};
const NameEntry kVert[] = {
    {5193, "KVD1964 height"}, {5703, "NAVD88 height"}, {5773, "EGM96 height"}, {3855, "EGM2008 height"},
    {5714, "MSL height"}, {5711, "AHD height"}, {6695, "JGD2011 vertical height"},
};
bool is3dGeographic(int e) { return e == 4927 || e == 4979 || e == 4937; }

std::string norm(const std::string& s) {
    std::string r;
    for (unsigned char c : s) if (std::isalnum(c)) r += char(std::tolower(c));
    return r;
}
const HorizEntry* findHoriz(int epsg) { for (auto& h : kHoriz) if (h.epsg == epsg) return &h; return nullptr; }
const HorizEntry* findHorizByName(const std::string& name) {
    std::string n = norm(name);
    if (n.empty()) return nullptr;
    for (auto& h : kHoriz) if (n == norm(h.name) || (h.altName[0] && n == norm(h.altName))) return &h;
    return nullptr;
}
int epsgFromText(const std::string& s) {  // "...EPSG:5186..." / "EPSG::5186"
    auto p = s.find("EPSG:");
    if (p == std::string::npos) return 0;
    p += 5;
    while (p < s.size() && (s[p] == ':' || s[p] == ' ')) ++p;
    return std::atoi(s.c_str() + p);
}
}  // namespace

const char* epsgName(int e) {
    if (auto* h = findHoriz(e)) return h->altName[0] ? h->altName : h->name;
    for (auto& n : kOtherHoriz) if (n.epsg == e) return n.name;
    return "";
}
const char* verticalEpsgName(int e) {
    for (auto& n : kVert) if (n.epsg == e) return n.name;
    return "";
}

bool tmParamsForEpsg(int epsg, TmParams& o) {
    auto* h = findHoriz(epsg);
    if (!h) return false;
    o = TmParams{h->a, h->invF, h->lat0, h->lon0, h->k0, h->FE, h->FN};
    return true;
}

// ---------------------------------------------------------------- TM (Krüger, Karney 2011 6차)
namespace {
struct TmCoef { double n, A, e, al[7], be[7]; };
TmCoef tmCoef(double a, double invF) {
    TmCoef c{};
    double f = 1.0 / invF, n = f / (2 - f), n2 = n * n, n3 = n2 * n, n4 = n3 * n, n5 = n4 * n, n6 = n5 * n;
    c.n = n; c.e = std::sqrt(f * (2 - f));
    c.A = a / (1 + n) * (1 + n2 / 4 + n4 / 64 + n6 / 256);
    c.al[1] = n / 2 - 2 * n2 / 3 + 5 * n3 / 16 + 41 * n4 / 180 - 127 * n5 / 288 + 7891 * n6 / 37800;
    c.al[2] = 13 * n2 / 48 - 3 * n3 / 5 + 557 * n4 / 1440 + 281 * n5 / 630 - 1983433 * n6 / 1935360;
    c.al[3] = 61 * n3 / 240 - 103 * n4 / 140 + 15061 * n5 / 26880 + 167603 * n6 / 181440;
    c.al[4] = 49561 * n4 / 161280 - 179 * n5 / 168 + 6601661 * n6 / 7257600;
    c.al[5] = 34729 * n5 / 80640 - 3418889 * n6 / 1995840;
    c.al[6] = 212378941 * n6 / 319334400;
    c.be[1] = n / 2 - 2 * n2 / 3 + 37 * n3 / 96 - n4 / 360 - 81 * n5 / 512 + 96199 * n6 / 604800;
    c.be[2] = n2 / 48 + n3 / 15 - 437 * n4 / 1440 + 46 * n5 / 105 - 1118711 * n6 / 3870720;
    c.be[3] = 17 * n3 / 480 - 37 * n4 / 840 - 209 * n5 / 4480 + 5569 * n6 / 90720;
    c.be[4] = 4397 * n4 / 161280 - 11 * n5 / 504 - 830251 * n6 / 7257600;
    c.be[5] = 4583 * n5 / 161280 - 108847 * n6 / 3991680;
    c.be[6] = 20648693 * n6 / 638668800;
    return c;
}
constexpr double D2R = 3.14159265358979323846 / 180.0;
void fwdRaw(const TmCoef& c, double lat, double dlon, double& xi, double& eta) {
    double s = std::sin(lat);
    double t = std::sinh(std::atanh(s) - c.e * std::atanh(c.e * s));
    double xp = std::atan2(t, std::cos(dlon)), ep = std::atanh(std::sin(dlon) / std::sqrt(1 + t * t));
    xi = xp; eta = ep;
    for (int j = 1; j <= 6; ++j) { xi += c.al[j] * std::sin(2 * j * xp) * std::cosh(2 * j * ep); eta += c.al[j] * std::cos(2 * j * xp) * std::sinh(2 * j * ep); }
}
}  // namespace

void tmForward(const TmParams& p, double latDeg, double lonDeg, double& E, double& N) {
    TmCoef c = tmCoef(p.a, p.invF);
    double xi, eta, xi0, eta0;
    fwdRaw(c, latDeg * D2R, (lonDeg - p.lon0) * D2R, xi, eta);
    fwdRaw(c, p.lat0 * D2R, 0, xi0, eta0);
    E = p.FE + p.k0 * c.A * eta;
    N = p.FN + p.k0 * c.A * (xi - xi0);
}

void tmInverse(const TmParams& p, double E, double N, double& latDeg, double& lonDeg) {
    TmCoef c = tmCoef(p.a, p.invF);
    double xi0, eta0;
    fwdRaw(c, p.lat0 * D2R, 0, xi0, eta0);
    double xi = (N - p.FN) / (p.k0 * c.A) + xi0, eta = (E - p.FE) / (p.k0 * c.A);
    double xp = xi, ep = eta;
    for (int j = 1; j <= 6; ++j) { xp -= c.be[j] * std::sin(2 * j * xi) * std::cosh(2 * j * eta); ep -= c.be[j] * std::cos(2 * j * xi) * std::sinh(2 * j * eta); }
    double chi = std::asin(std::sin(xp) / std::cosh(ep));
    double lat = chi;
    for (int it = 0; it < 20; ++it) {  // 등각위도 → 측지위도
        double s = std::sin(lat);
        double nl = 2 * std::atan(std::tan(D2R * 45 + chi / 2) * std::pow((1 + c.e * s) / (1 - c.e * s), c.e / 2)) - D2R * 90;
        if (std::fabs(nl - lat) < 1e-14) { lat = nl; break; }
        lat = nl;
    }
    latDeg = lat / D2R;
    lonDeg = p.lon0 + std::atan2(std::sinh(ep), std::cos(xp)) / D2R;
}

// ---------------------------------------------------------------- 해석
namespace {
bool isProj(const std::string& k) { return k == "PROJCRS" || k == "PROJECTEDCRS" || k == "PROJCS"; }
bool isGeog(const std::string& k) { return k == "GEOGCRS" || k == "GEOGRAPHICCRS" || k == "GEOGCS" || k == "GEODCRS" || k == "GEODETICCRS" || k == "BASEGEOGCRS"; }
bool isVert(const std::string& k) { return k == "VERTCRS" || k == "VERTICALCRS" || k == "VERT_CS"; }
bool isCompound(const std::string& k) { return k == "COMPOUNDCRS" || k == "COMPD_CS"; }
bool isCrs(const std::string& k) { return isProj(k) || isGeog(k) || isVert(k) || isCompound(k) || k == "GEOCCS" || k == "BOUNDCRS" || k == "ENGCRS" || k == "LOCAL_CS"; }

int directEpsg(const WktNode& n) {  // 이 CRS 자체의 ID/AUTHORITY(하위 CONVERSION·DATUM 의 ID 는 보지 않음)
    for (auto* id : n.children("ID")) if (ieq(id->str(0), "EPSG")) return std::atoi(id->str(1).c_str());
    for (auto* id : n.children("AUTHORITY")) if (ieq(id->str(0), "EPSG")) return std::atoi(id->str(1).c_str());
    return 0;
}
const WktNode* findEllipsoid(const WktNode& n) {
    for (auto& c : n.kids) {
        if (c.key == "ELLIPSOID" || c.key == "SPHEROID") return &c;
        if (!c.key.empty()) if (auto* r = findEllipsoid(c)) return r;
    }
    return nullptr;
}
std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower((unsigned char)c)); return s; }
const WktNode* findDeep(const WktNode& n, const char* key) {
    for (auto& c : n.kids) {
        if (c.key == key) return &c;
        if (!c.key.empty()) if (auto* r = findDeep(c, key)) return r;
    }
    return nullptr;
}
// BOUNDCRS 이면 SOURCECRS 안의 CRS(아니면 그대로)
const WktNode* unwrapBound(const WktNode* n) {
    if (!n || n->key != "BOUNDCRS") return n;
    if (const WktNode* src = n->child("SOURCECRS"))
        for (auto& k : src->kids) if (isCrs(k.key)) return &k;
    return n;
}
// 수직 좌표계(VERTCRS/VERT_CS, wrapper = 감싼 BOUNDCRS 또는 자신): 이름·EPSG·VDATUM·GEOIDMODEL·격자 파일
void readVertical(const WktNode& v, const WktNode& wrapper, SrsDesc& d) {
    d.vertKind = VertKind::Gravity;
    d.verticalName = v.str(0);
    d.verticalEpsg = directEpsg(v);
    const WktNode* dat = v.child("VDATUM"); if (!dat) dat = v.child("VERT_DATUM"); if (!dat) dat = v.child("VERTICALDATUM");
    if (dat) d.verticalDatum = dat->str(0);
    if (const WktNode* gm = v.child("GEOIDMODEL")) d.geoidModel = gm->str(0);
    if (d.geoidModel.empty() && dat)
        for (auto* ex : dat->children("EXTENSION")) if (ieq(ex->str(0), "PROJ4_GRIDS")) d.geoidModel = ex->str(1);
    if (d.geoidModel.empty())
        if (const WktNode* pf = findDeep(wrapper, "PARAMETERFILE")) d.geoidModel = pf->str(1);
}

void readTm(const WktNode& n, SrsDesc& d) {
    // WKT2: CONVERSION[ ..., METHOD["Transverse Mercator"], PARAMETER[...] ] / WKT1: PROJECTION["Transverse_Mercator"], PARAMETER[...]
    const WktNode* conv = n.child("CONVERSION");
    const WktNode* holder = conv ? conv : &n;
    std::string method = conv ? (conv->child("METHOD") ? conv->child("METHOD")->str(0) : "") : (n.child("PROJECTION") ? n.child("PROJECTION")->str(0) : "");
    std::string m = norm(method);
    if (m != "transversemercator") return;
    bool got[5] = {};
    for (auto* p : holder->children("PARAMETER")) {
        std::string k = norm(p->str(0));
        double v = p->num(1);
        if (k == "latitudeofnaturalorigin" || k == "latitudeoforigin") { d.tmLat0 = v; got[0] = true; }
        else if (k == "longitudeofnaturalorigin" || k == "centralmeridian") { d.tmLon0 = v; got[1] = true; }
        else if (k == "scalefactoratnaturalorigin" || k == "scalefactor") { d.tmK0 = v; got[2] = true; }
        else if (k == "falseeasting") { d.tmFE = v; got[3] = true; }
        else if (k == "falsenorthing") { d.tmFN = v; got[4] = true; }
    }
    if (!got[2]) d.tmK0 = 1;
    d.hasTm = got[1] && got[3] && got[4];
    if (auto* el = findEllipsoid(n)) { d.ellipsoid = el->str(0); d.tmA = el->num(1, 6378137.0); d.tmInvF = el->num(2, 298.257222101); }
}

bool tmMatches(const SrsDesc& d, const HorizEntry& h) {
    return std::fabs(d.tmLat0 - h.lat0) < 1e-7 && std::fabs(d.tmLon0 - h.lon0) < 1e-7 && std::fabs(d.tmK0 - h.k0) < 1e-9 &&
           std::fabs(d.tmFE - h.FE) < 1e-3 && std::fabs(d.tmFN - h.FN) < 1e-3 && std::fabs(d.tmA - h.a) < 1e-3 && std::fabs(d.tmInvF - h.invF) < 1e-6;
}

void readAxes(const WktNode& n, SrsDesc& d, bool projected) {
    auto axes = n.children("AXIS");
    if (!axes.empty()) {
        std::string dir0 = lower(axes[0]->str(1)), nm0 = lower(axes[0]->str(0));
        d.axisNorthFirst = dir0 == "north" || nm0.find("north") == 0;
    }
    for (auto* a : axes) {
        std::string nm = lower(a->str(0)), dir = lower(a->str(1));
        if (nm.find("ellipsoidal height") != std::string::npos || (dir == "up" && (nm == "h" || nm.find("(h)") != std::string::npos))) {
            d.vertKind = VertKind::Ellipsoidal; d.verticalName = a->str(0);
        } else if (nm.find("gravity-related") != std::string::npos) {
            d.vertKind = VertKind::Gravity; d.verticalName = a->str(0);
        }
    }
    const WktNode* cs = n.child("CS");
    if (cs && d.vertKind == VertKind::Unspecified && std::atoi(cs->str(1).c_str()) == 3 && (projected || ieq(cs->str(0), "ellipsoidal"))) {
        // 3D 인데 축 이름으로 못 정함 → 3번째 축이 위쪽이면 타원체고로 본다(WKT2 3D 투영/지리 좌표계의 정의)
        if (axes.size() >= 3 && lower(axes[2]->str(1)) == "up") { d.vertKind = VertKind::Ellipsoidal; d.verticalName = axes[2]->str(0); }
    }
}

void readBbox(const WktNode& n, SrsDesc& d) {
    const WktNode* bb = n.child("BBOX");
    if (!bb) if (auto* u = n.child("USAGE")) bb = u->child("BBOX");
    if (bb) { d.bboxS = bb->num(0); d.bboxW = bb->num(1); d.bboxN = bb->num(2); d.bboxE = bb->num(3); d.hasBbox = true; }
}

void readHorizontal(const WktNode& n, SrsDesc& d) {
    d.horizontalName = n.str(0);
    readBbox(n, d);
    if (isProj(n.key)) {
        readTm(n, d);
        readAxes(n, d, true);
        if (n.key == "PROJCS" && !d.ellipsoid.empty()) {}  // WKT1 은 2D(축 2개)가 보통
        int e = directEpsg(n);
        if (e) { d.horizontalEpsg = e; d.horizontalHow = "WKT ID"; }
        if (!e) {
            const WktNode* rem = n.child("REMARK");
            if (rem && lower(rem->str(0)).find("promoted to 3d") != std::string::npos) d.promotedTo3D = true;
            int r = rem ? epsgFromText(rem->str(0)) : 0;
            if (r && findHoriz(r)) { d.horizontalEpsg = r; d.horizontalHow = "WKT REMARK"; e = r; }
        }
        if (const WktNode* rem2 = n.child("REMARK")) if (lower(rem2->str(0)).find("promoted to 3d") != std::string::npos) d.promotedTo3D = true;
        if (!e) if (auto* h = findHorizByName(d.horizontalName)) { d.horizontalEpsg = h->epsg; d.horizontalHow = "이름"; e = h->epsg; }
        if (!e && d.hasTm) for (auto& h : kHoriz) if (tmMatches(d, h)) { d.horizontalEpsg = h.epsg; d.horizontalHow = "매개변수"; e = h.epsg; break; }
        if (e && d.hasTm) {
            if (auto* h = findHoriz(e)) if (!tmMatches(d, *h))
                d.warnings.push_back("WKT 의 투영 매개변수가 EPSG:" + std::to_string(e) + " 내장 값과 다릅니다(원점·가산값 확인 필요).");
        }
        if (!d.hasTm) if (auto* h = findHoriz(e)) {
            d.hasTm = true; d.tmLat0 = h->lat0; d.tmLon0 = h->lon0; d.tmK0 = h->k0; d.tmFE = h->FE; d.tmFN = h->FN; d.tmA = h->a; d.tmInvF = h->invF;
        }
        if (!e) d.warnings.push_back("투영 좌표계 \"" + d.horizontalName + "\" 의 EPSG 번호를 알아내지 못했습니다(원문 WKT 는 그대로 사용).");
        if (e == 5174 || e == 2097) d.warnings.push_back("구 좌표계(Bessel, 가산 북 500000)입니다. 현행 EPSG:5186 과 수백 m 다릅니다.");
    } else if (isGeog(n.key)) {
        readAxes(n, d, false);
        if (auto* el = findEllipsoid(n)) d.ellipsoid = el->str(0);
        d.horizontalEpsg = directEpsg(n);
        if (d.horizontalEpsg) d.horizontalHow = "WKT ID";
        d.warnings.push_back("지리 좌표계(경위도)입니다. 거리·단면 계산은 투영 좌표(m)가 필요합니다.");
    } else {
        d.warnings.push_back("지원하지 않는 좌표계 종류: " + n.key);
    }
}
}  // namespace

static SrsDesc describeSrsRaw(const std::string& srs0) {
    SrsDesc d;
    std::string s = srs0;
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    size_t b = 0;
    while (b < s.size() && std::isspace((unsigned char)s[b])) ++b;
    s = s.substr(b);
    if (s.empty()) {
        d.kind = SrsKind::None;
        d.warnings.push_back("좌표계(SRS)가 없습니다. 좌표는 로컬값이며 실좌표가 아닙니다.");
        return d;
    }
    std::string up = s.substr(0, 5);
    for (auto& c : up) c = char(std::toupper((unsigned char)c));
    if (up == "EPSG:") {
        const char* p = s.c_str() + 5;
        char* e = nullptr;
        long h = std::strtol(p, &e, 10);
        if (e == p || h <= 0) { d.kind = SrsKind::Unknown; d.warnings.push_back("EPSG 번호를 읽을 수 없습니다: " + s); return d; }
        while (*e == ' ') ++e;
        long v = 0;
        if (*e == '+') {
            ++e; while (*e == ' ') ++e;
            if (std::strncmp(e, "EPSG:", 5) == 0 || std::strncmp(e, "epsg:", 5) == 0) e += 5;
            char* e2 = nullptr;
            v = std::strtol(e, &e2, 10);
            if (e2 == e || v <= 0) {
                // iTwin 등 글자 표기 "EPSG:5186+EGM96" / "+KNGeoid18" — 이름으로 높이 기준 판별
                std::string vt = e;
                while (!vt.empty() && std::isspace((unsigned char)vt.back())) vt.pop_back();
                VDatum vd = vdatumFromText(vt);
                if (vt.empty() || vd == VDatum::Unknown || vd == VDatum::Ellipsoidal) {
                    d.kind = SrsKind::Unknown; d.warnings.push_back("복합 좌표계의 수직 기준을 읽을 수 없습니다: " + s); return d;
                }
                v = vdatumInfo(vd).epsg;
                d.verticalName = vt;
                if (!v) { d.vertKind = VertKind::Gravity; }
            }
        }
        d.kind = (v || d.vertKind == VertKind::Gravity) ? SrsKind::CompoundEpsg : SrsKind::Epsg;
        d.horizontalEpsg = int(h); d.horizontalHow = "EPSG 코드";
        d.horizontalName = epsgName(int(h));
        TmParams tp;
        if (tmParamsForEpsg(int(h), tp)) { d.hasTm = true; d.tmLat0 = tp.lat0; d.tmLon0 = tp.lon0; d.tmK0 = tp.k0; d.tmFE = tp.FE; d.tmFN = tp.FN; d.tmA = tp.a; d.tmInvF = tp.invF; d.ellipsoid = tp.invF == GRS80_INVF ? "GRS 1980" : "WGS 84"; d.axisNorthFirst = h >= 5179 && h <= 5188; }
        if (v) { d.verticalEpsg = int(v); d.vertKind = VertKind::Gravity; if (d.verticalName.empty() || verticalEpsgName(int(v))[0]) d.verticalName = verticalEpsgName(int(v)); }
        else if (is3dGeographic(int(h))) { d.vertKind = VertKind::Ellipsoidal; d.verticalName = "ellipsoidal height"; }
        if (d.horizontalName.empty()) d.warnings.push_back("EPSG:" + std::to_string(h) + " 는 내장 이름표에 없습니다(번호는 그대로 사용).");
        if (h == 5174 || h == 2097) d.warnings.push_back("구 좌표계(Bessel, 가산 북 500000)입니다. 현행 EPSG:5186 과 수백 m 다릅니다.");
        return d;
    }
    if (up.substr(0, 4) == "ENU:") {
        double la = 0, lo = 0;
        if (std::sscanf(s.c_str() + 4, " %lf , %lf", &la, &lo) == 2 && std::fabs(la) <= 90 && std::fabs(lo) <= 180) {
            d.kind = SrsKind::Enu; d.enuLat = la; d.enuLon = lo; d.vertKind = VertKind::LocalEnu;
            d.horizontalName = "Local ENU"; d.verticalName = "ENU up";
            d.warnings.push_back("로컬 ENU 좌표계입니다. 투영 좌표(EPSG)가 아니므로 GeoTIFF·SHP·3D DXF 실좌표로 쓸 수 없습니다.");
        } else {
            d.kind = SrsKind::Unknown;
            d.warnings.push_back("ENU 원점(위도,경도)을 읽을 수 없습니다: " + s);
        }
        return d;
    }
    WktNode root;
    std::string perr;
    if (!parseWkt(s, root, &perr) || !isCrs(root.key)) {
        d.kind = SrsKind::Unknown;
        d.warnings.push_back("알 수 없는 좌표계 표기입니다" + (perr.empty() ? std::string() : " (" + perr + ")") + ". 원문: " + s.substr(0, 120));
        return d;
    }
    d.kind = SrsKind::Wkt;
    const WktNode* top = &root;
    if (top->key == "BOUNDCRS") {
        const WktNode* src = top->child("SOURCECRS");
        const WktNode* inner = nullptr;
        if (src) for (auto& k : src->kids) if (isCrs(k.key)) { inner = &k; break; }
        if (!inner) { d.kind = SrsKind::Unknown; d.warnings.push_back("BOUNDCRS 의 SOURCECRS 를 찾지 못했습니다."); return d; }
        top = inner;
    }
    if (isCompound(top->key)) {
        std::vector<const WktNode*> comps;
        for (auto& k : top->kids) if (isCrs(k.key)) comps.push_back(&k);
        const WktNode* h = nullptr; const WktNode* v = nullptr; const WktNode* vw = nullptr;
        for (auto* c0 : comps) { const WktNode* c = unwrapBound(c0); if (isVert(c->key)) { if (!v) { v = c; vw = c0; } } else if (!h) h = c; }
        if (h) readHorizontal(*h, d);
        else d.warnings.push_back("복합 좌표계에 수평 좌표계가 없습니다.");
        readBbox(*top, d);
        int ce = directEpsg(*top);
        (void)ce;
        if (v) readVertical(*v, *vw, d);
        if (d.horizontalName.empty()) d.horizontalName = top->str(0);
    } else if (isVert(top->key)) {
        readVertical(*top, root, d);
        d.warnings.push_back("수직 좌표계만 있고 수평 좌표계가 없습니다.");
    } else {
        readHorizontal(*top, d);
    }
    return d;
}

// 높이 기준 판별(EPSG → 이름·VDATUM·지오이드 모델 글자). 값은 바꾸지 않음
SrsDesc describeSrs(const std::string& srs) {
    SrsDesc d = describeSrsRaw(srs);
    switch (d.vertKind) {
    case VertKind::Unspecified: d.vdatum = VDatum::None; break;
    case VertKind::Ellipsoidal: d.vdatum = VDatum::Ellipsoidal; break;
    case VertKind::LocalEnu: d.vdatum = VDatum::LocalEnu; break;
    case VertKind::Gravity: {
        VDatum v = vdatumFromEpsg(d.verticalEpsg);
        for (const std::string* t : {&d.verticalName, &d.verticalDatum, &d.geoidModel})
            if (v == VDatum::Unknown || v == VDatum::KVD1964) {
                VDatum w = vdatumFromText(*t);
                // KNGeoid 격자로 정의된 높이는 KVD1964 보다 구체적인 정보이므로 우선
                if (w != VDatum::Unknown && w != VDatum::Ellipsoidal && (v == VDatum::Unknown || w == VDatum::KNGeoid)) v = w;
            }
        d.vdatum = v;
        if (!d.verticalEpsg && vdatumInfo(v).epsg) d.verticalEpsg = vdatumInfo(v).epsg;
        if (v == VDatum::Unknown)
            d.warnings.push_back("높이 기준(" + (d.verticalName.empty() ? std::string("이름 없음") : d.verticalName) + ")을 내장 표에서 알아보지 못했습니다. 이름 그대로 표시합니다.");
        break;
    }
    }
    return d;
}

void applyHeightDeclaration(SrsDesc& d, VDatum v) {
    if (v == VDatum::None || v == VDatum::Unknown || v == VDatum::LocalEnu) return;
    d.srsVerticalKo = d.verticalKo();
    {
        std::string a = d.shortAscii(), h = d.horizontalEpsg ? "EPSG:" + std::to_string(d.horizontalEpsg) : std::string();
        if (!h.empty() && a.compare(0, h.size(), h) == 0) a = a.substr(h.size());
        while (!a.empty() && (a[0] == ' ' || a[0] == '+')) a.erase(0, 1);
        if (d.vertKind == VertKind::Gravity && d.verticalEpsg) a = "EPSG:" + std::to_string(d.verticalEpsg);
        d.srsVerticalAscii = a.empty() ? "unspecified" : a;
    }
    const VDatumInfo& in = vdatumInfo(v);
    d.vdatum = v;
    d.heightDeclared = true;
    d.verticalDatum.clear(); d.geoidModel.clear();
    if (v == VDatum::Ellipsoidal) {
        d.vertKind = VertKind::Ellipsoidal; d.verticalEpsg = 0; d.verticalName = "ellipsoidal height";
        if (d.ellipsoid.empty()) d.ellipsoid = "GRS 1980";
    } else {
        d.vertKind = VertKind::Gravity; d.verticalEpsg = in.epsg;
        const char* n = in.epsg ? verticalEpsgName(in.epsg) : "";
        d.verticalName = n[0] ? n : in.shortName;
    }
}

// ---------------------------------------------------------------- 표시
static std::string shortEllipsoid(const std::string& e) {
    std::string n = norm(e);
    if (n == "grs1980" || n == "grs80") return "GRS80";
    if (n == "wgs84" || n == "wgs1984") return "WGS84";
    if (n.find("bessel") != std::string::npos) return "Bessel";
    return e;
}

static std::string verticalKoBase(const SrsDesc& d);
std::string SrsDesc::verticalKo() const {
    std::string b = verticalKoBase(*this);
    return heightDeclared ? b + " [사용자 지정]" : b;
}
static std::string verticalKoBase(const SrsDesc& self) {
    const auto& vertKind = self.vertKind; const auto& ellipsoid = self.ellipsoid; const auto& vdatum = self.vdatum;
    const auto& verticalName = self.verticalName; const auto& verticalEpsg = self.verticalEpsg; const auto& geoidModel = self.geoidModel;
    switch (vertKind) {
    case VertKind::Ellipsoidal: return "타원체고" + (ellipsoid.empty() ? std::string() : "(" + shortEllipsoid(ellipsoid) + ")");
    case VertKind::Gravity: {
        std::string n = vdatum != VDatum::Unknown && vdatum != VDatum::None ? std::string(vdatumInfo(vdatum).nameKo)
                        : verticalName.empty() ? std::string("수직 좌표계") : verticalName;
        std::string extra;
        if (verticalEpsg) extra = "EPSG:" + std::to_string(verticalEpsg);
        if (vdatum == VDatum::KNGeoid) { std::string m = !geoidModel.empty() ? geoidModel : verticalName; if (!m.empty() && m != "KNGeoid") extra = extra.empty() ? m : extra + ", " + m; }
        if (!extra.empty()) n += " (" + extra + ")";
        return n;
    }
    case VertKind::LocalEnu: return "ENU 로컬 높이(원점 접평면 기준)";
    default: return "SRS 에 명시 안 됨";
    }
}

std::string SrsDesc::labelKo() const {
    switch (kind) {
    case SrsKind::None: return "좌표계 없음(로컬 좌표)";
    case SrsKind::Unknown: return "좌표계 미상(해석 불가)";
    case SrsKind::Enu: {
        char b[96]; std::snprintf(b, sizeof b, "로컬 ENU(%.6f, %.6f)", enuLat, enuLon);
        return std::string(b) + " / 높이 " + verticalKo();
    }
    default: break;
    }
    std::string h = horizontalEpsg ? "수평 EPSG:" + std::to_string(horizontalEpsg) : "수평 " + (horizontalName.empty() ? std::string("?") : horizontalName) + "(EPSG 미확인)";
    return h + " / 높이 " + verticalKo();
}

std::string SrsDesc::shortAscii() const {
    char b[160];
    switch (kind) {
    case SrsKind::None: return "no SRS (local)";
    case SrsKind::Unknown: return "unknown SRS";
    case SrsKind::Enu: std::snprintf(b, sizeof b, "ENU:%.6f,%.6f", enuLat, enuLon); return b;
    default: break;
    }
    std::string h = horizontalEpsg ? "EPSG:" + std::to_string(horizontalEpsg) : (horizontalName.empty() ? std::string("WKT") : horizontalName);
    std::string r = h;
    if (vertKind == VertKind::Gravity) r = verticalEpsg ? h + "+" + std::to_string(verticalEpsg) : h + " + " + verticalName;
    else if (vertKind == VertKind::Ellipsoidal) r = h + " h=ellipsoidal" + (ellipsoid.empty() ? std::string() : "(" + shortEllipsoid(ellipsoid) + ")");
    if (heightDeclared) r += " [height datum declared by user, values not converted; SRS: " + srsVerticalAscii + "]";
    return r;
}

std::string SrsDesc::tooltipKo() const {
    std::string t;
    if (!horizontalName.empty()) t += "수평 좌표계: " + horizontalName + "\n";
    if (horizontalEpsg) t += "EPSG:" + std::to_string(horizontalEpsg) + " (판별 근거: " + horizontalHow + ")\n";
    t += "높이 기준: " + verticalKo() + "\n";
    if (!verticalDatum.empty()) t += "수직 측지 기준: " + verticalDatum + "\n";
    if (!geoidModel.empty()) t += "지오이드 모델/격자: " + geoidModel + "\n";
    if (hasTm) {
        char b[200];
        std::snprintf(b, sizeof b, "TM: 원점 위도 %.4f°, 중앙자오선 %.4f°, 축척 %.4f, 가산 동 %.0f / 북 %.0f\n", tmLat0, tmLon0, tmK0, tmFE, tmFN);
        t += b;
    }
    if (axisNorthFirst) t += "SRS 공식 축 순서는 북(N)→동(E)이지만, 3MX 좌표는 x=동(E), y=북(N) 으로 저장됩니다. 화면 X=동, Y=북.\n";
    t += "높이는 모델 SRS 값 그대로 표시합니다(지오이드·정표고 자동 변환 없음).\n";
    if (heightDeclared)
        t += "높이 기준을 사용자가 '" + std::string(vdatumInfo(vdatum).nameKo) + "'(으)로 지정했습니다 — 이름표만 바뀌고 값은 그대로입니다. SRS 원래 표기: " + srsVerticalKo + "\n";
    if (vertKind == VertKind::Ellipsoidal && !heightDeclared)
        t += "주의: iTwin/ContextCapture 는 GCP 높이를 그대로 쓰면서도 3D 좌표계의 높이를 '타원체고'로 표기합니다. "
             "측량값(예: EGM96 정표고)을 그대로 넣었다면 Z는 그 측량 높이 기준입니다 — 「높이 기준 지정」으로 이름표를 맞추세요. "
             "확실하지 않으면 기준점과 대조하세요(한국에서 타원체고와 정표고 차는 약 20–30 m).\n";
    if (vdatum == VDatum::EGM96 || vdatum == VDatum::EGM2008)
        t += "참고: 전 지구 지오이드(" + std::string(vdatumInfo(vdatum).shortName) + ") 기준 높이는 한국 정표고(KVD1964, 인천만 평균해수면)와 같지 않습니다. 기준점과 대조하세요.\n";
    if (vertKind == VertKind::Unspecified && known())
        t += "주의: 높이 기준이 SRS 에 없습니다. ContextCapture/iTwin 기본은 타원체고(Bentley 문서) — 기준점과 대조해 확인하세요.\n";
    for (auto& w : warnings) t += "⚠ " + w + "\n";
    if (!t.empty() && t.back() == '\n') t.pop_back();
    return t;
}

}  // namespace asec

// ---------------------------------------------------------------- 모델 점검(SrsInfo + 로컬 상자)
#include "asec/tmx.hpp"

namespace asec {

double float32Step(double v) {
    v = std::fabs(v);
    if (v < 1e-30) return 0;
    int e;
    std::frexp(v, &e);           // v = m·2^e, m∈[0.5,1)
    return std::ldexp(1.0, e - 24);  // float32 가수 24비트
}

static bool inBox(const SrsDesc& d, double lat, double lon) { return lat >= d.bboxS && lat <= d.bboxN && lon >= d.bboxW && lon <= d.bboxE; }

SrsReport analyzeSrs(const SrsInfo& s, const Box3& lb) {
    SrsReport r;
    r.desc = s.describe();
    r.warnings = r.desc.warnings;
    r.labelKo = r.desc.labelKo();
    // metadata.xml 과 대조(수평 EPSG 기준)
    if (!s.metadataSrs.empty() && !s.srs.empty()) {
        SrsDesc m = describeSrs(s.metadataSrs);
        if (m.horizontalEpsg && r.desc.horizontalEpsg && m.horizontalEpsg != r.desc.horizontalEpsg)
            r.warnings.push_back("3MX 의 SRS(EPSG:" + std::to_string(r.desc.horizontalEpsg) + ")와 metadata.xml(EPSG:" + std::to_string(m.horizontalEpsg) + ")이 다릅니다. 3MX 값을 씁니다.");
        if (!r.desc.heightDeclared && m.vertKind != r.desc.vertKind && m.vertKind != VertKind::Unspecified)
            r.warnings.push_back("metadata.xml 의 높이 기준(" + m.verticalKo() + ")이 3MX(" + r.desc.verticalKo() + ")와 다릅니다. 3MX 값을 씁니다.");
    }
    if (r.desc.promotedTo3D && r.desc.vertKind == VertKind::Ellipsoidal && !r.desc.heightDeclared)
        r.warnings.push_back("높이 기준 확인: iTwin 이 EPSG:" + std::to_string(r.desc.horizontalEpsg) +
                             " 을 3D 로 승격하면서 높이 축 이름을 '타원체고'로 붙였을 뿐입니다. 측량값을 그대로 넣었다면 Z는 측량 높이 기준(예: EGM96)입니다. "
                             "「높이 기준 지정」에서 EGM96 등으로 지정하세요(값은 바뀌지 않고 이름표만 바뀜).");
    if (s.srs.empty() && !s.metadataSrs.empty()) r.warnings.push_back("3MX 에는 SRS 가 없고 metadata.xml 에만 있습니다(" + s.metadataSrs.substr(0, 60) + ").");
    // 원점·정밀도
    bool projected = r.desc.known() && r.desc.kind != SrsKind::Enu;
    if (lb.valid()) {
        r.maxLocalXY = std::max({std::fabs(lb.mn.x), std::fabs(lb.mx.x), std::fabs(lb.mn.y), std::fabs(lb.mx.y)});
        r.float32StepMm = float32Step(r.maxLocalXY) * 1000.0;
        if (r.maxLocalXY > 8192.0) {
            r.precisionWarning = true;
            char b[256];
            std::snprintf(b, sizeof b, "로컬 좌표가 큽니다(최대 %.0f m%s). 메시(float32)의 좌표 간격이 약 %.1f mm 라 mm 정밀도를 보장할 수 없습니다.",
                          r.maxLocalXY, (s.hasOrigin && s.origin.x == 0 && s.origin.y == 0) ? ", SRSOrigin=0" : "", r.float32StepMm);
            r.warnings.push_back(b);
        }
    }
    if (projected && !s.hasOrigin) r.warnings.push_back("SRSOrigin 이 없습니다. 원점 0 으로 봅니다(로컬 = 실좌표).");
    // 위경도(표시·축 순서 점검): x=E, y=N
    if (r.desc.hasTm && lb.valid()) {
        TmParams p{r.desc.tmA, r.desc.tmInvF, r.desc.tmLat0, r.desc.tmLon0, r.desc.tmK0, r.desc.tmFE, r.desc.tmFN};
        Vec3 c = s.toWorld(lb.center());
        tmInverse(p, c.x, c.y, r.lat, r.lon);
        r.hasLatLon = std::isfinite(r.lat) && std::isfinite(r.lon);
        if (r.hasLatLon && r.desc.hasBbox && !inBox(r.desc, r.lat, r.lon)) {
            double la2, lo2;
            tmInverse(p, c.y, c.x, la2, lo2);
            if (inBox(r.desc, la2, lo2)) r.warnings.push_back("모델 위치가 좌표계 사용 범위 밖이고, x/y 를 바꾸면 범위 안입니다 — 축 순서(동/북) 뒤바뀜 의심.");
            else r.warnings.push_back("모델 위치가 좌표계 사용 범위(WKT BBOX) 밖입니다 — 좌표계 확인 필요.");
        } else if (r.hasLatLon && !r.desc.hasBbox && r.desc.horizontalEpsg >= 5179 && r.desc.horizontalEpsg <= 5188 &&
                   !(r.lat > 32 && r.lat < 39.5 && r.lon > 124 && r.lon < 132.5)) {
            r.warnings.push_back("모델 위치가 한국 범위 밖입니다 — 좌표계·축 순서 확인 필요.");
        }
    }
    std::string t = r.desc.tooltipKo();
    char b[256];
    std::snprintf(b, sizeof b, "\nSRSOrigin = %.3f, %.3f, %.3f%s", s.origin.x, s.origin.y, s.origin.z, s.hasOrigin ? "" : " (없음)");
    t += b;
    if (r.hasLatLon) { std::snprintf(b, sizeof b, "\n모델 중심 ≈ 위도 %.5f°, 경도 %.5f° (x=동, y=북 으로 역계산)", r.lat, r.lon); t += b; }
    if (lb.valid()) { std::snprintf(b, sizeof b, "\n로컬 좌표 최대 |x|,|y| = %.1f m → float32 간격 %.3f mm", r.maxLocalXY, r.float32StepMm); t += b; }
    for (size_t i = r.desc.warnings.size(); i < r.warnings.size(); ++i) t += "\n⚠ " + r.warnings[i];
    r.tooltipKo = t;
    return r;
}

}  // namespace asec
