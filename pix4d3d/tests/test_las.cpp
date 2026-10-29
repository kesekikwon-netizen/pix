// LAS 점구름 판독·단면·내보내기(PIX4D 개조). 합성 LAS 파일을 직접 만들어 판독기 검증.
#include "asec/las.hpp"
#include "catch_amalgamated.hpp"
#include "common.hpp"
#include <cstring>
#include <fstream>
#include <sstream>

using namespace asec;
using Catch::Approx;

namespace {

struct TPoint {
  double X, Y, Z; // 실좌표(파일 단계)
  uint8_t R, G, B;
};

const double kOx = 200000.0, kOy = 450000.0, kOz = 40.0, kScale = 0.001;

void putH(std::vector<uint8_t> &b, size_t o, uint16_t v) { std::memcpy(b.data() + o, &v, 2); }
void putI(std::vector<uint8_t> &b, size_t o, uint32_t v) { std::memcpy(b.data() + o, &v, 4); }
void putD(std::vector<uint8_t> &b, size_t o, double v) { std::memcpy(b.data() + o, &v, 8); }

/// LAS 1.2 형식 2(26 B/점, RGB u16) 합성 파일. epsg 0 = GeoKey 없음, wkt = 2112 WKT 복합 좌표계,
/// laz 나쁜 사용자 아이디 VLR(rid 22204)도 옵션.
bool makeLas(const fs::path &f, const std::vector<TPoint> &pts, int epsg, int vEpsg,
             bool wkt = false, const char *lazUserId = nullptr) {
  std::vector<uint8_t> gk; // 지오 키 본문
  if (!wkt && epsg > 0) {
    const size_t n = vEpsg > 0 ? 24 : 16;
    gk.assign(n, 0);
    putH(gk, 0, 1);
    putH(gk, 2, 1);
    putH(gk, 4, 0);
    putH(gk, 6, vEpsg > 0 ? 2 : 1);
    putH(gk, 8, 3072);
    putH(gk, 10, 0);
    putH(gk, 12, 1);
    putH(gk, 14, uint16_t(epsg));
    if (vEpsg > 0) {
      putH(gk, 16, 4096);
      putH(gk, 18, 0);
      putH(gk, 20, 1);
      putH(gk, 22, uint16_t(vEpsg));
    }
  }
  std::string wktBody;
  if (wkt)
    wktBody = "COMPOUNDCRS[\"합성\",EPSG:" + std::to_string(epsg ? epsg : 5186) +
              ",EPSG:" + std::to_string(vEpsg ? vEpsg : 5186) + "]";
  size_t off = 227;
  if (!gk.empty())
    off += 54 + gk.size();
  if (wkt)
    off += 54 + wktBody.size();
  if (lazUserId)
    off += 54 + 2;

  std::vector<uint8_t> b(227, 0);
  std::memcpy(&b[0], "LASF", 4);
  b[24] = 1;
  b[25] = 2;
  putH(b, 94, 227);
  putI(b, 96, uint32_t(off));
  b[104] = 2;
  putH(b, 105, 26);
  putI(b, 107, uint32_t(pts.size()));
  putD(b, 131, kScale);
  putD(b, 139, kScale);
  putD(b, 147, kScale);
  putD(b, 155, kOx);
  putD(b, 163, kOy);
  putD(b, 171, kOz);
  double mnx = 1e300, mny = 1e300, mnz = 1e300, mxx = -1e300, mxy = -1e300, mxz = -1e300;
  for (const auto &p : pts) {
    mnx = std::min(mnx, p.X);
    mny = std::min(mny, p.Y);
    mnz = std::min(mnz, p.Z);
    mxx = std::max(mxx, p.X);
    mxy = std::max(mxy, p.Y);
    mxz = std::max(mxz, p.Z);
  }
  putD(b, 179, mxx);
  putD(b, 187, mnx);
  putD(b, 195, mxy);
  putD(b, 203, mny);
  putD(b, 211, mxz);
  putD(b, 219, mnz);

  std::vector<uint8_t> rec(26, 0);
  auto wpt = [&](const TPoint &p) {
    std::memset(rec.data(), 0, rec.size());
    const int32_t ix = int32_t(std::lround((p.X - kOx) / kScale));
    const int32_t iy = int32_t(std::lround((p.Y - kOy) / kScale));
    const int32_t iz = int32_t(std::lround((p.Z - kOz) / kScale));
    std::memcpy(&rec[0], &ix, 4);
    std::memcpy(&rec[4], &iy, 4);
    std::memcpy(&rec[8], &iz, 4);
    const uint16_t rr = uint16_t(p.R) << 8, gg = uint16_t(p.G) << 8, bb = uint16_t(p.B) << 8;
    std::memcpy(&rec[20], &rr, 2);
    std::memcpy(&rec[22], &gg, 2);
    std::memcpy(&rec[24], &bb, 2);
    return rec;
  };

  std::ofstream out(f, std::ios::binary);
  if (!out)
    return false;
  out.write(reinterpret_cast<const char *>(b.data()), std::streamsize(b.size()));
  if (!gk.empty()) {
    std::vector<uint8_t> vh(54, 0);
    std::memcpy(&vh[2], "LASF_Projection", 15); // 나머지 0
    putH(vh, 18, 34735);
    putH(vh, 20, uint16_t(gk.size()));
    std::memcpy(&vh[22], "OGC COORD SYS", 13);
    out.write(reinterpret_cast<const char *>(vh.data()), std::streamsize(vh.size()));
    out.write(reinterpret_cast<const char *>(gk.data()), std::streamsize(gk.size()));
  }
  if (wkt) {
    std::vector<uint8_t> vh(54, 0);
    std::memcpy(&vh[2], "LASF_Projection", 15);
    putH(vh, 18, 2112);
    putH(vh, 20, uint16_t(wktBody.size()));
    std::memcpy(&vh[22], "OGC WKT CS", 10);
    out.write(reinterpret_cast<const char *>(vh.data()), std::streamsize(vh.size()));
    out.write(wktBody.data(), std::streamsize(wktBody.size()));
  }
  if (lazUserId) {
    std::vector<uint8_t> vh(54, 0);
    std::memcpy(&vh[2], lazUserId, std::min<size_t>(std::strlen(lazUserId), 15));
    putH(vh, 18, 22204);
    putH(vh, 20, 2);
    out.write(reinterpret_cast<const char *>(vh.data()), std::streamsize(vh.size()));
    const uint8_t zpad[2] = {0, 0};
    out.write(reinterpret_cast<const char *>(zpad), 2);
  }
  for (const auto &p : pts) {
    rec = wpt(p);
    out.write(reinterpret_cast<const char *>(rec.data()), std::streamsize(rec.size()));
  }
  out.flush();
  return bool(out);
}

std::vector<uint8_t> readFileBytes2(const fs::path &f) {
  std::ifstream in(f, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

} // namespace

TEST_CASE("readLas: 헤더 정보·점 순회(형식 2, RGB u16 상위 바이트)") {
  auto dir = tmpDir("las_read");
  auto f = dir / "a.las";
  const std::vector<TPoint> pts = {
      {200000.5, 450000.5, 40.25, 0, 255, 128},
      {200007.25, 450003.75, 42.5, 255, 0, 64},
      {200010.0, 450006.0, 45.0, 13, 200, 100},
      {200002.0, 450001.0, 41.0, 255, 255, 0},
  };
  REQUIRE(makeLas(f, pts, 5186, 0));

  LasInfo info;
  std::string err;
  REQUIRE(readLas(f.string(), {}, info, &err));
  CHECK(info.versionMinor == 2);
  CHECK(info.pointFormat == 2);
  CHECK(info.pointCount == 4);
  CHECK(info.scale == Approx(kScale));
  CHECK(info.ox == Approx(kOx));
  CHECK(info.worldBounds.valid());
  CHECK(info.worldBounds.mn.x == Approx(200000.5).margin(1e-9));
  CHECK(info.worldBounds.mx.z == Approx(45.0).margin(1e-9));
  CHECK(info.epsg == 5186);
  CHECK(info.verticalEpsg == 0);
  CHECK(info.hasRgb);
  CHECK(info.note.empty());

  std::vector<std::array<double, 3>> got;
  std::vector<std::array<uint8_t, 3>> gotRgb;
  const bool ok = readLas(
      f.string(),
      [&](uint64_t, double x, double y, double z, const uint8_t *rgb) {
        got.push_back({x, y, z});
        gotRgb.push_back({rgb[0], rgb[1], rgb[2]});
        return true;
      },
      info, &err);
  REQUIRE(ok);
  CHECK(got.size() == 4);
  for (size_t k = 0; k < 4; ++k) {
    CHECK(got[k][0] == Approx(pts[k].X).margin(0.001));
    CHECK(got[k][1] == Approx(pts[k].Y).margin(0.001));
    CHECK(got[k][2] == Approx(pts[k].Z).margin(0.001));
  }
  CHECK(gotRgb[0][0] == 0);
  CHECK(gotRgb[0][1] == 255);
  CHECK(gotRgb[0][2] == 128);
  CHECK(gotRgb[1][0] == 255);
  CHECK(gotRgb[1][1] == 0);
  CHECK(gotRgb[1][2] == 64);
}

TEST_CASE("readLas: 지오 키 수직 4096 / WKT 2112 복합 좌표계") {
  auto dir = tmpDir("las_srs");
  LasInfo info;
  std::string err;
  auto f1 = dir / "gk.las";
  REQUIRE(makeLas(f1, {{200000, 450000, 40, 1, 2, 3}}, 5187, 5188));
  REQUIRE(readLas(f1.string(), {}, info, &err));
  CHECK(info.epsg == 5187);
  CHECK(info.verticalEpsg == 5188);

  auto f2 = dir / "wkt.las";
  REQUIRE(makeLas(f2, {{200000, 450000, 40, 1, 2, 3}}, 5186, 5186, true));
  info = LasInfo();
  REQUIRE(readLas(f2.string(), {}, info, &err));
  CHECK(info.epsg == 5186);
  CHECK(info.verticalEpsg == 5186);
}

TEST_CASE("loadPointCloud: 로컬 변환·원점·srs·노트") {
  auto dir = tmpDir("las_load");
  auto f = dir / "a.las";
  const std::vector<TPoint> pts = {
      {200000.5, 450000.5, 40.25, 10, 20, 30},
      {200007.25, 450003.75, 42.5, 40, 50, 60},
      {200010.0, 450006.0, 45.0, 70, 80, 90},
  };
  REQUIRE(makeLas(f, pts, 5186, 0));

  PointCloud pc;
  std::string err;
  REQUIRE(loadPointCloud(f.string(), 8000000, pc, &err));
  CHECK(pc.size() == 3);
  CHECK(pc.totalInFile == 3);
  CHECK(pc.srs.hasOrigin);
  CHECK(pc.srs.srs == "EPSG:5186");
  CHECK(pc.srs.origin.x == Approx(200000));
  CHECK(pc.srs.origin.y == Approx(450000));
  CHECK(pc.srs.origin.z == Approx(40));
  CHECK(double(pc.x[0]) == Approx(0.5));
  CHECK(double(pc.y[2]) == Approx(6.0));
  CHECK(double(pc.z[2]) == Approx(5.0));
  CHECK(pc.bounds.valid());
  CHECK(pc.bounds.mx.x == Approx(10.0).margin(1e-6));
  CHECK(pc.bounds.mn.z == Approx(0.25).margin(1e-6));
  CHECK(pc.rgb.size() == 9);
  CHECK(pc.rgb[0] == 10);
  CHECK(pc.note.find("LAS 1.2 형식 2 · 파일 3점 · 열린 점 3개") != std::string::npos);
}

TEST_CASE("loadPointCloud: 좌표계 기록 없음 → EPSG:5186 가정 노트(srs 문자열은 수직 없음)") {
  auto dir = tmpDir("las_eps0");
  auto f = dir / "a.las";
  REQUIRE(makeLas(f, {{200000, 450000, 40, 0, 0, 0}}, 0, 0));
  PointCloud pc;
  std::string err;
  REQUIRE(loadPointCloud(f.string(), 8000000, pc, &err));
  CHECK(pc.srs.srs == "EPSG:5186");
  CHECK(pc.note.find("가정") != std::string::npos);
  CHECK(pc.note.find("중복") == std::string::npos);
}

TEST_CASE("loadPointCloud: 복합 srs 문자열(EPSG:h+v)") {
  auto dir = tmpDir("las_epsv");
  auto f = dir / "a.las";
  REQUIRE(makeLas(f, {{200001, 450001, 41, 0, 0, 0}}, 5186, 5188));
  PointCloud pc;
  std::string err;
  REQUIRE(loadPointCloud(f.string(), 8000000, pc, &err));
  CHECK(pc.srs.srs == "EPSG:5186+5188");
}

TEST_CASE("loadPointCloud: 상한 도달 → 균등 간격 샘플링") {
  auto dir = tmpDir("las_stride");
  auto f = dir / "a.las";
  std::vector<TPoint> pts;
  for (int i = 0; i < 12; ++i)
    pts.push_back({200000.0 + 0.5 * i, 450000.0, 40.0 + 0.1 * i, 0, 0, 0});
  REQUIRE(makeLas(f, pts, 5186, 0));
  PointCloud pc;
  std::string err;
  REQUIRE(loadPointCloud(f.string(), 6, pc, &err)); // 상한 6 → 균등 구간(끝점 포함)
  CHECK(pc.totalInFile == 12);
  CHECK(pc.size() == 6);
  CHECK(pc.note.find("간격 샘플링") != std::string::npos);
  CHECK(double(pc.z.front()) == Approx(0.0).margin(1e-6)); // i = 0
  CHECK(double(pc.z.back()) == Approx(1.1).margin(1e-6));  // i = 11(마지막 점)
}

TEST_CASE("loadPointCloud: 1 cm 복셀 중복 제거") {
  auto dir = tmpDir("las_dup");
  auto f = dir / "a.las";
  const std::vector<TPoint> pts = {
      {200001.112, 450001.112, 41.112, 0, 0, 0},
      {200001.117, 450001.117, 41.117, 0, 0, 0}, // 같은 1 cm 셀 → 제거
      {200001.129, 450001.129, 41.129, 0, 0, 0},
  };
  REQUIRE(makeLas(f, pts, 5186, 0));
  PointCloud pc;
  std::string err;
  REQUIRE(loadPointCloud(f.string(), 8000000, pc, &err));
  CHECK(pc.size() == 2);
  CHECK(pc.note.find("중복 1점") != std::string::npos);
}

TEST_CASE("loadPointCloud: LASzip VLR → 거부") {
  auto dir = tmpDir("las_laz");
  auto f = dir / "a.las";
  REQUIRE(makeLas(f, {{200001, 450001, 41, 0, 0, 0}}, 0, 0, false, "laszip"));
  PointCloud pc;
  std::string err;
  REQUIRE_FALSE(loadPointCloud(f.string(), 8000000, pc, &err));
  CHECK(err.find("LASzip") != std::string::npos);
}

TEST_CASE("terrainMeshFromPoints: 셀 mean z와 격자 삼각형") {
  PointCloud pc;
  pc.x = {0.0f, 1.0f, 0.0f, 1.0f, 0.5f};
  pc.y = {0.0f, 0.0f, 1.0f, 1.0f, 0.5f};
  pc.z = {0.0f, 1.0f, 2.0f, 3.0f, 2.0f};
  pc.bounds = Box3{{0, 0, 0}, {1, 1, 3}};
  Mesh m;
  std::string err;
  REQUIRE(terrainMeshFromPoints(pc, 1.0, m, nullptr));
  CHECK(m.vertexCount() == 4); // (nx+1)(ny+1) = 2×2
  CHECK(m.triangleCount() == 2);
  CHECK(m.bbox.mn.x == Approx(0.0));
  CHECK(m.bbox.mx.y == Approx(1.0));
  // 한 셀 mean = (0+1+2+3+2)/5 = 1.6 → 네 꼭짓점 전부
  for (size_t i = 0; i < m.pos.size(); i += 3)
    CHECK(m.pos[i + 2] == Approx(1.6).margin(1e-6));

  Mesh m2;
  REQUIRE(terrainMeshFromPoints(pc, 0.25, m2, nullptr));
  CHECK(m2.vertexCount() == 25);   // 5×5
  CHECK(m2.triangleCount() == 10); // 점 있는 셀 10개(5점이 각자 다른 셀)
}

TEST_CASE("renderPlanPoints: 픽셀당 가장 높은 점 스플랫") {
  PointCloud pc;
  pc.x = {0.2f, 0.7f};
  pc.y = {0.2f, 0.7f};
  pc.z = {5.0f, 3.0f};
  pc.rgb = {10, 20, 30, 99, 99, 99};
  pc.bounds = Box3{{0.2, 0.2, 3}, {0.7, 0.7, 5}};
  RgbaImage img;
  REQUIRE(renderPlanPoints(pc, 0.0, 1.0, 1.0, 1, 1, img, nullptr));
  CHECK(img.w == 1);
  CHECK(img.h == 1);
  CHECK(img.px[0] == 10); // z 5 m 가 이긴다
  CHECK(img.px[1] == 20);
  CHECK(img.px[2] == 30);
  CHECK(img.px[3] == 255);
}

TEST_CASE("computeSectionPoints: 곡선·입면 영상·z 여백") {
  PointCloud pc;
  Box3 b;
  for (double t = 0.0; t <= 10.0 + 1e-9; t += 0.05) {
    const float x = float(t), zz = float(1.0 + 0.5 * std::fabs(t - 5.0));
    for (double dy : {0.0, 0.02, 0.04, -0.02, -0.04}) {
      const float y = float(5.0 + dy);
      pc.x.push_back(x);
      pc.y.push_back(y);
      pc.z.push_back(zz);
      b.add(Vec3(x, y, zz));
    }
  }
  pc.bounds = b;
  SectionRequest rq;
  rq.line = SectionLine{{0.0, 5.0}, {10.0, 5.0}, 0.0, 0.0}; // 데이터가 s 전 구간을 덮음
  rq.cleanup.simplifyTol = 0.0;                             // 빈 사슬을 그대로 검증(공선 단순화 끔)
  rq.imageRes = 0.5;
  rq.zMargin = 0.25;
  SectionOutput out;
  std::string err;
  REQUIRE(computeSectionPoints(pc, rq, out, &err));
  CHECK(out.result.rawSegments >= 99);
  CHECK(out.result.profile.size() == 1); // s 빈이 모두 어려워 한 폴리라인
  const auto &pl = out.result.profile.front();
  CHECK(pl.size() >= 100);
  CHECK(pl.front().x == Approx(0.0).margin(0.11));
  CHECK(pl.back().x >= Approx(9.9));
  CHECK(out.result.zMin == Approx(0.7).margin(1e-6)); // (1 - 0.25) 버림
  CHECK(out.result.zMax == Approx(3.8).margin(1e-6)); // (3.5 + 0.25) 올림
  CHECK(out.image.s0 == 0.0);
  CHECK(out.image.z1 == Approx(out.result.zMax));
  CHECK(out.image.img.w >= 19);
  CHECK(out.image.img.h >= 6);
  const size_t nPix = size_t(out.image.img.w) * out.image.img.h;
  CHECK(out.image.img.px.size() == nPix * 4);
  bool anyDrawn = false;
  for (size_t i = 0; i < nPix; ++i)
    if (out.image.img.px[i * 4 + 3])
      anyDrawn = true;
  CHECK(anyDrawn);
  CHECK(out.image.trianglesDrawn > 0);
}

TEST_CASE("computeSectionPoints: 선 위에 점이 없으면 실패") {
  PointCloud pc;
  pc.x = {50.f};
  pc.y = {50.f};
  pc.z = {5.f};
  pc.bounds = Box3{{50, 50, 5}, {50, 50, 5}};
  SectionRequest rq;
  rq.line = SectionLine{{1.0, 5.0}, {11.0, 5.0}, 0.0, 1.0};
  SectionOutput out;
  std::string err;
  REQUIRE_FALSE(computeSectionPoints(pc, rq, out, &err));
  CHECK(err.find("점이 없습니다") != std::string::npos);
}

TEST_CASE("exportPointCloud XYZ: 실좌표 = 로컬 + 원점, RGB 옵션") {
  auto dir = tmpDir("las_xyz");
  PointCloud pc;
  pc.x = {0.0f, 10.0f};
  pc.y = {0.0f, 4.0f};
  pc.z = {5.0f, 7.0f};
  pc.rgb = {7, 8, 9, 10, 20, 30};
  pc.bounds = Box3{{0, 0, 5}, {10, 4, 7}};
  SrsInfo srs;
  srs.srs = "EPSG:5186";
  srs.origin = Vec3(200000, 450000, 40);
  srs.hasOrigin = true;

  PtExportOptions o; // XYZ + Whole 기본
  o.rgb = true;
  PtExportStats st;
  std::string err;
  auto f = dir / "a.xyz";
  REQUIRE(exportPointCloud(pc, srs, SectionLine{}, o, f.string(), st, &err));
  CHECK(st.points == 2);
  std::ifstream in(f);
  std::string line;
  REQUIRE(std::getline(in, line));
  CHECK(line == "200000.000 450000.000 45.000 7 8 9");
  REQUIRE(std::getline(in, line));
  CHECK(line == "200010.000 450004.000 47.000 10 20 30");
  REQUIRE_FALSE(std::getline(in, line));

  o.rgb = false;
  auto f2 = dir / "b.xyz";
  REQUIRE(exportPointCloud(pc, srs, SectionLine{}, o, f2.string(), st, &err));
  std::ifstream in2(f2);
  std::string line2;
  REQUIRE(std::getline(in2, line2));
  CHECK(line2 == "200000.000 450000.000 45.000");
}

TEST_CASE("exportPointCloud: 띠 선택은 부호 무관 1점, 간격 솎기") {
  auto dir = tmpDir("las_band");
  PointCloud pc;
  pc.x = {2.0f, 2.0f, 2.0f};
  pc.y = {4.2f, 3.8f, 8.0f};
  pc.z = {1.0f, 2.0f, 9.0f};
  pc.bounds = Box3{{2, 3.8, 1}, {2, 8, 9}};
  SrsInfo srs;

  PtExportOptions o;
  o.area = PtArea::Band;
  SectionLine band{{0.0, 4.0}, {10.0, 4.0}, 0.0, 3.0};
  PtExportStats st;
  std::string err;
  REQUIRE(exportPointCloud(pc, srs, band, o, (dir / "band.xyz").string(), st, &err));
  CHECK(st.points == 1); // y 4.2/3.8 중 한쪽만(n 부호 무관), y 8은 거리 4 로 제외

  // 간격 솎기는 Whole 영역에서 확인(부호 무관)
  PointCloud pc2;
  pc2.x = {2.0f, 3.0f};
  pc2.y = {4.0f, 4.0f};
  pc2.z = {1.0f, 2.0f};
  pc2.bounds = Box3{{2, 4, 1}, {3, 4, 2}};
  PtExportOptions o2;
  o2.spacing = 5.0; // x 셀 floor(2/5)=floor(3/5)=0 → 1점
  REQUIRE(exportPointCloud(pc2, srs, SectionLine{}, o2, (dir / "sparse.xyz").string(), st, &err));
  CHECK(st.points == 1);
  o2.spacing = 0.5; // x 셀 4 / 6 → 2점
  REQUIRE(exportPointCloud(pc2, srs, SectionLine{}, o2, (dir / "dense.xyz").string(), st, &err));
  CHECK(st.points == 2);
}

TEST_CASE("exportPointCloud LAS → 자체 판독기 라운드트립(좌표계 지오 키 포함)") {
  auto dir = tmpDir("las_rtrip");
  PointCloud pc;
  pc.x = {0.25f, 12.75f, 5.0f};
  pc.y = {0.5f, 9.25f, 6.0f};
  pc.z = {1.5f, 4.0f, 70.0f};
  pc.rgb = {1, 2, 3, 128, 200, 255, 255, 0, 0};
  pc.bounds = Box3{{0.25, 0.5, 1.5}, {12.75, 9.25, 70}};
  SrsInfo srs;
  srs.srs = "EPSG:5186+5188";
  srs.origin = Vec3(10000, 20000, 10);
  srs.hasOrigin = true;

  PtExportOptions o;
  o.format = PointFormat::LAS;
  PtExportStats st;
  std::string err;
  auto f = dir / "out.las";
  REQUIRE(exportPointCloud(pc, srs, SectionLine{}, o, f.string(), st, &err));
  CHECK(st.points == 3);

  const auto b = readFileBytes2(f);
  CHECK(b.size() == 227 + 54 + 24 + size_t(3) * 26);
  auto rd16 = [&](size_t o2) {
    uint16_t v;
    std::memcpy(&v, &b[o2], 2);
    return v;
  };
  auto rd32 = [&](size_t o2) {
    uint32_t v;
    std::memcpy(&v, &b[o2], 4);
    return v;
  };
  auto rdd2 = [&](size_t o2) {
    double v;
    std::memcpy(&v, &b[o2], 8);
    return v;
  };
  CHECK(rd32(96) == 227 + 54 + 24); // 지오 키(수평+수직) VLR
  CHECK(std::string(reinterpret_cast<const char *>(&b[227 + 2]), 15) == "LASF_Projection");
  CHECK(rd16(105) == 26);
  CHECK(rd32(107) == 3);
  CHECK(rdd2(131) == Approx(0.001));

  LasInfo info;
  size_t n = 0;
  const double exX[3] = {10000.25, 10012.75, 10005.0};
  const double exY[3] = {20000.5, 20009.25, 20006.0};
  const double exZ[3] = {11.5, 14.0, 80.0};
  const uint8_t exRgb[3][3] = {{1, 2, 3}, {128, 200, 255}, {255, 0, 0}};
  REQUIRE(readLas(
      f.string(),
      [&](uint64_t, double x, double y, double z, const uint8_t *rgb) {
        REQUIRE(n < 3);
        CHECK(x == Approx(exX[n]).margin(0.001));
        CHECK(y == Approx(exY[n]).margin(0.001));
        CHECK(z == Approx(exZ[n]).margin(0.001));
        CHECK(rgb[0] == exRgb[n][0]);
        CHECK(rgb[1] == exRgb[n][1]);
        CHECK(rgb[2] == exRgb[n][2]);
        ++n;
        return true;
      },
      info, &err));
  CHECK(n == 3);
  CHECK(info.epsg == 5186);
  CHECK(info.verticalEpsg == 5188);
}
