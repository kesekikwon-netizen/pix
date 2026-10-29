// LAS 점구름 판독(PIX4D 개조, P1+P2): 높이(Z) 좌표가 있는 3D 파일을 그대로 열어
//  평면 표시(정사영상 스플랫)와 단면(입면 영상·곡선 근사)까지 App 기존 파이프라인에 연결한다.
//  좌표계: 파일 기록 EPSG를 존중(5186/5187 내외면 그대로), 기록 없으면 열리고 경고만 남긴다.
//  내부 원칙: 3MX와 같은 "로컬 좌표 + SRSOrigin" 모델로 맞춘다(origin = 실좌표 최소값 아래 버림).
//  LASzip(.laz)은 지원하지 않는다.
#pragma once
#include "asec/engine.hpp"
#include "asec/geom.hpp"
#include "asec/pointcloud.hpp" // PointFormat
#include "asec/raster.hpp"
#include "asec/section.hpp"
#include "asec/tmx.hpp" // SrsInfo
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace asec {

// ---- 파일 판독 스트리밍(전체 점을 메모리에 두지 않음) ----
struct LasInfo {
  uint8_t versionMinor = 0;                 // 1.x의 x
  uint8_t pointFormat = 0;                  // 0..10(샘플만 RGB: 2,3,5,7,8,10)
  uint64_t pointCount = 0;                  // 헤더 기록(정보용)
  double scale = 0, ox = 0, oy = 0, oz = 0; // x = int*scale + offset
  Box3 worldBounds;                         // 파일이 선언한 실좌표 바운딩박스(검증용)
  int epsg = 0;                             // 수평 EPSG(VLR에서). 0 = 기록 없음/모름
  int verticalEpsg = 0;                     // 수직 좌표계 EPSG(복합일 때). 타원체고면 0
  bool hasRgb = false;                      // RGB 필드 존재
  std::string note;                         // 사용자에게 보여 줄 참고(한국어)
};

/// 점 방문자. world = 실좌표(X=동,E, Y=북,N, Z=SRS 그대로). rgb는 있는 파일만 !=nullptr(3바이트
/// R,G,B).
using LasPointVisit =
    std::function<bool(uint64_t i, double x, double y, double z, const uint8_t *rgb)>;
/// LAS 1.x 퍼블릭 헤더 파일을 순서대로 방문. 파일이 깨지면 false(*err). 레거시 처리만(1.0–1.4,
/// point data record length 등은 헤더 그대로).
bool readLas(const std::string &path, const LasPointVisit &visit, LasInfo &out,
             std::string *err = nullptr, const std::atomic<bool> *cancel = nullptr);

// ---- 메모리 점구름(로컬 float SoA + RGB) ----
struct PointCloud {
  std::vector<float> x, y, z; // 로컬 좌표(작은 값·float32). world = local + srs.origin
  std::vector<uint8_t> rgb;   // 3바이트/점(빈 배열 = 색 없음)
  SrsInfo srs;                // origin = 실좌표 최소값(버림). XYZ는 그대로Euclidean
  Box3 bounds;                // 로컬 바운딩박스(=사용자 모델 coordinate)
  uint64_t totalInFile = 0;   // 열기 전 파일 점 수
  std::string note;           // 열기 요약(감쇠·좌표계 가정 등, 한국어)
  size_t size() const { return x.size(); }
};

/// LAS 파일을 로컬 PointCloud로 감쇠 적재(maxPoints 초과 시 균등 간격 샘플링으로 줄임 — 씬 전체 첫
/// 부분 보존).
bool loadPointCloud(const std::string &path, uint64_t maxPoints, PointCloud &out,
                    std::string *err = nullptr, const std::atomic<bool> *cancel = nullptr);

// ---- 지형(DEM) 메시: 보기·커서 Z·픽/내보내기가 재사용 ----
/// 등간격 격자 셀 mean z → 사각 격자 메시(로컬 좌표, uv는 바운딩박스 정사영상 매핑). cell<=0이면
/// 자동(약 380×380 셀 이하).
bool terrainMeshFromPoints(const PointCloud &pc, double cell, Mesh &out,
                           const std::atomic<bool> *cancel = nullptr);

// ---- 평면(정사영상) 스플랫 래스터: exportPlan 과 같은 좌표 규약 ----
/// x0,y1 = 이미지 왼쪽/위 실좌표(엔다운: row 0 = y1). res m/px. 색 = 점 RGB(없으면 회색). 배경은
/// 투명.
bool renderPlanPoints(const PointCloud &pc, double x0, double y1, double res, int W, int H,
                      RgbaImage &out, const std::atomic<bool> *cancel = nullptr);

// ---- 단면: 기존 computeSection 과 같은 결과(SectionResult) 규약 유지 ----
/// 점구름 단면. 이미지 경계는 computeSection 과 동일(s0=0, s1=f.L, z0=zMin, z1=zMax,
/// res=rq.imageRes, 근영 rr.dNear=-front, rr.dFar=back). 컷 평면 d=0 기준 뒤쪽(0..back) 점만 그려
/// 메시 절단과 같아 보인다. 곡선 근사는 |d| ≤ max(0.1, 0.4*spacing) 슬랩의 셀 bin 최대 z →
/// buildProfile → snapEnds.
bool computeSectionPoints(const PointCloud &pc, const SectionRequest &rq, SectionOutput &out,
                          std::string *err = nullptr, const std::atomic<bool> *cancel = nullptr);

// ---- 점 내보내기: 메시 기반 exportPoints와 같은 옵션의 점구름 버전 ----
enum class PtArea { Whole, Band, Box };
struct PtExportOptions {
  PointFormat format = PointFormat::XYZ; // XYZ 텍스트 / LAS(1.2, point format 2=RGB)
  PtArea area = PtArea::Whole;
  double spacing = 0; // >0: 두 점이 이보다 가까우지 않게 거친 셀 데시메이션
  Box3 box;           // area==Box 일 때(로컬 좌표)
  bool rgb = true;
};
struct PtExportStats {
  uint64_t points = 0;
};
bool exportPointCloud(const PointCloud &src, const SrsInfo &srs, const SectionLine &bandLine,
                      const PtExportOptions &o, const std::string &path, PtExportStats &out,
                      std::string *err = nullptr, const std::atomic<bool> *cancel = nullptr,
                      const std::function<void(size_t)> &progress = {});

} // namespace asec
