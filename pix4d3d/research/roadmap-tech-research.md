# 발굴 단면뷰어 2차 로드맵 기술 조사

- 작성: 2026-10-06 (KST) · 단면뷰어 리서치
- 범위: ① 3MX/3MXB·LOD ② 메시 측정 ③ 다중 단면 ④ 유구 윤곽선 DXF/SHP ⑤ 측량앱 기준점·좌표계 ⑥ 정사영상·DSM ⑦ 등고선
- 원칙: 웹 문서·소스로 확인한 내용만 적었고, 확인하지 못한 부분은 **미확인**으로 표시했다. 난이도·기간은 모두 **추정**이다.
- 이 문서를 쓰면서 코드는 수정하지 않았다. `/workspace/survey-app`은 읽지도 않았다. 따라서 측량앱의 내보내기 형식은 **미확인**이다.

---

## 0. 현재 코드베이스 파악 (읽기 전용으로 확인)

| 항목 | 현황 |
|---|---|
| 언어/빌드 | C++17, CMake ≥ 3.16. `project(ExcavSection 1.0.0)`. 코어는 C/C++만, Windows는 **MinGW 교차 빌드(“manual linking, no moc”)**(`ASEC_QT_WIN_DIR`) |
| 코어 라이브러리 `archsection` (Qt 무관, “MDL 재사용 대상”) | `tmx`(3MX/3MXB 읽기·쓰기 + OpenCTM 메모리 디코드/인코드), `lod`(TileCache LRU 1.5 GB, 잎 수집, 해상도별 수집, 화면 예산 선택), `section`(수직 평면 × 삼각형, 선분 잇기·정리, DP 단순화, 레벨선), `raster`(CPU Z버퍼: 단면 입면 영상 + **평면 정사 `renderPlan`**), `dxf`(자체 DXF R2000 작성기: LWPOLYLINE/3D POLYLINE/TEXT/IMAGE), `export`(단면 DXF, GeoRef, 사이드카 JSON), `tiff`(자체 GeoTIFF 작성기, Deflate, 스트립, ModelPixelScale/Tiepoint/GeoKey), `pointcloud`(XYZ/LAS 1.2), `obj`(OBJ+MTL, `metadata.xml` 의 SRS/SRSOrigin), `engine`(MeshSource/TmxSource/StaticSource, `computeSection`) |
| 서드파티(번들) | OpenCTM(zlib 라이선스, 내장 LZMA), nlohmann/json, stb_image/stb_image_write, Catch2 amalgamated. 링크하는 외부 라이브러리는 **zlib 하나뿐**이다. **GDAL·PROJ·CGAL 등은 쓰지 않는다** |
| 앱(`app/`) | Qt Widgets + `QOpenGLWidget`(PlanView: 위에서 내려다보는 정사 표시, 커서 Z는 **표시용 LOD 메시**의 `HeightIndex`로 계산), SectionView(QPainter), MainWindow(리본: 파일/홈/보기/분석/추출/내보내기). 작업 스레드 + 취소 플래그. moc 없이 람다/`std::function` 사용 |
| 이미 되는 기능 | 3MX/OBJ 열기, 단면선 1개(앞/뒤 두께), 입면 영상, 레벨선, 단면 DXF(2D 도면/3D 실좌표), 단면 PNG/TIFF/GeoTIFF, **평면 GeoTIFF(정사)**, 점군 XYZ/LAS, 단면선 CSV |
| 시험 | `tests/`(Catch2: section/stitch/tmx/dxf/raster/pipeline/tiff/points) + `dxf_audit.py`(ezdxf 감사) |
| 앱 빌드 | `app/CMakeLists.txt`(08:33 KST 에 개발 측이 추가한 것을 확인): Linux 는 `find_package(Qt6 Widgets OpenGLWidgets OpenGL)`, Windows 교차 빌드는 `cmake/mingw-w64-x86_64.cmake`(x86_64-w64-mingw32-g++-posix) + **Qt6 정적 라이브러리(`libQt6*.a`) 직접 링크 + `-static`** 이다. `build-linux` 는 `ASEC_BUILD_APP=OFF` 로 구성돼 있고, git 은 아직 커밋이 없다 |

