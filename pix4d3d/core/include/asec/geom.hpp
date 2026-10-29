// archsection core — 기본 기하 타입 (Qt·MicroStation 무관, 순수 C++17)
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace asec {

struct Vec2 {
    double x = 0, y = 0;
    Vec2() = default;
    Vec2(double x_, double y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    double dot(const Vec2& o) const { return x * o.x + y * o.y; }
    double cross(const Vec2& o) const { return x * o.y - y * o.x; }
    double len() const { return std::sqrt(x * x + y * y); }
};

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
};

struct Box3 {
    Vec3 mn{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    Vec3 mx{-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};
    bool valid() const { return mn.x <= mx.x && mn.y <= mx.y && mn.z <= mx.z; }
    void add(const Vec3& p) {
        mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
        mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
    }
    void add(const Box3& b) { if (b.valid()) { add(b.mn); add(b.mx); } }
    Vec3 center() const { return (mn + mx) * 0.5; }
    double diag() const { if (!valid()) return 0; Vec3 d = mx - mn; return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z); }
};

/// 디코드된 RGBA8 이미지(텍스처). 앱이 JPG를 풀어서 채운다(코어는 이미지 코덱 의존 없음).
struct RgbaImage {
    int w = 0, h = 0;
    std::vector<uint8_t> px;  // w*h*4, 행 0 = 이미지 위쪽
    bool empty() const { return w <= 0 || h <= 0 || px.empty(); }
};

/// 인코딩된 텍스처(3MXB textureBuffer 또는 외부 파일) + 디코드 결과 자리.
struct Texture {
    std::string format;           // "jpg" 등
    std::vector<uint8_t> encoded; // 원본 바이트
    std::string filePath;         // textureFile 이면 경로
    RgbaImage rgba;               // 디코드 결과(앱이 채움)
};
using TexturePtr = std::shared_ptr<Texture>;

/// 삼각형 메시. 좌표는 로컬(= 실좌표 − SRSOrigin), float 로 보관(OpenCTM 과 동일).
struct Mesh {
    std::vector<float> pos;     // xyz * n
    std::vector<float> uv;      // uv * n (없으면 빈 배열). v=0 이 이미지 아래쪽(OpenGL 규약)
    std::vector<uint32_t> idx;  // 3 * 삼각형 수
    TexturePtr texture;
    Box3 bbox;
    size_t vertexCount() const { return pos.size() / 3; }
    size_t triangleCount() const { return idx.size() / 3; }
    void computeBBox() {
        bbox = Box3();
        for (size_t i = 0; i + 2 < pos.size(); i += 3) bbox.add(Vec3(pos[i], pos[i + 1], pos[i + 2]));
    }
};
using MeshPtr = std::shared_ptr<Mesh>;

}  // namespace asec
