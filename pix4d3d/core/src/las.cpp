// LAS 점구름 판독·래스터·단면 근사(PIX4D 개조 P1+P2). 정책과 좌표 규약은 las.hpp 참고.
// - LAS 1.0–1.4 퍼블릭 헤더 + 표준 VLR만(텍스트 좌표계 키·WKT). LASzip(.laz) 미지원.
// - loadPointCloud: 균등 간격 샘플링(전체 첫 부분 보존) + 1 cm 복셀 중복 제거 → 로컬 float(원점 =
// 실좌표 최소값 버림).
// - computeSectionPoints: computeSection(메시)와 같은 결과 규약 — s0=0, z0=zMin, res=imageRes,
//   컷 앞(observe d<0)은 잘라 버리고 [0, back] 두께 띠 점만 래스터에 스플랫.
#include "asec/las.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace asec {

namespace {

std::wstring toWide(const std::string &s) {
#if defined(_WIN32)
  if (s.empty())
    return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(size_t(n), 0);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
#else
  return std::wstring(s.begin(), s.end());
#endif
}
std::ifstream openBin(const std::string &path) {
#if defined(_WIN32)
  return std::ifstream(std::filesystem::path(toWide(path)), std::ios::binary);
#else
  return std::ifstream(path, std::ios::binary);
#endif
}

inline uint16_t rd16(const uint8_t *p) {
  uint16_t v;
  std::memcpy(&v, p, 2);
  return v;
}
inline uint32_t rd32(const uint8_t *p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}
inline int32_t rdi32(const uint8_t *p) {
  int32_t v;
  std::memcpy(&v, p, 4);
  return v;
}
inline double rdd(const uint8_t *p) {
  double v;
  std::memcpy(&v, p, 8);
  return v;
}

/// 문자열(WKT·"EPSG:n")에서 EPSG 번호를 모두 찾는다("EPSG:5186", "EPSG::5186", ID["EPSG",5186],
/// AUTHORITY["EPSG","5186"])
std::vector<int> allEpsgs(const std::string &t) {
  std::vector<int> es;
  size_t pos = 0;
  while ((pos = t.find("EPSG", pos)) != std::string::npos) {
    size_t p = pos + 4;
    for (int steps = 0; p < t.size() && !std::isdigit((unsigned char)t[p]) && steps < 8;
         ++steps, ++p) {
    }
    const size_t s0 = p;
    while (p < t.size() && std::isdigit((unsigned char)t[p]))
      ++p;
    if (p > s0 && p - s0 <= 5) {
      const int v = std::atoi(t.substr(s0, p - s0).c_str());
      if (v > 0 && v < 32768)
        es.push_back(v);
    }
    pos += 4;
  }
  return es;
}

/// GeoKeyDirectory(LASF_Projection recordId 34735) 시작부 {버전,버전,키수,3} + 키 {id,loc,cnt,val}.
/// 수평 EPSG = 3072, 수직 = 4096.
void scanGeoKeys(const uint8_t *b, size_t n, int &horiz, int &vert) {
  if (n < 8)
    return;
  const uint16_t count = rd16(b + 6);
  for (uint16_t i = 0; i < count; ++i) {
    const size_t off = size_t(8) + size_t(i) * 8;
    if (off + 8 > n)
      break;
    const uint16_t id = rd16(b + off), v = rd16(b + off + 6);
    if (id == 3072 && v > 0 && horiz == 0)
      horiz = v;
    else if (id == 4096 && v > 0 && vert == 0)
      vert = v;
  }
}

/// RGB 필드 오프셋: 형식 2,3,5,7,8,10 만 색이 있다. 없으면 -1
inline int rgbOffsetOf(uint8_t fmt) {
  switch (fmt) {
  case 2:
  case 5:
    return 20;
  case 3:
    return 28;
  case 7:
  case 8:
  case 10:
    return 30;
  default:
    return -1;
  }
}

struct VKey3 {
  int64_t x, y, z;
  bool operator==(const VKey3 &o) const { return x == o.x && y == o.y && z == o.z; }
};
struct VKeyH {
  size_t operator()(const VKey3 &k) const {
    uint64_t h = uint64_t(k.x) * 0x9E3779B97F4A7C15ULL;
    h ^= uint64_t(k.y) + 0xC2B2AE3D27D4EB4FULL + (h << 6) + (h >> 2);
    h ^= uint64_t(k.z) * 0x165667B19E3779F9ULL + (h << 6) + (h >> 2);
    return size_t(h);
  }
};

inline int64_t floorCm(double v) { return int64_t(std::floor(v / 0.01)); }

} // namespace

