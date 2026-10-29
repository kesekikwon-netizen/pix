// 좌표계(SRS) 문자열 해석 — PROJ 없이(의존 없음).
//  지원: "EPSG:n", 복합 "EPSG:h+v", "ENU:lat,lon", WKT1(PROJCS/GEOGCS/COMPD_CS/VERT_CS), WKT2(PROJCRS/GEOGCRS/COMPOUNDCRS/VERTCRS/BOUNDCRS)
//  원칙: 높이는 모델 SRS 그대로(변환·지오이드 보정 안 함). 여기서는 무엇인지 "알아보고 보여 주기"만 한다.
//  주의: 3MX 의 x,y 는 SRS 의 공식 축 순서와 상관없이 x=동(E), y=북(N) 으로 저장된다(실제 iTwin 표본으로 확인:
//        EPSG:5186 WKT 의 축은 northing(X), easting(Y) 이지만 SRSOrigin [148093, 98119] = E, N → 제주).
#pragma once
#include <string>
#include <vector>
#include "asec/vdatum.hpp"

namespace asec {

enum class SrsKind { None, Epsg, CompoundEpsg, Wkt, Enu, Unknown };
enum class VertKind {
    Unspecified,   // SRS 에 높이 기준이 없음(예: "EPSG:5186" 2D)
    Ellipsoidal,   // 타원체고 h (3D 투영/지리 좌표계, AXIS "ellipsoidal height")
    Gravity,       // 수직 좌표계(VERTCRS/VERT_CS, 예: KVD1964 height = 정표고 계열)
    LocalEnu,      // ENU 로컬(원점 접평면 기준)
};

/// WKT 트리(키워드[인자...]). 인자는 따옴표 문자열·숫자·이름(그대로 text) 또는 하위 노드
struct WktNode {
    std::string key;                 // 대문자 키워드. 잎(값)이면 빈 문자열
    std::string text;                // 잎 값(따옴표 제거)
    bool quoted = false;
    std::vector<WktNode> kids;
    const WktNode* child(const char* k) const;            // 첫 하위 노드(키워드 일치, 대소문자 무시)
    std::vector<const WktNode*> children(const char* k) const;
    std::string str(size_t i) const;                      // i 번째 잎 값(없으면 "")
    double num(size_t i, double def = 0) const;
};
/// WKT 파서. 실패 시 false(err 에 위치)
bool parseWkt(const std::string& s, WktNode& out, std::string* err = nullptr);

struct SrsDesc {
    SrsKind kind = SrsKind::None;
    int horizontalEpsg = 0;          // 수평(2D) EPSG. 0 = 모름
    std::string horizontalHow;       // 판별 근거: "EPSG 코드" / "WKT ID" / "WKT REMARK" / "이름" / "매개변수"
    int verticalEpsg = 0;            // 수직 좌표계 EPSG(복합일 때). 타원체고는 0
    VertKind vertKind = VertKind::Unspecified;
    std::string horizontalName;      // 예: "KGD2002 / Central Belt 2010"
    std::string verticalName;        // 예: "KVD1964 height" / "ellipsoidal height (h)"
    std::string verticalDatum;       // VDATUM/VERT_DATUM 이름(예: "EGM96 geoid")
    std::string geoidModel;          // WKT2 GEOIDMODEL / WKT1 EXTENSION PROJ4_GRIDS / PARAMETERFILE(예: "egm96_15.gtx")
    bool promotedTo3D = false;       // WKT REMARK "Promoted to 3D from EPSG:…"(iTwin 이 2D 좌표계를 3D 로 승격 → 높이를 '타원체고'로 표기)
    bool heightDeclared = false;     // 사용자가 "높이 기준 지정"으로 이름표를 바꿈(값 변환 없음)
    std::string srsVerticalKo, srsVerticalAscii;  // 지정 전 SRS 원래 높이 표기(지정했을 때만)
    VDatum vdatum = VDatum::None;    // 식별한 높이 기준(EGM96·EGM2008·KVD1964·KNGeoid·타원체고 …). 표시용 — 높이 값은 안 바꿈
    std::string ellipsoid;           // 예: "GRS 1980"
    bool axisNorthFirst = false;     // WKT 공식 축 순서가 북→동(3MX 좌표 순서와 무관 — 표시용)
    double enuLat = 0, enuLon = 0;   // ENU 원점
    // TM 매개변수(투영 좌표계일 때, WKT 또는 내장 표)
    bool hasTm = false;
    double tmLat0 = 0, tmLon0 = 0, tmK0 = 1, tmFE = 0, tmFN = 0, tmA = 6378137.0, tmInvF = 298.257222101;
    double bboxS = 0, bboxW = 0, bboxN = 0, bboxE = 0; bool hasBbox = false;  // WKT USAGE BBOX(위도·경도)
    std::vector<std::string> warnings;  // 사용자에게 보여 줄 경고(한국어)

    bool known() const { return kind != SrsKind::None && kind != SrsKind::Unknown; }
    /// UI 한 줄: "수평 EPSG:5186 / 높이 타원체고(GRS80)"
    std::string labelKo() const;
    /// 수직 기준만: "타원체고(GRS80)" / "KVD1964 height (EPSG:5193)" / "SRS 에 명시 안 됨"
    std::string verticalKo() const;
    /// 파일용 짧은 ASCII 표기: "EPSG:5186 h=ellipsoidal(GRS 1980)" / "EPSG:5186+5193" / "ENU:37.5,127" / "unknown SRS"
    std::string shortAscii() const;
    /// 툴팁: 이름·판별 근거·축 순서·경고·주의(높이 변환 안 함 등) 여러 줄
    std::string tooltipKo() const;
};

/// SRS 문자열 해석(빈 문자열 = None)
SrsDesc describeSrs(const std::string& srs);
/// "높이 기준 지정": 높이 값은 그대로 두고 높이 기준 이름표만 v 로 바꾼다(None/Unknown 이면 아무것도 안 함).
/// 수직 EPSG(GeoTIFF 4096·LAS 키)와 표기·툴팁이 지정값을 따르고, 원래 SRS 표기는 srsVertical* 에 남는다.
void applyHeightDeclaration(SrsDesc& d, VDatum v);
/// 내장 표: 수평 EPSG 이름(모르면 "")
const char* epsgName(int epsg);
/// 내장 표: 수직 EPSG 이름(모르면 "")
const char* verticalEpsgName(int epsg);

// ---- 횡메르카토르(TM) 계산(Krüger 6차 급수, 서브 mm) — 표시/검증용(높이는 다루지 않음) ----
struct TmParams { double a = 6378137.0, invF = 298.257222101, lat0 = 38, lon0 = 127, k0 = 1, FE = 200000, FN = 600000; };
/// 내장 표의 TM 매개변수(5179–5188, 32651/32652). 없으면 false
bool tmParamsForEpsg(int epsg, TmParams& out);
void tmForward(const TmParams& p, double latDeg, double lonDeg, double& E, double& N);
void tmInverse(const TmParams& p, double E, double N, double& latDeg, double& lonDeg);

}  // namespace asec
