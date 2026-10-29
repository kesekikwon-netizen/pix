// 높이(수직) 기준 식별 + 나중에 "사용자가 직접 고를 때만" 쓰는 지오이드 변환 틀.
//  원칙: 모델 높이는 SRS 그대로. 이 프로그램은 자동으로 높이를 바꾸지 않는다.
//  식별: EPSG 번호(5773 EGM96, 3855 EGM2008, 5193 KVD1964 인천 평균해수면 …), WKT VERTCRS/VDATUM/GEOIDMODEL 이름,
//        WKT1 EXTENSION["PROJ4_GRIDS"], WKT2 PARAMETERFILE, iTwin 식 "EPSG:5186+EGM96" 같은 글자 표기.
//  변환(미사용·준비만): 타원체고 h ↔ 지오이드 기반 높이 H, H = h − N(위도, 경도). N 은 등록된 GeoidModel(격자)에서.
//  격자는 이 버전에 동봉하지 않는다(라이선스 확인 전). 후보: EGM96 15′ 격자(NGA 공개 — 재배포 조건 미검증),
//  EGM2008(NGA, 미검증), KNGeoid18(국토지리정보원, 공공누리 조건상 동봉 재배포 불확실).
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace asec {

enum class VDatum {
    Unknown,      // 높이 기준이 있으나 무엇인지 모름(이름만 표시)
    None,         // SRS 에 높이 기준 없음
    Ellipsoidal,  // 타원체고 h
    EGM96,        // EGM96 지오이드 기반 높이(EPSG:5773)
    EGM2008,      // EGM2008 지오이드 기반 높이(EPSG:3855)
    KVD1964,      // 한국 정표고(인천만 평균해수면, EPSG:5193)
    KNGeoid,      // KNGeoid(국토지리정보원 하이브리드 지오이드) 기반 높이 — 실질적으로 KVD1964 를 따름
    LocalEnu,     // ENU 로컬 높이
};

struct VDatumInfo {
    VDatum id = VDatum::Unknown;
    int epsg = 0;                // 수직 CRS EPSG(있으면)
    const char* shortName = "";  // "EGM96"
    const char* nameKo = "";     // "EGM96 지오이드 높이"
    const char* geoidKey = "";   // 이 기준으로 바꿀 때 필요한 지오이드 모델 키("egm96", "egm2008", "kngeoid") — 타원체고 등은 ""
};
/// 내장 표(없으면 Unknown 항목)
const VDatumInfo& vdatumInfo(VDatum d);
/// EPSG 번호로(모르면 Unknown)
VDatum vdatumFromEpsg(int epsg);
/// 저장용 키("none","ellipsoidal","egm96","egm2008","kvd1964","kngeoid","enu","unknown") ↔ 값
const char* vdatumKey(VDatum d);
VDatum vdatumFromKey(const std::string& key);
/// 사용자가 "높이 기준 지정"으로 고를 수 있는 것(타원체고, EGM96, EGM2008, KVD1964, KNGeoid)
std::vector<VDatum> declarableVDatums();
/// 이름·격자 파일명 글자로(대소문자·기호 무시: "EGM96 height", "egm96_15.gtx", "KNGeoid18", "Korean Vertical Datum 1964", "Incheon")
VDatum vdatumFromText(const std::string& text);

// ---------------------------------------------------------------- 변환 틀(사용자가 명시적으로 고를 때만)
/// 지오이드 높이 N(m) = 타원체고 − 지오이드 기반 높이
class GeoidModel {
public:
    virtual ~GeoidModel() = default;
    virtual std::string name() const = 0;
    /// 위도·경도(도, 해당 타원체 기준)에서 N. 범위 밖이면 false
    virtual bool undulation(double latDeg, double lonDeg, double& N) const = 0;
};

/// 규칙 격자(위도·경도) + 쌍선형 보간. GTX(PROJ/NOAA 형식: 빅엔디언 double×4 + int32×2 + float32 격자) 읽기 지원
class GridGeoidModel : public GeoidModel {
public:
    std::string label;
    double lat0 = 0, lon0 = 0, dLat = 1, dLon = 1;  // 남서 모서리, 간격(도)
    int rows = 0, cols = 0;                          // rows = 위도 방향
    std::vector<float> N;                            // 행 = 남→북, 열 = 서→동
    float nodata = -88.8888f;
    std::string name() const override { return label; }
    bool undulation(double latDeg, double lonDeg, double& out) const override;
    /// GTX 파일 읽기. 실패 시 false
    bool loadGtx(const std::string& path, std::string* err = nullptr);
};

/// 지오이드 모델 등록부(키: "egm96", "egm2008", "kngeoid"). 비어 있음이 기본(격자 미동봉)
class GeoidRegistry {
public:
    void add(const std::string& key, std::shared_ptr<GeoidModel> m) { models_[key] = std::move(m); }
    const GeoidModel* find(const std::string& key) const { auto it = models_.find(key); return it == models_.end() ? nullptr : it->second.get(); }
    bool empty() const { return models_.empty(); }
private:
    std::map<std::string, std::shared_ptr<GeoidModel>> models_;
};

/// 높이 하나를 from → to 기준으로. 타원체고 ↔ 지오이드 기반, 지오이드 기반 ↔ 다른 지오이드 기반(타원체고 경유).
/// 필요한 격자가 등록부에 없거나 지원하지 않는 조합이면 false(err 에 한국어 이유) — 값은 바꾸지 않는다.
/// 주의: GRS80 과 WGS84 타원체 차이(높이 0.1 mm 수준)는 무시. 기준점 검증 없이 쓰지 말 것.
bool convertHeight(VDatum from, VDatum to, double latDeg, double lonDeg, double z, double& out, const GeoidRegistry& reg, std::string* err = nullptr);

}  // namespace asec
