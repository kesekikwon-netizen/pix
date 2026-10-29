// 좌표계 해석: EPSG/복합/ENU/WKT1/WKT2, 실제 iTwin WKT(KGD2002 / Central Belt 2010, 3D 타원체고), TM 계산, 모델 점검
#include "catch_amalgamated.hpp"
#include "asec/srs.hpp"
#include "asec/tmx.hpp"
#include "asec/obj.hpp"
#include <cmath>
#include <cstdlib>

using namespace asec;
using Catch::Approx;

// 사용자 실제 모델(iTwin Capture Engine, Production_2.3mx)의 SRS 원문 그대로
static const char* kRealWkt = R"WKT(PROJCRS["KGD2002 / Central Belt 2010",BASEGEOGCRS["KGD2002",DATUM["Korean Geodetic Datum 2002",ELLIPSOID["GRS 1980",6378137,298.257222101,LENGTHUNIT["metre",1]]],PRIMEM["Greenwich",0,ANGLEUNIT["degree",0.0174532925199433]],ID["EPSG",4927]],CONVERSION["Korea Central Belt 2010",METHOD["Transverse Mercator",ID["EPSG",9807]],PARAMETER["Latitude of natural origin",38,ANGLEUNIT["degree",0.0174532925199433],ID["EPSG",8801]],PARAMETER["Longitude of natural origin",127,ANGLEUNIT["degree",0.0174532925199433],ID["EPSG",8802]],PARAMETER["Scale factor at natural origin",1,SCALEUNIT["unity",1],ID["EPSG",8805]],PARAMETER["False easting",200000,LENGTHUNIT["metre",1],ID["EPSG",8806]],PARAMETER["False northing",600000,LENGTHUNIT["metre",1],ID["EPSG",8807]],ID["EPSG",5102]],CS[Cartesian,3],AXIS["northing (X)",north,ORDER[1],LENGTHUNIT["metre",1,ID["EPSG",9001]]],AXIS["easting (Y)",east,ORDER[2],LENGTHUNIT["metre",1,ID["EPSG",9001]]],AXIS["ellipsoidal height (h)",up,ORDER[3],LENGTHUNIT["metre",1,ID["EPSG",9001]]],USAGE[SCOPE["unknown"],AREA["Republic of Korea (South Korea) - onshore between 126°E and 128°E."],BBOX[33.14,126,38.33,128]],REMARK["Promoted to 3D from EPSG:5186"]])WKT";
// projinfo(PROJ) 로 만든 표본
static const char* kWkt1_5186 = R"WKT(PROJCS["KGD2002 / Central Belt 2010",GEOGCS["KGD2002",DATUM["Korean_Geodetic_Datum_2002",SPHEROID["GRS 1980",6378137,298.257222101,AUTHORITY["EPSG","7019"]],AUTHORITY["EPSG","6737"]],PRIMEM["Greenwich",0,AUTHORITY["EPSG","8901"]],UNIT["degree",0.0174532925199433,AUTHORITY["EPSG","9122"]],AUTHORITY["EPSG","4737"]],PROJECTION["Transverse_Mercator"],PARAMETER["latitude_of_origin",38],PARAMETER["central_meridian",127],PARAMETER["scale_factor",1],PARAMETER["false_easting",200000],PARAMETER["false_northing",600000],UNIT["metre",1,AUTHORITY["EPSG","9001"]],AUTHORITY["EPSG","5186"]])WKT";
static const char* kWkt2_Compound = R"WKT(COMPOUNDCRS["KGD2002 / Central Belt 2010 + KVD1964 height",PROJCRS["KGD2002 / Central Belt 2010",BASEGEOGCRS["KGD2002",DATUM["Korean Geodetic Datum 2002",ELLIPSOID["GRS 1980",6378137,298.257222101,LENGTHUNIT["metre",1]]],PRIMEM["Greenwich",0,ANGLEUNIT["degree",0.0174532925199433]],ID["EPSG",4737]],CONVERSION["Korea Central Belt 2010",METHOD["Transverse Mercator",ID["EPSG",9807]],PARAMETER["Latitude of natural origin",38,ANGLEUNIT["degree",0.0174532925199433],ID["EPSG",8801]],PARAMETER["Longitude of natural origin",127,ANGLEUNIT["degree",0.0174532925199433],ID["EPSG",8802]],PARAMETER["Scale factor at natural origin",1,SCALEUNIT["unity",1],ID["EPSG",8805]],PARAMETER["False easting",200000,LENGTHUNIT["metre",1],ID["EPSG",8806]],PARAMETER["False northing",600000,LENGTHUNIT["metre",1],ID["EPSG",8807]]],CS[Cartesian,2],AXIS["northing (X)",north,ORDER[1],LENGTHUNIT["metre",1]],AXIS["easting (Y)",east,ORDER[2],LENGTHUNIT["metre",1]],USAGE[SCOPE["Cadastre, topographic mapping."],AREA["Republic of Korea (South Korea) - onshore between 126°E and 128°E."],BBOX[33.14,126,38.33,128]],ID["EPSG",5186]],VERTCRS["KVD1964 height",VDATUM["Korean Vertical Datum 1964"],CS[vertical,1],AXIS["gravity-related height (H)",up,LENGTHUNIT["metre",1]],USAGE[SCOPE["Geodesy, engineering survey."],AREA["Republic of Korea (South Korea) - onshore."],BBOX[33.14,124.53,38.64,131.01]],ID["EPSG",5193]]])WKT";
static const char* kWkt1_Compound = R"WKT(COMPD_CS["KGD2002 / Central Belt 2010 + KVD1964 height",PROJCS["KGD2002 / Central Belt 2010",GEOGCS["KGD2002",DATUM["Korean_Geodetic_Datum_2002",SPHEROID["GRS 1980",6378137,298.257222101,AUTHORITY["EPSG","7019"]],AUTHORITY["EPSG","6737"]],PRIMEM["Greenwich",0,AUTHORITY["EPSG","8901"]],UNIT["degree",0.0174532925199433,AUTHORITY["EPSG","9122"]],AUTHORITY["EPSG","4737"]],PROJECTION["Transverse_Mercator"],PARAMETER["latitude_of_origin",38],PARAMETER["central_meridian",127],PARAMETER["scale_factor",1],PARAMETER["false_easting",200000],PARAMETER["false_northing",600000],UNIT["metre",1,AUTHORITY["EPSG","9001"]],AUTHORITY["EPSG","5186"]],VERT_CS["KVD1964 height",VERT_DATUM["Korean Vertical Datum 1964",2005,AUTHORITY["EPSG","1049"]],UNIT["metre",1,AUTHORITY["EPSG","9001"]],AXIS["Gravity-related height",UP],AUTHORITY["EPSG","5193"]]])WKT";
static const char* kEsri_5187 = R"WKT(PROJCS["KGD2002_East_Belt_2010",GEOGCS["GCS_KGD2002",DATUM["D_Korea_Geodetic_Datum_2002",SPHEROID["GRS_1980",6378137.0,298.257222101]],PRIMEM["Greenwich",0.0],UNIT["Degree",0.0174532925199433]],PROJECTION["Transverse_Mercator"],PARAMETER["False_Easting",200000.0],PARAMETER["False_Northing",600000.0],PARAMETER["Central_Meridian",129.0],PARAMETER["Scale_Factor",1.0],PARAMETER["Latitude_Of_Origin",38.0],UNIT["Meter",1.0]])WKT";
static const char* kWkt2_4927 = R"WKT(GEOGCRS["KGD2002",DATUM["Korean Geodetic Datum 2002",ELLIPSOID["GRS 1980",6378137,298.257222101,LENGTHUNIT["metre",1]]],PRIMEM["Greenwich",0,ANGLEUNIT["degree",0.0174532925199433]],CS[ellipsoidal,3],AXIS["geodetic latitude (Lat)",north,ORDER[1],ANGLEUNIT["degree",0.0174532925199433]],AXIS["geodetic longitude (Lon)",east,ORDER[2],ANGLEUNIT["degree",0.0174532925199433]],AXIS["ellipsoidal height (h)",up,ORDER[3],LENGTHUNIT["metre",1]],USAGE[SCOPE["Geodesy."],AREA["Republic of Korea (South Korea) - onshore and offshore."],BBOX[28.6,122.71,40.27,134.28]],ID["EPSG",4927]])WKT";

