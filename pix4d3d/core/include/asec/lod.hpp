// 3MX LOD 트리 탐색: 화면용 예산 선택, 단면용 최고 해상도(잎) 타일 수집. 스레드 안전 캐시.
#pragma once
#include <functional>
#include <map>
#include <mutex>
#include "asec/section.hpp"
#include "asec/tmx.hpp"

namespace asec {

/// 타일 파일 캐시(경로 → TmxTile). 디코드된 노드는 그대로 보관. 바이트 예산을 넘으면 오래된 것부터 버림.
class TileCache {
public:
    explicit TileCache(size_t byteBudget = size_t(1536) << 20) : budget_(byteBudget) {}
    std::shared_ptr<TmxTile> get(const fs::path& p, std::string* err);
    /// 노드 디코드(+선택: 텍스처 디코드 콜백). 스레드 안전: 타일 단위 잠금으로 지오메트리를 디코드하고,
    /// 텍스처(JPG) 디코드는 잠금 밖에서 한다. 돌려준 뒤에는 그 노드의 메시·텍스처가 완성되어 있다.
    bool decode(const std::shared_ptr<TmxTile>& t, size_t node, std::string* err);
    /// 지오메트리만(텍스처 JPG 디코드 없음) — 단면선(잘린 면) 계산용. 나중에 decode() 하면 텍스처만 이어서 채움
    bool decodeGeometry(const std::shared_ptr<TmxTile>& t, size_t node, std::string* err);
    std::function<void(Texture&)> textureDecoder;  // 앱이 JPG→RGBA 를 채움(여러 스레드에서 동시에 불릴 수 있음)
    size_t loadedBytes() const;
    void clear();
private:
    mutable std::mutex mu_;
    std::map<fs::path, std::shared_ptr<TmxTile>> tiles_;
    std::map<fs::path, uint64_t> lastUse_;
    std::map<fs::path, size_t> bytes_;  // 타일별 메모리(디코드 때 갱신) — 다른 스레드가 디코드 중인 타일을 훑지 않으려고 따로 둠
    uint64_t clock_ = 0;
    size_t budget_;
    void evictLocked();
};

struct LeafStats { size_t tilesVisited = 0, leafNodes = 0, meshes = 0, triangles = 0; int maxDepth = 0; size_t fallbackNodes = 0; };

/// 띠와 겹치는 최고 해상도(자식 없는) 노드의 메시를 모은다. 자식 파일을 못 읽으면 그 노드 메시로 대체(fallbackNodes).
bool collectLeafMeshes(TileCache& cache, const fs::path& root, const BandQuad& band, std::vector<MeshPtr>& out, LeafStats* st, std::string* err,
                       const std::atomic<bool>* cancel = nullptr, bool textures = true, size_t maxThreads = 8);

/// 정사영상 내보내기용: XY 상자와 겹치는 노드 중 해상도 res(m/px)에 충분한 단계(3MX maxScreenDiameter 규칙:
/// 노드 대각선/res <= maxScreenDiameter 이면 그 노드, 아니면 자식으로). 잎은 항상 사용.
bool collectMeshesForResolution(TileCache& cache, const fs::path& root, const Box3& areaXY, double res, std::vector<MeshPtr>& out, LeafStats* st, std::string* err,
                                const std::atomic<bool>* cancel = nullptr);
/// 단면 미리보기용: 띠와 겹치는 노드 중 해상도 res(m)에 충분한 단계(위와 같은 규칙). res<=0 이면 잎(collectLeafMeshes)
bool collectBandMeshesForResolution(TileCache& cache, const fs::path& root, const BandQuad& band, double res, std::vector<MeshPtr>& out, LeafStats* st,
                                    std::string* err, const std::atomic<bool>* cancel = nullptr);

/// 화면 표시용: 큰 노드부터 세분해 삼각형 예산 안에서 가장 고른 해상도 집합 선택.
bool selectDisplayMeshes(TileCache& cache, const fs::path& root, size_t triBudget, std::vector<MeshPtr>& out, Box3* bbox, std::string* err,
                         const std::function<void(double)>& progress = {});

}  // namespace asec
