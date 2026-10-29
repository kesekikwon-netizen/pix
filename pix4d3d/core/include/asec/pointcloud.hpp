// 3MX → 점군 내보내기(XYZ 텍스트 / LAS 1.2 점 형식 2). 최고 해상도(잎) 꼭짓점, 실좌표(SRSOrigin 적용), 텍스처 RGB(선택), 복셀 솎기(선택).
#pragma once
#include <functional>
#include "asec/lod.hpp"

namespace asec {

enum class PointArea { Whole, Band, Box };
enum class PointFormat { XYZ, LAS };

struct PointExportOptions {
    PointArea area = PointArea::Whole;
    BandQuad band;        // area == Band (로컬 XY)
    Box3 box;             // area == Box (로컬 XY, z 무시)
    double spacing = 0;   // 0 = 모든 꼭짓점(1 mm 안에 겹치는 점은 1개로), >0 = 복셀 크기(m) 당 1점
    bool rgb = true;      // 텍스처에서 색 추출(없으면 회색 180)
    PointFormat format = PointFormat::XYZ;
    int decimals = 3;     // XYZ 텍스트 소수 자리(3 = mm)
};

struct PointExportStats { size_t points = 0, vertices = 0, meshes = 0; Box3 worldBounds; };

/// 잎 메시를 하나씩 방문(메모리에 전부 올리지 않음). nodeFilter(bb) 가 false 면 그 가지를 건너뜀. cb 가 false 면 중단.
bool visitLeafMeshes(TileCache& cache, const fs::path& root, const std::function<bool(const Box3&)>& nodeFilter,
                     const std::function<bool(const MeshPtr&)>& cb, std::string* err, const std::atomic<bool>* cancel = nullptr);

/// 메시 묶음(3MX 잎 방문 또는 OBJ 고정 메시)을 점군으로. meshVisitor 가 각 메시를 넘겨준다.
bool exportPoints(const std::function<bool(const std::function<bool(const MeshPtr&)>&)>& meshVisitor, const SrsInfo& srs,
                  const PointExportOptions& o, const fs::path& out, PointExportStats* st, std::string* err, const std::atomic<bool>* cancel = nullptr,
                  const std::function<void(size_t)>& progress = {});

/// 3MX 편의 함수
bool exportPointsTmx(TileCache& cache, const std::vector<fs::path>& roots, const SrsInfo& srs, const PointExportOptions& o, const fs::path& out,
                     PointExportStats* st, std::string* err, const std::atomic<bool>* cancel = nullptr, const std::function<void(size_t)>& progress = {});
inline bool exportPointsTmx(TileCache& cache, const fs::path& root, const SrsInfo& srs, const PointExportOptions& o, const fs::path& out,
                            PointExportStats* st, std::string* err, const std::atomic<bool>* cancel = nullptr, const std::function<void(size_t)>& progress = {}) {
    return exportPointsTmx(cache, std::vector<fs::path>{root}, srs, o, out, st, err, cancel, progress);
}

}  // namespace asec
