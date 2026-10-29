// 최소 DXF R2000(AC1015) 작성기: 레이어, LWPOLYLINE, 3D POLYLINE, LINE, TEXT, IMAGE(+IMAGEDEF/REACTOR).
// AutoCAD·MicroStation·QGIS 가 읽는 구조(ezdxf 감사 통과 기준). 레이어·문자는 ASCII 권장.
#pragma once
#include <filesystem>
#include <sstream>
#include "asec/geom.hpp"

namespace asec {

class DxfWriter {
public:
    /// color = AutoCAD 색 번호(1 빨강, 8 회색, 250 등), lineweight = 1/100 mm (-3 기본)
    void addLayer(const std::string& name, int color, int lineweight = -3);
    void polyline2d(const std::string& layer, const std::vector<Vec2>& pts, bool closed = false, double z = 0);
    void polyline3d(const std::string& layer, const std::vector<Vec3>& pts, bool closed = false);
    void line(const std::string& layer, const Vec3& a, const Vec3& b);
    /// halign: 0 왼쪽 1 가운데 2 오른쪽, valign: 0 기준선 1 아래 2 가운데 3 위
    void text(const std::string& layer, const Vec3& p, double height, const std::string& s, int halign = 0, int valign = 0, double rotDeg = 0);
    /// 수직면 등 임의 평면 위 문자: extrusion = 평면 법선(WCS), p 는 WCS 점(내부에서 OCS 로 변환)
    void textOnPlane(const std::string& layer, const Vec3& p, const Vec3& extrusion, double height, const std::string& s, int halign = 0, int valign = 0);
    /// 래스터 참조. insert = 영상 왼쪽 아래 모서리, uPix/vPix = 픽셀 하나의 가로/세로 벡터(실좌표)
    void image(const std::string& layer, const std::string& fileName, int wPx, int hPx, const Vec3& insert, const Vec3& uPix, const Vec3& vPix);
    bool save(const std::filesystem::path& p, std::string* err) const;
    std::string str() const;
    size_t entityCount() const { return nEnt_; }

private:
    struct Layer { std::string name; int color, lw; };
    struct Img { std::string file; int w, h; Vec3 ins, u, v; std::string layer; unsigned hEnt, hDef, hReact; };
    std::vector<Layer> layers_;
    std::vector<Img> images_;
    std::ostringstream ent_;
    mutable unsigned next_ = 0x100;
    size_t nEnt_ = 0;
    Box3 ext_;
    unsigned h() const { return next_++; }
};

/// 좌표 문자열(유효숫자 보존)
std::string dxfNum(double v);

}  // namespace asec