static std::string replaceAll(std::string s, const std::string& a, const std::string& b) {
    for (size_t p = s.find(a); p != std::string::npos; p = s.find(a, p + b.size())) s.replace(p, a.size(), b);
    return s;
}

TEST_CASE("SRS: 실제 iTwin WKT2 PROJCRS(3D 승격) → 수평 EPSG:5186 + 타원체고(GRS80)") {
    SrsDesc d = describeSrs(kRealWkt);
    CHECK(d.kind == SrsKind::Wkt);
    CHECK(d.horizontalEpsg == 5186);          // CONVERSION ID 5102·BASEGEOGCRS ID 4927 을 잘못 집지 않음
    CHECK(d.horizontalHow == "WKT REMARK");
    CHECK(d.vertKind == VertKind::Ellipsoidal);
    CHECK(d.verticalEpsg == 0);
    CHECK(d.ellipsoid == "GRS 1980");
    CHECK(d.horizontalName == "KGD2002 / Central Belt 2010");
    CHECK(d.axisNorthFirst);
    CHECK(d.hasTm);
    CHECK(d.tmLon0 == 127); CHECK(d.tmFN == 600000); CHECK(d.tmFE == 200000);
    CHECK(d.hasBbox); CHECK(d.bboxS == Approx(33.14)); CHECK(d.bboxE == Approx(128));
    CHECK(d.warnings.empty());
    CHECK(d.labelKo() == "수평 EPSG:5186 / 높이 타원체고(GRS80)");
    CHECK(d.shortAscii() == "EPSG:5186 h=ellipsoidal(GRS80)");
    std::string tip = d.tooltipKo();
    CHECK(tip.find("기준점과 대조") != std::string::npos);   // iTwin 타원체고 표기 주의
    CHECK(tip.find("자동 변환 없음") != std::string::npos);
    CHECK(tip.find("x=동(E), y=북(N)") != std::string::npos);
    SrsInfo s; s.srs = kRealWkt;
    CHECK(s.epsg() == 5186);
    CHECK(s.verticalEpsg() == 0);
}

