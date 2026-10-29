#include "asec/dxf.hpp"

#include <cstdio>
#include <fstream>

namespace asec {

std::string dxfNum(double v) {
    if (std::fabs(v) < 5e-13) v = 0;
    char b[64];
    std::snprintf(b, sizeof b, "%.6f", v);
    // 뒤쪽 0 정리(정수도 소수점 하나 남김)
    std::string s(b);
    auto dot = s.find('.');
    if (dot != std::string::npos) {
        while (s.size() > dot + 2 && s.back() == '0') s.pop_back();
    }
    return s;
}

namespace {
struct Out {
    std::ostringstream& o;
    void g(int c, const std::string& v) { o << c << "\n" << v << "\n"; }
    void g(int c, const char* v) { o << c << "\n" << v << "\n"; }
    void g(int c, int v) { o << c << "\n" << v << "\n"; }
    void g(int c, unsigned v) { o << c << "\n" << v << "\n"; }
    void gd(int c, double v) { o << c << "\n" << dxfNum(v) << "\n"; }
    void hx(int c, unsigned v) { char b[16]; std::snprintf(b, sizeof b, "%X", v); g(c, std::string(b)); }
    void p3(int c, const Vec3& p) { gd(c, p.x); gd(c + 10, p.y); gd(c + 20, p.z); }
};
const unsigned MODEL_BR = 0x17, PAPER_BR = 0x1B;
}  // namespace

void DxfWriter::addLayer(const std::string& n, int c, int lw) {
    for (auto& l : layers_) if (l.name == n) { l.color = c; l.lw = lw; return; }
    layers_.push_back({n, c, lw});
}

void DxfWriter::polyline2d(const std::string& layer, const std::vector<Vec2>& pts, bool closed, double z) {
    if (pts.size() < 2) return;
    Out o{ent_};
    o.g(0, "LWPOLYLINE"); o.hx(5, h()); o.hx(330, MODEL_BR); o.g(100, "AcDbEntity"); o.g(8, layer);
    o.g(100, "AcDbPolyline"); o.g(90, int(pts.size())); o.g(70, closed ? 1 : 0);
    if (z != 0) o.gd(38, z);
    for (auto& p : pts) { o.gd(10, p.x); o.gd(20, p.y); ext_.add(Vec3(p.x, p.y, z)); }
    ++nEnt_;
}

void DxfWriter::polyline3d(const std::string& layer, const std::vector<Vec3>& pts, bool closed) {
    if (pts.size() < 2) return;
    Out o{ent_};
    unsigned hp = h();
    o.g(0, "POLYLINE"); o.hx(5, hp); o.hx(330, MODEL_BR); o.g(100, "AcDbEntity"); o.g(8, layer);
    o.g(100, "AcDb3dPolyline"); o.g(66, 1); o.p3(10, Vec3()); o.g(70, 8 | (closed ? 1 : 0));
    for (auto& p : pts) {
        o.g(0, "VERTEX"); o.hx(5, h()); o.hx(330, hp); o.g(100, "AcDbEntity"); o.g(8, layer);
        o.g(100, "AcDbVertex"); o.g(100, "AcDb3dPolylineVertex"); o.p3(10, p); o.g(70, 32);
        ext_.add(p);
    }
    o.g(0, "SEQEND"); o.hx(5, h()); o.hx(330, hp); o.g(100, "AcDbEntity"); o.g(8, layer);
    ++nEnt_;
}

void DxfWriter::line(const std::string& layer, const Vec3& a, const Vec3& b) {
    Out o{ent_};
    o.g(0, "LINE"); o.hx(5, h()); o.hx(330, MODEL_BR); o.g(100, "AcDbEntity"); o.g(8, layer);
    o.g(100, "AcDbLine"); o.p3(10, a); o.p3(11, b);
    ext_.add(a); ext_.add(b);
    ++nEnt_;
}

void DxfWriter::text(const std::string& layer, const Vec3& p, double ht, const std::string& s, int ha, int va, double rot) {
    Out o{ent_};
    o.g(0, "TEXT"); o.hx(5, h()); o.hx(330, MODEL_BR); o.g(100, "AcDbEntity"); o.g(8, layer);
    o.g(100, "AcDbText"); o.p3(10, p); o.gd(40, ht); o.g(1, s);
    if (rot != 0) o.gd(50, rot);
    o.g(7, "Standard");
    if (ha || va) { o.g(72, ha); o.p3(11, p); }
    o.g(100, "AcDbText");
    if (va) o.g(73, va);
    ext_.add(p);
    ++nEnt_;
}

void DxfWriter::textOnPlane(const std::string& layer, const Vec3& p, const Vec3& N0, double ht, const std::string& s, int ha, int va) {
    double nl = std::sqrt(N0.x * N0.x + N0.y * N0.y + N0.z * N0.z);
    Vec3 N = N0 * (1.0 / nl);
    // 임의 축 알고리즘(AutoCAD OCS)
    Vec3 Ax;
    if (std::fabs(N.x) < 1.0 / 64 && std::fabs(N.y) < 1.0 / 64) Ax = Vec3(N.z, 0, -N.x);  // Wy × N
    else Ax = Vec3(-N.y, N.x, 0);                                                          // Wz × N
    double al = std::sqrt(Ax.x * Ax.x + Ax.y * Ax.y + Ax.z * Ax.z);
    Ax = Ax * (1.0 / al);
    Vec3 Ay(N.y * Ax.z - N.z * Ax.y, N.z * Ax.x - N.x * Ax.z, N.x * Ax.y - N.y * Ax.x);
    auto dot = [](const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    Vec3 q(dot(p, Ax), dot(p, Ay), dot(p, N));
    Out o{ent_};
    o.g(0, "TEXT"); o.hx(5, h()); o.hx(330, MODEL_BR); o.g(100, "AcDbEntity"); o.g(8, layer);
    o.g(100, "AcDbText"); o.p3(10, q); o.gd(40, ht); o.g(1, s); o.g(7, "Standard");
    if (ha || va) { o.g(72, ha); o.p3(11, q); }
    o.gd(210, N.x); o.gd(220, N.y); o.gd(230, N.z);
    o.g(100, "AcDbText");
    if (va) o.g(73, va);
    ext_.add(p);
    ++nEnt_;
}

void DxfWriter::image(const std::string& layer, const std::string& file, int w, int hh, const Vec3& ins, const Vec3& u, const Vec3& v) {
    Img im{file, w, hh, ins, u, v, layer, h(), h(), h()};
    images_.push_back(im);
    // 개체는 호출 순서대로 기록(그리기 순서 = 파일 순서: 레벨선 → 영상 → 단면선)
    Out o{ent_};
    o.g(0, "IMAGE"); o.hx(5, im.hEnt); o.hx(330, MODEL_BR); o.g(100, "AcDbEntity"); o.g(8, im.layer);
    o.g(100, "AcDbRasterImage"); o.g(90, 0); o.p3(10, im.ins); o.p3(11, im.u); o.p3(12, im.v);
    o.gd(13, im.w); o.gd(23, im.h); o.hx(340, im.hDef); o.g(70, 3); o.g(280, 0); o.g(281, 50); o.g(282, 50); o.g(283, 0);
    o.hx(360, im.hReact); o.g(71, 1); o.g(91, 2); o.gd(14, -0.5); o.gd(24, -0.5); o.gd(14, im.w - 0.5); o.gd(24, im.h - 0.5);
    ext_.add(ins); ext_.add(ins + u * w + v * hh);
    ++nEnt_;
}

std::string DxfWriter::str() const {
    std::ostringstream s;
    Out o{s};
    unsigned seed = next_ + 0x10;
    Box3 e = ext_;
    if (!e.valid()) { e.add(Vec3(0, 0, 0)); e.add(Vec3(1, 1, 0)); }
    // HEADER
    o.g(0, "SECTION"); o.g(2, "HEADER");
    o.g(9, "$ACADVER"); o.g(1, "AC1015");
    o.g(9, "$ACADMAINTVER"); o.g(70, 6);
    o.g(9, "$DWGCODEPAGE"); o.g(3, "ANSI_949");
    o.g(9, "$INSBASE"); o.p3(10, Vec3());
    o.g(9, "$EXTMIN"); o.p3(10, e.mn);
    o.g(9, "$EXTMAX"); o.p3(10, e.mx);
    o.g(9, "$LIMMIN"); o.gd(10, e.mn.x); o.gd(20, e.mn.y);
    o.g(9, "$LIMMAX"); o.gd(10, e.mx.x); o.gd(20, e.mx.y);
    o.g(9, "$LTSCALE"); o.gd(40, 1.0);
    o.g(9, "$TEXTSTYLE"); o.g(7, "Standard");
    o.g(9, "$CLAYER"); o.g(8, "0");
    o.g(9, "$LUNITS"); o.g(70, 2);
    o.g(9, "$LUPREC"); o.g(70, 4);
    o.g(9, "$HANDSEED"); o.hx(5, seed);
    o.g(9, "$MEASUREMENT"); o.g(70, 1);
    o.g(9, "$INSUNITS"); o.g(70, 6);
    o.g(9, "$LWDISPLAY"); o.g(290, 1);
    o.g(9, "$DIMSTYLE"); o.g(2, "Standard");
    o.g(9, "$TILEMODE"); o.g(70, 1);
    o.g(9, "$EXTNAMES"); o.g(290, 1);
    o.g(0, "ENDSEC");
    // CLASSES
    o.g(0, "SECTION"); o.g(2, "CLASSES");
    auto cls = [&](const char* dxf, const char* cpp, const char* app, int flags, int ent) {
        o.g(0, "CLASS"); o.g(1, dxf); o.g(2, cpp); o.g(3, app); o.g(90, flags); o.g(280, 0); o.g(281, ent);
    };
    cls("ACDBDICTIONARYWDFLT", "AcDbDictionaryWithDefault", "ObjectDBX Classes", 0, 0);
    cls("ACDBPLACEHOLDER", "AcDbPlaceHolder", "ObjectDBX Classes", 0, 0);
    cls("LAYOUT", "AcDbLayout", "ObjectDBX Classes", 0, 0);
    cls("RASTERVARIABLES", "AcDbRasterVariables", "ISM", 0, 0);
    cls("IMAGE", "AcDbRasterImage", "ISM", 2175, 1);
    cls("IMAGEDEF", "AcDbRasterImageDef", "ISM", 0, 0);
    cls("IMAGEDEF_REACTOR", "AcDbRasterImageDefReactor", "ISM", 1, 0);
    o.g(0, "ENDSEC");
    // TABLES
    o.g(0, "SECTION"); o.g(2, "TABLES");
    auto tab = [&](const char* name, unsigned hnd, int n) {
        o.g(0, "TABLE"); o.g(2, name); o.hx(5, hnd); o.hx(330, 0); o.g(100, "AcDbSymbolTable"); o.g(70, n);
    };
    auto rec = [&](const char* type, unsigned hnd, unsigned owner, const char* sub) {
        o.g(0, type); o.hx(5, hnd); o.hx(330, owner); o.g(100, "AcDbSymbolTableRecord"); o.g(100, sub);
    };
    tab("VPORT", 0x8, 1);
    rec("VPORT", 0x23, 0x8, "AcDbViewportTableRecord");
    {
        Vec3 c = e.center(); double hgt = std::max(e.mx.y - e.mn.y, (e.mx.x - e.mn.x) / 1.6) * 1.1;
        o.g(2, "*Active"); o.g(70, 0); o.gd(10, 0); o.gd(20, 0); o.gd(11, 1); o.gd(21, 1); o.gd(12, c.x); o.gd(22, c.y);
        o.gd(13, 0); o.gd(23, 0); o.gd(14, 0.5); o.gd(24, 0.5); o.gd(15, 0.5); o.gd(25, 0.5);
        o.gd(16, 0); o.gd(26, 0); o.gd(36, 1); o.gd(17, 0); o.gd(27, 0); o.gd(37, 0);
        o.gd(40, hgt > 0 ? hgt : 10); o.gd(41, 1.6); o.gd(42, 50); o.gd(43, 0); o.gd(44, 0); o.gd(50, 0); o.gd(51, 0);
        o.g(71, 0); o.g(72, 1000); o.g(73, 1); o.g(74, 3); o.g(75, 0); o.g(76, 0); o.g(77, 0); o.g(78, 0);
        o.g(281, 0); o.g(65, 0); o.gd(146, 0);
    }
    o.g(0, "ENDTAB");
    tab("LTYPE", 0x2, 3);
    for (auto [nm, hd] : {std::pair<const char*, unsigned>{"ByBlock", 0x24}, {"ByLayer", 0x25}, {"Continuous", 0x26}}) {
        rec("LTYPE", hd, 0x2, "AcDbLinetypeTableRecord");
        o.g(2, nm); o.g(70, 0); o.g(3, std::string(hd == 0x26 ? "Solid line" : "")); o.g(72, 65); o.g(73, 0); o.gd(40, 0);
    }
    o.g(0, "ENDTAB");
    std::vector<Layer> L = layers_;
    bool has0 = false;
    for (auto& l : L) if (l.name == "0") has0 = true;
    if (!has0) L.insert(L.begin(), Layer{"0", 7, -3});
    tab("LAYER", 0x1, int(L.size()));
    unsigned lh = 0x40;
    for (auto& l : L) {
        rec("LAYER", lh++, 0x1, "AcDbLayerTableRecord");
        o.g(2, l.name); o.g(70, 0); o.g(62, l.color); o.g(6, "Continuous"); o.g(370, l.lw); o.g(390, "13");
    }
    o.g(0, "ENDTAB");
    tab("STYLE", 0x5, 1);
    rec("STYLE", 0x29, 0x5, "AcDbTextStyleTableRecord");
    o.g(2, "Standard"); o.g(70, 0); o.gd(40, 0); o.gd(41, 1); o.gd(50, 0); o.g(71, 0); o.gd(42, 2.5); o.g(3, "txt"); o.g(4, "");
    o.g(0, "ENDTAB");
    tab("VIEW", 0x7, 0); o.g(0, "ENDTAB");
    tab("UCS", 0x6, 0); o.g(0, "ENDTAB");
    tab("APPID", 0x3, 1);
    rec("APPID", 0x2A, 0x3, "AcDbRegAppTableRecord"); o.g(2, "ACAD"); o.g(70, 0);
    o.g(0, "ENDTAB");
    o.g(0, "TABLE"); o.g(2, "DIMSTYLE"); o.hx(5, 0x4); o.hx(330, 0); o.g(100, "AcDbSymbolTable"); o.g(70, 1); o.g(100, "AcDbDimStyleTable");
    o.g(0, "DIMSTYLE"); o.hx(105, 0x2B); o.hx(330, 0x4); o.g(100, "AcDbSymbolTableRecord"); o.g(100, "AcDbDimStyleTableRecord");
    o.g(2, "Standard"); o.g(70, 0); o.g(340, "29");
    o.g(0, "ENDTAB");
    tab("BLOCK_RECORD", 0x9, 2);
    rec("BLOCK_RECORD", MODEL_BR, 0x9, "AcDbBlockTableRecord"); o.g(2, "*Model_Space"); o.g(340, "1A");
    rec("BLOCK_RECORD", PAPER_BR, 0x9, "AcDbBlockTableRecord"); o.g(2, "*Paper_Space"); o.g(340, "1E");
    o.g(0, "ENDTAB");
    o.g(0, "ENDSEC");
    // BLOCKS
    o.g(0, "SECTION"); o.g(2, "BLOCKS");
    auto blk = [&](unsigned hb, unsigned he, unsigned owner, const char* nm) {
        o.g(0, "BLOCK"); o.hx(5, hb); o.hx(330, owner); o.g(100, "AcDbEntity"); o.g(8, "0"); o.g(100, "AcDbBlockBegin");
        o.g(2, nm); o.g(70, 0); o.p3(10, Vec3()); o.g(3, nm); o.g(1, "");
        o.g(0, "ENDBLK"); o.hx(5, he); o.hx(330, owner); o.g(100, "AcDbEntity"); o.g(8, "0"); o.g(100, "AcDbBlockEnd");
    };
    blk(0x18, 0x19, MODEL_BR, "*Model_Space");
    blk(0x1C, 0x1D, PAPER_BR, "*Paper_Space");
    o.g(0, "ENDSEC");
    // ENTITIES
    o.g(0, "SECTION"); o.g(2, "ENTITIES");
    s << ent_.str();
    o.g(0, "ENDSEC");
    // OBJECTS
    const unsigned IMGDICT = 0x31, IMGVARS = 0x32;
    o.g(0, "SECTION"); o.g(2, "OBJECTS");
    o.g(0, "DICTIONARY"); o.hx(5, 0xA); o.hx(330, 0); o.g(100, "AcDbDictionary"); o.g(281, 1);
    o.g(3, "ACAD_GROUP"); o.hx(350, 0xC);
    if (!images_.empty()) { o.g(3, "ACAD_IMAGE_DICT"); o.hx(350, IMGDICT); o.g(3, "ACAD_IMAGE_VARS"); o.hx(350, IMGVARS); }
    o.g(3, "ACAD_LAYOUT"); o.hx(350, 0xD);
    o.g(3, "ACAD_MLINESTYLE"); o.hx(350, 0x10);
    o.g(3, "ACAD_PLOTSETTINGS"); o.hx(350, 0x11);
    o.g(3, "ACAD_PLOTSTYLENAME"); o.hx(350, 0x12);
    o.g(0, "DICTIONARY"); o.hx(5, 0xC); o.hx(330, 0xA); o.g(100, "AcDbDictionary"); o.g(281, 1);
    o.g(0, "DICTIONARY"); o.hx(5, 0xD); o.hx(330, 0xA); o.g(100, "AcDbDictionary"); o.g(281, 1);
    o.g(3, "Model"); o.hx(350, 0x1A); o.g(3, "Layout1"); o.hx(350, 0x1E);
    o.g(0, "DICTIONARY"); o.hx(5, 0x10); o.hx(330, 0xA); o.g(100, "AcDbDictionary"); o.g(281, 1); o.g(3, "Standard"); o.hx(350, 0x22);
    o.g(0, "DICTIONARY"); o.hx(5, 0x11); o.hx(330, 0xA); o.g(100, "AcDbDictionary"); o.g(281, 1);
    o.g(0, "ACDBDICTIONARYWDFLT"); o.hx(5, 0x12); o.hx(330, 0xA); o.g(100, "AcDbDictionary"); o.g(281, 1);
    o.g(3, "Normal"); o.hx(350, 0x13); o.g(100, "AcDbDictionaryWithDefault"); o.hx(340, 0x13);
    o.g(0, "ACDBPLACEHOLDER"); o.hx(5, 0x13); o.hx(330, 0x12);
    o.g(0, "MLINESTYLE"); o.hx(5, 0x22); o.hx(330, 0x10); o.g(100, "AcDbMlineStyle"); o.g(2, "Standard"); o.g(70, 0); o.g(3, "");
    o.g(62, 256); o.gd(51, 90); o.gd(52, 90); o.g(71, 2); o.gd(49, 0.5); o.g(62, 256); o.g(6, "BYLAYER"); o.gd(49, -0.5); o.g(62, 256); o.g(6, "BYLAYER");
    auto layout = [&](unsigned hnd, const char* nm, int flags, int tab, unsigned br) {
        o.g(0, "LAYOUT"); o.hx(5, hnd); o.hx(330, 0xD); o.g(100, "AcDbPlotSettings"); o.g(1, ""); o.g(4, "A3"); o.g(6, "");
        o.gd(40, 7.5); o.gd(41, 20); o.gd(42, 7.5); o.gd(43, 20); o.gd(44, 420); o.gd(45, 297); o.gd(46, 0); o.gd(47, 0); o.gd(48, 0); o.gd(49, 0);
        o.gd(140, 0); o.gd(141, 0); o.gd(142, 1); o.gd(143, 1); o.g(70, flags); o.g(72, 1); o.g(73, 0); o.g(74, 5); o.g(7, ""); o.g(75, 16); o.g(76, 0); o.g(77, 2); o.g(78, 300);
        o.gd(147, 1); o.gd(148, 0); o.gd(149, 0);
        o.g(100, "AcDbLayout"); o.g(1, nm); o.g(70, 1); o.g(71, tab); o.gd(10, 0); o.gd(20, 0); o.gd(11, 420); o.gd(21, 297);
        o.gd(12, 0); o.gd(22, 0); o.gd(32, 0); o.gd(14, 1e20); o.gd(24, 1e20); o.gd(34, 1e20); o.gd(15, -1e20); o.gd(25, -1e20); o.gd(35, -1e20);
        o.gd(146, 0); o.gd(13, 0); o.gd(23, 0); o.gd(33, 0); o.gd(16, 1); o.gd(26, 0); o.gd(36, 0); o.gd(17, 0); o.gd(27, 1); o.gd(37, 0); o.g(76, 1); o.hx(330, br);
    };
    layout(0x1A, "Model", 1024, 0, MODEL_BR);
    layout(0x1E, "Layout1", 0, 1, PAPER_BR);
    if (!images_.empty()) {
        o.g(0, "DICTIONARY"); o.hx(5, IMGDICT); o.hx(330, 0xA); o.g(100, "AcDbDictionary"); o.g(281, 1);
        for (size_t i = 0; i < images_.size(); ++i) { o.g(3, "SECTION_IMAGE_" + std::to_string(i + 1)); o.hx(350, images_[i].hDef); }
        o.g(0, "RASTERVARIABLES"); o.hx(5, IMGVARS); o.g(102, "{ACAD_REACTORS"); o.hx(330, 0xA); o.g(102, "}"); o.hx(330, 0xA);
        o.g(100, "AcDbRasterVariables"); o.g(90, 0); o.g(70, 0); o.g(71, 1); o.g(72, 6);  // 72=6 → 미터
        for (auto& im : images_) {
            o.g(0, "IMAGEDEF"); o.hx(5, im.hDef); o.g(102, "{ACAD_REACTORS"); o.hx(330, IMGDICT); o.hx(330, im.hReact); o.g(102, "}");
            o.hx(330, IMGDICT); o.g(100, "AcDbRasterImageDef"); o.g(90, 0); o.g(1, im.file);
            o.gd(10, im.w); o.gd(20, im.h); o.gd(11, im.u.x != 0 || im.u.y != 0 ? std::sqrt(im.u.x * im.u.x + im.u.y * im.u.y + im.u.z * im.u.z) : 1);
            o.gd(21, std::sqrt(im.v.x * im.v.x + im.v.y * im.v.y + im.v.z * im.v.z)); o.g(280, 1); o.g(281, 2);  // 281=2 → 미터
            o.g(0, "IMAGEDEF_REACTOR"); o.hx(5, im.hReact); o.hx(330, im.hEnt); o.g(100, "AcDbRasterImageDefReactor"); o.g(90, 2); o.hx(330, im.hEnt);
        }
    }
    o.g(0, "ENDSEC");
    o.g(0, "EOF");
    return s.str();
}

bool DxfWriter::save(const std::filesystem::path& p, std::string* err) const {
    std::ofstream f(p, std::ios::binary);
    if (!f) { if (err) *err = "DXF 를 쓸 수 없습니다: " + p.u8string(); return false; }
    std::string s = str();
    f.write(s.data(), std::streamsize(s.size()));
    return bool(f);
}

}  // namespace asec
