// 합성 3MX 시험 데이터: 해석적 지형(완만한 경사 + 원형 수혈 + 주혈) → LOD 3단 3MXB(OpenCTM) + JPG 텍스처
#pragma once
#include "asec/tmx.hpp"

namespace asec::synth {
struct Params {
    fs::path dir;
    Vec3 origin{200000.0, 450000.0, 40.0};
    std::string srs = "EPSG:5186";
    double sizeX = 16, sizeY = 12;
    double leafSpacing = 0.04;  // 최고 해상도 격자 간격(m)
    bool bushes = false;        // 지면 위 떠 있는 덤불(닫힌 단면 고리 시연용 울퉁불퉁한 공 메시)
    int texSize = 256;
};
double height(double x, double y);                 // 로컬 z
void color(double x, double y, uint8_t rgb[3]);    // 텍스처 색(해석적)
/// LOD: L0 1노드(간격 leaf*20) → L1 2x2(간격 leaf*5) → L2 4x4(간격 leaf)
bool write(const Params& p, fs::path* out3mx, std::string* err);
/// 격자 메시 하나(시험용): [x0,x1]×[y0,y1], 간격 h, uv=타일 정규화
/// gridMesh 와 같은 삼각분할(a-b-d, a-d-c)의 선형 보간 높이(원점 0,0 기준 균일 격자 h)
double gridZ(double x, double y, double h);
MeshPtr gridMesh(double x0, double y0, double x1, double y1, double h);
/// 덤불: 중심(cx,cy, 지면+lift), 반지름 r 의 울퉁불퉁한 닫힌 공
MeshPtr bushMesh(double cx, double cy, double r, double lift, int seg);
}  // namespace asec::synth