TEST_CASE("SRS: REMARK 가 없으면 이름으로, 이름도 다르면 TM 매개변수로 EPSG 판별") {
    std::string noRemark = replaceAll(kRealWkt, "REMARK[\"Promoted to 3D from EPSG:5186\"]", "REMARK[\"x\"]");
    SrsDesc a = describeSrs(noRemark);
    CHECK(a.horizontalEpsg == 5186); CHECK(a.horizontalHow == "이름");
    std::string noName = replaceAll(noRemark, "KGD2002 / Central Belt 2010", "My custom TM");
    SrsDesc b = describeSrs(noName);
    CHECK(b.horizontalEpsg == 5186); CHECK(b.horizontalHow == "매개변수");
    CHECK(b.vertKind == VertKind::Ellipsoidal);
    std::string other = replaceAll(noName, "PARAMETER[\"Longitude of natural origin\",127", "PARAMETER[\"Longitude of natural origin\",127.5");
    SrsDesc c = describeSrs(other);
    CHECK(c.horizontalEpsg == 0);
    CHECK_FALSE(c.warnings.empty());
    CHECK(c.labelKo().find("EPSG 미확인") != std::string::npos);
    // REMARK 는 5186 인데 매개변수가 다르면 경고
    std::string mism = replaceAll(kRealWkt, "PARAMETER[\"False northing\",600000", "PARAMETER[\"False northing\",500000");
    SrsDesc m = describeSrs(mism);
    CHECK(m.horizontalEpsg == 5186);
    CHECK(m.warnings.size() == 1);
}

