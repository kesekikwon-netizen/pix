// 명령줄 단면: asec-section <scene.3mx|mesh.obj> AX AY BX BY [--local] [--front m] [--back m] [--res m] [--scale N] [--out prefix] [--world3d]
// 좌표는 기본 실좌표(EPSG). 결과: prefix.dxf, prefix_image.png(입면 영상, DXF 래스터 참조), prefix_profile.csv
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "asec/engine.hpp"
#include "asec/obj.hpp"
#include "asec/sheet.hpp"
#include "stb_image.h"
#include "stb_image_write.h"

using namespace asec;

static void decodeTex(Texture& t) {
    int w, h, c;
    unsigned char* p = stbi_load_from_memory(t.encoded.data(), int(t.encoded.size()), &w, &h, &c, 4);
    if (!p) return;
    t.rgba.w = w; t.rgba.h = h; t.rgba.px.assign(p, p + size_t(w) * h * 4);
    stbi_image_free(p);
}

int main(int argc, char** argv) {
    if (argc < 6) { std::fprintf(stderr, "usage: asec-section <scene.3mx|mesh.obj> AX AY BX BY [--local] [--front m] [--back m(기본 3)] [--res m] [--scale N] [--out prefix] [--world3d]\n"); return 2; }
    fs::path in = fs::u8path(argv[1]);
    double ax = std::atof(argv[2]), ay = std::atof(argv[3]), bx = std::atof(argv[4]), by = std::atof(argv[5]);
    bool local = false, w3 = false; double front = 0, back = kDefaultBackDepth, res = 0.004, scale = 20; std::string out = "section";
    for (int i = 6; i < argc; ++i) {
        std::string a = argv[i];
        auto nx = [&]() { return i + 1 < argc ? argv[++i] : (char*)"0"; };
        if (a == "--local") local = true;
        else if (a == "--world3d") w3 = true;
        else if (a == "--front") front = std::atof(nx());
        else if (a == "--back") back = std::atof(nx());
        else if (a == "--res") res = std::atof(nx());
        else if (a == "--scale") scale = std::atof(nx());
        else if (a == "--out") out = nx();
    }
    std::unique_ptr<MeshSource> src;
    std::string err;
    auto ext = in.extension().u8string();
    for (auto& ch : ext) ch = char(std::tolower(ch));
    if (ext == ".obj") {
        auto s = std::make_unique<StaticSource>();
        if (!loadObj(in, s->meshes, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        findMetadataXml(in, s->srs);
        for (auto& m : s->meshes) { if (m->texture) decodeTex(*m->texture); s->bounds.add(m->bbox); }
        src = std::move(s);
    } else {
        auto s = std::make_unique<TmxSource>();
        s->cache->textureDecoder = decodeTex;
        if (!s->open(in, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        src = std::move(s);
    }
    SectionRequest rq;
    Vec3 o = local ? Vec3() : src->srs.origin;
    rq.line.a = Vec2(ax - o.x, ay - o.y); rq.line.b = Vec2(bx - o.x, by - o.y);
    rq.line.front = front; rq.line.back = back; rq.imageRes = res;
    SectionOutput so;
    if (!computeSection(*src, rq, so, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    std::string png = out + "_image.png";
    auto& im = so.image.img;
    if (!im.empty()) stbi_write_png(png.c_str(), im.w, im.h, 4, im.px.data(), im.w * 4);
    DxfExportOptions dx;
    dx.mode = w3 ? DxfCoordMode::World3D : DxfCoordMode::Drawing2D;
    dx.scaleDenom = scale;
    dx.image = !im.empty(); dx.imageFile = fs::u8path(png).filename().u8string(); dx.imageW = im.w; dx.imageH = im.h;
    dx.imageS0 = so.image.s0; dx.imageZ0 = so.image.z0(); dx.imageRes = so.image.res;
    if (!exportSectionDxf(so.result, dx, fs::u8path(out + ".dxf"), &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    FILE* csv = std::fopen((out + "_profile.csv").c_str(), "w");
    if (csv) {
        std::fprintf(csv, "part,s_m,X,Y,Z\n");
        int k = 0;
        for (auto& pl : so.result.profile) { ++k; for (auto& p : pl) { Vec3 w = sectionToWorld(so.result, p.x, p.y); std::fprintf(csv, "%d,%.4f,%.4f,%.4f,%.4f\n", k, p.x, w.x, w.y, w.z); } }
        std::fclose(csv);
    }
    size_t nv = 0; for (auto& pl : so.result.profile) nv += pl.size();
    std::printf("tiles(leaf)=%zu tris=%zu rawSeg=%zu polylines=%zu vertices=%zu z=[%.2f,%.2f] image=%dx%d res=%.4f  collect=%.0fms cut=%.0fms image=%.0fms\n",
                so.stats.leafNodes, so.stats.triangles, so.result.rawSegments, so.result.profile.size(), nv, so.result.zMin + src->srs.origin.z,
                so.result.zMax + src->srs.origin.z, im.w, im.h, so.image.res, so.msCollect, so.msCut, so.msImage);
    return 0;
}
