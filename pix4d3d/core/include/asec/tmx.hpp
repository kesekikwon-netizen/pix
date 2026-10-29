// Bentley 3MX (공개 명세 3MX 1.0) 읽기/쓰기.
//  .3mx  = JSON 루트(layers[].type=="meshPyramid", SRS, SRSOrigin, root)
//  .3mxb = "3MXBO" + uint32 헤더길이 + JSON 헤더(nodes, resources) + 리소스 버퍼(헤더 순서)
//  geometryBuffer 형식 "ctm" = OpenCTM.  자식(children)은 현재 .3mxb 기준 상대 경로.
#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include "asec/geom.hpp"
#include "asec/srs.hpp"

namespace asec {
namespace fs = std::filesystem;

struct SrsInfo {
    std::string srs;      // 원문: "EPSG:5186", "EPSG:5186+5193", "ENU:lat,lon", WKT1/WKT2 (없으면 빈 문자열)
    Vec3 origin;          // SRSOrigin. 실좌표 = 로컬 + origin (x=동 E, y=북 N, z=높이 — 모델 SRS 그대로)
    bool hasOrigin = false;
    std::string metadataSrs;  // 같은/위 폴더 metadata.xml 의 <SRS>(3MX 일 때 대조용, 없으면 빈 문자열)
    Vec3 toWorld(const Vec3& local) const { return local + origin; }
    Vec3 toLocal(const Vec3& world) const { return world - origin; }
    /// 사용자가 지정한 실제 높이 기준(None = SRS 표기 그대로). 이름표만 — 높이 값은 바꾸지 않는다
    VDatum heightDeclared = VDatum::None;
    /// 해석 결과(asec/srs.hpp). WKT·ENU·복합 모두. 높이 기준 지정이 있으면 반영
    SrsDesc describe() const { SrsDesc d = describeSrs(srs); applyHeightDeclaration(d, heightDeclared); return d; }
    /// 수평 EPSG(WKT 는 ID·REMARK·이름·TM 매개변수로 판별). 모르면 0
    int epsg() const;
    /// 수직 기준 EPSG(복합 "EPSG:h+v" 또는 WKT COMPOUNDCRS 의 VERTCRS ID). 타원체고·없음은 0.
    /// 높이 값은 바꾸지 않는다(모델 SRS 그대로) — 이 번호는 메타데이터(GeoTIFF·LAS 키, 표시)로만 전달
    int verticalEpsg() const;
    /// 파일(DXF 제목·JSON·LAS) 용 짧은 표기
    std::string shortLabel() const { return describe().shortAscii(); }
};

/// 모델 좌표계 점검 결과(표시·경고용). 높이 값은 절대 바꾸지 않는다.
struct SrsReport {
    SrsDesc desc;
    std::string labelKo;                 // "수평 EPSG:5186 / 높이 타원체고(GRS80)"
    std::string tooltipKo;               // 여러 줄(원점·위경도·정밀도·경고 포함)
    std::vector<std::string> warnings;   // desc.warnings + 원점·정밀도·축 순서·metadata 불일치
    bool hasLatLon = false; double lat = 0, lon = 0;  // 모델 중심의 대략 위경도(TM 역계산, x=E y=N)
    double maxLocalXY = 0;               // 로컬 |x|,|y| 최대(m)
    double float32StepMm = 0;            // 그 크기에서 float32 간격(mm)
    bool precisionWarning = false;       // 로컬 좌표가 커서(> 8192 m) mm 정밀도 보장 안 됨
};
/// localBounds = 모델 로컬 상자(SRSOrigin 빼기 전)
SrsReport analyzeSrs(const SrsInfo& s, const Box3& localBounds);
/// float32 로 v(m)를 저장할 때 간격(m)
double float32Step(double v);

struct TmxLayer {
    std::string id, name;
    SrsInfo srs;
    fs::path rootFile;  // 이 레이어의 최상위 .3mxb 절대 경로
};

struct TmxScene {
    std::string name, description;
    SrsInfo srs;                       // 첫(사용) 레이어의 SRS
    fs::path rootFile;                 // 첫 레이어 루트(호환용)
    std::vector<TmxLayer> layers;      // 사용하는 meshPyramid 레이어 전부(병합 3MX = 여러 개). SRS·원점이 첫 레이어와 같은 것만
    size_t skippedLayers = 0;          // SRS·원점이 달라 뺀 레이어 수
    std::vector<std::string> warnings; // 사용자 경고(한국어)
    std::vector<fs::path> roots() const { std::vector<fs::path> r; for (auto& l : layers) r.push_back(l.rootFile); return r; }
};

struct TmxNode {
    std::string id;
    Box3 bb;                         // 로컬 좌표
    double maxScreenDiameter = 0;
    std::vector<fs::path> children;  // 자식 .3mxb 절대 경로(비어 있으면 최고 해상도 = 잎)
    std::vector<std::string> resourceIds;
    std::vector<MeshPtr> meshes;     // decodeNode 후 채워짐
    bool decoded = false;            // 지오메트리 디코드됨
    bool ready = false;              // TileCache::decode 완료(텍스처까지). TmxTile::mu 아래에서만 읽고 쓴다
    bool isLeaf() const { return children.empty(); }
};

struct TmxTile {
    fs::path path;
    /// 타일 단위 잠금(TileCache 가 디코드 때 사용). 서로 다른 타일은 동시에 디코드된다
    std::shared_ptr<std::mutex> mu = std::make_shared<std::mutex>();
    std::vector<TmxNode> nodes;
    // 내부: 원본 바이트와 리소스 위치(지연 디코드용)
    std::vector<uint8_t> bytes;
    struct Res { std::string id, type, format, texId, file; size_t off = 0, size = 0; Box3 bb; };
    std::vector<Res> resources;
    std::vector<std::pair<std::string, TexturePtr>> textures;
};

/// OpenCTM 메모리 디코드/인코드
bool decodeCtm(const uint8_t* data, size_t size, Mesh& out, std::string* err);
bool encodeCtm(const Mesh& m, std::vector<uint8_t>& out, std::string* err, bool mg2 = true, double vertexPrecision = 0.0005);

bool readTmxScene(const fs::path& file3mx, TmxScene& out, std::string* err);
/// 헤더와 버퍼를 읽는다(지오메트리는 decodeNode 로 지연 디코드).
bool readTmxTile(const fs::path& file3mxb, TmxTile& out, std::string* err);
bool decodeNode(TmxTile& tile, size_t nodeIndex, std::string* err);
inline bool decodeAll(TmxTile& t, std::string* err) {
    for (size_t i = 0; i < t.nodes.size(); ++i) if (!decodeNode(t, i, err)) return false;
    return true;
}

// ---- 쓰기(합성 시험 데이터·변환용) ----
struct TmxWriteNode {
    std::string id;
    double maxScreenDiameter = 0;
    std::vector<std::string> childFiles;  // 이 .3mxb 기준 상대 경로
    std::vector<MeshPtr> meshes;          // 각 메시 texture->encoded(jpg) 를 버퍼로 씀
};
bool writeTmxTile(const fs::path& file3mxb, const std::vector<TmxWriteNode>& nodes, std::string* err);
bool writeTmxScene(const fs::path& file3mx, const std::string& name, const SrsInfo& srs, const std::string& rootRelative, std::string* err);
/// 병합 3MX(레이어 여러 개) 쓰기 — 시험·도구용. 각 레이어: 이름, SRS, 루트(.3mx 기준 상대 경로)
struct TmxWriteLayer { std::string name; SrsInfo srs; std::string rootRelative; };
bool writeTmxSceneLayers(const fs::path& file3mx, const std::string& name, const std::vector<TmxWriteLayer>& layers, std::string* err);

/// 파일 전체 읽기(유니코드 경로 안전)
bool readFileBytes(const fs::path& p, std::vector<uint8_t>& out);

}  // namespace asec
