// TIFF / GeoTIFF 작성(외부 라이브러리 없이 태그 직접 기록, Deflate+수평 예측자). 축척·DPI ↔ 지상 해상도.
#pragma once
#include <filesystem>
#include "asec/geom.hpp"

namespace asec {

/// 축척 1:denom, 인쇄 dpi 일 때 픽셀 하나의 실제 크기(m) = 0.0254 × denom / dpi
inline double groundResolution(double scaleDenom, double dpi) { return 0.0254 * scaleDenom / dpi; }
/// 1 m 당 픽셀 수
inline double pixelsPerMeter(double scaleDenom, double dpi) { return dpi / (0.0254 * scaleDenom); }

struct GeoRef {
    bool enabled = false;
    double tieX = 0, tieY = 0;      // 픽셀 (0,0) 의 왼쪽 위 모서리 좌표(RasterPixelIsArea)
    double scaleX = 1, scaleY = 1;  // m / 픽셀 (양수, y 는 아래로 감소)
    int epsg = 0;                   // 0 = 사용자 정의(로컬: 단면 거리·표고 등)
    int vertEpsg = 0;               // 수직 기준(복합 EPSG 의 '+' 뒤, 예 5711). 0 = 기록 안 함
    std::string citation;
};

struct TiffOptions {
    double dpi = 300;
    bool deflate = true;
    bool alpha = true;  // RGBA(투명) / false 면 RGB(흰 바탕 합성)
    GeoRef geo;
    std::string description;
    std::string software = "Excavation Section Viewer";
};

bool writeTiff(const std::filesystem::path& p, const RgbaImage& img, const TiffOptions& o, std::string* err);
/// ESRI 월드 파일(.tfw): 픽셀 중심 기준
bool writeWorldFile(const std::filesystem::path& p, const GeoRef& g, std::string* err);

}  // namespace asec
