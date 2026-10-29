#include "catch_amalgamated.hpp"
#include "asec/lod.hpp"
#include "asec/obj.hpp"
#include "common.hpp"
#include "synth.hpp"
#include <fstream>

using namespace asec;
using Catch::Approx;

TEST_CASE("OpenCTM MG2 왕복: 정밀도 0.5 mm 이내, UV·인덱스 보존") {
    auto m = synth::gridMesh(0, 0, 3, 2, 0.1);
    std::vector<uint8_t> b; std::string err;
    REQUIRE(encodeCtm(*m, b, &err));
    Mesh d;
    REQUIRE(decodeCtm(b.data(), b.size(), d, &err));
    REQUIRE(d.vertexCount() == m->vertexCount());
    REQUIRE(d.triangleCount() == m->triangleCount());
    REQUIRE(d.uv.size() == m->uv.size());
    // MG2 는 꼭짓점 순서를 바꿀 수 있으므로 상자·삼각형 면적 합으로 비교
    CHECK(d.bbox.mn.x == Approx(m->bbox.mn.x).margin(0.0005));
    CHECK(d.bbox.mx.z == Approx(m->bbox.mx.z).margin(0.0005));
    auto area = [](const Mesh& q) { double A = 0; for (size_t t = 0; t < q.triangleCount(); ++t) {
        const float* a = &q.pos[3 * q.idx[3 * t]]; const float* b2 = &q.pos[3 * q.idx[3 * t + 1]]; const float* c = &q.pos[3 * q.idx[3 * t + 2]];
        A += 0.5 * std::fabs((b2[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b2[1] - a[1])); } return A; };
    CHECK(area(d) == Approx(area(*m)).epsilon(1e-3));
}

TEST_CASE("3MXB 형식: 매직·헤더·버퍼 순서, 자식 경로 해석") {
    auto dir = tmpDir("tmxb");
    TmxWriteNode n; n.id = "n0"; n.maxScreenDiameter = 123; n.childFiles = {"sub/child.3mxb"};
    auto m = synth::gridMesh(0, 0, 1, 1, 0.25);
    m->texture = std::make_shared<Texture>(); m->texture->format = "jpg"; m->texture->encoded = {0xFF, 0xD8, 1, 2, 3, 0xFF, 0xD9};
    n.meshes.push_back(m);
    std::string err;
    REQUIRE(writeTmxTile(dir / "a.3mxb", {n}, &err));
    std::vector<uint8_t> raw; REQUIRE(readFileBytes(dir / "a.3mxb", raw));
    CHECK(std::string(raw.begin(), raw.begin() + 5) == "3MXBO");
    TmxTile t;
    REQUIRE(readTmxTile(dir / "a.3mxb", t, &err));
    REQUIRE(t.nodes.size() == 1);
    CHECK(t.nodes[0].id == "n0");
    CHECK(t.nodes[0].maxScreenDiameter == Approx(123));
    REQUIRE(t.nodes[0].children.size() == 1);
    CHECK(t.nodes[0].children[0] == (dir / "sub" / "child.3mxb").lexically_normal());
    REQUIRE(decodeNode(t, 0, &err));
    REQUIRE(t.nodes[0].meshes.size() == 1);
    REQUIRE(t.nodes[0].meshes[0]->texture);
    CHECK(t.nodes[0].meshes[0]->texture->encoded == m->texture->encoded);
    CHECK(t.resources[0].type == "textureBuffer");   // 텍스처 버퍼가 먼저, 지오메트리 다음
    CHECK(t.resources[1].type == "geometryBuffer");
    CHECK(t.resources[1].format == "ctm");
}

TEST_CASE("손상된 3MXB 는 오류로 거절") {
    auto dir = tmpDir("bad");
    { std::ofstream f(dir / "x.3mxb", std::ios::binary); f << "NOPE"; }
    TmxTile t; std::string err;
    CHECK_FALSE(readTmxTile(dir / "x.3mxb", t, &err));
    CHECK_FALSE(err.empty());
    { std::ofstream f(dir / "y.3mxb", std::ios::binary); f.write("3MXBO\xff\xff\x00\x00{", 10); }
    CHECK_FALSE(readTmxTile(dir / "y.3mxb", t, &err));
}

TEST_CASE("3MX 루트: SRS·SRSOrigin 읽기, 로컬+원점=실좌표") {
    auto dir = tmpDir("scene");
    synth::Params p; p.dir = dir; p.leafSpacing = 0.1; p.texSize = 32;
    fs::path f; std::string err;
    REQUIRE(synth::write(p, &f, &err));
    TmxScene s;
    REQUIRE(readTmxScene(f, s, &err));
    CHECK(s.srs.srs == "EPSG:5186");
    CHECK(s.srs.epsg() == 5186);
    CHECK(s.srs.hasOrigin);
    CHECK(s.srs.origin.x == Approx(200000.0));
    CHECK(s.srs.origin.z == Approx(40.0));
    Vec3 w = s.srs.toWorld(Vec3(1.5, 2.5, 5.3));
    CHECK(w.x == Approx(200001.5)); CHECK(w.y == Approx(450002.5)); CHECK(w.z == Approx(45.3));
    CHECK(s.rootFile.filename() == "Root.3mxb");
}

TEST_CASE("LOD: 띠와 겹치는 최고 해상도(잎) 노드만 수집") {
    auto dir = tmpDir("lod");
    synth::Params p; p.dir = dir; p.leafSpacing = 0.1; p.texSize = 32;
    fs::path f; std::string err;
    REQUIRE(synth::write(p, &f, &err));
    TmxScene s; REQUIRE(readTmxScene(f, s, &err));
    TileCache cache;
    // y=6 을 따라 x 1→15 → 4x4 격자에서 y 행 2개(경계 y=6 에 걸침) × x 4열 = 8 잎
    SectionLine ln{{1, 6.01}, {15, 6.01}, 0.0, 0.05};
    std::vector<MeshPtr> ms; LeafStats st;
    REQUIRE(collectLeafMeshes(cache, s.rootFile, sectionBand(ln), ms, &st, &err));
    CHECK(st.leafNodes == 4);       // 띠(y 6.01~6.06)는 위쪽 행만: 4 잎
    CHECK(st.maxDepth == 2);
    CHECK(st.fallbackNodes == 0);
    SectionLine ln2{{1, 5.9}, {15, 5.9}, 0.0, 0.2};  // y 5.9~6.1 → 두 행 = 8 잎
    ms.clear();
    REQUIRE(collectLeafMeshes(cache, s.rootFile, sectionBand(ln2), ms, &st, &err));
    CHECK(st.leafNodes == 8);
    // 자식 파일이 없으면 부모 노드로 대체
    fs::remove(dir / "Data" / "L1" / "L2_0.3mxb");
    TileCache c2; ms.clear();
    REQUIRE(collectLeafMeshes(c2, s.rootFile, sectionBand(SectionLine{{1, 1}, {3, 1}, 0, 0.1}), ms, &st, &err));
    CHECK(st.fallbackNodes == 1);
}

TEST_CASE("화면용 LOD 선택: 예산 안에서 세분") {
    auto dir = tmpDir("disp");
    synth::Params p; p.dir = dir; p.leafSpacing = 0.1; p.texSize = 32;
    fs::path f; std::string err;
    REQUIRE(synth::write(p, &f, &err));
    TmxScene s; REQUIRE(readTmxScene(f, s, &err));
    TileCache c;
    std::vector<MeshPtr> small, big; Box3 bb;
    REQUIRE(selectDisplayMeshes(c, s.rootFile, 1000, small, &bb, &err));
    CHECK(small.size() == 1);  // 루트만
    REQUIRE(selectDisplayMeshes(c, s.rootFile, 10000000, big, &bb, &err));
    CHECK(big.size() == 16);   // 전부 잎
    CHECK(bb.mx.x == Approx(16.0).margin(0.001));
}

TEST_CASE("OBJ + metadata.xml") {
    auto dir = tmpDir("obj");
    std::filesystem::create_directories(dir / "Tile_000");
    { std::ofstream f(dir / "metadata.xml"); f << "<?xml version=\"1.0\"?>\n<ModelMetadata version=\"1\">\n <SRS>EPSG:5186</SRS>\n <SRSOrigin>200100.5,450200.25,30</SRSOrigin>\n</ModelMetadata>\n"; }
    { std::ofstream f(dir / "Tile_000" / "t.mtl"); f << "newmtl m0\nmap_Kd t.jpg\n"; }
    { std::ofstream f(dir / "Tile_000" / "t.jpg", std::ios::binary); f << "JPG"; }
    { std::ofstream f(dir / "Tile_000" / "t.obj"); f << "mtllib t.mtl\nv 0 0 1\nv 1 0 1\nv 1 1 2\nv 0 1 2\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nusemtl m0\nf 1/1 2/2 3/3 4/4\n"; }
    std::vector<MeshPtr> ms; std::string err;
    REQUIRE(loadObj(dir / "Tile_000" / "t.obj", ms, &err));
    REQUIRE(ms.size() == 1);
    CHECK(ms[0]->triangleCount() == 2);
    REQUIRE(ms[0]->texture);
    SrsInfo s;
    REQUIRE(findMetadataXml(dir / "Tile_000" / "t.obj", s));
    CHECK(s.srs == "EPSG:5186");
    CHECK(s.origin.x == Approx(200100.5)); CHECK(s.origin.y == Approx(450200.25)); CHECK(s.origin.z == Approx(30));
}