bool readLas(const std::string &path, const LasPointVisit &visit, LasInfo &out, std::string *err,
             const std::atomic<bool> *cancel) {
  out = LasInfo();
  std::ifstream f = openBin(path);
  if (!f) {
    if (err)
      *err = "열 수 없습니다: " + path;
    return false;
  }
  uint8_t h[227];
  if (!f.read(reinterpret_cast<char *>(h), 227) || std::memcmp(h, "LASF", 4) != 0) {
    if (err)
      *err = "LAS 파일이 아닙니다(시그니처 없음): " + path;
    return false;
  }
  const uint8_t vMaj = h[24], vMin = h[25];
  if (vMaj != 1) {
    if (err)
      *err = "LAS 1.x 만 지원합니다(버전 " + std::to_string(vMaj) + "." + std::to_string(vMin) +
             "): " + path;
    return false;
  }
  const uint16_t hdrSize = std::max<uint16_t>(rd16(h + 94), 227);
  const uint32_t offsetToData = rd32(h + 96);
  const uint16_t recLenH = rd16(h + 105);
  const uint32_t nPointsH = rd32(h + 107);
  if (offsetToData < uint32_t(hdrSize) || recLenH < 12) {
    if (err)
      *err = "LAS 헤더 수치가 올바르지 않습니다: " + path;
    return false;
  }
  const double sx = rdd(h + 131), sy = rdd(h + 139), sz = rdd(h + 147);
  if (!(sx > 0) || !(sy > 0) || !(sz > 0)) {
    if (err)
      *err = "LAS 축척(scale)이 올바르지 않습니다: " + path;
    return false;
  }

  // 헤더 남는 부분(LAS 1.4의 확장 필드) 건너뛰기
  if (hdrSize > 227)
    f.ignore(hdrSize - 227);

  // VLR(공개 헤더 다음부터 점 데이터 전까지; 54바이트 VLR 헤더
  // [reserved2|userId16|rid2|len2|desc32])
  std::string note;
  uint64_t cur = hdrSize;
  while (cur + 54 <= offsetToData) {
    uint8_t vh[54];
    if (!f.read(reinterpret_cast<char *>(vh), 54))
      break;
    cur += 54;
    const uint16_t rid = rd16(vh + 18), len = rd16(vh + 20);
    if (size_t(len) > offsetToData - cur)
      break; // 비정상 길이 → 나머지 VLR 신뢰 불가
    std::string userId(reinterpret_cast<const char *>(vh + 2), 16);
    userId.assign(userId.c_str()); // 널 뒤 잘라내기
    if (userId == "LASF_Projection" && (rid == 34735 || rid == 2112) && len >= 4) {
      std::string body(size_t(len), 0);
      if (f.read(body.data(), len)) {
        if (rid == 34735) {
          int h2 = 0, v2 = 0;
          scanGeoKeys(reinterpret_cast<const uint8_t *>(body.data()), size_t(len), h2, v2);
          if (h2 > 0)
            out.epsg = out.epsg == 0 ? h2 : out.epsg;
          if (v2 > 0 && out.verticalEpsg == 0)
            out.verticalEpsg = v2;
        } else {
          const auto es = allEpsgs(body);
          const bool comp = body.find("COMPOUNDCRS") != std::string::npos ||
                            body.find("COMPDCS") != std::string::npos;
          if (!es.empty()) {
            if (out.epsg == 0)
              out.epsg = es.front();
            if (comp && es.size() >= 2 && out.verticalEpsg == 0)
              out.verticalEpsg = es.back();
          }
        }
      }
    } else {
      if (userId.find("laszip") != std::string::npos || userId.find("LAzip") != std::string::npos)
        note = "LASzip 압축(.laz)은 지원하지 않습니다 — LAS 파일로 변환해 주세요";
      f.ignore(len);
    }
    cur += len;
  }

  out.versionMinor = vMin;
  out.pointFormat = h[104];
  out.pointCount = nPointsH;
  out.scale = sx;
  out.ox = rdd(h + 155);
  out.oy = rdd(h + 163);
  out.oz = rdd(h + 171);
  Box3 wb;
  wb.mn = Vec3(rdd(h + 187), rdd(h + 203), rdd(h + 219));
  wb.mx = Vec3(rdd(h + 179), rdd(h + 195), rdd(h + 211));
  out.worldBounds = wb;
  out.hasRgb = rgbOffsetOf(h[104]) >= 0;
  out.note = note;
  if (!visit)
    return true; // 정보만 필요할 때

  // 점 순회: 청크 단위로 읽어 방문자에게 전달
  f.clear();
  f.seekg(std::streamoff(offsetToData), std::ios::beg);
  if (!f) {
    if (err)
      *err = "점 데이터 위치가 올바르지 않습니다: " + path;
    return false;
  }
  const size_t recL = size_t(recLenH);
  std::vector<uint8_t> buf(std::min<size_t>(recL * 8192, size_t(1) << 20), 0);
  size_t per = recL > 0 ? buf.size() / recL : 0;
  uint64_t idx = 0;
  const int rOff = rgbOffsetOf(h[104]);
  while (idx < nPointsH) {
    if (cancel && cancel->load())
      return true; // 취소 = 부분 데이터로 반환
    const size_t chunk = size_t(std::min<uint64_t>(uint64_t(per), nPointsH - idx));
    if (chunk == 0)
      break;
    if (!f.read(reinterpret_cast<char *>(buf.data()), std::streamsize(chunk * recL)))
      break;
    for (size_t c = 0; c < chunk && idx < nPointsH; ++c, ++idx) {
      const uint8_t *p = buf.data() + c * recL;
      const double x = sx * rdi32(p) + out.ox;
      const double y = sy * rdi32(p + 4) + out.oy;
      const double z = sz * rdi32(p + 8) + out.oz;
      // RGB 는 채널당 u16(LAS 1.2 규격) → 앱의 8비트 색으로 상위 바이트 취사
      uint8_t rgb8[3];
      if (rOff >= 0 && recL > size_t(rOff) + 5) {
        rgb8[0] = uint8_t(rd16(p + rOff) >> 8);
        rgb8[1] = uint8_t(rd16(p + rOff + 2) >> 8);
        rgb8[2] = uint8_t(rd16(p + rOff + 4) >> 8);
        if (!visit(idx, x, y, z, rgb8))
          return true; // 방문자 중단 = 정상(부분) 종료
      } else if (!visit(idx, x, y, z, nullptr)) {
        return true;
      }
    }
  }
  return true;
}

