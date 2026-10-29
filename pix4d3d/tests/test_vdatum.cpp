// 높이(수직) 기준 식별(EGM96·EGM2008·KVD1964·KNGeoid·타원체고) + 지오이드 변환 틀(자동 변환 없음)
#include "catch_amalgamated.hpp"
#include "asec/srs.hpp"
#include "asec/vdatum.hpp"
#include "asec/tmx.hpp"
#include "asec/export.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>

using namespace asec;

// projinfo(PROJ) 로 만든 실제 WKT
static const char* kW2Egm96 = R"WKT(COMPOUNDCRS["KGD2002 / Central Belt 2010 + EGM96 height",PROJCRS["KGD2002 / Central Belt 2010",BASEGEOGCRS["KGD2002",DATUM["Korean Geodetic Datum 2002",ELLIPSOID["GRS 1980",6378137,298.257222101,LENGTHUNIT["metre",1]]],PRIMEM["Greenwich",0,ANGLEUNIT["degree",0.0174532925199433]],ID["EPSG",4737]],CONVERSION["Korea Central Belt 2010",METHOD["Transverse Mercator",ID["EPSG",9807]],PARAMETER["Latitude of natural origin",38,ANGLEUNIT["degree",0.0174532925199433],ID["EPSG",8801]],PARAMETER["Longitude of natural origin",127,ANGLEUNIT["degree",0.0174532925199433],ID["EPSG",8802]],PARAMETER["Scale factor at natural origin",1,SCALEUNIT["unity",1],ID["EPSG",8805]],PARAMETER["False easting",200000,LENGTHUNIT["metre",1],ID["EPSG",8806]],PARAMETER["False northing",600000,LENGTHUNIT["metre",1],ID["EPSG",8807]]],CS[Cartesian,2],AXIS["northing (X)",north,ORDER[1],LENGTHUNIT["metre",1]],AXIS["easting (Y)",east,ORDER[2],LENGTHUNIT["metre",1]],USAGE[SCOPE["Cadastre, topographic mapping."],AREA["Republic of Korea (South Korea) - onshore between 126°E and 128°E."],BBOX[33.14,126,38.33,128]],ID["EPSG",5186]],VERTCRS["EGM96 height",VDATUM["EGM96 geoid"],CS[vertical,1],AXIS["gravity-related height (H)",up,LENGTHUNIT["metre",1]],USAGE[SCOPE["Geodesy."],AREA["World."],BBOX[-90,-180,90,180]],ID["EPSG",5773]]])WKT";
static const char* kW1Egm08 = R"WKT(COMPD_CS["KGD2002 / Central Belt 2010 + EGM2008 height",PROJCS["KGD2002 / Central Belt 2010",GEOGCS["KGD2002",DATUM["Korean_Geodetic_Datum_2002",SPHEROID["GRS 1980",6378137,298.257222101,AUTHORITY["EPSG","7019"]],AUTHORITY["EPSG","6737"]],PRIMEM["Greenwich",0,AUTHORITY["EPSG","8901"]],UNIT["degree",0.0174532925199433,AUTHORITY["EPSG","9122"]],AUTHORITY["EPSG","4737"]],PROJECTION["Transverse_Mercator"],PARAMETER["latitude_of_origin",38],PARAMETER["central_meridian",127],PARAMETER["scale_factor",1],PARAMETER["false_easting",200000],PARAMETER["false_northing",600000],UNIT["metre",1,AUTHORITY["EPSG","9001"]],AUTHORITY["EPSG","5186"]],VERT_CS["EGM2008 height",VERT_DATUM["EGM2008 geoid",2005,AUTHORITY["EPSG","1027"]],UNIT["metre",1,AUTHORITY["EPSG","9001"]],AXIS["Gravity-related height",UP],AUTHORITY["EPSG","3855"]]])WKT";
static const char* kW2BoundGrid = R"WKT(COMPOUNDCRS["unknown",PROJCRS["unknown",BASEGEOGCRS["unknown",DATUM["Unknown based on GRS 1980 ellipsoid",ELLIPSOID["GRS 1980",6378137,298.257222101,LENGTHUNIT["metre",1],ID["EPSG",7019]]],PRIMEM["Greenwich",0,ANGLEUNIT["degree",0.0174532925199433],ID["EPSG",8901]]],CONVERSION["unknown",METHOD["Transverse Mercator",ID["EPSG",9807]],PARAMETER["Latitude of natural origin",38,ANGLEUNIT["degree",0.0174532925199433],ID["EPSG",8801]],PARAMETER["Longitude of natural origin",127,ANGLEUNIT["degree",0.0174532925199433],ID["EPSG",8802]],PARAMETER["Scale factor at natural origin",1,SCALEUNIT["unity",1],ID["EPSG",8805]],PARAMETER["False easting",200000,LENGTHUNIT["metre",1],ID["EPSG",8806]],PARAMETER["False northing",600000,LENGTHUNIT["metre",1],ID["EPSG",8807]]],CS[Cartesian,2],AXIS["(E)",east,ORDER[1],LENGTHUNIT["metre",1,ID["EPSG",9001]]],AXIS["(N)",north,ORDER[2],LENGTHUNIT["metre",1,ID["EPSG",9001]]]],BOUNDCRS[SOURCECRS[VERTCRS["unknown",VDATUM["unknown using geoidgrids=egm96_15.gtx"],CS[vertical,1],AXIS["gravity-related height (H)",up,LENGTHUNIT["metre",1,ID["EPSG",9001]]]]],TARGETCRS[GEOGCRS["WGS 84",DATUM["World Geodetic System 1984",ELLIPSOID["WGS 84",6378137,298.257223563,LENGTHUNIT["metre",1]]],PRIMEM["Greenwich",0,ANGLEUNIT["degree",0.0174532925199433]],CS[ellipsoidal,3],AXIS["latitude",north,ORDER[1],ANGLEUNIT["degree",0.0174532925199433]],AXIS["longitude",east,ORDER[2],ANGLEUNIT["degree",0.0174532925199433]],AXIS["ellipsoidal height",up,ORDER[3],LENGTHUNIT["metre",1]],ID["EPSG",4979]]],ABRIDGEDTRANSFORMATION["unknown to WGS 84 ellipsoidal height",METHOD["GravityRelatedHeight to Geographic3D"],PARAMETERFILE["Geoid (height correction) model file","egm96_15.gtx",ID["EPSG",8666]]]]])WKT";
static const char* kW1Grids = R"WKT(COMPD_CS["unknown",PROJCS["unknown",GEOGCS["unknown",DATUM["Unknown based on GRS 1980 ellipsoid",SPHEROID["GRS 1980",6378137,298.257222101,AUTHORITY["EPSG","7019"]]],PRIMEM["Greenwich",0,AUTHORITY["EPSG","8901"]],UNIT["degree",0.0174532925199433,AUTHORITY["EPSG","9122"]]],PROJECTION["Transverse_Mercator"],PARAMETER["latitude_of_origin",38],PARAMETER["central_meridian",127],PARAMETER["scale_factor",1],PARAMETER["false_easting",200000],PARAMETER["false_northing",600000],UNIT["metre",1,AUTHORITY["EPSG","9001"]],AXIS["Easting",EAST],AXIS["Northing",NORTH]],VERT_CS["unknown",VERT_DATUM["unknown using geoidgrids=egm96_15.gtx",2005,EXTENSION["PROJ4_GRIDS","egm96_15.gtx"]],UNIT["metre",1,AUTHORITY["EPSG","9001"]],AXIS["Gravity-related height",UP]]])WKT";

