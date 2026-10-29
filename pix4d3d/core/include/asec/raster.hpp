// 단면 입면 영상: 단면 평면에 수직으로 본 정사(orthographic) 텍스처 영상, 두께 띠로 잘라서 렌더(CPU, Z 버퍼).
// 같은 (s, z) 좌표계를 쓰므로 단면선·레벨선과 정확히 겹친다.
#pragma once
#include <atomic>
#include "asec/section.hpp"

namespace asec {

struct ElevationImage {
    RgbaImage img;         // 행 0 = 위(z1), 열 0 = s0. 빈 곳 alpha = 0
    double s0 = 0, z1 = 0; // 왼쪽 위 모서리의 (s, z)
    double res = 0.01;     // m / 픽셀
    double s1() const { return s0 + img.w * res; }
    double z0() const { return z1 - img.h * res; }
    size_t trianglesDrawn = 0;
};

struct RasterRequest {
    double s0 = 0, s1 = 1, z0 = 0, z1 = 1;  // 영상 범위(m)
    double res = 0.005;                      // m/px
    double dNear = 0, dFar = 0.5;            // 두께 띠(+d = 보는 방향)
    size_t maxPixels = size_t(60) << 20;     // 안전 한도
    double depthFade = 0;                    // 깊이 음영 세기(0 = 끔, 0.6 = 가장 먼 면을 흰색 쪽으로 60%)
    double fadeRef = 2.0;                    // 음영 기준 깊이(m): max(dFar, fadeRef) 에서 최대 — 얇은 띠에서는 약하게
    int threads = 0;                         // 0 = 자동(최대 8), 1 = 단일 스레드
};

/// meshes 를 단면 좌표계로 정사 투영. 텍스처(rgba)가 없으면 법선 음영 회색.
bool renderElevation(const std::vector<MeshPtr>& meshes, const SectionFrame& f, const RasterRequest& rq, ElevationImage& out,
                     const std::atomic<bool>* cancel = nullptr);

/// 평면 정사영상(위에서 수직으로, 가장 높은 면). x0,y1 = 왼쪽 위 모서리(로컬), res m/px, W×H
bool renderPlan(const std::vector<MeshPtr>& meshes, double x0, double y1, double res, int W, int H, RgbaImage& out, const std::atomic<bool>* cancel = nullptr);

/// 메시들의 z 범위(띠 안 삼각형 기준)
bool bandZRange(const std::vector<MeshPtr>& meshes, const SectionFrame& f, double dNear, double dFar, double sMin, double sMax, double& zmin, double& zmax);

}  // namespace asec