bool loadPointCloud(const std::string &path, uint64_t maxPoints, PointCloud &out, std::string *err,
                    const std::atomic<bool> *cancel) {
  out = PointCloud();
  if (!maxPoints)
    maxPoints = 8000000;
  LasInfo info; // 1차 판독: 헤더·VLR 정보만(점 순회 없음)
  if (!readLas(path, nullptr, info, err, cancel))
    return false;
  if (!info.note.empty()) {
    if (err)
      *err = info.note;
    return false;
  } // LASzip 등

  const uint64_t total = info.pointCount;
  const bool sample = total > maxPoints;
  const uint64_t stride = sample ? (total + maxPoints - 1) / maxPoints : 1;
  uint64_t kept = 0, nextKeep = 0; // 균등 구간 샘플링(첫 점·마지막 점 모두 포함)
  std::vector<double> wx, wy, wz;
  std::vector<uint8_t> rgb;
  bool wantRgb = info.hasRgb;
  if (stride > 1) {
    const size_t expect = size_t(std::min<uint64_t>(maxPoints * 2, maxPoints + 1024));
    wx.reserve(expect);
    wy.reserve(expect);
    wz.reserve(expect);
  }

  // 2차 판독: 간격 샘플링 + 1 cm 복셀 중복 제거
  std::unordered_set<VKey3, VKeyH> dedup;
  dedup.reserve(size_t(std::min<uint64_t>(maxPoints + 16, 1u << 20)));
  double wmnX = 0, wmnY = 0, wmnZ = 0, wmxX = 0, wmxY = 0, wmxZ = 0;
  bool first = true;
  uint64_t droppedDup = 0;
  auto visit = [&](uint64_t i, double x, double y, double z, const uint8_t *rgbIn) -> bool {
    if (sample) {
      if (i != nextKeep)
        return true;
      ++kept;
      nextKeep =
          (maxPoints > 1 && kept < maxPoints) ? kept * (total - 1) / (maxPoints - 1) : ~uint64_t(0);
    }
    if (cancel && cancel->load())
      return false;
    if (first) {
      wmnX = wmxX = x;
      wmnY = wmxY = y;
      wmnZ = wmxZ = z;
      first = false;
    } else {
      wmnX = std::min(wmnX, x);
      wmnY = std::min(wmnY, y);
      wmnZ = std::min(wmnZ, z);
      wmxX = std::max(wmxX, x);
      wmxY = std::max(wmxY, y);
      wmxZ = std::max(wmxZ, z);
    }
    if (!dedup.insert(VKey3{floorCm(x), floorCm(y), floorCm(z)}).second) {
      ++droppedDup;
      return true;
    }
    wx.push_back(x);
    wy.push_back(y);
    wz.push_back(z);
    if (wantRgb) {
      if (rgbIn) {
        rgb.push_back(rgbIn[0]);
        rgb.push_back(rgbIn[1]);
        rgb.push_back(rgbIn[2]);
      } else
        wantRgb = false; // recLen 이 RGB 필드까지 못 미치면 색 없음으로 통일
    }
    return true;
  };
  LasInfo pass2;
  if (!readLas(path, visit, pass2, err, cancel))
    return false;
  const size_t n = wx.size();
  if (n == 0 || first) {
    if (err)
      *err = "파일 안에 점이 없습니다: " + path;
    return false;
  }

  // 원점 = 실좌표 최소값 아래 버림 → 로컬 float(3MX SRSOrigin 과 같은 모델)
  const Vec3 origin(std::floor(wmnX), std::floor(wmnY), std::floor(wmnZ));
  out.x.resize(n);
  out.y.resize(n);
  out.z.resize(n);
  for (size_t i = 0; i < n; ++i) {
    out.x[i] = float(wx[i] - origin.x);
    out.y[i] = float(wy[i] - origin.y);
    out.z[i] = float(wz[i] - origin.z);
  }
  std::vector<double>().swap(wx);
  std::vector<double>().swap(wy);
  std::vector<double>().swap(wz);
  out.rgb = std::move(rgb);
  if (!wantRgb)
    std::vector<uint8_t>().swap(out.rgb);
  Box3 bb;
  bb.mn = Vec3(wmnX - origin.x, wmnY - origin.y, wmnZ - origin.z);
  bb.mx = Vec3(wmxX - origin.x, wmxY - origin.y, wmxZ - origin.z);
  out.bounds = bb;
  out.totalInFile = total;

  // 좌표계: 파일 기록 EPSG 존중, 기록 없으면 EPSG:5186(중부 원점) 가정 + 경고
  int epsg = info.epsg;
  const int vEpsg = info.verticalEpsg;
  std::string note;
  char line[256] = {};
  std::snprintf(line, sizeof line, "LAS 1.%d 형식 %d · 파일 %llu점 · 열린 점 %zu개",
                int(info.versionMinor), int(info.pointFormat), (unsigned long long)total, n);
  note += line;
  if (stride > 1) {
    std::snprintf(line, sizeof line, " · 상한 %llu점 도달 → %llu 간격 샘플링",
                  (unsigned long long)maxPoints, (unsigned long long)stride);
    note += line;
  }
  if (droppedDup) {
    std::snprintf(line, sizeof line, " · 1 cm 복셀 중복 %llu점 제거",
                  (unsigned long long)droppedDup);
    note += line;
  }
  if (epsg == 0) {
    epsg = 5186;
    note += "; 좌표계 기록 없음 → EPSG:5186(중부 원점) 가정";
  } else if (epsg != 5186 && epsg != 5187) {
    note += "; 좌표계 EPSG:" + std::to_string(epsg) + "(지원 목록 밖 — 변환 없이 그대로 표기)";
  }
  if (vEpsg > 0)
    note += "; 높이는 수직 EPSG:" + std::to_string(vEpsg);
  out.note = note;
  out.srs = SrsInfo();
  out.srs.srs = vEpsg > 0 ? "EPSG:" + std::to_string(epsg) + "+" + std::to_string(vEpsg)
                          : "EPSG:" + std::to_string(epsg);
  out.srs.origin = origin;
  out.srs.hasOrigin = true;
  return true;
}