TEST_CASE("수직 기준: 복합 EPSG 번호 → EGM96 / EGM2008 / KVD1964") {
    SrsDesc a = describeSrs("EPSG:5186+5773");
    CHECK(a.vdatum == VDatum::EGM96); CHECK(a.verticalEpsg == 5773);
    CHECK(a.labelKo() == "수평 EPSG:5186 / 높이 EGM96 지오이드 기준 높이 (EPSG:5773)");
    CHECK(a.shortAscii() == "EPSG:5186+5773");
    CHECK(a.tooltipKo().find("한국 정표고") != std::string::npos);
    SrsDesc b = describeSrs("EPSG:5186+3855");
    CHECK(b.vdatum == VDatum::EGM2008); CHECK(b.verticalName == "EGM2008 height");
    SrsDesc c = describeSrs("EPSG:5186+5193");
    CHECK(c.vdatum == VDatum::KVD1964);
    CHECK(c.verticalKo() == "KVD1964 정표고(인천만 평균해수면) (EPSG:5193)");
    SrsDesc w = describeSrs("EPSG:4326+5773");
    CHECK(w.vdatum == VDatum::EGM96);
}

TEST_CASE("수직 기준: iTwin 식 글자 표기 EPSG:h+EGM96 / +KNGeoid18") {
    SrsDesc a = describeSrs("EPSG:5186+EGM96");
    CHECK(a.kind == SrsKind::CompoundEpsg); CHECK(a.vdatum == VDatum::EGM96); CHECK(a.verticalEpsg == 5773);
    SrsDesc k = describeSrs("EPSG:5186+KNGeoid18");
    CHECK(k.kind == SrsKind::CompoundEpsg); CHECK(k.vdatum == VDatum::KNGeoid); CHECK(k.verticalEpsg == 0);
    CHECK(k.labelKo() == "수평 EPSG:5186 / 높이 KNGeoid 지오이드 기준 정표고 (KNGeoid18)");
    CHECK(k.shortAscii() == "EPSG:5186 + KNGeoid18");
    SrsDesc bad = describeSrs("EPSG:5186+foo");
    CHECK(bad.kind == SrsKind::Unknown); CHECK_FALSE(bad.warnings.empty());
}

