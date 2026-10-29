// asec-info: 3MX 모델 점검·성능 측정(코어만, GPU 없음)
//   asec-info <scene.3mx> [--tree] [--decode] [--pick X Y]... [--section AX AY BX BY [--front m] [--back m]] [--local] [--notex] [--csv out.csv]
//   --tree    : 모든 타일 머리를 읽어 노드·깊이·잎 수, 잎 경계 상자 Z 범위
//   --decode  : 잎을 모두 디코드해 삼각형 수·실제 꼭짓점 Z 범위·디코드 시간(스레드 병렬)
//   --pick    : 실좌표(--local 이면 로컬) 연직 잎 피킹(차가운/따뜻한 캐시 시간)
//   --section : 단면 미리보기(거친 LOD) / 최종(잎) 시간, 차가운·따뜻한 캐시
//   --csv     : 측정값을 name,value,unit CSV 로
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include "asec/engine.hpp"
#include "asec/pick.hpp"
#include "stb_image.h"

using namespace asec;
static void decodeTex(Texture& t) {  // 앱과 같은 JPG→RGBA(현실적인 시간). --notex 면 끔
    int w, h, c;
    unsigned char* p = stbi_load_from_memory(t.encoded.data(), int(t.encoded.size()), &w, &h, &c, 4);
    if (!p) return;
    t.rgba.w = w; t.rgba.h = h; t.rgba.px.assign(p, p + size_t(w) * h * 4);
    stbi_image_free(p);
}
using clk = std::chrono::steady_clock;
static double ms(clk::time_point t) { return std::chrono::duration<double, std::milli>(clk::now() - t).count(); }

