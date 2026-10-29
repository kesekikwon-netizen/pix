#include "synth.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include "stb_image_write.h"

namespace asec::synth {

static double smoothstep(double e0, double e1, double x) {
    double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

double height(double x, double y) {
    double z = 5.0 + 0.04 * x + 0.02 * y + 0.05 * std::sin(0.8 * x) * std::cos(0.6 * y);
    double r = std::hypot(x - 8.0, y - 6.0);
    z -= 0.6 * (1.0 - smoothstep(2.7, 3.0, r));     // 수혈(반지름 3 m, 깊이 60 cm, 벽 30 cm 폭)
    double rh = std::hypot(x - 9.5, y - 6.0);
    z -= 0.4 * (1.0 - smoothstep(0.15, 0.25, rh));  // 주혈(반지름 25 cm, 40 cm)
    // 수혈 바닥의 돌(높이 12~22 cm)
    static const double stones[][4] = {{6.6, 5.4, 0.28, 0.18}, {7.4, 6.8, 0.22, 0.14}, {8.9, 5.1, 0.30, 0.22}, {6.9, 6.3, 0.18, 0.12}, {10.2, 6.6, 0.25, 0.16}};
    for (auto& st : stones) z += st[3] * (1.0 - smoothstep(0.0, st[2], std::hypot(x - st[0], y - st[1])));
    // 둑(baulk): y 7.2~8.0 에 35 cm 높은 띠, 옆면 경사 8 cm 폭
    z += 0.35 * smoothstep(7.12, 7.2, y) * (1.0 - smoothstep(8.0, 8.08, y));
    return z;
}

void color(double x, double y, uint8_t c[3]) {
    double r = std::hypot(x - 8.0, y - 6.0);
    double n = 0.5 + 0.5 * std::sin(x * 7.3 + std::cos(y * 5.1) * 2.0) * std::cos(y * 6.7);
    double R = 176, G = 132, B = 92;                // 황갈색 생토
    if (r < 3.0) { R = 108; G = 82; B = 60; }       // 수혈 내부 암갈색
    if (std::hypot(x - 9.5, y - 6.0) < 0.25) { R = 60; G = 46; B = 36; }
    for (auto& st : std::initializer_list<std::array<double, 3>>{{6.6, 5.4, 0.28}, {7.4, 6.8, 0.22}, {8.9, 5.1, 0.30}, {6.9, 6.3, 0.18}, {10.2, 6.6, 0.25}})
        if (std::hypot(x - st[0], y - st[1]) < st[2]) { R = 150; G = 146; B = 138; }  // 회색 돌
    if (y > 7.12 && y < 8.08) { double band = std::fmod(y * 9.0, 1.0); R = 190 - 30 * (band < 0.5); G = 150 - 20 * (band < 0.5); B = 100; }  // 둑 층위 줄
    R += 18 * (n - 0.5); G += 14 * (n - 0.5); B += 10 * (n - 0.5);
    // 1 m 그리드 표시(흰 선)
    double fx = x - std::floor(x), fy = y - std::floor(y);
    if (fx < 0.015 || fy < 0.015) { R = 235; G = 232; B = 225; }
    c[0] = uint8_t(std::clamp(R, 0.0, 255.0)); c[1] = uint8_t(std::clamp(G, 0.0, 255.0)); c[2] = uint8_t(std::clamp(B, 0.0, 255.0));
}

double gridZ(double x, double y, double h) {
    double fi = x / h, fj = y / h;
    long i = long(std::floor(fi)), j = long(std::floor(fj));
    double u = fi - i, v = fj - j;
    double za = height(i * h, j * h), zb = height((i + 1) * h, j * h), zc = height(i * h, (j + 1) * h), zd = height((i + 1) * h, (j + 1) * h);
    // 삼각형 (a,b,d): v <= u, (a,d,c): v > u
    if (v <= u) return za + u * (zb - za) + v * (zd - zb);
    return za + v * (zc - za) + u * (zd - zc);
}

MeshPtr gridMesh(double x0, double y0, double x1, double y1, double h) {
    auto m = std::make_shared<Mesh>();
    int nx = std::max(1, int(std::lround((x1 - x0) / h))), ny = std::max(1, int(std::lround((y1 - y0) / h)));
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i <= nx; ++i) {
            double x = x0 + (x1 - x0) * i / nx, y = y0 + (y1 - y0) * j / ny;
            m->pos.insert(m->pos.end(), {float(x), float(y), float(height(x, y))});
            m->uv.insert(m->uv.end(), {float(double(i) / nx), float(double(j) / ny)});
        }
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            uint32_t a = uint32_t(j * (nx + 1) + i), b = a + 1, c = a + uint32_t(nx + 1), d = c + 1;
            m->idx.insert(m->idx.end(), {a, b, d, a, d, c});
        }
    m->computeBBox();
    return m;
}

MeshPtr bushMesh(double cx, double cy, double r, double lift, int seg) {
    auto m = std::make_shared<Mesh>();
    const double cz = height(cx, cy) + lift;
    const int nl = seg, nm = seg * 2;
    for (int i = 0; i <= nl; ++i)
        for (int j = 0; j <= nm; ++j) {
            double th = 3.14159265358979 * i / nl, ph = 2 * 3.14159265358979 * (j % nm) / nm;
            double rr = r * (1 + std::sin(th) * (0.12 * std::sin(3 * ph + 1.3) * std::cos(th) + 0.08 * std::cos(5 * th + 2 * ph)));  // 극점에서 ph 무관(한 점)
            m->pos.insert(m->pos.end(), {float(cx + rr * std::sin(th) * std::cos(ph)), float(cy + rr * std::sin(th) * std::sin(ph)), float(cz + 0.8 * rr * std::cos(th))});
            m->uv.insert(m->uv.end(), {float(double(j) / nm), float(1.0 - double(i) / nl)});
        }
    for (int i = 0; i < nl; ++i)
        for (int j = 0; j < nm; ++j) {
            uint32_t a = uint32_t(i * (nm + 1) + j), b = a + 1, c = a + uint32_t(nm + 1), d = c + 1;
            m->idx.insert(m->idx.end(), {a, c, d, a, d, b});
        }
    m->computeBBox();
    return m;
}