TEST_CASE("수직 기준: WKT2 COMPOUNDCRS(EGM96)·WKT1 COMPD_CS(EGM2008)") {
    SrsDesc a = describeSrs(kW2Egm96);
    CHECK(a.horizontalEpsg == 5186); CHECK(a.vdatum == VDatum::EGM96); CHECK(a.verticalEpsg == 5773);
    CHECK(a.verticalDatum == "EGM96 geoid");
    SrsDesc b = describeSrs(kW1Egm08);
    CHECK(b.horizontalEpsg == 5186); CHECK(b.vdatum == VDatum::EGM2008); CHECK(b.verticalEpsg == 3855);
    CHECK(b.verticalDatum == "EGM2008 geoid");
}

TEST_CASE("수직 기준: 격자 파일로만 정의(BOUNDCRS PARAMETERFILE / WKT1 PROJ4_GRIDS) → EGM96") {
    SrsDesc a = describeSrs(kW2BoundGrid);
    CHECK(a.vertKind == VertKind::Gravity);
    CHECK(a.geoidModel == "egm96_15.gtx");
    CHECK(a.vdatum == VDatum::EGM96);
    CHECK(a.horizontalEpsg == 5186);  // TM 매개변수로
    SrsDesc b = describeSrs(kW1Grids);
    CHECK(b.geoidModel == "egm96_15.gtx");
    CHECK(b.vdatum == VDatum::EGM96);
}

TEST_CASE("수직 기준: WKT2 GEOIDMODEL(KNGeoid18) 은 KVD1964 보다 우선, 모르는 이름은 경고+이름 표시") {
    std::string w = R"WKT(COMPOUNDCRS["KGD2002 / Central Belt 2010 + KVD1964 height",PROJCRS["KGD2002 / Central Belt 2010",BASEGEOGCRS["KGD2002",DATUM["Korean Geodetic Datum 2002",ELLIPSOID["GRS 1980",6378137,298.257222101]]],CONVERSION["Korea Central Belt 2010",METHOD["Transverse Mercator"],PARAMETER["Latitude of natural origin",38],PARAMETER["Longitude of natural origin",127],PARAMETER["Scale factor at natural origin",1],PARAMETER["False easting",200000],PARAMETER["False northing",600000]],CS[Cartesian,2],AXIS["northing (X)",north],AXIS["easting (Y)",east],ID["EPSG",5186]],VERTCRS["KVD1964 height",VDATUM["Korean Vertical Datum 1964"],CS[vertical,1],AXIS["gravity-related height (H)",up,LENGTHUNIT["metre",1]],GEOIDMODEL["KNGeoid18"],ID["EPSG",5193]]])WKT";
    SrsDesc a = describeSrs(w);
    CHECK(a.geoidModel == "KNGeoid18");
    CHECK(a.vdatum == VDatum::KNGeoid);
    CHECK(a.verticalEpsg == 5193);
    CHECK(a.verticalKo() == "KNGeoid 지오이드 기준 정표고 (EPSG:5193, KNGeoid18)");
    std::string u = R"WKT(COMPOUNDCRS["x",PROJCRS["KGD2002 / Central Belt 2010",BASEGEOGCRS["KGD2002",DATUM["Korean Geodetic Datum 2002",ELLIPSOID["GRS 1980",6378137,298.257222101]]],CONVERSION["c",METHOD["Transverse Mercator"]],ID["EPSG",5186]],VERTCRS["Local site datum height",VDATUM["Site benchmark"],CS[vertical,1],AXIS["gravity-related height (H)",up]]])WKT";
    SrsDesc b = describeSrs(u);
    CHECK(b.vdatum == VDatum::Unknown);
    CHECK(b.verticalKo() == "Local site datum height");
    bool warned = false; for (auto& x : b.warnings) warned |= x.find("Local site datum height") != std::string::npos;
    CHECK(warned);
}