TEST_CASE("SRS: WKT1 PROJCS(AUTHORITY), ESRI 이름, 높이 기준 없음") {
    SrsDesc d = describeSrs(kWkt1_5186);
    CHECK(d.kind == SrsKind::Wkt); CHECK(d.horizontalEpsg == 5186); CHECK(d.horizontalHow == "WKT ID");
    CHECK(d.vertKind == VertKind::Unspecified);
    CHECK(d.labelKo() == "수평 EPSG:5186 / 높이 SRS 에 명시 안 됨");
    CHECK(d.tooltipKo().find("ContextCapture/iTwin 기본은 타원체고") != std::string::npos);
    SrsDesc e = describeSrs(kEsri_5187);
    CHECK(e.horizontalEpsg == 5187); CHECK(e.horizontalHow == "이름");
    CHECK(e.tmLon0 == 129);
}

TEST_CASE("SRS: 복합 COMPOUNDCRS(WKT2)·COMPD_CS(WKT1) → 수평 5186 + 수직 KVD1964(5193)") {
    for (const char* w : {kWkt2_Compound, kWkt1_Compound}) {
        SrsDesc d = describeSrs(w);
        CHECK(d.kind == SrsKind::Wkt);
        CHECK(d.horizontalEpsg == 5186);
        CHECK(d.verticalEpsg == 5193);
        CHECK(d.vertKind == VertKind::Gravity);
        CHECK(d.verticalName == "KVD1964 height");
        CHECK(d.labelKo() == "수평 EPSG:5186 / 높이 KVD1964 정표고(인천만 평균해수면) (EPSG:5193)");
        CHECK(d.vdatum == VDatum::KVD1964);
        CHECK(d.shortAscii() == "EPSG:5186+5193");
        SrsInfo s; s.srs = w;
        CHECK(s.verticalEpsg() == 5193);
    }
}

TEST_CASE("SRS: GEOGCRS 3D(4927) → 타원체고 + 지리 좌표계 경고, BOUNDCRS 는 SOURCECRS 로") {
    SrsDesc d = describeSrs(kWkt2_4927);
    CHECK(d.horizontalEpsg == 4927); CHECK(d.vertKind == VertKind::Ellipsoidal); CHECK_FALSE(d.warnings.empty());
    std::string bound = std::string("BOUNDCRS[SOURCECRS[") + kWkt1_5186 + "],TARGETCRS[GEOGCRS[\"WGS 84\"]],ABRIDGEDTRANSFORMATION[\"x\"]]";
    SrsDesc b = describeSrs(bound);
    CHECK(b.horizontalEpsg == 5186);
}

