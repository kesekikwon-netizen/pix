// 단면 계산 파이프라인(앱·CLI·향후 MDL 공용): 최고 해상도 타일 수집 → 자르기 → 이어붙이기·정리 → 입면 영상
#pragma once
#include <chrono>
#include "asec/export.hpp"
#include "asec/lod.hpp"
#include "asec/raster.hpp"

namespace asec {

/// 메시 공급원: 3MX(LOD 트리) 또는 고정 메시(OBJ)
class MeshSource {
public:
    virtual ~MeshSource() = default;
    virtual bool leafMeshes(const BandQuad& band, std::vector<MeshPtr>& out, LeafStats* st, std::string* err, const std::atomic<bool>* cancel) = 0;
    /// 해상도 res(m)에 충분한 LOD 의 메시(미리보기). 기본 = 잎. res<=0 이면 잎
    virtual bool bandMeshes(const BandQuad& band, double res, std::vector<MeshPtr>& out, LeafStats* st, std::string* err, const std::atomic<bool>* cancel) {
        (void)res;
        return leafMeshes(band, out, st, err, cancel);
    }
    /// 단면선(잘린 면)용 최고 해상도 잎 메시 — 텍스처 불필요. 기본 = leafMeshes
    virtual bool cutMeshes(const BandQuad& band, std::vector<MeshPtr>& out, LeafStats* st, std::string* err, const std::atomic<bool>* cancel) {
        return leafMeshes(band, out, st, err, cancel);
    }
    SrsInfo srs;
    Box3 bounds;  // 로컬
};

class TmxSource : public MeshSource {
public:
    TmxScene scene;
    std::shared_ptr<TileCache> cache = std::make_shared<TileCache>();
    bool open(const fs::path& p, std::string* err);
    bool leafMeshes(const BandQuad& band, std::vector<MeshPtr>& out, LeafStats* st, std::string* err, const std::atomic<bool>* cancel) override;
    bool bandMeshes(const BandQuad& band, double res, std::vector<MeshPtr>& out, LeafStats* st, std::string* err, const std::atomic<bool>* cancel) override;
    /// 잎 지오메트리만(텍스처 디코드 없음, 디코드 스레드 2개 — 끄는 동안 화면·평면 스트리밍과 CPU 를 덜 다툼)
    bool cutMeshes(const BandQuad& band, std::vector<MeshPtr>& out, LeafStats* st, std::string* err, const std::atomic<bool>* cancel) override;
    /// XY 상자 + 해상도(정사영상용, 모든 레이어)
    bool areaMeshes(const Box3& areaXY, double res, std::vector<MeshPtr>& out, LeafStats* st, std::string* err, const std::atomic<bool>* cancel = nullptr);
};

class StaticSource : public MeshSource {
public:
    std::vector<MeshPtr> meshes;
    bool leafMeshes(const BandQuad& band, std::vector<MeshPtr>& out, LeafStats* st, std::string* err, const std::atomic<bool>* cancel) override;
};

struct SectionRequest {
    SectionLine line;
    CleanupParams cleanup;
    bool wantImage = true;
    double imageRes = 0.004;   // m/px
    size_t maxImagePixels = size_t(24) << 20;
    double zMargin = 0.25;     // 표시 범위 위아래 여유
    double depthFade = 0;      // 입면 깊이 음영(0 = 끔). 먼 면일수록 옅게
    double meshRes = 0;        // >0: 미리보기 — 입면 영상은 이 해상도(m)에 충분한 거친 LOD(빠름). 0: 최고 해상도 잎(최종)
    /// 잘린 면(단면선)은 미리보기에서도 최고 해상도 잎으로 자름(단면 평면 ±1 mm 얇은 띠의 잎만 모음 — 뒤 3 m 배경과 무관).
    /// false 면 옛 동작(미리보기는 거친 LOD 로 자름) — 비교 시험용
    bool leafProfileAlways = true;
};

struct SectionOutput {
    SectionResult result;
    ElevationImage image;
    LeafStats stats;           // 입면 영상(두께 띠)에 쓴 메시
    LeafStats cutStats;        // 단면선(잘린 면)에 쓴 메시
    bool previewLod = false;   // 입면 영상을 거친 LOD 로 그린 미리보기
    bool cutFromLeaf = true;   // 단면선을 최고 해상도 잎으로 자름
    double msCollect = 0, msCut = 0, msImage = 0;
};

bool computeSection(MeshSource& src, const SectionRequest& rq, SectionOutput& out, std::string* err, const std::atomic<bool>* cancel = nullptr);

}  // namespace asec