// ---- 지형(DEM) 메시: 셀 mean z → 사각 격자(uv = 바운딩박스 정사영상, v=0 은 아래쪽 y) ----
bool terrainMeshFromPoints(const PointCloud &pc, double cellIn, Mesh &out,
                           const std::atomic<bool> *cancel) {
  out = Mesh();
  if (pc.size() < 4 || !pc.bounds.valid())
    return false;
  const double spanX = std::max(1e-6, double(pc.bounds.mx.x) - pc.bounds.mn.x);
  const double spanY = std::max(1e-6, double(pc.bounds.mx.y) - pc.bounds.mn.y);
  double cell = cellIn > 0 ? cellIn : std::max(0.25, std::sqrt(spanX * spanY / 62500.0));
  int nx = 0, ny = 0;
  for (;;) {
    nx = std::max(1, int(std::ceil(spanX / cell)));
    ny = std::max(1, int(std::ceil(spanY / cell)));
    if (size_t(nx) * size_t(ny) <= size_t(1) << 20)
      break;
    cell *= 1.25;
  }
  const size_t nCells = size_t(nx) * size_t(ny);
  std::vector<double> sum(nCells, 0.0);
  std::vector<uint32_t> cnt(nCells, 0);
  for (size_t i = 0; i < pc.size(); ++i) {
    if (cancel && cancel->load())
      return false;
    const int c = std::clamp(int((double(pc.x[i]) - pc.bounds.mn.x) / cell), 0, nx - 1);
    const int r = std::clamp(int((double(pc.y[i]) - pc.bounds.mn.y) / cell), 0, ny - 1);
    const size_t ci = size_t(r) * nx + c;
    sum[ci] += pc.z[i];
    ++cnt[ci];
  }
  std::vector<double> hgt(nCells, 0.0);
  for (size_t i = 0; i < nCells; ++i)
    if (cnt[i])
      hgt[i] = sum[i] / cnt[i];
  std::vector<double>().swap(sum);

  // 격자 꼭짓점 (ny+1)×(nx+1): 높이 = 인접 4셀 mean 덩어리(켜진 것만)
  const int gx = nx + 1, gy = ny + 1;
  std::vector<float> &pos = out.pos;
  pos.reserve(size_t(gx) * gy * 3);
  out.uv.reserve(size_t(gx) * gy * 2);
  for (int j = 0; j < gy; ++j) {
    for (int i = 0; i < gx; ++i) {
      if (cancel && cancel->load())
        return false;
      double s = 0;
      int c2 = 0;
      for (int di = -1; di <= 0; ++di)
        for (int dj = -1; dj <= 0; ++dj) {
          const int ci = i + di, cj = j + dj;
          if (ci < 0 || ci >= nx || cj < 0 || cj >= ny)
            continue;
          const size_t k = size_t(cj) * nx + ci;
          if (!cnt[k])
            continue;
          s += hgt[k];
          ++c2;
        }
      const double x = pc.bounds.mn.x + (i < nx ? i * cell : spanX);
      const double y = pc.bounds.mn.y + (j < ny ? j * cell : spanY);
      pos.push_back(float(x));
      pos.push_back(float(y));
      pos.push_back(float(c2 ? s / c2 : 0.0));
      out.uv.push_back(float(double(x - pc.bounds.mn.x) / spanX));
      out.uv.push_back(float(double(y - pc.bounds.mn.y) / spanY)); // v=0 = 아래(min y)
    }
  }
  auto vid = [gx](int i, int j) { return uint32_t(j) * gx + uint32_t(i); };
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i) {
      if (!cnt[size_t(j) * nx + i])
        continue; // 점 없는 셀은 삼각형 없음
      out.idx.push_back(vid(i, j));
      out.idx.push_back(vid(i + 1, j));
      out.idx.push_back(vid(i, j + 1));
      out.idx.push_back(vid(i + 1, j));
      out.idx.push_back(vid(i + 1, j + 1));
      out.idx.push_back(vid(i, j + 1));
    }
  out.computeBBox();
  return out.idx.size() >= 6;
}