TEST_CASE("SRS: EPSG 문자열·복합·ENU·없음·잘못된 값") {
    SrsDesc a = describeSrs("EPSG:5186");
    CHECK(a.kind == SrsKind::Epsg); CHECK(a.horizontalEpsg == 5186); CHECK(a.vertKind == VertKind::Unspecified);
    CHECK(a.horizontalName == "KGD2002 / Central Belt 2010"); CHECK(a.hasTm);
    SrsDesc b = describeSrs("EPSG:5186+5193");
    CHECK(b.kind == SrsKind::CompoundEpsg); CHECK(b.verticalEpsg == 5193); CHECK(b.verticalName == "KVD1964 height");
    CHECK(describeSrs("EPSG:5186+EPSG:5193").verticalEpsg == 5193);
    CHECK(describeSrs("  EPSG:5187 ").horizontalEpsg == 5187);
    SrsDesc g = describeSrs("EPSG:4979");
    CHECK(g.vertKind == VertKind::Ellipsoidal);
    SrsDesc e = describeSrs("ENU:37.5,127.25");
    CHECK(e.kind == SrsKind::Enu); CHECK(e.enuLat == 37.5); CHECK(e.enuLon == 127.25); CHECK(e.vertKind == VertKind::LocalEnu);
    CHECK_FALSE(e.warnings.empty());
    CHECK(e.labelKo().find("로컬 ENU") == 0);
    SrsDesc n = describeSrs("");
    CHECK(n.kind == SrsKind::None); CHECK_FALSE(n.warnings.empty()); CHECK(n.labelKo() == "좌표계 없음(로컬 좌표)");
    for (const char* bad : {"garbage", "EPSG:abc", "ENU:x,y", "PROJCRS[\"x\"", "EPSG:5186+", "FOO[\"x\"]"}) {
        SrsDesc u = describeSrs(bad);
        INFO(bad);
        CHECK(u.kind == SrsKind::Unknown);
        CHECK_FALSE(u.warnings.empty());
        CHECK(u.labelKo() == "좌표계 미상(해석 불가)");
    }
    SrsDesc old = describeSrs("EPSG:5174");
    CHECK(old.horizontalEpsg == 5174); CHECK_FALSE(old.warnings.empty());
}

TEST_CASE("WKT 파서: 따옴표 이스케이프·둥근 괄호(WKT1)·중첩") {
    WktNode n;
    REQUIRE(parseWkt("VERT_CS(\"a \"\"b\"\" c\",AUTHORITY(\"EPSG\",\"5193\"))", n));
    CHECK(n.key == "VERT_CS"); CHECK(n.str(0) == "a \"b\" c");
    REQUIRE(n.child("AUTHORITY")); CHECK(n.child("AUTHORITY")->str(1) == "5193");
    CHECK_FALSE(parseWkt("A[\"x\"] trailing", n));
    CHECK_FALSE(parseWkt("A[\"x\",B[1]", n));
}

TEST_CASE("TM(Krüger): PROJ(cs2cs) 기준값과 1 mm 이내, 왕복 0.1 mm 이내") {
    struct R { int epsg; double lat, lon, N, E; };
    const R ref[] = {
        {5186, 38, 127, 600000.0, 200000.0},
        {5186, 33.5, 126.5, 100816.3764, 153538.4958},
        {5186, 37.5, 128.4, 545424.9723, 323798.8422},
        {5186, 34, 125.6, 157047.1817, 70656.4377},
        {5185, 37.5, 127.5, 547441.3993, 421081.9385}, {5185, 36.0, 126.1, 378604.3090, 299181.9568},
        {5186, 37.5, 127.5, 544621.5644, 244212.8688}, {5186, 33.25, 126.9, 72981.1539, 190681.1213},
        {5187, 37.5, 127.5, 545561.2421, 67357.8729}, {5187, 38.3, 129.2, 633318.7197, 217494.6977},
        {5188, 36.0, 126.1, 389168.9262, -241969.6813}, {5188, 38.3, 129.2, 634833.0087, 42541.7304},
    };
    for (auto& r : ref) {
        TmParams p; REQUIRE(tmParamsForEpsg(r.epsg, p));
        double E, N; tmForward(p, r.lat, r.lon, E, N);
        INFO(r.epsg << " " << r.lat << " " << r.lon);
        CHECK(std::fabs(E - r.E) < 0.001);
        CHECK(std::fabs(N - r.N) < 0.001);
        double la, lo; tmInverse(p, E, N, la, lo);
        double e2, n2; tmForward(p, la, lo, e2, n2);
        CHECK(std::fabs(e2 - E) < 1e-4); CHECK(std::fabs(n2 - N) < 1e-4);
        CHECK(std::fabs(la - r.lat) < 1e-9); CHECK(std::fabs(lo - r.lon) < 1e-9);
    }
    // 실제 모델 SRSOrigin(E=148093, N=98119) → 제주(cs2cs: 33.475431115, 126.441556173)
    TmParams p; tmParamsForEpsg(5186, p);
    double la, lo; tmInverse(p, 148093, 98119, la, lo);
    CHECK(std::fabs(la - 33.475431115) < 1e-8);
    CHECK(std::fabs(lo - 126.441556173) < 1e-8);
}