TEST_CASE("수직 기준: 사용자 실제 모델(3D PROJCRS 타원체고) 은 그대로 타원체고") {
    SrsDesc d = describeSrs("EPSG:4979");
    CHECK(d.vdatum == VDatum::Ellipsoidal);
    CHECK(describeSrs("EPSG:5186").vdatum == VDatum::None);
    CHECK(describeSrs("ENU:33.47,126.44").vdatum == VDatum::LocalEnu);
    CHECK(vdatumFromText("egm2008-1.tif") == VDatum::EGM2008);
    CHECK(vdatumFromText("Incheon MSL") == VDatum::KVD1964);
    CHECK(vdatumFromEpsg(1049) == VDatum::KVD1964);
}

static void writeGtx(const std::string& path, double lat0, double lon0, double dlat, double dlon, int rows, int cols, float (*f)(double, double)) {
    FILE* fp = std::fopen(path.c_str(), "wb");
    REQUIRE(fp);
    auto be64 = [&](double v) { uint64_t u; std::memcpy(&u, &v, 8); for (int i = 7; i >= 0; --i) std::fputc(int((u >> (8 * i)) & 0xff), fp); };
    auto be32 = [&](uint32_t u) { for (int i = 3; i >= 0; --i) std::fputc(int((u >> (8 * i)) & 0xff), fp); };
    be64(lat0); be64(lon0); be64(dlat); be64(dlon); be32(uint32_t(rows)); be32(uint32_t(cols));
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) { float v = f(lat0 + r * dlat, lon0 + c * dlon); uint32_t u; std::memcpy(&u, &v, 4); be32(u); }
    std::fclose(fp);
}

TEST_CASE("지오이드 변환 틀: GTX 격자 읽기·쌍선형 보간·타원체고↔EGM96, 격자 없으면 변환 안 함") {
    auto p = (std::filesystem::temp_directory_path() / "asec_test_geoid.gtx").string();
    // N = 20 + 0.5*(lat-33) + 0.25*(lon-126) (선형 → 쌍선형 보간이 정확해야 함)
    writeGtx(p, 33.0, 126.0, 0.25, 0.25, 9, 9, [](double la, double lo) { return float(20 + 0.5 * (la - 33) + 0.25 * (lo - 126)); });
    auto g = std::make_shared<GridGeoidModel>();
    std::string err;
    REQUIRE(g->loadGtx(p, &err));
    CHECK(g->rows == 9); CHECK(g->cols == 9);
    double N = 0;
    REQUIRE(g->undulation(33.4754, 126.4416, N));
    CHECK(N == Catch::Approx(20 + 0.5 * 0.4754 + 0.25 * 0.4416).margin(1e-5));
    CHECK_FALSE(g->undulation(40, 126.5, N));  // 범위 밖
    GeoidRegistry empty;
    double out = 0;
    CHECK_FALSE(convertHeight(VDatum::Ellipsoidal, VDatum::EGM96, 33.4754, 126.4416, 56.8852, out, empty, &err));
    CHECK(out == 56.8852);  // 값 그대로
    CHECK(err.find("egm96") != std::string::npos);
    GeoidRegistry reg; reg.add("egm96", g);
    REQUIRE(convertHeight(VDatum::Ellipsoidal, VDatum::EGM96, 33.4754, 126.4416, 56.8852, out, reg, &err));
    CHECK(out == Catch::Approx(56.8852 - N).margin(1e-6));
    double back = 0;
    REQUIRE(convertHeight(VDatum::EGM96, VDatum::Ellipsoidal, 33.4754, 126.4416, out, back, reg, &err));
    CHECK(back == Catch::Approx(56.8852).margin(1e-9));
    CHECK_FALSE(convertHeight(VDatum::Ellipsoidal, VDatum::KVD1964, 33.4754, 126.4416, 56.0, out, reg, &err));  // KNGeoid 격자 없음
    CHECK_FALSE(convertHeight(VDatum::LocalEnu, VDatum::EGM96, 33.4754, 126.4416, 1.0, out, reg, &err));
    CHECK(convertHeight(VDatum::EGM96, VDatum::EGM96, 0, 0, 5.0, out, empty));
    CHECK(out == 5.0);
    std::filesystem::remove(p);
}