// ---- 평면 정사영상 스플랫(위에서 수직으로, 픽셀당 가장 높은 점) ----
bool renderPlanPoints(const PointCloud &pc, double x0, double y1, double res, int W, int H,
                      RgbaImage &out, const std::atomic<bool> *cancel) {
  out.w = out.h = 0;
  out.px.clear();
  if (pc.x.empty() || W <= 0 || H <= 0 || res <= 0)
    return false;
  const size_t n = pc.size();
  const double areaXY =
      (double(pc.bounds.mx.x) - pc.bounds.mn.x) * (double(pc.bounds.mx.y) - pc.bounds.mn.y);
  const double spacing = areaXY > 0 ? std::sqrt(areaXY / n) : 1.0;
  const int R = std::clamp(int(std::ceil(0.6 * spacing / res)) + 1, 1, 9);
  const size_t nPix = size_t(W) * H;
  std::vector<float> zb(nPix, -1e30f); // 가장 높은 점이 픽셀을 차지
  std::vector<int32_t> pick(nPix, -1);
  for (size_t i = 0; i < n; ++i) {
    if (cancel && cancel->load())
      return false;
    const int c0 = int(std::floor((double(pc.x[i]) - x0) / res));
    const int r0 = int(std::floor((y1 - double(pc.y[i])) / res));
    if (c0 + R < 0 || c0 - R >= W || r0 + R < 0 || r0 - R >= H)
      continue;
    const float z = pc.z[i];
    for (int dr = -R; dr <= R; ++dr)
      for (int dc = -R; dc <= R; ++dc) {
        const int c = c0 + dc, r = r0 + dr;
        if (c < 0 || c >= W || r < 0 || r >= H)
          continue;
        const size_t pi = size_t(r) * W + c;
        if (z > zb[pi]) {
          zb[pi] = z;
          pick[pi] = int32_t(i);
        }
      }
  }
  out.w = W;
  out.h = H;
  out.px.assign(nPix * 4, 0); // 배경 투명
  const bool hasRgb = pc.rgb.size() == n * 3;
  for (size_t pi = 0; pi < nPix; ++pi) {
    const int32_t p = pick[pi];
    if (p < 0)
      continue;
    uint8_t *o = &out.px[pi * 4];
    if (hasRgb) {
      o[0] = pc.rgb[size_t(p) * 3 + 0];
      o[1] = pc.rgb[size_t(p) * 3 + 1];
      o[2] = pc.rgb[size_t(p) * 3 + 2];
    } else {
      o[0] = o[1] = o[2] = 180;
    }
    o[3] = 255;
  }
  return true;
}