TEST_CASE("analyzeSrs: 실제 모델 원점 → 제주, 축 순서 정상 / x·y 바꾸면 축 순서 경고 / 원점 0 큰 좌표 → 정밀도 경고 / metadata 불일치") {
    SrsInfo s; s.srs = kRealWkt; s.origin = Vec3(148093, 98119, 0); s.hasOrigin = true; s.metadataSrs = "EPSG:5186";
    Box3 lb; lb.add(Vec3(-150, -120, 20)); lb.add(Vec3(160, 130, 60));
    SrsReport r = analyzeSrs(s, lb);
    // iTwin "Promoted to 3D" → 타원체고 표기 확인 안내 1개만
    REQUIRE(r.warnings.size() == 1);
    CHECK(r.warnings[0].find("높이 기준 확인") != std::string::npos);
    CHECK(r.warnings[0].find("EGM96") != std::string::npos);
    CHECK(r.desc.promotedTo3D);
    CHECK(r.hasLatLon);
    CHECK(r.lat == Approx(33.476).margin(0.01)); CHECK(r.lon == Approx(126.442).margin(0.01));
    CHECK_FALSE(r.precisionWarning);
    CHECK(r.labelKo == "수평 EPSG:5186 / 높이 타원체고(GRS80)");
    CHECK(r.tooltipKo.find("SRSOrigin = 148093.000, 98119.000, 0.000") != std::string::npos);

    SrsInfo sw = s; sw.origin = Vec3(98119, 148093, 0);
    SrsReport r2 = analyzeSrs(sw, lb);
    REQUIRE(r2.warnings.size() == 2);
    CHECK(r2.warnings[1].find("축 순서") != std::string::npos);

    SrsInfo z; z.srs = "EPSG:5186"; z.origin = Vec3(); z.hasOrigin = true;
    Box3 big; big.add(Vec3(148000, 98000, 20)); big.add(Vec3(148300, 98300, 60));
    SrsReport r3 = analyzeSrs(z, big);
    CHECK(r3.precisionWarning);
    CHECK(r3.float32StepMm == Approx(15.625));
    CHECK(r3.warnings.back().find("SRSOrigin=0") != std::string::npos);

    SrsInfo mm = s; mm.metadataSrs = "EPSG:5187";
    mm.heightDeclared = VDatum::EGM96;  // 안내를 없애고 불일치 경고만 보기
    CHECK(analyzeSrs(mm, lb).warnings.size() == 1);

    SrsInfo none;
    SrsReport r4 = analyzeSrs(none, lb);
    CHECK(r4.desc.kind == SrsKind::None); CHECK_FALSE(r4.warnings.empty());

    CHECK(float32Step(500000) == 0.03125);
    CHECK(float32Step(8192) == Approx(0.0009765625));
}

TEST_CASE("metadata.xml: XML 엔터티(&quot;)가 든 WKT 도 읽힌다") {
    SrsInfo s;
    REQUIRE(parseMetadataXml("<ModelMetadata><SRS>VERT_CS[&quot;KVD1964 height&quot;,AUTHORITY[&quot;EPSG&quot;,&quot;5193&quot;]]</SRS></ModelMetadata>", s));
    CHECK(s.srs == "VERT_CS[\"KVD1964 height\",AUTHORITY[\"EPSG\",\"5193\"]]");
}