static const char* kRealPromoted = R"WKT(PROJCRS["KGD2002 / Central Belt 2010",BASEGEOGCRS["KGD2002",DATUM["Korean Geodetic Datum 2002",ELLIPSOID["GRS 1980",6378137,298.257222101,LENGTHUNIT["metre",1]]],PRIMEM["Greenwich",0,ANGLEUNIT["degree",0.0174532925199433]]],CONVERSION["Korea Central Belt 2010",METHOD["Transverse Mercator",ID["EPSG",9807]],PARAMETER["Latitude of natural origin",38,ANGLEUNIT["degree",0.0174532925199433]],PARAMETER["Longitude of natural origin",127,ANGLEUNIT["degree",0.0174532925199433]],PARAMETER["Scale factor at natural origin",1,SCALEUNIT["unity",1]],PARAMETER["False easting",200000,LENGTHUNIT["metre",1]],PARAMETER["False northing",600000,LENGTHUNIT["metre",1]]],CS[Cartesian,3],AXIS["northing (X)",north,ORDER[1],LENGTHUNIT["metre",1]],AXIS["easting (Y)",east,ORDER[2],LENGTHUNIT["metre",1]],AXIS["ellipsoidal height (h)",up,ORDER[3],LENGTHUNIT["metre",1]],REMARK["Promoted to 3D from EPSG:5186"]])WKT";

TEST_CASE("높이 기준 지정: iTwin 타원체고 표기 모델을 EGM96 으로 — 이름표·수직 EPSG·내보내기만, 값 그대로") {
    SrsInfo s; s.srs = kRealPromoted; s.origin = Vec3(148093, 98119, 0); s.hasOrigin = true;
    Box3 lb; lb.add(Vec3(-20, -20, 54)); lb.add(Vec3(20, 20, 64));
    SrsReport r0 = analyzeSrs(s, lb);
    CHECK(r0.labelKo == "수평 EPSG:5186 / 높이 타원체고(GRS80)");
    CHECK(s.verticalEpsg() == 0);
    s.heightDeclared = VDatum::EGM96;
    SrsReport r = analyzeSrs(s, lb);
    CHECK(r.labelKo == "수평 EPSG:5186 / 높이 EGM96 지오이드 기준 높이 (EPSG:5773) [사용자 지정]");
    CHECK(r.warnings.empty());  // 안내 사라짐
    CHECK(r.desc.heightDeclared);
    CHECK(r.desc.srsVerticalKo == "타원체고(GRS80)");
    CHECK(r.tooltipKo.find("SRS 원래 표기: 타원체고(GRS80)") != std::string::npos);
    CHECK(s.epsg() == 5186);
    CHECK(s.verticalEpsg() == 5773);
    CHECK(s.shortLabel() == "EPSG:5186+5773 [height datum declared by user, values not converted; SRS: h=ellipsoidal(GRS80)]");
    // GeoTIFF 수직 GeoKey(4096) 가 지정값을 따름
    GeoRef g = planGeoRef(s, 0, 0, 0.01);
    CHECK(g.epsg == 5186); CHECK(g.vertEpsg == 5773);
    CHECK(g.citation.find("declared") != std::string::npos);
    // 값은 그대로: 원점·좌표 변환 동일
    CHECK(s.toWorld(Vec3(0, 0, 56.8852)).z == 56.8852);
    // 다른 선택
    s.heightDeclared = VDatum::KVD1964; CHECK(s.verticalEpsg() == 5193);
    CHECK(s.describe().labelKo() == "수평 EPSG:5186 / 높이 KVD1964 정표고(인천만 평균해수면) (EPSG:5193) [사용자 지정]");
    s.heightDeclared = VDatum::EGM2008; CHECK(s.verticalEpsg() == 3855);
    s.heightDeclared = VDatum::Ellipsoidal; CHECK(s.verticalEpsg() == 0); CHECK(s.describe().vertKind == VertKind::Ellipsoidal);
    CHECK(analyzeSrs(s, lb).warnings.empty());  // 타원체고로 확인했으면 안내 없음
    s.heightDeclared = VDatum::None; CHECK(analyzeSrs(s, lb).warnings.size() == 1);
    // 복합 SRS(EPSG:5186+5193) 를 EGM96 으로 지정 → 원래 표기 보존
    SrsInfo c; c.srs = "EPSG:5186+5193"; c.heightDeclared = VDatum::EGM96;
    CHECK(c.describe().srsVerticalAscii == "EPSG:5193");
    CHECK(c.verticalEpsg() == 5773);
}

TEST_CASE("높이 기준 지정: 저장 키 왕복, 선택지") {
    for (VDatum d : declarableVDatums()) CHECK(vdatumFromKey(vdatumKey(d)) == d);
    CHECK(vdatumFromKey("none") == VDatum::None);
    CHECK(vdatumFromKey("garbage") == VDatum::Unknown);
    auto v = declarableVDatums();
    CHECK(v.front() == VDatum::EGM96);
    CHECK(std::find(v.begin(), v.end(), VDatum::Ellipsoidal) != v.end());
}