// ---- 단면: computeSection(메시)와 같은 결과 규약 ----
bool computeSectionPoints(const PointCloud &pc, const SectionRequest &rq, SectionOutput &out,
                          std::string *err, const std::atomic<bool> *cancel) {
  out = SectionOutput();
  const SectionFrame f(rq.line);
  if (!(f.L >= 1e-3)) {
    if (err)
      *err = "단면선이 너무 짧습니다";
    return false;
  }
  if (pc.x.empty() || !pc.bounds.valid()) {
    if (err)
      *err = "열린 점이 없습니다";
    return false;
  }
  const size_t n = pc.size();
  const double areaXY =
      (double(pc.bounds.mx.x) - pc.bounds.mn.x) * (double(pc.bounds.mx.y) - pc.bounds.mn.y);
  const double spacing = areaXY > 0 ? std::sqrt(areaXY / n) : 1.0;
  const double slab = std::max(0.1, 0.4 * spacing); // 단면 곡선 근사용 전후 절두체
  const double sMin = 0.0, sMax = f.L;
  const double dFar = rq.line.back > 0.01 ? rq.line.back : slab;

  struct Pt {
    double s, z, d;
    size_t i;
  };
  std::vector<Pt> band; // 컷 뒤 띠 [0, dFar] — 입면 영상
  std::vector<Pt> prof; // |d| ≤ slab — 단면선 근사
  double zmin = 1e300, zmax = -1e300;
  for (size_t i = 0; i < n; ++i) {
    if (cancel && cancel->load()) {
      out = SectionOutput();
      if (err)
        *err = "취소됨";
      return false;
    }
    const double x = pc.x[i], y = pc.y[i], z = pc.z[i];
    const double s = f.s(x, y);
    if (s < sMin || s > sMax)
      continue;
    const double d = f.d(x, y);
    if (std::fabs(d) <= slab)
      prof.push_back({s, z, d, i});
    if (d >= 0 && d <= dFar) {
      band.push_back({s, z, d, i});
      zmin = std::min(zmin, z);
      zmax = std::max(zmax, z);
    }
  }
  if (band.empty()) {
    if (err)
      *err = "단면 선 위에 점이 없습니다";
    return false;
  }

  // 곡선 근사: s 빈 최대 z → 연속 빈을 잇는 선분
  const int bins = std::clamp(int((sMax - sMin) / std::max(slab, 0.02)) + 1, 32, 16384);
  std::vector<double> bz(size_t(bins), -1e300);
  std::vector<int> bc(size_t(bins), 0);
  for (const auto &p : prof) {
    const size_t b = std::clamp(size_t(std::floor((p.s - sMin) * bins / (sMax - sMin))), size_t(0),
                                size_t(bins) - 1);
    bz[b] = std::max(bz[b], p.z);
    ++bc[b];
  }
  std::vector<CutSeg> segs;
  segs.reserve(size_t(bins));
  for (int b = 0; b + 1 < bins; ++b)
    if (bc[size_t(b)] && bc[size_t(b) + 1])
      segs.push_back({sMin + (sMax - sMin) * b / bins, bz[size_t(b)],
                      sMin + (sMax - sMin) * (b + 1) / bins, bz[size_t(b) + 1]});

  out.cutStats.meshes = 1;
  out.cutStats.triangles = prof.size();
  out.cutStats.fallbackNodes = 0;
  out.result.line = rq.line;
  out.result.srs = pc.srs;
  out.result.rawSegments = segs.size();
  out.result.triangles = band.size(); // 참고: 띠에 걸린 점 수
  out.result.tiles = 1;
  out.result.profile = buildProfile(segs, rq.cleanup);
  snapEnds(out.result.profile, 0.0, f.L, rq.cleanup.weldTol * 2.5);
  for (const auto &pl : out.result.profile)
    for (const auto &p : pl) {
      zmin = std::min(zmin, p.y);
      zmax = std::max(zmax, p.y);
    }
  if (zmax < zmin) {
    if (err)
      *err = "단면 선 위에 있는 점이 없습니다";
    return false;
  }

  out.result.zMin = std::floor((zmin - rq.zMargin) * 10.0) / 10.0;
  out.result.zMax = std::ceil((zmax + rq.zMargin) * 10.0) / 10.0;

  // 입면 영상(스플랫): renderElevation 과 같은 좌표 규약(s0=0, z1=zMax, res)
  const double z1 = out.result.zMax, z0 = out.result.zMin;
  double res = rq.imageRes > 0 ? rq.imageRes : 0.004;
  const size_t maxPx = rq.maxImagePixels ? rq.maxImagePixels : size_t(24) << 20;
  int W = int(std::ceil((sMax - sMin) / res)), H = int(std::ceil((z1 - z0) / res));
  while (W > 0 && H > 0 && size_t(W) * size_t(H) > maxPx) {
    res *= 1.25;
    W = int(std::ceil((sMax - sMin) / res));
    H = int(std::ceil((z1 - z0) / res));
  }
  if (W < 1 || H < 1) {
    if (err)
      *err = "단면 영상 범위가 올바르지 않습니다";
    return false;
  }
  W = std::min(W, 32768);
  H = std::min(H, 32768);
  const size_t nPix = size_t(W) * H;
  const int R = std::clamp(int(std::ceil(0.6 * spacing / res)) + 1, 1, 9);
  std::vector<float> dep(nPix, 1e30f); // 가까운(작은 d) 점이 이긴다
  std::vector<int32_t> pick(nPix, -1);
  for (const auto &p : band) {
    if (cancel && cancel->load()) {
      out = SectionOutput();
      if (err)
        *err = "취소됨";
      return false;
    }
    const int c0 = int(std::floor(p.s / res));
    const int r0 = int(std::floor((z1 - p.z) / res));
    if (c0 + R < 0 || c0 - R >= W || r0 + R < 0 || r0 - R >= H)
      continue;
    for (int dr = -R; dr <= R; ++dr)
      for (int dc = -R; dc <= R; ++dc) {
        const int c = c0 + dc, r = r0 + dr;
        if (c < 0 || c >= W || r < 0 || r >= H)
          continue;
        const size_t pi = size_t(r) * W + c;
        if (p.d < dep[pi]) {
          dep[pi] = float(p.d);
          pick[pi] = int32_t(p.i);
        }
      }
  }
  const bool hasRgb = pc.rgb.size() == n * 3;
  const double fadeRef = std::max(dFar, 2.0);
  out.image.img.w = W;
  out.image.img.h = H;
  out.image.img.px.assign(nPix * 4, 0); // 배경 투명
  for (size_t pi = 0; pi < nPix; ++pi) {
    const int32_t p = pick[pi];
    if (p < 0)
      continue;
    const size_t pt = size_t(p);
    float fac = 0;
    if (rq.depthFade > 0)
      fac = float(std::clamp(dep[pi] / fadeRef, 0.0, 1.0) * rq.depthFade);
    uint8_t *o = &out.image.img.px[pi * 4];
    double b = hasRgb ? pc.rgb[pt * 3 + 0] : 180.0;
    double g = hasRgb ? pc.rgb[pt * 3 + 1] : 180.0;
    double rr2 = hasRgb ? pc.rgb[pt * 3 + 2] : 180.0;
    o[0] = uint8_t(std::round(b + (255.0 - b) * fac));
    o[1] = uint8_t(std::round(g + (255.0 - g) * fac));
    o[2] = uint8_t(std::round(rr2 + (255.0 - rr2) * fac));
    o[3] = 255;
  }
  out.image.s0 = 0.0;
  out.image.z1 = z1;
  out.image.res = res;
  out.image.trianglesDrawn = pick.size();
  return true;
}

