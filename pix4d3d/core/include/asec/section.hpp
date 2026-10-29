// 단면 자르기: 수직 단면 평면 × 삼각형, 선분 이어 붙이기·정리, 레벨선, 두께 띠.
#pragma once
#include <atomic>
#include "asec/geom.hpp"

namespace asec {

/// 단면선 A→B (로컬 XY). 단면 두께 띠: 단면 평면에서 보는 방향 쪽으로 back, 반대쪽으로 front (m).
struct SectionLine {
    Vec2 a, b;
    double front = 0.0;  // 관찰자 쪽(앞) 두께
    double back = 3.0;   // 보는 방향(뒤) 깊이 — 입면 배경. 기본 3 m(입면도용, 1.2.1~; asec::kDefaultBackDepth 와 같음)
};

/// 단면 좌표계. s = A 에서의 거리(수평), d = 평면에서의 거리(+ = 보는 방향), z = 높이.
/// 화면: 오른쪽 = A→B, 위 = +Z, 관찰자는 d<0 쪽에서 +d 방향을 본다(n = A→B 의 왼쪽 법선).
struct SectionFrame {
    Vec2 a, u, n;
    double L = 0;
    explicit SectionFrame(const SectionLine& l) {
        a = l.a;
        Vec2 d = l.b - l.a;
        L = d.len();
        u = L > 0 ? d * (1.0 / L) : Vec2(1, 0);
        n = Vec2(-u.y, u.x);
    }
    double s(double x, double y) const { return (x - a.x) * u.x + (y - a.y) * u.y; }
    double d(double x, double y) const { return (x - a.x) * n.x + (y - a.y) * n.y; }
    Vec2 planXY(double s_, double d_ = 0) const { return a + u * s_ + n * d_; }
};

struct CutSeg { double s0, z0, s1, z1; };
using Polyline = std::vector<Vec2>;  // (s, z)

/// 메시를 d = dOffset 평면으로 잘라 [sMin,sMax] 안의 선분을 out 에 추가. 반환 = 추가된 선분 수.
/// 꼭짓점이 평면 위(d==0)면 + 쪽으로 간주(기호적 섭동) → 겹선·퇴화 선분 없음.
/// 공유 모서리 교점은 꼭짓점 번호 순으로 계산해 이웃 삼각형과 비트 단위로 같은 점이 나온다.
size_t cutMesh(const Mesh& m, const SectionFrame& f, double dOffset, double sMin, double sMax, std::vector<CutSeg>& out);

struct CleanupParams {
    double weldTol = 0.0005;     // 0.5 mm 이내 점 합치기
    double gapTol = 0.03;        // 3 cm 이내 끊김 잇기(타일 경계·작은 구멍)
    double spikeLen = 0.02;      // 2 cm 이하 되돌아가는 가시 제거
    double spikeTurnDeg = 150;   // 진행 방향이 이 각도 이상 꺾이면 가시
    double minFragment = 0.02;   // 2 cm 미만 조각 버림
    double simplifyTol = 0.0005; // 0.5 mm 더글라스-포이커(형상 보존)
    bool smooth = false;         // 가벼운 평활(기본 끔 = 원본 충실)
    int smoothIter = 3;
};

/// 선분 → 연속 폴리라인(용접 격자 해시). 분기점(차수≥3)에서 끊는다.
std::vector<Polyline> stitchSegments(const std::vector<CutSeg>& segs, double weldTol);
/// 정리: 중복점 제거 → 가시 제거 → 끊김 잇기 → 작은 조각 제거 → 단순화 → (선택)평활 → s 증가 방향·정렬
void cleanupPolylines(std::vector<Polyline>& lines, const CleanupParams& p);
inline std::vector<Polyline> buildProfile(const std::vector<CutSeg>& segs, const CleanupParams& p) {
    auto L = stitchSegments(segs, p.weldTol);
    cleanupPolylines(L, p);
    return L;
}

/// 끝점이 단면 끝(s0/s1)에서 tol 이내면 마지막 선분 연장선 위로 정확히 맞춤(용접으로 생긴 0.x mm 어긋남 제거)
void snapEnds(std::vector<Polyline>& lines, double s0, double s1, double tol);

// 개별 정리 단계(시험용으로 공개)
void removeDuplicatePoints(Polyline& pl, double tol);
int removeSpikes(Polyline& pl, double maxLen, double turnDeg);
void joinGaps(std::vector<Polyline>& lines, double gapTol);
Polyline simplifyDP(const Polyline& pl, double tol);
void smoothTaubin(Polyline& pl, int iters);
double polylineLength(const Polyline& pl);
/// 품질 지표: 중복 꼭짓점 수(연속 두 점 거리 < tol)
size_t countDuplicateVertices(const std::vector<Polyline>& lines, double tol);

// ---- 레벨선 ----
enum class LevelClass { Minor = 0, Major = 1, Master = 2 };  // 10cm / 50cm / 1m
struct LevelLine { long cm; double z; LevelClass cls; };
/// [zmin,zmax] 안의 stepCm 간격 레벨(cm 정수 산술 → 부동소수 누적 오차 없음)
std::vector<LevelLine> levelLines(double zmin, double zmax, int stepCm = 10, int majorCm = 50, int masterCm = 100);
/// 표고 표기 "45.30"
std::string formatElevation(double z, int decimals = 1);  // 표고 라벨: 기본 소수 1자리("78.5", "79.0")
/// 라벨이 겹치지 않는 최소 라벨 간격(cm): {10,50,100,500,1000} 중 화면 간격 >= minPx
int labelStepCm(double pxPerMeter, double minPx);
/// 레벨선 화면/인쇄 간격 규칙: 선은 기본 10 cm(사용자 설정 baseCm, 이보다 촘촘하게는 안 함), 선 사이가 minLinePx 보다
/// 좁아지면 50 cm → 1 m → 5 m → 10 m 로 솎음. 숫자는 50 cm(78.5/79.0) 기본, 글자가 겹치면 1 m → 5 m → 10 m(그리는 선 위에만).
struct LevelPlan { int lineCm = 10; int labelCm = 50; };
LevelPlan planLevels(double pxPerMeter, double minLinePx, double minLabelPx, int baseCm = 10);
/// 보기 좋은 눈금 간격(1·2·5 ×10^k) — 목표 개수 근처
double niceStep(double range, int targetCount);
/// 단면선 기복(m): 열린 단면선(지면) 점 높이의 5–95 백분위 차. 닫힌 고리(덤불·돌 덩어리)는 뺌. 점 없으면 0
double profileRelief(const std::vector<Polyline>& profile);
/// 화면 세로 과장 추천(1·2·5·10): 기복이 보이는 높이 visibleZ(m, 1:1 일 때)의 15% 이상 차지하는 가장 작은 배율.
/// 기복 없음·보이는 높이 없음이면 1. 화면 보기 전용 — 도면·내보내기는 언제나 1:1
int suggestVerticalExaggeration(double relief, double visibleZ);

// ---- 띠/타일 선택 ----
struct BandQuad { Vec2 p[4]; };
BandQuad sectionBand(const SectionLine& l, double extraMargin = 0);
/// 볼록 사각형 띠와 축정렬 상자(XY)의 교차(SAT)
bool bandIntersectsBox(const BandQuad& q, const Box3& b);

}  // namespace asec