struct Csv {
    std::vector<std::tuple<std::string, std::string, std::string>> rows;
    void add(const std::string& k, double v, const char* unit) { char b[64]; std::snprintf(b, sizeof b, "%.6g", v); rows.emplace_back(k, b, unit); }
    void addS(const std::string& k, const std::string& v) { rows.emplace_back(k, v, ""); }
};

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: asec-info <scene.3mx> [--tree] [--decode] [--pick X Y]... [--section AX AY BX BY [--front m] [--back m]] [--local] [--notex] [--csv out.csv]\n"); return 2; }
    fs::path in = fs::u8path(argv[1]);
    bool tree = false, decode = false, local = false;
    std::vector<std::pair<double, double>> picks;
    bool haveSec = false; double sa[4] = {}; double secFront = 0, secBack = 3.0; bool tex = true;   // 뒤 기본 3 m(앱과 같음)
    std::string csvPath;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--tree") tree = true;
        else if (a == "--decode") decode = tree = true;
        else if (a == "--local") local = true;
        else if (a == "--notex") tex = false;
        else if (a == "--front" && i + 1 < argc) secFront = std::atof(argv[++i]);
        else if (a == "--back" && i + 1 < argc) secBack = std::atof(argv[++i]);
        else if (a == "--pick" && i + 2 < argc) { picks.push_back({std::atof(argv[i + 1]), std::atof(argv[i + 2])}); i += 2; }
        else if (a == "--section" && i + 4 < argc) { for (int k = 0; k < 4; ++k) sa[k] = std::atof(argv[i + 1 + k]); i += 4; haveSec = true; }
        else if (a == "--csv" && i + 1 < argc) csvPath = argv[++i];
    }
    Csv csv;
    std::string err;
    auto t0 = clk::now();
    TmxSource src;
    if (tex) src.cache->textureDecoder = decodeTex;
    if (!src.open(in, &err)) { std::fprintf(stderr, "open failed: %s\n", err.c_str()); return 1; }
    double openMs = ms(t0);
    SrsReport rep = analyzeSrs(src.srs, src.bounds);
    std::printf("파일        %s\n열기        %.1f ms (루트 타일 머리만)\n레이어      %zu (제외 %zu)\n", in.u8string().c_str(), openMs, src.scene.layers.size(), src.scene.skippedLayers);
    std::printf("좌표계      %s\n원문 종류   %s, 판별 근거 %s\n", rep.labelKo.c_str(),
                rep.desc.kind == SrsKind::Wkt ? "WKT" : rep.desc.kind == SrsKind::Epsg ? "EPSG" : rep.desc.kind == SrsKind::CompoundEpsg ? "EPSG 복합" : rep.desc.kind == SrsKind::Enu ? "ENU" : rep.desc.kind == SrsKind::None ? "없음" : "미상",
                rep.desc.horizontalHow.empty() ? "-" : rep.desc.horizontalHow.c_str());
    std::printf("SRSOrigin   %.3f, %.3f, %.3f%s\n", src.srs.origin.x, src.srs.origin.y, src.srs.origin.z, src.srs.hasOrigin ? "" : " (없음)");
    if (rep.hasLatLon) std::printf("중심 위경도 %.6f, %.6f (x=동, y=북)\n", rep.lat, rep.lon);
    Box3 w; w.add(src.srs.toWorld(src.bounds.mn)); w.add(src.srs.toWorld(src.bounds.mx));
    std::printf("범위(실)    X %.3f ~ %.3f, Y %.3f ~ %.3f, Z %.3f ~ %.3f (루트 노드 상자)\n", w.mn.x, w.mx.x, w.mn.y, w.mx.y, w.mn.z, w.mx.z);
    std::printf("로컬 최대   %.1f m → float32 간격 %.4f mm\n", rep.maxLocalXY, rep.float32StepMm);
    if (!src.srs.metadataSrs.empty()) std::printf("metadata    %s\n", src.srs.metadataSrs.c_str());
    for (auto& x : rep.warnings) std::printf("경고        %s\n", x.c_str());
    for (auto& x : src.scene.warnings) std::printf("경고        %s\n", x.c_str());
    csv.add("open_ms", openMs, "ms"); csv.addS("srs_label", rep.labelKo); csv.add("horizontal_epsg", rep.desc.horizontalEpsg, "");
    csv.add("layers", double(src.scene.layers.size()), "");

    if (tree) {
        auto tt = clk::now();
        std::map<int, size_t> perDepth, leafPerDepth;
        size_t tiles = 0, nodes = 0, leaves = 0, emptyNodes = 0; uint64_t bytes = 0;
        Box3 leafBox;
        std::vector<std::pair<fs::path, int>> stack;
        for (auto& r : src.scene.roots()) stack.push_back({r, 0});
        std::set<fs::path> seen;
        size_t missing = 0;
        while (!stack.empty()) {
            auto [p, d] = stack.back(); stack.pop_back();
            if (!seen.insert(p).second) continue;
            TmxTile t; std::string e;
            if (!readTmxTile(p, t, &e)) { ++missing; continue; }
            ++tiles; bytes += t.bytes.size();
            for (auto& n : t.nodes) {
                ++nodes; perDepth[d]++;
                if (n.resourceIds.empty()) ++emptyNodes;
                if (n.isLeaf()) { ++leaves; leafPerDepth[d]++; leafBox.add(n.bb); }
                for (auto& c : n.children) stack.push_back({c, d + 1});
            }
        }
        double treeMs = ms(tt);
        std::printf("\n[트리] 타일 %zu (없음 %zu), 노드 %zu (빈 노드 %zu), 잎 %zu, 파일 합 %.1f MB, 머리 읽기 %.0f ms\n", tiles, missing, nodes, emptyNodes, leaves, bytes / 1048576.0, treeMs);
        for (auto& [d, n] : perDepth) std::printf("  깊이 %d: 노드 %zu, 잎 %zu\n", d, n, leafPerDepth[d]);
        if (leafBox.valid()) std::printf("  잎 상자 Z(실) %.3f ~ %.3f\n", leafBox.mn.z + src.srs.origin.z, leafBox.mx.z + src.srs.origin.z);
        csv.add("tiles", double(tiles), ""); csv.add("nodes", double(nodes), ""); csv.add("leaves", double(leaves), ""); csv.add("max_depth", double(perDepth.empty() ? 0 : perDepth.rbegin()->first), "");
        csv.add("tile_bytes_mb", bytes / 1048576.0, "MB");
        if (decode) {
            auto td = clk::now();
            std::vector<MeshPtr> ms_; LeafStats st;
            SectionLine all; all.a = Vec2(src.bounds.mn.x - 1, src.bounds.center().y); all.b = Vec2(src.bounds.mx.x + 1, src.bounds.center().y);
            all.front = all.back = (src.bounds.mx.y - src.bounds.mn.y) / 2 + 1;
            if (!src.leafMeshes(sectionBand(all), ms_, &st, &err, nullptr)) { std::fprintf(stderr, "decode failed: %s\n", err.c_str()); return 1; }
            double decMs = ms(td);
            Box3 vb; size_t verts = 0;
            for (auto& m : ms_) { vb.add(m->bbox); verts += m->vertexCount(); }
            std::printf("[디코드] 잎 노드 %zu, 메시 %zu, 삼각형 %zu, 꼭짓점 %zu, %.0f ms (%s, 최대 8 스레드)\n", st.leafNodes, st.meshes, st.triangles, verts, decMs, tex ? "지오메트리+텍스처" : "지오메트리만");
            if (vb.valid()) std::printf("  실제 Z(실) %.4f ~ %.4f  (모델 SRS 높이 그대로)\n", vb.mn.z + src.srs.origin.z, vb.mx.z + src.srs.origin.z);
            csv.add("leaf_triangles", double(st.triangles), ""); csv.add("decode_all_ms", decMs, "ms");
            if (vb.valid()) { csv.add("z_min", vb.mn.z + src.srs.origin.z, "m"); csv.add("z_max", vb.mx.z + src.srs.origin.z, "m"); }
        }
    }
    Vec3 o = local ? Vec3() : src.srs.origin;
    for (auto& pk : picks) {
        for (int pass = 0; pass < 2; ++pass) {
            PickResult r;
            if (!pickVertical(src, pk.first - o.x, pk.second - o.y, r, &err)) { std::fprintf(stderr, "pick failed: %s\n", err.c_str()); break; }
            if (!r.hit) { std::printf("[피킹] %.3f, %.3f: 표면 없음\n", pk.first, pk.second); break; }
            std::printf("[피킹 %s] X %.3f Y %.3f → Z %.4f (%s, 잎 %zu, 삼각형 %zu, %.1f ms)\n", pass ? "따뜻" : "차가움", r.world.x, r.world.y, r.world.z, zSourceKo(r.source),
                        r.stats.leafNodes, r.trianglesTested, r.ms);
            csv.add(std::string("pick_ms_") + (pass ? "warm" : "cold"), r.ms, "ms");
        }
    }
    if (haveSec) {
        SectionRequest rq;
        rq.line.a = Vec2(sa[0] - o.x, sa[1] - o.y); rq.line.b = Vec2(sa[2] - o.x, sa[3] - o.y); rq.line.front = secFront; rq.line.back = secBack;
        double L = SectionFrame(rq.line).L;
        struct Run { const char* name; bool final; };
        TmxSource cold; if (tex) cold.cache->textureDecoder = decodeTex; cold.open(in, &err);
        for (int pass = 0; pass < 2; ++pass)
            for (Run run : {Run{"미리보기", false}, Run{"최종", true}}) {
                SectionRequest q = rq;
                q.imageRes = run.final ? std::max(0.003, L / 4000.0) : std::max(0.008, L / 700.0);
                q.maxImagePixels = run.final ? size_t(24) << 20 : size_t(3) << 20;
                q.meshRes = run.final ? 0.0 : q.imageRes;
                SectionOutput out;
                auto ts = clk::now();
                MeshSource& s = cold;  // 0: 차가운 캐시, 1: 같은 소스 다시(따뜻)
                if (!computeSection(s, q, out, &err)) { std::fprintf(stderr, "section failed: %s\n", err.c_str()); return 1; }
                double tms = ms(ts);
                size_t nv = 0; for (auto& pl : out.result.profile) nv += pl.size();
                std::printf("[단면 %s·%s] L %.2f m, 타일 %zu 깊이 %d 삼각형 %zu, 윤곽 %zu(%zu점), Z(실) %.2f~%.2f, 수집 %.0f 절단 %.0f 영상 %.0f = %.0f ms\n", run.name,
                            pass ? "따뜻" : "차가움", L, out.stats.leafNodes, out.stats.maxDepth, out.stats.triangles, out.result.profile.size(), nv,
                            out.result.zMin + src.srs.origin.z, out.result.zMax + src.srs.origin.z, out.msCollect, out.msCut, out.msImage, tms);
                csv.add(std::string("section_") + (run.final ? "final" : "preview") + (pass ? "_warm_ms" : "_cold_ms"), tms, "ms");
                csv.add(std::string("section_") + (run.final ? "final" : "preview") + "_triangles", double(out.stats.triangles), "");
            }
    }
    if (!csvPath.empty()) {
        FILE* f = std::fopen(csvPath.c_str(), "wb");
        if (f) {
            std::fprintf(f, "name,value,unit\n");
            for (auto& [k, v, u] : csv.rows) std::fprintf(f, "%s,\"%s\",%s\n", k.c_str(), v.c_str(), u.c_str());
            std::fclose(f);
        }
    }
    return 0;
}