// ---- 점 내보내기(XYZ 텍스트 / LAS 1.2 점 형식 2) ----
namespace {

#if defined(_WIN32)
std::ofstream openOutBin(const std::string &path) {
  return std::ofstream(std::filesystem::path(toWide(path)), std::ios::binary);
}
#else
std::ofstream openOutBin(const std::string &path) { return std::ofstream(path, std::ios::binary); }
#endif

// LAS 1.2 퍼블릭 헤더 쓰기(pointcloud.cpp 작성기와 같은 배치)
void putU16(std::vector<uint8_t> &b, size_t off, uint16_t v) { std::memcpy(&b[off], &v, 2); }
void putI32(std::vector<uint8_t> &b, size_t off, int32_t v) { std::memcpy(&b[off], &v, 4); }
void putU32(std::vector<uint8_t> &b, size_t off, uint32_t v) { std::memcpy(&b[off], &v, 4); }
void putF64(std::vector<uint8_t> &b, size_t off, double v) { std::memcpy(&b[off], &v, 8); }

} // namespace

bool exportPointCloud(const PointCloud &src, const SrsInfo &srs, const SectionLine &bandLine,
                      const PtExportOptions &o, const std::string &path, PtExportStats &out,
                      std::string *err, const std::atomic<bool> *cancel,
                      const std::function<void(size_t)> &progress) {
  out = PtExportStats();
  if (src.x.empty() || !src.bounds.valid()) {
    if (err)
      *err = "내보낼 점이 없습니다";
    return false;
  }

  const SectionFrame fb(bandLine);
  const size_t n = src.size();
  const bool hasRgb = o.rgb && src.rgb.size() == n * 3;
  const Vec3 orig = srs.hasOrigin ? srs.origin : Vec3();
  const double spacing = o.spacing > 0 ? o.spacing : 0;

  // 선택 + 거친 셀 솎기(spacing 복셀별 1 점)
  std::vector<size_t> sel;
  sel.reserve(n / 2);
  std::unordered_set<VKey3, VKeyH> cell;
  double wmnX = 0, wmnY = 0, wmnZ = 0, wmxX = 0, wmxY = 0, wmxZ = 0;
  bool first = true;
  for (size_t i = 0; i < n; ++i) {
    if (cancel && cancel->load()) {
      if (err)
        *err = "취소됨";
      return false;
    }
    const double px2 = double(src.x[i]) + orig.x, py2 = double(src.y[i]) + orig.y,
                 pz2 = double(src.z[i]) + orig.z;
    bool keep = true;
    switch (o.area) {
    case PtArea::Box:
      keep = px2 >= o.box.mn.x && px2 <= o.box.mx.x && py2 >= o.box.mn.y && py2 <= o.box.mx.y &&
             pz2 >= o.box.mn.z && pz2 <= o.box.mx.z;
      break;
    case PtArea::Band: {
      const double s = fb.s(px2, py2), d = fb.d(px2, py2);
      keep = s >= 0.0 && s <= fb.L && d >= -0.001 && d <= std::max(bandLine.back, 0.001) + 0.001;
      break;
    }
    case PtArea::Whole:
    default:
      keep = true;
      break;
    }
    if (keep && spacing > 0) {
      const int64_t cx = int64_t(std::floor(px2 / spacing));
      const int64_t cy = int64_t(std::floor(py2 / spacing));
      keep = cell.insert(VKey3{cx, cy, 0}).second; // z 무시(평면 솎기 = 사용자처럼 진영 비교)
    }
    if (!keep)
      continue;
    if (first) {
      wmnX = wmxX = px2;
      wmnY = wmxY = py2;
      wmnZ = wmxZ = pz2;
      first = false;
    } else {
      wmnX = std::min(wmnX, px2);
      wmnY = std::min(wmnY, py2);
      wmnZ = std::min(wmnZ, pz2);
      wmxX = std::max(wmxX, px2);
      wmxY = std::max(wmxY, py2);
      wmxZ = std::max(wmxZ, pz2);
    }
    sel.push_back(i);
  }
  if (sel.empty()) {
    if (err)
      *err = "선택한 범위 안에 점이 없습니다";
    return false;
  }

  std::ofstream f = openOutBin(path);
  if (!f) {
    if (err)
      *err = "쓸 수 없습니다: " + path;
    return false;
  }

  if (o.format == PointFormat::LAS) {
    // ----- LAS 1.2, 점 형식 2(RGB), scale 0.001, origin = 실좌표 최소값 버림 -----
    const Vec3 originW(std::floor(wmnX), std::floor(wmnY), std::floor(wmnZ));
    const int epsg = srs.epsg();
    const int vEpsg = srs.verticalEpsg();
    uint16_t gkBody[12] = {1, 1, 0, 1, 3072, 0, 1, uint16_t(epsg > 0 ? epsg : 0), 0, 0, 0, 0};
    int gkCount = 1;
    if (vEpsg > 0) {
      gkBody[3] = 2;
      gkCount = 2;
      gkBody[4 + 4] = 4096;
      gkBody[4 + 5] = 0;
      gkBody[4 + 6] = 1;
      gkBody[4 + 7] = uint16_t(vEpsg);
    }
    const size_t gkBytes = size_t(8) + size_t(gkCount) * 8;
    const uint32_t offsetToData = uint32_t(227 + (epsg > 0 ? 54 + gkBytes : 0));
    std::vector<uint8_t> head(227, 0);
    std::memcpy(&head[0], "LASF", 4);
    head[24] = 1;
    head[25] = 2;
    std::memcpy(&head[26], "FA        ", 10);  // sysId(빈 곳 = 공백)
    std::memcpy(&head[58], "archsection", 11); // 소프트웨어
    putU16(head, 94, 227);
    putU32(head, 96, offsetToData);
    putU32(head, 100, epsg > 0 ? 1 : 0);
    head[104] = 2;
    putU16(head, 105, 26);
    putU32(head, 107, uint32_t(sel.size()));
    putF64(head, 131, 0.001);
    putF64(head, 139, 0.001);
    putF64(head, 147, 0.001);
    putF64(head, 155, originW.x);
    putF64(head, 163, originW.y);
    putF64(head, 171, originW.z);
    putF64(head, 179, wmxX);
    putF64(head, 187, wmnX);
    putF64(head, 195, wmxY);
    putF64(head, 203, wmnY);
    putF64(head, 211, wmxZ);
    putF64(head, 219, wmnZ);
    f.write(reinterpret_cast<const char *>(head.data()), std::streamsize(head.size()));
    if (epsg > 0) {
      std::vector<uint8_t> vh(54, 0);
      std::memcpy(&vh[2], "LASF_Projection", 15); // 16번째 바이트는 0(널 패딩)이어야 판독기가 인식
      putU16(vh, 18, 34735);
      putU16(vh, 20, uint16_t(gkBytes));
      std::memcpy(&vh[22], "OGC COORDINATE SYSTEM", 21);
      f.write(reinterpret_cast<const char *>(vh.data()), std::streamsize(vh.size()));
      std::vector<uint8_t> body(gkBytes, 0);
      std::memcpy(body.data(), gkBody, gkBytes);
      f.write(reinterpret_cast<const char *>(body.data()), std::streamsize(body.size()));
    }
    std::vector<uint8_t> rec(26, 0);
    uint64_t written = 0;
    for (const size_t i : sel) {
      if (cancel && cancel->load()) {
        if (err)
          *err = "취소됨";
        return false;
      }
      const double px2 = double(src.x[i]) + orig.x, py2 = double(src.y[i]) + orig.y,
                   pz2 = double(src.z[i]) + orig.z;
      putI32(rec, 0, int32_t(std::lround((px2 - originW.x) / 0.001)));
      putI32(rec, 4, int32_t(std::lround((py2 - originW.y) / 0.001)));
      putI32(rec, 8, int32_t(std::lround((pz2 - originW.z) / 0.001)));
      if (hasRgb) {
        // LAS 형식 2 RGB 는 채널당 u16 — 8비트 원본은 상위 바이트(<<8)로 복원 가능하게 쓴다
        putU16(rec, 20, uint16_t(src.rgb[i * 3 + 0]) << 8);
        putU16(rec, 22, uint16_t(src.rgb[i * 3 + 1]) << 8);
        putU16(rec, 24, uint16_t(src.rgb[i * 3 + 2]) << 8);
      } else {
        rec[20] = rec[21] = rec[22] = rec[23] = rec[24] = rec[25] = 180;
      }
      f.write(reinterpret_cast<const char *>(rec.data()), std::streamsize(rec.size()));
      ++written;
      if (progress && (written % 100000) == 0)
        progress(written);
    }
    out.points = written;
  } else if (o.format == PointFormat::XYZ) {
    // ----- XYZ 텍스트: "X Y Z" (mm 자리) (+ RGB) -----
    char buf[96];
    std::string outBuf;
    outBuf.reserve(sel.size() * 24);
    uint64_t w = 0;
    for (const size_t i : sel) {
      if (cancel && cancel->load()) {
        if (err)
          *err = "취소됨";
        return false;
      }
      const double px2 = double(src.x[i]) + orig.x, py2 = double(src.y[i]) + orig.y,
                   pz2 = double(src.z[i]) + orig.z;
      if (hasRgb)
        std::snprintf(buf, sizeof buf, "%.3f %.3f %.3f %d %d %d\n", px2, py2, pz2,
                      int(src.rgb[i * 3]), int(src.rgb[i * 3 + 1]), int(src.rgb[i * 3 + 2]));
      else
        std::snprintf(buf, sizeof buf, "%.3f %.3f %.3f\n", px2, py2, pz2);
      outBuf += buf;
      if (outBuf.size() > size_t(1) << 20) {
        f.write(outBuf.data(), std::streamsize(outBuf.size()));
        outBuf.clear();
      }
      ++w;
      if (progress && (w % 100000) == 0)
        progress(w);
    }
    if (!outBuf.empty())
      f.write(outBuf.data(), std::streamsize(outBuf.size()));
    out.points = w;
  } else {
    if (err)
      *err = "지원하지 않는 점 내보내기 형식입니다";
    return false;
  }
  if (!f.good()) {
    out = PtExportStats();
    if (err)
      *err = "쓰기 실패(디스크?): " + path;
    return false;
  }
  return true;
}

} // namespace asec
