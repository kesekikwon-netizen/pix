#include "asec/obj.hpp"
#include <cstring>

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace asec {

static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

static std::map<std::string, std::string> readMtl(const fs::path& p) {
    std::map<std::string, std::string> m;
    std::ifstream f(p);
    std::string line, cur;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.rfind("newmtl ", 0) == 0) cur = trim(line.substr(7));
        else if (line.rfind("map_Kd ", 0) == 0 && !cur.empty()) {
            std::string t = trim(line.substr(7));
            auto sp = t.find_last_of(' ');  // 옵션(-s 등)이 있으면 마지막 토큰
            if (t[0] == '-' && sp != std::string::npos) t = t.substr(sp + 1);
            m[cur] = t;
        }
    }
    return m;
}

bool loadObj(const fs::path& path, std::vector<MeshPtr>& out, std::string* err) {
    std::ifstream f(path);
    if (!f) { if (err) *err = "OBJ 를 열 수 없습니다"; return false; }
    std::vector<float> V, T;
    std::map<std::string, std::string> mtl;
    struct Group { std::vector<float> pos, uv; std::vector<uint32_t> idx; std::map<uint64_t, uint32_t> remap; std::string tex; };
    std::map<std::string, Group> groups;
    std::string curMat = "";
    Group* g = &groups[curMat];
    std::string line;
    while (std::getline(f, line)) {
        if (line.size() < 2) continue;
        if (line[0] == 'v' && line[1] == ' ') {
            float x = 0, y = 0, z = 0; std::sscanf(line.c_str() + 2, "%f %f %f", &x, &y, &z);
            V.insert(V.end(), {x, y, z});
        } else if (line[0] == 'v' && line[1] == 't') {
            float u = 0, v = 0; std::sscanf(line.c_str() + 3, "%f %f", &u, &v);
            T.insert(T.end(), {u, v});
        } else if (line.rfind("mtllib ", 0) == 0) {
            auto m = readMtl(path.parent_path() / fs::u8path(trim(line.substr(7))));
            mtl.insert(m.begin(), m.end());
        } else if (line.rfind("usemtl ", 0) == 0) {
            curMat = trim(line.substr(7));
            g = &groups[curMat];
        } else if (line[0] == 'f' && line[1] == ' ') {
            std::istringstream ss(line.substr(2));
            std::string tok;
            std::vector<uint32_t> poly;
            while (ss >> tok) {
                long vi = std::strtol(tok.c_str(), nullptr, 10), ti = 0;
                auto s1 = tok.find('/');
                if (s1 != std::string::npos && s1 + 1 < tok.size() && tok[s1 + 1] != '/') ti = std::strtol(tok.c_str() + s1 + 1, nullptr, 10);
                long nv = long(V.size() / 3), nt = long(T.size() / 2);
                if (vi < 0) vi = nv + vi + 1;
                if (ti < 0) ti = nt + ti + 1;
                if (vi < 1 || vi > nv) { if (err) *err = "OBJ 면 인덱스 오류"; return false; }
                if (ti > nt) ti = 0;
                uint64_t key = (uint64_t(vi) << 32) | uint64_t(ti);
                auto it = g->remap.find(key);
                uint32_t id;
                if (it == g->remap.end()) {
                    id = uint32_t(g->pos.size() / 3);
                    g->pos.insert(g->pos.end(), {V[3 * (vi - 1)], V[3 * (vi - 1) + 1], V[3 * (vi - 1) + 2]});
                    if (ti > 0) g->uv.insert(g->uv.end(), {T[2 * (ti - 1)], T[2 * (ti - 1) + 1]});
                    else g->uv.insert(g->uv.end(), {0.f, 0.f});
                    g->remap[key] = id;
                } else id = it->second;
                poly.push_back(id);
            }
            for (size_t k = 1; k + 1 < poly.size(); ++k) g->idx.insert(g->idx.end(), {poly[0], poly[k], poly[k + 1]});
        }
    }
    for (auto& kv : groups) {
        Group& G = kv.second;
        if (G.idx.empty()) continue;
        auto m = std::make_shared<Mesh>();
        m->pos = std::move(G.pos); m->uv = std::move(G.uv); m->idx = std::move(G.idx);
        auto it = mtl.find(kv.first);
        if (it != mtl.end()) {
            auto tex = std::make_shared<Texture>();
            fs::path tp = path.parent_path() / fs::u8path(it->second);
            tex->filePath = tp.u8string();
            auto ext = tp.extension().u8string();
            tex->format = ext.empty() ? "jpg" : ext.substr(1);
            if (readFileBytes(tp, tex->encoded)) m->texture = tex;
        }
        m->computeBBox();
        out.push_back(m);
    }
    if (out.empty()) { if (err) *err = "OBJ 에 면이 없습니다"; return false; }
    return true;
}

static std::string tagText(const std::string& x, const std::string& tag) {
    auto a = x.find("<" + tag + ">");
    if (a == std::string::npos) return "";
    a += tag.size() + 2;
    auto b = x.find("</" + tag + ">", a);
    if (b == std::string::npos) return "";
    std::string t = trim(x.substr(a, b - a)), r;  // XML 엔터티 풀기(WKT 의 &quot; 등)
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '&') {
            static const char* ent[][2] = {{"&quot;", "\""}, {"&apos;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&amp;", "&"}};
            bool hit = false;
            for (auto& e : ent) if (t.compare(i, std::strlen(e[0]), e[0]) == 0) { r += e[1]; i += std::strlen(e[0]) - 1; hit = true; break; }
            if (hit) continue;
        }
        r += t[i];
    }
    return r;
}

bool parseMetadataXml(const std::string& xml, SrsInfo& out) {
    out = SrsInfo();
    out.srs = tagText(xml, "SRS");
    std::string o = tagText(xml, "SRSOrigin");
    if (!o.empty()) {
        double x = 0, y = 0, z = 0;
        if (std::sscanf(o.c_str(), "%lf , %lf , %lf", &x, &y, &z) == 3 || std::sscanf(o.c_str(), "%lf %lf %lf", &x, &y, &z) == 3) {
            out.origin = Vec3(x, y, z); out.hasOrigin = true;
        }
    }
    return !out.srs.empty() || out.hasOrigin;
}

bool findMetadataXml(const fs::path& objPath, SrsInfo& out, fs::path* found) {
    fs::path d = objPath.parent_path();
    for (int i = 0; i < 4 && !d.empty(); ++i) {
        fs::path m = d / "metadata.xml";
        std::vector<uint8_t> b;
        if (readFileBytes(m, b)) {
            if (parseMetadataXml(std::string(b.begin(), b.end()), out)) { if (found) *found = m; return true; }
        }
        if (d == d.parent_path()) break;
        d = d.parent_path();
    }
    return false;
}

}  // namespace asec
