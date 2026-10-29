// 정밀 높이 피킹(CPU, double): 최고 해상도(잎) 메시에 광선-삼각형 교차.
//  - 화면(GPU)·화면용 LOD 를 쓰지 않는다. 꼭짓점은 원본 float32 로컬 좌표 → double 로 올려 계산, 실좌표 = 로컬 + SRSOrigin(double)
//  - 높이는 모델 SRS 값 그대로(변환 없음)
#pragma once
#include <atomic>
#include "asec/engine.hpp"

namespace asec {

enum class ZSource {
    None,
    LeafSurface,      // 최고 해상도(잎) 표면 — 정밀
    LeafWithFallback, // 일부 타일의 자식 파일이 없어 상위 LOD 로 대체됨
    Coarse,           // 화면용 거친 LOD(대략값, 앱에서만)
    SectionCursor,    // 단면 화면 커서 위치(s,z) — 표면 피킹 아님(앱에서만)
};
const char* zSourceKo(ZSource s);

struct PickResult {
    bool hit = false;
    Vec3 local;              // 교점(로컬, double)
    Vec3 world;              // 교점(실좌표 = 로컬 + SRSOrigin)
    double t = 0;            // 광선 매개변수(로컬 m)
    ZSource source = ZSource::None;
    size_t meshes = 0, trianglesTested = 0;
    LeafStats stats;
    double ms = 0;
};

/// 광선(로컬 double 원점 o, 방향 d) 과 가장 가까운 잎 표면 교점. 메시 경계 상자로 띠를 잡아 그 안의 잎만 읽는다.
bool pickRay(MeshSource& src, const Vec3& o, const Vec3& d, PickResult& out, std::string* err = nullptr, const std::atomic<bool>* cancel = nullptr);
/// 연직 피킹: 로컬 (x,y) 의 가장 높은 표면(지표). 측정·기준점 대조용
bool pickVertical(MeshSource& src, double x, double y, PickResult& out, std::string* err = nullptr, const std::atomic<bool>* cancel = nullptr);
/// 실좌표 (X=동, Y=북) 에서 연직 피킹
inline bool pickVerticalWorld(MeshSource& src, double X, double Y, PickResult& out, std::string* err = nullptr) {
    return pickVertical(src, X - src.srs.origin.x, Y - src.srs.origin.y, out, err);
}
/// 메시 묶음에 대한 광선 교차(double Möller–Trumbore). 가장 가까운 t(>= tMin). 맞으면 true
bool rayMeshes(const std::vector<MeshPtr>& meshes, const Vec3& o, const Vec3& d, double tMin, double& tBest, size_t* tested = nullptr);

}  // namespace asec