static void jpgWrite(void* ctx, void* data, int size) {
    auto* v = static_cast<std::vector<uint8_t>*>(ctx);
    auto* b = static_cast<uint8_t*>(data);
    v->insert(v->end(), b, b + size);
}

static TexturePtr makeTexture(double x0, double y0, double x1, double y1, int S) {
    std::vector<uint8_t> px(size_t(S) * S * 3);
    for (int r = 0; r < S; ++r)
        for (int c = 0; c < S; ++c) {
            double x = x0 + (x1 - x0) * (c + 0.5) / S;
            double y = y1 - (y1 - y0) * (r + 0.5) / S;  // 행 0 = 위 = y1 (v=1)
            color(x, y, &px[(size_t(r) * S + c) * 3]);
        }
    auto t = std::make_shared<Texture>();
    t->format = "jpg";
    stbi_write_jpg_to_func(jpgWrite, &t->encoded, S, S, 3, px.data(), 92);
    return t;
}

bool write(const Params& p, fs::path* out3mx, std::string* err) {
    std::error_code ec;
    fs::create_directories(p.dir / "Data" / "L1", ec);
    const double X = p.sizeX, Y = p.sizeY;
    auto node = [&](const std::string& id, double x0, double y0, double x1, double y1, double h, double msd, std::vector<std::string> kids) {
        TmxWriteNode n;
        n.id = id; n.maxScreenDiameter = msd; n.childFiles = std::move(kids);
        auto m = gridMesh(x0, y0, x1, y1, h);
        m->texture = makeTexture(x0, y0, x1, y1, p.texSize);
        n.meshes.push_back(m);
        if (p.bushes) {
            struct B { double x, y, r, lift; };
            for (const B& b : {B{13.0, 3.2, 0.9, 1.25}, B{3.0, 9.6, 0.7, 1.0}, B{12.2, 9.9, 1.0, 1.4}}) {
                if (b.x < x0 || b.x >= x1 || b.y < y0 || b.y >= y1) continue;
                auto bm = bushMesh(b.x, b.y, b.r, b.lift, h <= p.leafSpacing * 1.01 ? 28 : 10);
                auto t = std::make_shared<Texture>(); t->format = "jpg";
                const int S = 64; std::vector<uint8_t> px(S * S * 3);
                for (int k = 0; k < S * S; ++k) {
                    double nz = std::sin(k * 12.9898) * 43758.5453; nz -= std::floor(nz);
                    px[k * 3] = uint8_t(40 + 40 * nz); px[k * 3 + 1] = uint8_t(85 + 70 * nz); px[k * 3 + 2] = uint8_t(30 + 25 * nz);
                }
                stbi_write_jpg_to_func(jpgWrite, &t->encoded, S, S, 3, px.data(), 90);
                bm->texture = t;
                n.meshes.push_back(bm);
            }
        }
        return n;
    };
    // L2: L1 노드 하나당 2x2 → 전체 4x4
    for (int q = 0; q < 4; ++q) {
        int qi = q % 2, qj = q / 2;
        std::vector<TmxWriteNode> kids;
        for (int k = 0; k < 4; ++k) {
            int ki = qi * 2 + k % 2, kj = qj * 2 + k / 2;
            double x0 = X * ki / 4, x1 = X * (ki + 1) / 4, y0 = Y * kj / 4, y1 = Y * (kj + 1) / 4;
            kids.push_back(node("L2_" + std::to_string(ki) + "_" + std::to_string(kj), x0, y0, x1, y1, p.leafSpacing, 800, {}));
        }
        if (!writeTmxTile(p.dir / "Data" / "L1" / ("L2_" + std::to_string(q) + ".3mxb"), kids, err)) return false;
    }
    std::vector<TmxWriteNode> l1;
    for (int q = 0; q < 4; ++q) {
        int qi = q % 2, qj = q / 2;
        double x0 = X * qi / 2, x1 = X * (qi + 1) / 2, y0 = Y * qj / 2, y1 = Y * (qj + 1) / 2;
        l1.push_back(node("L1_" + std::to_string(q), x0, y0, x1, y1, p.leafSpacing * 5, 400, {"L2_" + std::to_string(q) + ".3mxb"}));
    }
    if (!writeTmxTile(p.dir / "Data" / "L1" / "L1.3mxb", l1, err)) return false;
    std::vector<TmxWriteNode> l0{node("L0", 0, 0, X, Y, p.leafSpacing * 20, 200, {"L1/L1.3mxb"})};
    if (!writeTmxTile(p.dir / "Data" / "Root.3mxb", l0, err)) return false;
    SrsInfo s; s.srs = p.srs; s.origin = p.origin; s.hasOrigin = true;
    fs::path f = p.dir / "Synthetic.3mx";
    if (!writeTmxScene(f, "합성 수혈 시험", s, "Data/Root.3mxb", err)) return false;
    if (out3mx) *out3mx = f;
    return true;
}

}  // namespace asec::synth
