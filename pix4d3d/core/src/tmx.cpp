#include "asec/tmx.hpp"
#include "asec/obj.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include "json.hpp"
#include "openctm.h"

namespace asec {
using json = nlohmann::json;

int SrsInfo::epsg() const { return describe().horizontalEpsg; }
int SrsInfo::verticalEpsg() const { return describe().verticalEpsg; }

bool readFileBytes(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto n = f.tellg();
    if (n < 0) return false;
    out.resize(size_t(n));
    f.seekg(0);
    if (n > 0) f.read(reinterpret_cast<char*>(out.data()), n);
    return bool(f);
}

static void setErr(std::string* e, const std::string& s) { if (e) *e = s; }

// ---------------- OpenCTM ----------------
namespace {
struct MemReader { const uint8_t* p; size_t n, pos; };
CTMuint CTMCALL readFn(void* buf, CTMuint count, void* user) {
    auto* r = static_cast<MemReader*>(user);
    size_t k = std::min<size_t>(count, r->n - r->pos);
    std::memcpy(buf, r->p + r->pos, k);
    r->pos += k;
    return CTMuint(k);
}
CTMuint CTMCALL writeFn(const void* buf, CTMuint count, void* user) {
    auto* v = static_cast<std::vector<uint8_t>*>(user);
    auto* b = static_cast<const uint8_t*>(buf);
    v->insert(v->end(), b, b + count);
    return count;
}
}  // namespace

bool decodeCtm(const uint8_t* data, size_t size, Mesh& out, std::string* err) {
    CTMcontext ctx = ctmNewContext(CTM_IMPORT);
    MemReader r{data, size, 0};
    ctmLoadCustom(ctx, readFn, &r);
    CTMenum e = ctmGetError(ctx);
    if (e != CTM_NONE) {
        setErr(err, std::string("OpenCTM: ") + ctmErrorString(e));
        ctmFreeContext(ctx);
        return false;
    }
    CTMuint vc = ctmGetInteger(ctx, CTM_VERTEX_COUNT), tc = ctmGetInteger(ctx, CTM_TRIANGLE_COUNT);
    const CTMfloat* v = ctmGetFloatArray(ctx, CTM_VERTICES);
    const CTMuint* ix = ctmGetIntegerArray(ctx, CTM_INDICES);
    out.pos.assign(v, v + size_t(vc) * 3);
    out.idx.assign(ix, ix + size_t(tc) * 3);
    out.uv.clear();
    if (ctmGetInteger(ctx, CTM_UV_MAP_COUNT) > 0) {
        const CTMfloat* uv = ctmGetFloatArray(ctx, CTM_UV_MAP_1);
        if (uv) out.uv.assign(uv, uv + size_t(vc) * 2);
    }
    ctmFreeContext(ctx);
    // 잘못된 인덱스 방어
    for (auto i : out.idx) if (i >= vc) { setErr(err, "OpenCTM: index out of range"); return false; }
    out.computeBBox();
    return true;
}

bool encodeCtm(const Mesh& m, std::vector<uint8_t>& out, std::string* err, bool mg2, double prec) {
    CTMcontext ctx = ctmNewContext(CTM_EXPORT);
    ctmDefineMesh(ctx, m.pos.data(), CTMuint(m.vertexCount()), m.idx.data(), CTMuint(m.triangleCount()), nullptr);
    if (!m.uv.empty()) ctmAddUVMap(ctx, m.uv.data(), "Diffuse color", nullptr);
    ctmCompressionMethod(ctx, mg2 ? CTM_METHOD_MG2 : CTM_METHOD_MG1);
    if (mg2) { ctmVertexPrecision(ctx, CTMfloat(prec)); ctmUVCoordPrecision(ctx, ctmGetNamedUVMap(ctx, "Diffuse color"), 1.0f / 4096.0f); }
    out.clear();
    ctmSaveCustom(ctx, writeFn, &out);
    CTMenum e = ctmGetError(ctx);
    ctmFreeContext(ctx);
    if (e != CTM_NONE) { setErr(err, std::string("OpenCTM: ") + ctmErrorString(e)); return false; }
    return true;
}

// ---------------- 3MX ----------------
static Box3 boxFrom(const json& j) {
    Box3 b;
    if (j.contains("bbMin") && j.contains("bbMax") && j["bbMin"].size() == 3 && j["bbMax"].size() == 3) {
        b.add(Vec3(j["bbMin"][0].get<double>(), j["bbMin"][1].get<double>(), j["bbMin"][2].get<double>()));
        b.add(Vec3(j["bbMax"][0].get<double>(), j["bbMax"][1].get<double>(), j["bbMax"][2].get<double>()));
    }
    return b;
}

bool readTmxScene(const fs::path& file3mx, TmxScene& out, std::string* err) {
    std::vector<uint8_t> b;
    if (!readFileBytes(file3mx, b)) { setErr(err, "파일을 읽을 수 없습니다"); return false; }
    json j;
    try { j = json::parse(b.begin(), b.end()); } catch (std::exception& e) { setErr(err, std::string("3MX JSON 오류: ") + e.what()); return false; }
    out = TmxScene();
    out.name = j.value("name", "");
    out.description = j.value("description", "");
    if (!j.contains("layers") || !j["layers"].is_array()) { setErr(err, "3MX: layers 없음"); return false; }
    size_t nPyr = 0;
    for (auto& L : j["layers"]) {
        if (!L.is_object() || L.value("type", "") != "meshPyramid") continue;
        ++nPyr;
        TmxLayer ly;
        ly.id = L.value("id", "");
        ly.name = L.value("name", "");
        if (L.contains("SRS") && L["SRS"].is_string()) ly.srs.srs = L["SRS"].get<std::string>();
        if (L.contains("SRSOrigin") && L["SRSOrigin"].is_array() && L["SRSOrigin"].size() == 3) {
            try {
                ly.srs.origin = Vec3(L["SRSOrigin"][0].get<double>(), L["SRSOrigin"][1].get<double>(), L["SRSOrigin"][2].get<double>());
                ly.srs.hasOrigin = true;
            } catch (std::exception&) {}
        }
        std::string root = L.value("root", "");
        if (root.empty()) { out.warnings.push_back("레이어 '" + ly.name + "' 에 root 가 없어 건너뜁니다."); continue; }
        ly.rootFile = (file3mx.parent_path() / fs::u8path(root)).lexically_normal();
        if (out.layers.empty()) {
            out.srs = ly.srs;
            out.rootFile = ly.rootFile;
            if (out.name.empty()) out.name = ly.name;
            out.layers.push_back(ly);
            continue;
        }
        bool dup = false;
        for (auto& o : out.layers) dup = dup || o.rootFile == ly.rootFile;
        if (dup) continue;
        // 병합 3MX: SRS·원점이 첫 레이어와 같아야 같은 로컬 좌표를 공유한다(Bentley 병합 규칙)
        SrsDesc d0 = out.srs.describe(), d1 = ly.srs.describe();
        bool sameSrs = ly.srs.srs == out.srs.srs ||
                       (d0.known() && d1.known() && d0.horizontalEpsg && d0.horizontalEpsg == d1.horizontalEpsg && d0.vertKind == d1.vertKind && d0.verticalEpsg == d1.verticalEpsg);
        Vec3 dO = ly.srs.origin - out.srs.origin;
        bool sameOrigin = ly.srs.hasOrigin == out.srs.hasOrigin && std::fabs(dO.x) < 1e-6 && std::fabs(dO.y) < 1e-6 && std::fabs(dO.z) < 1e-6;
        if (!sameSrs || !sameOrigin) {
            out.skippedLayers++;
            char b2[160];
            std::snprintf(b2, sizeof b2, " (원점 %.3f, %.3f, %.3f)", ly.srs.origin.x, ly.srs.origin.y, ly.srs.origin.z);
            out.warnings.push_back("레이어 '" + ly.name + "' 는 " + (!sameSrs ? "좌표계(" + d1.shortAscii() + ")" : std::string("SRSOrigin") + b2) +
                                   " 가 첫 레이어와 달라 열지 않았습니다(첫 레이어 기준으로만 표시).");
            continue;
        }
        out.layers.push_back(ly);
    }
    if (out.layers.empty()) { setErr(err, nPyr ? "3MX: 읽을 수 있는 meshPyramid 레이어 없음" : "3MX: meshPyramid 레이어 없음"); return false; }
    SrsInfo meta;
    if (findMetadataXml(file3mx, meta)) out.srs.metadataSrs = meta.srs;
    for (auto& l : out.layers) l.srs.metadataSrs = out.srs.metadataSrs;
    return true;
}

bool readTmxTile(const fs::path& p, TmxTile& t, std::string* err) {
    t = TmxTile();
    t.path = p;
    if (!readFileBytes(p, t.bytes)) { setErr(err, "타일을 읽을 수 없습니다: " + p.u8string()); return false; }
    auto& B = t.bytes;
    if (B.size() < 9 || std::memcmp(B.data(), "3MXBO", 5) != 0) { setErr(err, "3MXB 매직 번호 아님: " + p.u8string()); return false; }
    uint32_t hs = uint32_t(B[5]) | (uint32_t(B[6]) << 8) | (uint32_t(B[7]) << 16) | (uint32_t(B[8]) << 24);
    if (size_t(9) + hs > B.size()) { setErr(err, "3MXB 헤더 길이 오류"); return false; }
    json h;
    try { h = json::parse(B.begin() + 9, B.begin() + 9 + hs); } catch (std::exception& e) { setErr(err, std::string("3MXB 헤더 JSON 오류: ") + e.what()); return false; }
    size_t off = 9 + hs;
    fs::path dir = p.parent_path();
    if (h.contains("resources")) for (auto& r : h["resources"]) {
        TmxTile::Res R;
        R.id = r.value("id", ""); R.type = r.value("type", ""); R.format = r.value("format", "");
        R.texId = r.value("texture", ""); R.file = r.value("file", "");
        R.bb = boxFrom(r);
        if (R.type == "textureBuffer" || R.type == "geometryBuffer") {
            R.size = r.value("size", size_t(0));
            R.off = off;
            if (off + R.size > B.size()) { setErr(err, "3MXB 버퍼가 파일 끝을 넘음: " + p.u8string()); return false; }
            off += R.size;
        }
        if (R.type == "textureBuffer" || R.type == "textureFile") {
            auto tex = std::make_shared<Texture>();
            tex->format = R.format;
            if (R.type == "textureBuffer") tex->encoded.assign(B.begin() + R.off, B.begin() + R.off + R.size);
            else { tex->filePath = (dir / fs::u8path(R.file)).u8string(); readFileBytes(dir / fs::u8path(R.file), tex->encoded); }
            t.textures.emplace_back(R.id, tex);
        }
        t.resources.push_back(R);
    }
    if (h.contains("nodes")) for (auto& n : h["nodes"]) {
        TmxNode N;
        N.id = n.value("id", "");
        N.bb = boxFrom(n);
        N.maxScreenDiameter = n.value("maxScreenDiameter", 0.0);
        if (n.contains("children")) for (auto& c : n["children"]) N.children.push_back((dir / fs::u8path(c.get<std::string>())).lexically_normal());
        if (n.contains("resources")) for (auto& r : n["resources"]) N.resourceIds.push_back(r.get<std::string>());
        t.nodes.push_back(std::move(N));
    }
    return true;
}

bool decodeNode(TmxTile& t, size_t ni, std::string* err) {
    if (ni >= t.nodes.size()) return false;
    TmxNode& N = t.nodes[ni];
    if (N.decoded) return true;
    for (auto& rid : N.resourceIds) {
        for (auto& R : t.resources) {
            if (R.id != rid) continue;
            if (R.type == "geometryBuffer" && R.format == "ctm") {
                auto m = std::make_shared<Mesh>();
                if (!decodeCtm(t.bytes.data() + R.off, R.size, *m, err)) return false;
                for (auto& tx : t.textures) if (tx.first == R.texId) m->texture = tx.second;
                N.meshes.push_back(m);
            } else if (R.type == "geometryFile" && R.format == "ctm") {
                std::vector<uint8_t> b;
                if (!readFileBytes(t.path.parent_path() / fs::u8path(R.file), b)) { setErr(err, "CTM 파일 없음: " + R.file); return false; }
                auto m = std::make_shared<Mesh>();
                if (!decodeCtm(b.data(), b.size(), *m, err)) return false;
                for (auto& tx : t.textures) if (tx.first == R.texId) m->texture = tx.second;
                N.meshes.push_back(m);
            }
            // 그 밖의 형식(OBJ geometryFile 등)은 건너뜀
        }
    }
    N.decoded = true;
    return true;
}

static bool writeAll(const fs::path& p, const std::string& head, const std::vector<std::vector<uint8_t>>& bufs, std::string* err) {
    std::ofstream f(p, std::ios::binary);
    if (!f) { setErr(err, "쓸 수 없음: " + p.u8string()); return false; }
    f.write("3MXBO", 5);
    uint32_t hs = uint32_t(head.size());
    uint8_t le[4] = {uint8_t(hs), uint8_t(hs >> 8), uint8_t(hs >> 16), uint8_t(hs >> 24)};
    f.write(reinterpret_cast<char*>(le), 4);
    f.write(head.data(), std::streamsize(head.size()));
    for (auto& b : bufs) f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    return bool(f);
}

bool writeTmxTile(const fs::path& p, const std::vector<TmxWriteNode>& nodes, std::string* err) {
    json h; h["version"] = 1; h["nodes"] = json::array(); h["resources"] = json::array();
    std::vector<std::vector<uint8_t>> bufs;
    int gi = 0, ti = 0;
    for (auto& n : nodes) {
        json jn; jn["id"] = n.id; jn["maxScreenDiameter"] = n.maxScreenDiameter;
        jn["children"] = n.childFiles; jn["resources"] = json::array();
        Box3 nb;
        for (auto& m : n.meshes) {
            Mesh mm = *m; mm.computeBBox(); nb.add(mm.bbox);
            std::string texId;
            if (m->texture && !m->texture->encoded.empty()) {
                texId = "tex" + std::to_string(ti++);
                h["resources"].push_back({{"type", "textureBuffer"}, {"format", m->texture->format.empty() ? "jpg" : m->texture->format}, {"id", texId}, {"size", m->texture->encoded.size()}});
                bufs.push_back(m->texture->encoded);
            }
            std::vector<uint8_t> ctm;
            if (!encodeCtm(*m, ctm, err)) return false;
            std::string gid = "geometry" + std::to_string(gi++);
            json g = {{"type", "geometryBuffer"}, {"format", "ctm"}, {"id", gid}, {"size", ctm.size()},
                      {"bbMin", {mm.bbox.mn.x, mm.bbox.mn.y, mm.bbox.mn.z}}, {"bbMax", {mm.bbox.mx.x, mm.bbox.mx.y, mm.bbox.mx.z}}};
            if (!texId.empty()) g["texture"] = texId;
            h["resources"].push_back(g);
            bufs.push_back(std::move(ctm));
            jn["resources"].push_back(gid);
        }
        jn["bbMin"] = {nb.mn.x, nb.mn.y, nb.mn.z}; jn["bbMax"] = {nb.mx.x, nb.mx.y, nb.mx.z};
        h["nodes"].push_back(jn);
    }
    return writeAll(p, h.dump(), bufs, err);
}

bool writeTmxSceneLayers(const fs::path& p, const std::string& name, const std::vector<TmxWriteLayer>& layers, std::string* err) {
    json j;
    j["3mxVersion"] = 1; j["name"] = name; j["description"] = "archsection merged"; j["logo"] = "";
    j["sceneOptions"] = json::array({json{{"navigation_mode", "PAN"}}});
    j["layers"] = json::array();
    int k = 0;
    for (auto& l : layers)
        j["layers"].push_back({{"type", "meshPyramid"}, {"id", "mesh" + std::to_string(k++)}, {"name", l.name}, {"description", ""}, {"SRS", l.srs.srs},
                               {"SRSOrigin", {l.srs.origin.x, l.srs.origin.y, l.srs.origin.z}}, {"root", l.rootRelative}});
    std::ofstream f(p, std::ios::binary);
    if (!f) { setErr(err, "쓸 수 없음: " + p.u8string()); return false; }
    f << j.dump(2);
    return bool(f);
}

bool writeTmxScene(const fs::path& p, const std::string& name, const SrsInfo& srs, const std::string& rootRel, std::string* err) {
    json j;
    j["3mxVersion"] = 1; j["name"] = name; j["description"] = "archsection synthetic"; j["logo"] = "";
    j["sceneOptions"] = json::array({json{{"navigation_mode", "PAN"}}});
    json L = {{"type", "meshPyramid"}, {"id", "mesh0"}, {"name", name}, {"description", ""}, {"SRS", srs.srs},
              {"SRSOrigin", {srs.origin.x, srs.origin.y, srs.origin.z}}, {"root", rootRel}};
    j["layers"] = json::array({L});
    std::ofstream f(p, std::ios::binary);
    if (!f) { setErr(err, "쓸 수 없음: " + p.u8string()); return false; }
    f << j.dump(2);
    return bool(f);
}

}  // namespace asec
