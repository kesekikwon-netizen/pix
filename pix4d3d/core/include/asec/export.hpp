// 단면 결과 → DXF(벡터 + 래스터 참조). MDL 플러그인 등에서 그대로 재사용할 수 있게 Qt 무관.
#pragma once
#include "asec/dxf.hpp"
#include "asec/section.hpp"
#include "asec/tiff.hpp"
#include "asec/tmx.hpp"

namespace asec {

struct SectionResult {
    SectionLine line;
    SrsInfo srs;
    std::vector<Polyline> profile;  // (s, z로컬) 정리된 단면선
    double zMin = 0, zMax = 0;      // 로컬 z 표시 범위
    size_t rawSegments = 0, triangles = 0, tiles = 0;
};

enum class DxfCoordMode { Drawing2D, World3D };

struct DxfExportOptions {
    DxfCoordMode mode = DxfCoordMode::Drawing2D;  // 2D: X=A 기준 거리, Y=절대표고 / 3D: 실좌표(EPSG), 단면 수직면
    bool levels = true;
    int levelStepCm = 10;
    double scaleDenom = 20;   // 문자 크기 계산용(1/20 → 2 mm 글자 = 0.04 m)
    double textMm = 2.0;
    std::string title = "A-A'";
    // 래스터(선택)
    bool image = false;
    std::string imageFile;    // DXF 기준 상대 파일 이름(PNG)
    int imageW = 0, imageH = 0;
    double imageS0 = 0, imageZ0 = 0, imageRes = 0;  // 왼쪽 아래 (s, z로컬), m/px
};

/// 로컬 (s,z) → 실좌표 XYZ
Vec3 sectionToWorld(const SectionResult& r, double s, double zLocal);
bool exportSectionDxf(const SectionResult& r, const DxfExportOptions& o, const fs::path& out, std::string* err, std::string* dxfText = nullptr);

/// 평면 정사영상 GeoTIFF 기준: 왼쪽 위(로컬 x0, y1) + 해상도 → 실좌표 EPSG
GeoRef planGeoRef(const SrsInfo& srs, double x0Local, double y1Local, double res);
/// 단면 영상 GeoTIFF 기준(로컬 단면 좌표): X = A 로부터 거리(m), Y = 절대표고(m). 픽셀(0,0) 왼쪽 위 = (s0, zTopAbs)
GeoRef sectionGeoRef(const SectionResult& r, double s0, double zTopAbs, double res);
/// 방위각(진북 기준 시계 방향, 도) A→A′
double sectionAzimuthDeg(const SectionLine& l);
/// 단면 영상 보조 파일(JSON): A·A′ 실좌표, 길이, 방위, 두께, 축척·DPI·픽셀 크기, 픽셀→좌표 식
bool writeSectionSidecar(const fs::path& p, const SectionResult& r, const GeoRef& g, double scaleDenom, double dpi, int w, int h, std::string* err);

}  // namespace asec