**스택에서 나오는 설계 제약** (아래 추천 전체에 적용)
1. 코어는 계속 Qt 없이 두는 편이 좋다(MDL 재사용 의도).
2. 의존성을 아주 적게 쓰는 방침이다(DXF·TIFF 직접 구현). GDAL/PROJ 를 넣으면 MinGW 교차 빌드와 배포가 무거워진다. MXE 빌드 매트릭스에 GDAL·PROJ 패키지가 올라 있으므로 교차 빌드 자체는 가능해 보인다(https://mxe.cc/build-matrix.html). 다만 이 프로젝트 툴체인에서 실제로 빌드되는지는 **미확인**이다.
3. **정적 링크 배포**: Windows 빌드는 전부 정적 링크다. 새 의존성(GDAL/PROJ/SQLite 등)도 정적 빌드가 필요하다(MXE `x86_64-w64-mingw32.static` 타깃 등). Qt6 를 LGPL 로 쓰면서 정적 링크하면 재링크를 허용하는 등의 LGPL 의무가 따르는데, 이를 어떻게 이행할지는 이 조사 범위 밖이며 **미확인**이다(법무 확인 권장).
4. 라이선스: GPL 라이브러리(CGAL 고수준 패키지, VCGlib, libdxfrw, dxflib 오픈판)를 넣으면 배포 바이너리 전체에 GPL 의무가 생긴다. 배포 라이선스 방침이 정해지지 않았다면 **MIT/BSD/BSL/MPL/zlib 계열만** 쓰는 쪽을 권한다.

---

## ① 3MX / 3MXB 포맷과 LOD 로딩

### 확인된 포맷 구조 (Bentley 공식 문서, ContextCapture Help “3MX specification”)
- **.3mx (JSON 루트)**: `3mxVersion`, `name`, `description`, `logo`, `sceneOptions`, `layers[]`. 지금 정의된 레이어 형식은 `meshPyramid` 하나뿐이다. 레이어 필드는 `id`, `type`, `name`, `description`, `SRS`, `SRSOrigin[3]`(float32), `root`(루트 .3mxb 상대 경로)이다. 좌표는 **P = Pmesh + SRSOrigin** 이다.
- **.3mxb (바이너리)**: 매직 `3MXBO` + `uint32 SH`(헤더 길이) + JSON 헤더(`version:1`, `nodes[]`, `resources[]`) + 버퍼들. 버퍼는 resources 와 **같은 순서**로 놓인다. 한 파일에 **같은 부모를 둔 형제 노드 여러 개**가 들어갈 수 있다.
- **node**: `id`(파일 안에서만 유일, 트리 전체에서는 유일하지 않음), `bbMin`/`bbMax`(AABB), `maxScreenDiameter`(이 노드를 보여 줄 최대 화면 지름, px), `children`(자식 .3mxb 상대 경로 목록), `resources`(geometry ID 목록).
  - `resources` 가 비어 있는 **빈 노드**는 “부모를 숨기되 대체하지 않음”을 뜻한다.
  - **자식이 없는 노드**는 maxScreenDiameter 를 넘어도 계속 보인다(잎).
- **resource**: `textureBuffer`(format `jpg`, `size`), `geometryBuffer`(format `ctm`, `size`, `bbMin/bbMax`, 선택 `texture`), `textureFile`/`geometryFile`(`file` 외부 경로, 예시는 `obj`).
- **현재 구현(Bentley 내보내기)**: 텍스처는 **원본 JPEG 버퍼만** 쓴다. 지오메트리는 OpenCTM 버퍼(위치·UV·인덱스)이고 법선은 선택 사항인데 현재는 빠져 있다. 한 LOD 노드의 자식 전부를 3MXB 하나에 묶는다. OBJ/라벨은 넣지 않는다.
- **SRS 문자열**: `EPSG:코드`, **OGC WKT 그대로**, 또는 `ENU:위도,경도`(로컬 ENU)가 올 수 있다. 지리 좌표계(비미터)는 쓰지 않는다.
- **병합 3MX**: Bentley KB 에 따르면 SRS 와 원점이 같은 프로덕션 여러 개를 **`layers` 에 레이어 여러 개로 나열해** 병합할 수 있다.
- **높이**: ContextCapture 문서상 투영/지리 좌표계의 높이 기준은 **타원체고**이다. 사용자가 “Override vertical coordinate system”을 해야 지오이드 기반 높이가 된다(⑤ 참고).

### 현재 코드와 비교한 차이 (코드 읽기로 확인)
| 항목 | 현재 `tmx.cpp`/`lod.cpp` | 조치 제안 |
|---|---|---|
| 레이어 여러 개(병합 3MX) | `readTmxScene` 이 **첫 meshPyramid 만** 읽고 반환 | 레이어 전부를 MeshSource 여러 개로 다루기(SRS·원점이 같다는 전제를 확인하고 다르면 경고) |
| SRS = WKT / ENU | `epsg()` 가 0 을 돌려줌. 내보내기는 EPSG 0 으로 진행 | ENU 이면 “로컬 좌표” 경고를 띄우고 EPSG 출력 기능을 막기. WKT 이면 EPSG 식별(PROJ 필요) 또는 .prj 에 그대로 쓰기 |
| geometryFile(obj) | 건너뜀 | Bentley 내보내기는 생성하지 않으므로 우선순위 낮음 |
| LOD 선택 | 화면용은 **삼각형 예산으로 한 번 정적 선택**(시점과 무관). 단면은 잎을 씀. 정사는 `diag/res ≤ maxScreenDiameter` | 시점에 따라 바뀌는 선택(아래)은 2차 후보 |
| 스레드 | `TileCache::decode` 가 **캐시 전체 뮤텍스를 쥔 채** CTM 디코드와 JPEG 디코드 콜백을 실행 → 다중 스레드 로딩이 직렬화됨 | 타일별 `std::once_flag`/뮤텍스로 바꾸고, 디코드는 잠금 밖에서 |
| 정밀도 | 메시는 float32(OpenCTM 과 같음) | **SRSOrigin 이 0 이거나 원점에서 멀면** float32 정밀도가 떨어진다(ulp: \|x\|≈1만 m → 약 1 mm, ≈50만 m → 약 6 cm). 열 때 로컬 좌표 크기를 검사해 경고 권장. Bentley 문서 예시 중에는 `"SRSOrigin":[0,0,0]` 인 것도 있다 |

### 추천 방법: 시점 의존 LOD 페이징 (표시용)
- **선택 규칙(공식)**: 노드 경계구의 화면 투영 지름 d 가 `maxScreenDiameter` 를 넘으면 자식으로 바꾼다. 한 경로(루트→잎)에서 동시에 보이는 노드는 하나뿐이고, 한동안 보이지 않은 노드는 삭제한다.
  - 정사(현재 PlanView): d = 경계구 지름 / (m/px).
  - 원근: d ≈ 지름 × 초점거리(px) / 거리.
  - 참고: 공식 문서는 “bounding sphere” 를 쓰라고 하지만 경계구를 어떻게 구하는지는 명시하지 않는다(**미확인**). 지금 코드는 AABB 대각선을 쓰며, 이 값은 경계구 지름 이상이라 보수적(더 고해상도 쪽)이다.
- **구현 골격**: 프레임마다 트리를 돌며 (보임 && d > msd && 자식 로드 완료) 이면 자식으로 내려간다. 자식이 다 로드될 때까지 **부모를 계속 그려** 구멍이 생기지 않게 한다. 요청은 화면 크기 순 우선순위 큐로 관리한다. 작업 스레드 N개가 파일 읽기 → CTM 디코드 → JPEG 디코드를 하고, GL 업로드는 GUI 스레드에서 **프레임당 바이트 예산** 안에서 한다. GPU/CPU 각각 LRU 를 둔다.
- 참고 구현: osgPlugins-3mx 는 노드마다 `osg::PagedLOD` + `PIXEL_SIZE_ON_SCREEN` 을 쓰고 range 를 `[0, msd]`(자신), `[msd, 1e30]`(자식)으로 둔다(소스에서 확인).
- 단면·측정·정사·등고선은 지금처럼 **잎(또는 해상도 규칙)** 으로 따로 수집하고 표시 LOD 와 섞지 않는다(②③ 참고).

### 라이브러리 / 레퍼런스
| 이름 | URL | 라이선스 | 스택 호환 |
|---|---|---|---|
| Bentley 3MX 명세 (ContextCapture Help) | https://docs.bentley.com/LiveContent/web/ContextCapture%20Help-v17/en/GUID-CA7939A6-B5C4-4C5E-BC40-948CEF473358.html | 문서 | 기준 |
| OpenCTM 1.0.3 (이미 내장) | https://openctm.sourceforge.net/ | zlib | 사용 중 |
| ProjSEED/osgPlugins-3mx (C++, OSG 플러그인) | https://github.com/ProjSEED/osgPlugins-3mx | MIT | LOD 로직 참고용. OSG 의존이라 코드 이식은 불필요 |
| ProjSEED/lodToolkit (osgb/점군→3mx, 확장 3mx 문서) | https://github.com/ProjSEED/lodToolkit | MIT | 시험 데이터 생성용. 확장 필드(`offset`)는 비표준 |
| OctopusET/mxmxmx2tiles (Rust, 3mx→3D Tiles) | https://github.com/OctopusET/mxmxmx2tiles | Apache-2.0 | 파서·SRS→ECEF 참고(README 에 “validated against PROJ”). msd→geometricError 를 `diag/msd×16` 으로 매핑 |

### 주의점 / 리스크
- 실제 iTwin Capture Modeler 최신판의 3MX 출력이 문서(v17/v18 Help, 2021년 갱신)와 다른 점이 있는지는 **미확인**이다. 실데이터 샘플로 회귀 시험해야 한다.
- 병합 3MX(레이어 여러 개), ENU/WKT SRS, SRSOrigin=0 은 지금 코드에서 조용히 잘못 동작할 수 있는 입력이다.
- 난이도(추정): 포맷 보강(레이어 여러 개/SRS 종류/정밀도 경고) **하**. 시점 의존 페이징 + 스레드 로더 **중~상**.

---

## ② 메시 측정 (거리·면적·체적)

### 추천 방법
- **픽킹**: 화면 표시는 표시용 LOD 로 하되, **측정값은 항상 잎 메시에서 다시 계산**한다. 지금 커서 Z 는 표시용 LOD 의 `HeightIndex` 값이라 측정 정밀도로 쓰기에 부족하다.
  - 평면(위에서 본 뷰): 클릭 XY 주변의 작은 띠/상자로 `collectLeafMeshes` 를 호출한다. 연직 광선과 삼각형 교차 중 **최대 Z**(지표)를 취한다.
  - 3D 뷰가 생기면 광선–삼각형 교차(Möller–Trumbore) + 잎 메시 AABB/BVH 를 쓴다.
- **거리**: 점–점 3D 거리, 수평거리 √(ΔX²+ΔY²), 연직차 ΔZ, 경사 각도를 함께 표시한다. TM 평면 거리이므로 축척계수·표고 보정은 하지 않는다. 발굴 현장 규모에서는 무시할 수 있는 수준으로 보이나 정량값은 **미확인**이다.
- **면적**:
  - 투영(평면) 면적: 다각형 신발끈 공식.
  - 표면적: 다각형 기둥 안으로 잘라 낸 삼각형 면적의 합. 삼각형×다각형 클리핑에는 Clipper2(BSL-1.0)나 자체 Sutherland–Hodgman(볼록일 때)을 쓴다.
  - 두 값을 나란히 보여 주는 것이 혼동을 줄인다.
- **체적**:
  1. **기준면 대비 cut/fill (권장)**: 유구 윤곽선 다각형 + 기준면(수평 표고 지정, 또는 윤곽선 점들에 맞춘 최소제곱 평면)을 둔다. DSM 격자(⑥) 셀마다 (기준면 − DSM) × 셀면적을 더하고 양수/음수를 따로 집계한다. TIN/삼각기둥 방식이 더 정밀하다는 문헌이 있다.
  2. **닫힌 메시 부피**: 발산정리(부호 있는 사면체 합). **수밀·일관된 감김·다양체**가 전제라서 사진측량 메시(열린 표면)에는 그대로 쓸 수 없다. 기준면으로 뚜껑을 덮어 닫는 절차가 필요하다.
  - 2.5D DSM 방식은 **오버행(파고든 벽)** 을 표현하지 못한다. 유구에서 얼마나 자주 문제가 되는지는 **미확인**이다.
- **LOD 오차**: 표시 LOD 로 측정하면 모서리가 깎이고 면적·부피가 체계적으로 작게 나올 수 있다. 결과 옆에 “사용 LOD/잎 여부, 대체 노드 수(fallbackNodes)”를 기록하자.
- **정밀도 한계**: 메시 자체의 정확도(GCP·GSD)가 지배적이다. OpenCTM MG2 의 정점 양자화 정밀도를 Bentley 가 어떤 값으로 쓰는지는 **미확인**이다.

### 라이브러리
| 이름 | URL | 라이선스 | 스택 호환/비고 |
|---|---|---|---|
| 자체 구현(권장) | – | – | 신발끈·삼각형 면적·DSM 적분·광선교차는 수백 줄 규모. 코어 Qt 무관 유지 |
| libigl (`igl::centroid(V,F,c,vol)` 닫힌 메시 부피, `doublearea`, `ray_mesh_intersect`, `AABB`) | https://libigl.github.io/ | MPL-2.0(일부 하위 폴더는 별도 copyleft) | 헤더 전용 + Eigen(MPL-2.0). 쓸 만하지만 필요한 부분이 작아 자체 구현 대비 이득이 적음 |
| Clipper2 | https://github.com/AngusJohnson/Clipper2 | BSL-1.0 | 다각형 클리핑·오프셋. ④⑦에도 쓰임. 권장 |
| CGAL (PMP: area/volume/slicer) | https://www.cgal.org/license.html | 고수준 패키지 **GPL**(상용 라이선스 별도), 기초 패키지 LGPL | Boost 의존·컴파일 무거움 + GPL → **비권장** |
| VCGlib | https://github.com/cnr-isti-vclab/vcglib | **GPL-3.0** | 비권장(라이선스) |

### 리스크 / 난이도(추정)
- 거리·투영면적 **하**. 표면적(클리핑) **중**. 체적(DSM 의존, 기준면 UI) **중**. 닫힌 메시 부피 **상**(수밀화 필요).
- 참고: GDAL/수치 이론보다는 실무 문헌. 체적 비교 문헌 https://www.mdpi.com/2220-9964/10/6/399

---

## ③ 다중 단면 (평행 / 폴리라인 따라 연속)

### 추천 방법
- **평행 단면 세트**: 기준선 + 간격 + 개수(또는 범위)로 SectionLine N개를 만든다. 각각 지금의 `computeSection` 을 그대로 쓴다. 이름은 A-A′, B-B′ … 로 자동 부여한다.
- **폴리라인 따라 연속 단면(전개 단면)**: 폴리라인 각 구간을 SectionFrame 으로 잘라 이어 붙이고, 누적 거리(chainage)를 s 오프셋으로 준다. 꺾이는 점에서는 두께 띠가 바깥쪽은 겹치고 안쪽은 빈다. 띠를 **각의 이등분선에서 자르는** 방식이 필요하다(현재 BandQuad 는 볼록 사각형 전제).
- **폴리라인에 직교하는 횡단면 연속(도로 횡단식)**: 일정 간격의 측점마다 접선의 법선 방향 SectionLine 을 만든다. 곡률이 크면 이웃 단면끼리 교차할 수 있다는 점에 주의한다.
- **교차 알고리즘**: 지금 `cutMesh`(부호 거리 + 정준 꼭짓점 순서 → 이웃 삼각형과 비트 단위로 같은 교점)와 `stitchSegments`(용접 격자 해시)는 CGAL `Polygon_mesh_slicer`(AABB 트리 기반, 닫힌 고리는 첫 점 반복)와 같은 계열이다. 지금 구현을 유지하고 확장하는 것을 권한다.
- **LOD 일관성**: 모든 단면은 **잎만** 쓴다(지금 방식 유지). 자식 파일이 없어 상위 LOD 로 대체(`fallbackNodes>0`)되면 단면마다 정밀도가 달라지므로 결과·DXF 에 표기한다. 표시 LOD 메시로 단면을 그리면 안 된다.
- **성능**:
  1. 단면 N개의 띠를 합친 영역으로 잎을 **한 번만** 수집하고, 단면별로 메시 bbox 필터를 건다.
  2. 단면별 병렬 처리(스레드 풀)를 한다. 단, 위 ①의 `TileCache::decode` 전역 잠금을 먼저 풀어야 효과가 있다.
  3. 공간 순서로 처리해 캐시(1.5 GB) 스래싱을 막는다.
  4. 입면 영상은 단면마다 최대 24 Mpx 기본값이므로 N배 메모리에 주의해 순차로 내보낸다.
- **출력**: 단면별 DXF 를 따로 내거나, 한 DXF 에 세로로 배열한 도곽(축척 1:20 기준 문자 크기 등 지금 `DxfExportOptions` 재사용)으로 낸다. 평면도에 단면선 위치와 기호(A, A′)를 겹친 평면 DXF 를 함께 낸다.

### 라이브러리
| 이름 | URL | 라이선스 | 비고 |
|---|---|---|---|
| 자체 `section.cpp` (권장) | – | – | 이미 검증 시험 있음 |
| CGAL Polygon_mesh_slicer | https://doc.cgal.org/latest/PMP_Boolean_operations/classCGAL_1_1Polygon__mesh__slicer.html | GPL | 알고리즘 참고용. 도입 비권장 |

### 리스크 / 난이도(추정)
- 평행 세트 **하~중**(대부분 UI·내보내기 배치). 폴리라인 전개 단면 **중**(꺾임부 띠 처리). 병렬화 **중**(캐시 잠금 개선 필요).

---

## ④ 유구 윤곽선 내보내기 (DXF / SHP)

### 추천 방법
- **입력**: 평면 뷰에서 다각형 그리기. 정점 Z 는 ②의 잎 픽킹으로 채운다. 단면 결과에서 윤곽을 따오는 방식은 2단계로 미룬다.
- **DXF**: **지금 `DxfWriter` 를 확장**한다(외부 의존 0).
  - 2D 평면도용: 닫힌 `LWPOLYLINE`(70=1) + 고도(38). 3D 용: `POLYLINE` 3D(70=8|1). 둘 다 이미 지원한다.
  - 레이어 규칙 예: `FEATURE_OUTLINE`, `FEATURE_LABEL`, 유구 유형별 레이어. DXF 에는 속성 테이블이 없으므로 유구번호·명칭은 TEXT 라벨(필요하면 XDATA)로 붙이고, 같은 이름의 SHP 를 함께 낸다.
  - **한글**: R2000(AC1015)은 `$DWGCODEPAGE` 기반 ASCII 파일이다(`ANSI_949` = CP949). 코드 페이지 밖의 문자는 `\U+nnnn` 이스케이프로 쓴다. R2007(AC1021)부터는 UTF-8 이다(ezdxf 문서). 지금 작성기 주석도 “레이어·문자는 ASCII 권장”이다. → 레이어명은 ASCII, 한글 라벨은 `\U+` 이스케이프 또는 `$DWGCODEPAGE=ANSI_949`+CP949 로 쓴다. 레이어명에 `\U+` 를 쓸 수 있는지는 **미확인**이다.
- **SHP**: **shapelib** 를 권장한다(C 파일 몇 개, MIT OR LGPL-2.0+, MinGW 교차 빌드가 쉬움).
  - 형식: `SHPT_POLYGONZ`(3D) 또는 `SHPT_POLYGON`(2D). 링 방향 규칙(외곽 시계 방향)을 지킨다.
  - DBF 필드 예: `FID_NO`, `NAME`, `TYPE`, `AREA_2D`, `AREA_3D`, `Z_MIN`, `Z_MAX`, `DATE`. dBase 필드명은 10바이트 제한이라 **영문 필드명**을 쓴다.
  - **인코딩**: `DBFCreateEx(path, "UTF-8")` 또는 `"CP949"` 로 만들면 `.cpg` 가 생긴다(shapelib 소스에서 확인). shapelib 는 문자열을 **변환하지 않으므로** 앱에서 해당 인코딩 바이트로 넣어야 한다. 필드 폭은 **바이트** 단위다(UTF-8 한글 3바이트, CP949 2바이트).
  - GDAL 은 `.cpg`(없으면 LDID)를 읽어 UTF-8 로 변환한다. LDID 87 은 ISO-8859-1 로 취급하므로 `.cpg` 를 꼭 쓴다.
  - 국내 실무 SW 가 UTF-8 `.cpg` 를 제대로 읽는지는 **미확인**이다 → UTF-8/CP949 선택 옵션을 둔다.
  - **.prj**: EPSG 5185–5188 의 ESRI WKT 를 **고정 문자열로 내장**하면 PROJ 없이 쓸 수 있다(epsg.io `.esriwkt` 로 확인, 예: `PROJCS["KGD2002_Central_Belt_2010",…Central_Meridian 127, False_Easting 200000, False_Northing 600000, Scale 1, Lat_Origin 38]`).
- **GDAL/OGR 대안**: DXF·SHP·GPKG 를 한 API 로 다룰 수 있다. DXF 작성은 AutoCAD 2004 형식이고, 고도가 일정하지 않으면 POLYLINE, 다각형 기본값은 HATCH 이다(`DXF_WRITE_HATCH=FALSE` 필요). SHP 는 `ENCODING` 생성 옵션으로 다룬다. 의존성이 무거우므로 ⑤⑦에서 GDAL 을 이미 쓰기로 했을 때만 고려한다.

### 라이브러리
| 이름 | URL | 라이선스 | 호환성 |
|---|---|---|---|
| 자체 DxfWriter (확장) | – | – | **권장**. ezdxf 감사 시험 체계 이미 있음 |
| shapelib | https://github.com/OSGeo/shapelib | **MIT OR LGPL-2.0-or-later** (`shapefil.h` SPDX 확인) | **권장**. 정적 링크 가능 |
| GDAL/OGR | https://gdal.org/ | MIT 계열(최종 바이너리 라이선스는 함께 빌드한 의존성에 따라 다름) | 무거움, MXE 로 교차 빌드 가능성 있음(미검증) |
| libdxfrw | https://github.com/LibreCAD/libdxfrw | **GPL-2.0+** | 비권장(라이선스) |
| dxflib | https://www.ribbonsoft.com/en/90-dxflib | GPL-2.0+ / 상용 이중 | 비권장(라이선스·비용) |

### 리스크 / 난이도(추정)
- DXF 확장 **하**. SHP(shapelib) **하~중**(인코딩·링 방향·.prj). 다각형 편집 UI **중**.
- 참고: GDAL Shapefile 드라이버 https://gdal.org/en/stable/drivers/vector/shapefile.html · GDAL DXF 드라이버 https://gdal.org/en/stable/drivers/vector/dxf.html · ezdxf 인코딩 https://ezdxf.mozman.at/docs/dxfinternals/fileencoding.html

---

## ⑤ 측량앱 기준점 불러오기 (CSV/SHP, EPSG:5185–5188)

### 확인된 좌표계 사실
- **EPSG:5185/5186/5187/5188** = KGD2002 서부/중부/동부/동해 Belt 2010. 모두 GRS80, TM, 원점위도 38°, 축척 1.0, **FE 200000 / FN 600000** 이다. 중앙자오선만 125/127/129/131°E 로 다르다(epsg.io).
  - 따라서 이 네 좌표계끼리 변환하거나 경위도와 오갈 때는 **같은 측지계 안의 TM 투영 계산뿐이고 격자 파일이 필요 없다**.
- **EPSG 정의상 축 순서는 northing(X), easting(Y)** 이다(WKT2 `AXIS["northing (X)",north,ORDER[1]]`). 한국 측량 관행도 **X = 북, Y = 동** 이다.
  - 반면 3MX `SRSOrigin`(Bentley 예시 `[692625,4798280,0]` = E,N), GeoTIFF, SHP, DXF 는 **E 가 먼저**다. → **측량 CSV 를 불러올 때 X/Y 를 바꿔 넣는 것이 가장 흔한 오류**다.
  - PROJ 는 기본적으로 authority 축 순서를 따른다. GIS 순서로 쓰려면 `proj_normalize_for_visualization` 을 쓴다(GDAL 은 `OAMS_TRADITIONAL_GIS_ORDER`).
- **EPSG:5174** (Korean 1985 / Modified Central Belt): **Bessel** 타원체, 중앙자오선 127.002890277778°, **FN 500000**, towgs84 7변수(epsg.io proj4)이다. **EPSG:5181** 은 GRS80·FN 500000 이다.
  - 5174↔5186 은 타원체·측지계·원점이 모두 달라 **EPSG 코드만 바꿔 붙이면 큰 오차가 생긴다**. 5181↔5186 은 북 방향 100 km 차이다.
  - 5174 를 7변수로 변환했을 때의 정확도와 법정 변환 기준은 **미확인**이다(국토지리정보원 지침 확인 필요).

### 추천 방법
1. **입력 형식**: CSV(구분자·헤더 자동 감지 + 열 지정 대화상자: 점명/X/Y/Z/코드, “X=북” 체크 기본값) + SHP(PointZ, shapelib 로 읽기). 측량앱의 실제 내보내기 형식은 **미확인**이다(이 조사에서는 측량앱 폴더를 읽지 않음). 측량앱 팀에 열 순서·인코딩·좌표계 표기 방식을 확인해야 한다.
2. **좌표계 판정**:
   - 모델 SRS(EPSG:518x)와 점 좌표계가 같으면 `local = world − SRSOrigin`(지금 `SrsInfo::toLocal`)만 하면 끝난다.
   - 다르면 PROJ 로 변환하거나, 5185–5188 끼리라면 **자체 TM 구현**(Krüger 급수, PROJ 결과와 대조 시험)으로 변환한다.
   - 값 범위로 자동 경고를 띄운다. 예: N 값이 약 50만대이면 5174/5181(FN 500000)을 의심한다. X/Y 를 바꿔 넣었을 가능성도 검사한다.
3. **정합 검증**: 기준점마다 모델 표면 높이(잎 픽킹)와 기준점 Z 의 차 ΔZ, 그리고 평면 위치 차를 보여 준다. RMSE 와 표를 내보낸다. 이 기능이 곧 **모델 품질 QA** 가 된다.
4. **표고**:
   - ContextCapture 는 기본이 **타원체고**이고, 수직좌표계를 override 해야 지오이드 높이가 된다(Bentley 문서). 측량 기준점은 보통 정표고(H)로 보인다.
   - H = h − N(지오이드고). 국가 지오이드 모델 **KNGeoid18** 은 국토지리정보원이 격자(위도 33~39°, 경도 124~132°)로 제공한다. 중력자료가 부족한 지역은 **약 10 cm 수준 차이**가 날 수 있다고 공지되어 있다.
   - 공공데이터포털 이용허락은 **“출처표시·변경금지(제3유형)”** 이고 “허락 없이 제3자에게 양여할 수 없음” 문구가 있다. → **설치본에 격자를 넣어 재배포하는 것은 법적으로 불확실하다(미확인, 국토지리정보원 문의 필요)**. 사용자가 직접 받은 파일을 지정하게 하는 방식을 권한다.
   - PROJ 에서 쓰려면 GTG(GeoTIFF grid)로 변환해 `vgridshift` 로 적용하는 방법이 블로그에 소개돼 있다(공식 PROJ-data 포함 여부는 **미확인**).
   - 실무 대안: 모델과 기준점 사이의 ΔZ 평균(상수 오프셋)을 보여 주고, 사용자가 “표고 보정값”을 적용하게 한다. 발굴지 규모에서 지오이드 기울기를 무시해도 되는지는 **미확인**이다.

### 라이브러리
| 이름 | URL | 라이선스 | 호환성 |
|---|---|---|---|
| PROJ | https://proj.org/ | **MIT 계열**(COPYING 확인) | **SQLite 필수**(proj.db). 정적 빌드 시 리소스 내장 옵션(RFC-8). MXE 패키지 있음 |
| 자체 TM(5185–5188 전용) | – | – | 의존성 0. 같은 측지계라 격자 불필요. PROJ 와 교차검증 시험 필수 |
| shapelib | 위 ④ | MIT/LGPL | SHP 점 입력 |
| KNGeoid18 격자 | https://www.data.go.kr/data/15122553/fileData.do · https://map.ngii.go.kr/ms/mesrInfo/geoidIntro.do | 공공누리 제3유형(재배포 제약 가능) | 사용자 제공 파일로 |

### 리스크 / 난이도(추정)
- CSV/SHP 입력 + 같은 SRS 정합 **하**. 자체 TM 또는 PROJ 연동 **중**(PROJ 는 빌드·배포가 부담). 지오이드 처리 **중**(라이선스·데이터 문제가 더 큼). 5174 지원 **중~상**(변환 정확도 근거 확인 필요).
- 참고: https://epsg.io/5186 · https://epsg.io/5174 · ContextCapture 수직좌표계 https://docs.bentley.com/LiveContent/web/ContextCapture%20Help-v18/en/GUID-B6BA56E7-6124-4D90-9D00-D8A2FE7ACE20.html · SRS 문자열 https://docs.bentley.com/LiveContent/web/ContextCapture%20Help-v17/en/GUID-87395EA8-1312-4888-9E68-C6AE68A39FEA.html

---

## ⑥ 정사영상 생성 (+ DSM)

### 현재 상태
`renderPlan`(CPU 래스터, 위에서 수직 투영, 가장 높은 면, Z 버퍼)과 `collectMeshesForResolution`(해상도 규칙 LOD), 자체 GeoTIFF 작성기로 **평면 GeoTIFF 가 이미 있다**. 다만 전체 영상을 메모리에 올리는 RGBA 방식이고, 스트립 기반 일반 TIFF 이며, BigTIFF/타일 TIFF 는 확인되지 않았다.

### 추천 방법
- **DSM 동시 출력**: `renderPlan` 의 Z 버퍼를 float32 래스터로 함께 내보낸다. TIFF 에 `SampleFormat=3(IEEE float)`, `BitsPerSample=32` 를 쓰고, nodata 는 GDAL 전용 태그 `GDAL_NODATA(42113)`(ASCII)로 표기한다. 같은 GeoRef 를 공유하므로 정사영상과 픽셀이 정확히 맞는다.
- **해상도**: 잎 텍스처의 텍셀 크기보다 더 잘게 해도 정보가 늘지 않는다. 텍셀 크기는 삼각형의 세계 면적 대 UV 면적×텍스처 픽셀 수로 추정할 수 있다(방법 제안, 실데이터 검증 **미확인**). 기본값은 축척/DPI 기반(지금 `groundResolution`)으로 유지한다.
- **타일링/대용량**: 출력 영역을 타일(예: 4096²)로 나눠 타일마다 `collectMeshesForResolution(area=타일+여유)` → 렌더 → **스트립/타일 단위 스트리밍 쓰기**를 한다. 4 GB 를 넘으면 BigTIFF 가 필요하다(자체 작성기를 확장하거나 GDAL GTiff 사용). 오버뷰(피라미드)는 선택 사항이다.
- **GPU 대안**: Qt `QOffscreenSurface` + `QOpenGLFramebufferObject` 에 정사 투영으로 렌더하고 `toImage()`(glReadPixels, 비쌈)로 읽는다. FBO 크기는 드라이버 한도(`GL_MAX_RENDERBUFFER_SIZE` 등)를 직접 조회해 타일로 나눈다. 속도는 빠르지만 ⓐ 코어 Qt 무관 원칙과 충돌하고 ⓑ GPU/드라이버 차이와 헤드리스 환경 문제가 있다. → **기본은 CPU 경로(현재)를 유지**하고, 성능 문제가 확인되면 GPU 를 옵션으로 둔다.
- **정사의 한계**: 2.5D 이므로 수직 벽·오버행은 위에서 본 최상면만 남는다. 단면 입면 영상(이미 있음)으로 보완한다.
- **GeoTIFF**: EPSG 5185–5188 GeoKey + `.tfw`(이미 있음). 다른 SW 가 읽는지 GDAL `gdalinfo` 로 검증하는 시험을 추가할 것을 권한다.

### 라이브러리
| 이름 | URL | 라이선스 | 비고 |
|---|---|---|---|
| 자체 `raster.cpp`/`tiff.cpp` | – | – | **권장**(확장: float32, nodata, 스트리밍, BigTIFF) |
| GDAL GTiff/COG 드라이버 | https://gdal.org/ | MIT 계열 | 타일·BigTIFF·오버뷰·COG 가 바로 됨. 의존성 부담 |
| libtiff | http://www.libtiff.org/ | BSD 계열(libtiff 라이선스) | GDAL 없이 BigTIFF/타일 쓰기를 원하면 중간 대안(교차 빌드 확인 **미확인**) |
| Qt OpenGL 오프스크린 | https://doc.qt.io/qt-6/qoffscreensurface.html · https://doc.qt.io/qt-6/qopenglframebufferobject.html | LGPL(Qt) | 옵션 |

### 리스크 / 난이도(추정)
- DSM float32 동시 출력 **하**. 타일 스트리밍 + BigTIFF **중**. GPU 경로 **중~상**.

---

## ⑦ 등고선 생성

### 방법 비교
| 방식 | 장점 | 단점 |
|---|---|---|
| **DSM 래스터 → 마칭 스퀘어** (GDAL `GDALContourGenerateEx` 와 같은 알고리즘: 픽셀 중심 값을 선형 보간, 안장점·nodata 규칙 문서화) | 2.5D 라서 선이 서로 교차하지 않음. 간격·스무딩 제어가 쉬움. ⑥의 DSM 을 재사용 | 해상도에 의존. 오버행은 표현 못 함 |
| 메시 직접 수평 평면 교차 (`cutMesh` 의 수평판) | 원본 정밀도. 벽면 근처까지 정확 | 오버행·노이즈 때문에 고리가 겹치거나 작은 조각이 많이 생김. 영역 전체의 잎 메시가 필요(무거움) |

### 추천 방법
- **기본: DSM(⑥) → 자체 마칭 스퀘어**(수백 줄 규모) 또는 GDAL Contour.
  - 선분 연결은 기존 `stitchSegments`(용접 해시)를 2D 일반화해 재사용한다. 단순화는 기존 `simplifyDP` 를 쓴다.
  - **스무딩은 선이 아니라 DSM 에 먼저**(가우시안, σ 1–2 px 정도) 적용해 선끼리 교차하지 않게 한다. 선을 평활하면 교차가 생길 수 있다.
  - 간격: 사용자 지정. 주곡선/계곡선 구분은 지금 `levelLines` 의 cm 정수 산술(10/50/100 cm)을 재사용한다. 발굴 현장의 표준 간격 관행은 **미확인**이다.
  - nodata: 모델 바깥이나 구멍은 GDAL 규칙처럼 무인 지대로 처리한다.
- **라벨**: 계곡선마다 일정 간격으로 놓고, 접선 방향으로 회전하며, 글자가 뒤집히지 않게 한다. GDAL 출력은 “높은 쪽이 오른쪽”으로 방향이 일관되므로 라벨 위·아래 방향을 정하는 데 쓸 수 있다. DXF 는 기존 `text(…, rotDeg)` 를 쓴다.
- **출력**: DXF 는 `LWPOLYLINE`+고도(38, 2D) 또는 3D POLYLINE, 레이어 `CONTOUR_MINOR` / `CONTOUR_MAJOR` / `CONTOUR_LABEL` 로 나눈다. SHP 는 `SHPT_ARC(Z)` + `ELEV` 필드(shapelib)로 낸다. GDAL 을 쓴다면 `-3d`, `-a ELEV`, `-p`(폴리곤) 옵션에 해당한다.
- **좁은 영역 고정밀 옵션**: 유구 하나 정도의 범위라면 메시 직접 교차를 “정밀 모드”로 둔다.

### 라이브러리
| 이름 | URL | 라이선스 | 비고 |
|---|---|---|---|
| 자체 마칭 스퀘어 | – | – | **권장**(의존성 0) |
| GDAL `GDALContourGenerateEx` / `gdal_contour` | https://gdal.org/en/stable/api/gdal_alg.html · https://gdal.org/en/stable/programs/gdal_contour.html | MIT 계열 | GDAL 을 도입한다면 바로 사용 |

### 리스크 / 난이도(추정)
- DSM 기반 등고선 + DXF/SHP **중**(⑥ DSM 에 의존). 라벨 배치 품질 **중**. 메시 직접 방식 **중~상**.

---

## 8. 로드맵 우선순위 제안 (의존관계 포함)

```
[P0 기반] ─┬─ A. 좌표 기반 정비: 레이어 여러 개 3MX · SRS 종류(EPSG/WKT/ENU) · 축 순서 정책(E,N 내부 / X=N 입력) · float 정밀도 경고
           └─ B. 정밀 픽킹(잎 메시 연직 광선) + TileCache 잠금 개선
                 │
[P1 핵심] ─┬─ ⑤ 기준점 불러오기·정합 QA            ← A, B
           ├─ ② 거리·투영/표면 면적                 ← B
           ├─ ④ 유구 윤곽선 그리기 + DXF/SHP(shapelib, .prj 내장, .cpg) ← B (면적 표기는 ②)
           └─ ③ 평행 다중 단면(기존 엔진 재사용)      ← B(병렬화는 잠금 개선 후)
                 │
[P2 래스터] ─┬─ ⑥ 정사영상 + DSM float32 동시 출력, 타일 스트리밍 ← A
             ├─ ⑦ 등고선(DSM → 마칭 스퀘어 → DXF/SHP)            ← ⑥, ④(출력 경로)
             └─ ② 체적(기준면 cut/fill)                           ← ⑥ DSM + ④ 다각형
                 │
[P3 고도화] ─┬─ ① 시점 의존 LOD 페이징 + 스레드 로더(대형 모델 표시 성능)
             ├─ ③ 폴리라인 전개 단면 / 연속 횡단면
             ├─ ⑤ 지오이드(KNGeoid18, 사용자 제공) · 5174 등 구 좌표계(PROJ 도입 결정 후)
             └─ ⑥ BigTIFF/오버뷰, (선택) GPU 오프스크린
```

**결정해야 할 사항(로드맵 전에)**
1. **GDAL/PROJ 도입 여부**: 도입하지 않는 경로(자체 TM·DXF·TIFF + shapelib + Clipper2)라면 P0~P2 를 모두 처리할 수 있다. 5174·지오이드·WKT 식별까지 하려면 PROJ 가 필요하다. 이 결정에 따라 ⑤의 P3 항목 범위가 정해진다.
2. **배포 라이선스 방침**: GPL 라이브러리(CGAL 고수준, VCGlib, libdxfrw, dxflib)를 배제할지.
3. **측량앱 내보내기 사양**(CSV 열/인코딩/좌표계 표기): **미확인**. 측량앱 팀에 확인이 필요하다.
4. **실제 3MX 샘플**(최신 iTwin Capture Modeler 출력, 병합본, SRSOrigin 값)을 확보해 회귀 시험을 해야 한다.

**가장 큰 리스크 3가지**
1. **좌표·표고 기준 불일치**: X/Y 축 순서(측량 X=북 vs GIS/3MX E 먼저), 5174/5181/5186 혼동(FN 50만 vs 60만, Bessel vs GRS80), ContextCapture 의 기본 타원체고와 기준점 정표고의 차이. 이런 오류는 보기에 그럴듯하게 틀린 결과를 낸다. 정합 QA(⑤-3)와 자동 경고로 막아야 한다.
2. **LOD·정밀도로 생기는 측정 오차**: 표시 LOD 로 측정하거나, 상위 LOD 대체(fallbackNodes)가 생기거나, SRSOrigin 이 원점에서 멀어 float32 정밀도가 떨어지는 경우. 측정·단면·등고선은 반드시 잎 기준으로 하고, 사용 LOD 를 결과에 표기한다.
3. **의존성·라이선스·데이터 재배포**: GDAL/PROJ 를 MinGW **정적** 교차 빌드하는 부담(미검증), Qt6 정적 링크의 LGPL 의무(미확인), GPL 라이브러리를 넣었을 때의 전염, KNGeoid18 격자 재배포 제약(공공누리 제3유형, 양여 금지 문구).

---

## 참고 링크 모음
- Bentley 3MX 명세 목차: https://docs.bentley.com/LiveContent/web/ContextCapture%20Help-v17/en/GUID-CA7939A6-B5C4-4C5E-BC40-948CEF473358.html
- 3MX file: https://docs.bentley.com/LiveContent/web/ContextCapture%20Help-v18/en/GUID-569C1E44-D86A-4942-9ABE-36C33FB5A7EB.html
- 3MXB file: https://docs.bentley.com/LiveContent/web/ContextCapture%20Help-v17/en/GUID-2A3C8541-EF29-4F17-8999-D3398C8E7192.html
- Node data: https://docs.bentley.com/LiveContent/web/ContextCapture%20Help-v17/en/GUID-52BFA6B1-5076-48DB-8E07-5E025F475CB5.html
- Resource data: https://docs.bentley.com/LiveContent/web/ContextCapture%20Help-v17/en/GUID-DB8879C1-803E-43DB-8B5C-31941805A51A.html
- LOD principles: https://docs.bentley.com/LiveContent/web/ContextCapture%20Help-v18/en/GUID-CAA45E41-1720-4A98-A5E5-7D75AC0B3ADF.html
- References by ID(예시 헤더): https://docs.bentley.com/LiveContent/web/ContextCapture%20Help-v18/en/GUID-901F80C7-C7DF-44AC-8932-77CF56B58B30.html
- Current implementation: https://docs.bentley.com/LiveContent/web/ContextCapture%20Help-v17/en/GUID-E063264D-0CF4-4A42-8A68-955D4A646E5C.html
- 3MX 병합 KB: https://bentleysystems.service-now.com/community?id=kb_article&sysparm_article=KB0012237
- osgPlugins-3mx: https://github.com/ProjSEED/osgPlugins-3mx · lodToolkit: https://github.com/ProjSEED/lodToolkit · mxmxmx2tiles: https://github.com/OctopusET/mxmxmx2tiles
- CGAL 라이선스: https://www.cgal.org/license.html · libigl 라이선스: https://libigl.github.io/license/ · VCGlib: https://github.com/cnr-isti-vclab/vcglib · Clipper2: https://github.com/AngusJohnson/Clipper2
- shapelib: https://github.com/OSGeo/shapelib · libdxfrw: https://github.com/LibreCAD/libdxfrw · dxflib: https://www.ribbonsoft.com/en/90-dxflib
- GDAL Shapefile: https://gdal.org/en/stable/drivers/vector/shapefile.html · GDAL DXF: https://gdal.org/en/stable/drivers/vector/dxf.html · GDAL contour: https://gdal.org/en/stable/programs/gdal_contour.html · GDAL 알고리즘 API: https://gdal.org/en/stable/api/gdal_alg.html · GDAL 라이선스: https://gdal.org/en/stable/license.html
- PROJ: https://proj.org/ · PROJ RFC-8(리소스 내장): https://proj.org/en/stable/community/rfc/rfc-8.html · MXE: https://mxe.cc/build-matrix.html
- EPSG: https://epsg.io/5185 · https://epsg.io/5186 · https://epsg.io/5187 · https://epsg.io/5188 · https://epsg.io/5174 · https://epsg.io/5181
- KNGeoid18: https://www.data.go.kr/data/15122553/fileData.do · https://map.ngii.go.kr/ms/mesrInfo/geoidIntro.do
- ezdxf 인코딩: https://ezdxf.mozman.at/docs/dxfinternals/fileencoding.html
- Qt 오프스크린: https://doc.qt.io/qt-6/qoffscreensurface.html · https://doc.qt.io/qt-6/qopenglframebufferobject.html
