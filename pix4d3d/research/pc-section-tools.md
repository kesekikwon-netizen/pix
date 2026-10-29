# PC용 단면 도면 도구 비교 조사 (고고학 발굴 측량 실무 관점)

- 조사일: 2026-10-06 (KST)
- 대상: 현장 측량 데이터(GNSS RTK/VRS 측점, 토털스테이션 점, CSV/SHP/DXF, 한국 좌표계 EPSG:5185–5188)로 PC에서 **토층·유구 단면**, **지형 종단/횡단 프로파일**을 그리는 도구
- 원칙: 공식 페이지·문서·소스코드에서 확인한 내용만 적었습니다. 확인하지 못한 항목은 **미확인**으로 표시했습니다. 가격은 조사 시점 공개가이고 지역·판매처에 따라 다릅니다.

---

## 0. 핵심 요약

**실무 추천**
1. **QGIS 내장 Elevation Profile + 레이아웃 단면 아이템 (무료)**: 점 레이어(속성 Z 또는 Z 지오메트리)를 단면선 버퍼(tolerance) 안에서 바로 단면에 올립니다. 벡터 PDF, 2D 단면 DXF/SHP/GPKG/CSV로 내보낼 수 있고, 인쇄 레이아웃(3.30+)에 단면 그래프를 넣을 수 있습니다. QGIS 4.0부터는 거리:고도 축척비를 고정해 높이 과장을 줄 수 있습니다. EPSG:5185–5188은 EPSG DB로 지원됩니다. **첫 번째 추천입니다.**
2. **QGIS Profile Tool 플러그인 (무료)**: 점 레이어에 "Search buffer"를 걸어 단면에 투영하고, SVG/PDF/PNG/CSV 그래프와 **DXF(2D 거리-표고 또는 3D 폴리라인)**로 내보냅니다. CAD로 넘기는 경로가 가장 단순합니다.
3. **CAD 중심 조직이라면 DreamPlus(₩66,000/년, AutoCAD) 또는 BricsCAD Pro V26**: DreamPlus는 X,Y,Z 좌표를 3D 폴리선으로 가져와 횡단을 만들거나(PTCS), TIN에서 횡단(CFM)·종단(GRP)을 추출합니다. BricsCAD V26은 TIN 기반의 동적 단면 뷰, 높이 과장, 오프셋·표고 라벨을 지원합니다. 사진측량·포인트클라우드로 토층벽을 기록했다면 **CloudCompare(무료, GPL)**의 단면 추출과 DXF 폴리라인 출력을 함께 쓰는 것이 현실적입니다.

**기존 도구로 부족한 점 (별도 단면뷰어가 채울 틈)**
- 고고학 단면 도면 관례(토층 번호, 층 경계선, 수평 기준선과 해발 표기 "EL. xx.xxx m", A–A′ 단면 기호, 방위 표기, 1:20/1:40 축척 출력)를 **기본 제공하는 무료 도구가 없습니다.** QGIS/CAD에서는 사람이 손으로 꾸며야 합니다.
- **점 코드 → 층 경계선 자동 연결**(예: 같은 토층 코드 점들을 단면 위에서 순서대로 잇기) 기능은 지질용(qProf)이나 독일 체계(TachyGIS, ArchaeoCAD)에만 부분적으로 있고, 국내 코드 체계는 지원하지 않습니다.
- **한국 측량 CSV 관례(X=북, Y=동)와 축 순서 혼동**: EPSG:5186 등은 공식 축 순서가 northing, easting(X=북)입니다. GIS 도구는 X=동으로 읽는 경우가 많아 좌표가 바뀌는 사고가 잦습니다. 이 부분을 자동 판별·검증해 주는 도구는 확인하지 못했습니다.
- 높이 과장을 주면서도 **실제 표고 눈금과 축척 표기를 일관되게** 유지한 도면 출력, **DXF에 레이어·라벨·기준선까지 구조화**해 내보내는 출력은 무료 도구에서 미흡합니다. Profile Tool DXF에는 선만 있고, QGIS DXF는 2D 단면 지오메트리 위주입니다.
- **단면벽 정사영상(사진측량)과 측점 단면을 같은 단면 좌표계에 겹쳐 트레이싱**하는 흐름은 profileAAR/TachyGIS(독일)와 CloudCompare 조합으로만 가능하고 과정이 복잡합니다.
- 비GIS 실무자를 위한 **단순 UI**(파일 열기 → 단면선 2점 지정 → 버퍼 → 도면 출력)가 없습니다.

---

## 1. 비교 요약표

| 도구 | 범주 | 가격/라이선스 | 점→단면 투영 | 높이 과장 | DXF | PDF | SVG | EPSG:5185–5188 | 고고학 단면 적합성 |
|---|---|---|---|---|---|---|---|---|---|
| QGIS Elevation Profile (내장) | GIS | 무료, GPL | ○ (tolerance 버퍼) | 3.x: 1:1 잠금 / 4.0: 축척비 지정 | ○ (2D/3D 결과) | ○ (벡터) | 미확인(이미지 내보내기 포맷 목록 미확인) | ○ | 상 (가공 필요) |
| QGIS Profile Tool | QGIS 플러그인 | 무료 | ○ (점 레이어 Search buffer) | 1:1 잠금 옵션, 임의 수치 입력은 미확인 | ○ (2D/3D 폴리라인) | ○ | ○ | ○ (QGIS 상속) | 상 |
| qProf | QGIS 플러그인 | 무료, GPL-3.0 | △ (지형은 DEM/GPX/라인/점목록, 점 투영은 지질 자세 데이터용) | ○ (수치 지정) | × (SHP/CSV) | ○ | ○ | ○ (프로젝트 CRS) | 중 |
| profileAAR | QGIS 플러그인 | 무료(라이선스 미확인) | 단면 사진 보정용 기준점 변환 | 해당 없음 | 해당 없음 | 해당 없음 | 해당 없음 | ○ (QGIS) | 중상 (사진 단면 전용) |
| TachyGIS (Tachy2GIS_arch v2) | QGIS 플러그인(고고학) | 무료, GPL-3.0 | ○ (Profile-Tool 내장, 상세 미확인) | 미확인 | 미확인 | 미확인 | 미확인 | QGIS상 가능, 템플릿은 UTM32/33 | 상 (독일 체계) |
| survey2gis | 전처리(고고학) | 무료, GPL-3.0 | × (점→선/면 변환 전처리) | – | ○ (출력) | – | – | 미확인 | 보조 |
| AutoCAD Civil 3D | CAD | 연 구독, 공식 스토어 약 $2,870/년(검색 요약 기준) | ○ (COGO 점을 단면 뷰에 투영) | ○ (스타일) | ○ (네이티브) | ○ | 미확인 | ○ (EPSG:5186 할당 사례) | 중 (무겁고 비쌈) |
| BricsCAD Pro V26 | CAD | €710/년(EU 공식 스토어) / 영구 라이선스 있음 | △ (TIN·Civil Point 기반, 개별 점 투영 미확인) | ○ | ○ | ○ | 미확인 | 미확인 (EPSG 기반 CRS DB 있음) | 중 |
| ArchaeoCAD (ArcTron) | CAD 애드온(고고학) | 미확인(하드락 방식) | 미확인 (코드 기반 자동 작도로 "profiles" 생성) | 미확인 | ○ (DWG/DXF) | CAD 경유 | 미확인 | 미확인 | 상 (독일 체계, 유료) |
| TachyCAD Archaeology (FARO/kubit) | CAD 애드온(고고학) | FARO "Legacy Software" 분류 | 미확인 | 미확인 | ○ (AutoCAD) | CAD 경유 | 미확인 | 미확인 | 중 (단종 추정) |
| DreamPlus | 국산 CAD 애드온 | ₩66,000/년(AutoCAD), ₩99,000/년(LT·BricsCAD·GstarCAD) | △ (3D 폴리선→횡단, TIN→횡단) | 미확인 | ○ | CAD 경유 | – | 좌표값 그대로 사용 | 중상 (저가, 토목 관례) |
| CADian Pro + Dream | 국산 CAD | 영구 라이선스(가격은 본문 참고) | △ (종·횡단 자동 작도) | 미확인 | ○ | ○ | ○ (CADian export) | 좌표값 그대로 사용 | 중 |
| SitePlan CAD 플러그인 | 국산 CAD 애드온 | ₩99,000/12개월(측량) | 횡단면도(SSC) 있음, 상세 미확인 | 미확인 | CAD 경유 | CAD 경유 | – | Bessel↔GRS80·원점 변환 기능 | 미확인 |
| 이지소프트 종횡단 프로그램 | 국산 CAD 애드온 | 페이지에 ₩3,000,000 / 할인가 ₩1,800,000 병기 | TIN 기반 종·횡단 | 미확인 | ○ (AutoCAD) | CAD 경유 | – | 미확인 | 하 (골프장·토공용) |
| CloudCompare | 포인트클라우드 | 무료, GPL | ○ (두께 슬라이스, 단면 폴리라인) | 미확인 | ○ (폴리라인) | 미확인 | 미확인 | 좌표계 개념 약함(아래 참고) | 상 (사진측량·스캔 단면) |
| Agisoft Metashape Pro | 사진측량 | $3,499 영구(Pro) / Standard $179(측정 기능 없음) | DEM/모델 위 폴리라인 단면 | 미확인 | ○ (프로파일 저장) | ○ (보고서) | 미확인 | 미확인 | 중 (모델 단면 확인용) |
| Global Mapper (Pro) | GIS 상용 | 구독 Pro $2,199/년부터(검색 요약 기준) | 미확인 (래스터·지형 경로 프로파일) | ○ | ○ (v15.1+) | ○ | 미확인 | 미확인 | 중 |

범례: ○ 지원 확인 / △ 제한적·간접 / × 미지원 확인 / – 해당 없음 / 미확인

---

## 2. QGIS 계열

### 2.1 QGIS 내장 Elevation Profile (고도 프로파일 뷰) + 레이아웃 단면 아이템
- **공식 URL**: https://docs.qgis.org/3.44/en/docs/user_manual/map_views/elevation_profile.html , 레이아웃 아이템: https://docs.qgis.org/3.44/en/docs/user_manual/print_layout/layout_items/layout_elevation_profile.html
- **가격/라이선스**: 무료(QGIS, GPL). QGIS 4.0은 2026-03 출시됐고, 4.x 첫 LTR은 4.2(2026-10 예정)입니다.
- **입력**: 벡터(점·선·면, 2D/3D), 래스터 DEM, 메시, 포인트클라우드. CSV(구분자 텍스트), SHP, GPKG, DXF 등 QGIS가 여는 포맷은 모두 됩니다. 2D 점은 레이어 속성 → Elevation 탭에서 **속성값으로 Z를 지정**할 수 있습니다(예: ALTITUDE 필드).
- **단면 기능**
  - 단면선: 지도에 직접 그리거나 기존 선 피처를 선택합니다. 꺾인 선도 되고, 꼭짓점 위치를 단면에 세로선으로 표시합니다(Subsections Indicator).
  - **점→단면 투영**: Tolerance(지도 단위)로 단면선 양쪽 버퍼 안의 점·선·면을 단면에 표시합니다. 레이어별 tolerance도 줄 수 있고, Nudge Left/Right로 단면선을 평행 이동할 수 있습니다.
  - 높이 과장: 3.x에는 "Lock distance/elevation scales"(1:1 고정)가 있습니다. **QGIS 4.0에 거리:고도 축척비 지정 기능이 추가**됐습니다(PR #62794, 예: 10:1).
  - 측정: 단면 위 두 점 간 수평거리·고도차를 측정하고 스냅할 수 있습니다.
  - 레벨 표기: 축 눈금(라벨·주/보조 그리드 간격 설정)은 확인했습니다. 개별 점의 표고 라벨을 단면에 표시하는 기능은 **미확인**입니다.
  - 출력: **PDF(고품질 벡터)**, 이미지, **Export Results**로 3D 피처 / **2D 단면(X=거리, Y=표고)** / 거리-표고 표를 **DXF, CSV, SHP, GPKG** 등으로 저장합니다.
  - 인쇄 레이아웃(3.30+): "Add Elevation Profile"로 축·그리드·범위·tolerance·레이어를 지정해 도면에 배치합니다. 아틀라스 연동 속성(atlasDriven)이 API에 있습니다.
- **한국 좌표계**: EPSG DB 기반으로 EPSG:5185–5188(KGD2002 / West·Central·East·East Sea Belt 2010)을 지원합니다. 주의: EPSG:5186의 공식 축 순서는 **northing, easting(X=북)**입니다(epsg.org). CSV를 불러올 때 한국 측량 관례(X=북)와 QGIS의 X 필드(=동) 지정이 뒤바뀌지 않게 해야 합니다.
- **장점**: 무료이고 점 투영·버퍼·DXF·PDF·레이아웃까지 한 번에 됩니다. 포인트클라우드·DEM도 함께 겹칠 수 있습니다.
- **단점**: 고고학 관례(토층 번호, 기준선 EL 표기, A–A′)는 수작업입니다. 3.x에서는 임의 높이 과장이 불편합니다. 프로파일 뷰는 프로젝트를 닫으면 사라집니다(문서 경고). GIS 숙련도가 필요합니다.
- **고고학 적합성**: **상**. 토털스테이션·GNSS 점을 버퍼로 투영해 유구·토층 단면의 뼈대를 만들고, DXF로 넘겨 CAD에서 마감하는 흐름에 가장 적합합니다.

### 2.2 Profile Tool (플러그인, 메뉴명 "Terrain Profile")
- **공식 URL**: https://plugins.qgis.org/plugins/profiletool/ , 소스: https://github.com/PANOimagen/profiletool
- **가격/라이선스**: 무료. 최신 4.3.4(2026-03-19), QGIS 3.40–4.99 호환.
- 참고: 사용자가 말한 "Terrain profile"은 별도 플러그인이 아니라 Profile Tool의 메뉴(Plugins > Profile Tool > Terrain Profile)입니다.
- **입력**: 래스터 DEM, **표고 필드가 있는 점 벡터 레이어**.
- **단면 기능**
  - 임시 폴리라인이나 기존 선 피처로 프로파일을 만들고, 여러 레이어를 겹칩니다.
  - **점→단면 투영**: 소스코드에서 점 레이어마다 **"Search buffer"** 열을 확인했습니다. 단면선 버퍼 안의 점을 거리 기준으로 채택합니다.
  - 높이 과장: "양 축 같은 축척 유지(aspect ratio 1)" 옵션이 있습니다. 임의 과장 수치 입력은 **미확인**입니다(축을 자유롭게 줌하는 방식).
  - 출력: 그래프 **SVG, PDF, PNG, CSV**, **DXF**(3D 폴리라인 또는 2D 거리-표고 폴리라인, 레이어명 = QGIS 레이어명).
- **한국 좌표계**: QGIS 프로젝트 CRS를 따라가므로 사용 가능합니다.
- **장점**: 가볍고 빠릅니다. 점 레이어 버퍼 투영과 DXF 2D 출력으로 CAD에 넘기기 쉽습니다.
- **단점**: DXF에는 폴리라인만 있고 라벨·그리드·기준선이 없습니다. 도면 꾸미기 기능이 없습니다.
- **고고학 적합성**: **상**. 빠른 단면 확인과 CAD 이관용으로 좋습니다.

### 2.3 qProf
- **공식 URL**: https://plugins.qgis.org/plugins/qProf/ , https://github.com/mauroalberti/qProf
- **가격/라이선스**: 무료, GPL-3.0. 안정판 0.5.0, QGIS 3용 0.5.2와 QGIS 4용 0.6.3(실험판, 2026-05).
- **입력**: 지형은 **DEM, GPX, 라인 레이어, 점 목록**에서 가져옵니다. 지질 자료는 주향/경사 점, 선, 면입니다.
- **단면 기능**: 높이·경사 프로파일, 다중 프로파일. **높이 과장 수치 지정**과 z 범위를 지정할 수 있습니다. 지질 자세 데이터를 단면에 투영(가장 가까운 수직 투영 / 공통 축 / 개별 축)하고, **선·면 레이어와 단면의 교차**(예: 유구 경계 폴리곤과 단면선 교차 → 색 구분 표시)를 구합니다. 교차·투영은 2점 직선 단면에서만 됩니다.
- **출력**: 그림 **PDF, SVG, TIF**(그래픽 파라미터 저장 가능), 데이터 **SHP/CSV**. DXF는 직접 내보내지 않습니다.
- **한국 좌표계**: 프로젝트 CRS를 사용하므로 가능합니다.
- **장단점**: 폴리곤 교차로 유구 범위를 단면에 표시하는 아이디어가 유용합니다. 반면 지질 전용 UI이고, 토털스테이션 점을 표고 단면점으로 투영하는 용도에는 맞지 않습니다.
- **고고학 적합성**: **중**. 지형 종단과 유구 폴리곤 교차 용도.

### 2.4 profileAAR (고고학 전용)
- **공식 URL**: https://plugins.qgis.org/plugins/profileAAR/ , https://github.com/ISAAKiel/profileAAR
- **가격/라이선스**: 무료. 라이선스 종류는 **미확인**. 3.0(QGIS 4 호환, 2026-06), 2.0.4(QGIS 3.22+).
- **기능**: 발굴 **단면벽 사진을 사진측량으로 보정하기 위해 단면 기준점(측점 못)의 좌표를 변환**합니다(평면좌표+표고 → 단면 좌표계). 단면을 그리는 도구라기보다는 **단면 사진 정합용 전처리 도구**입니다. 독일 킬 대학(ISAAK) 제작이고 TachyGIS가 이를 참고했습니다.
- **고고학 적합성**: **중상**. 토층 사진 정사보정 후 QGIS에서 트레이싱하는 흐름에 유용합니다.

### 2.5 TachyGIS (Tachy2GIS_arch v2)
- **공식 URL**: https://tachygis.github.io/ , https://github.com/Landesamt-fuer-Archaeologie-Sachsen/Tachy2GIS_arch
- **가격/라이선스**: 무료, GPL-3.0. v2는 2026-04 출시, QGIS 3.40 LTR 대상, GeoPackage 기반.
- **기능**: 토털스테이션을 QGIS에 실시간 연동합니다(원래 Leica GSI). 고고학 속성 입력 UI가 있고, **Profile-Tool을 v2에서 개편**했습니다(변환 방법 선택, **교차 단면 기능: 미러링+조립**, 단면번호별 단면도 데이터 내보내기).
- **한국 좌표계**: QGIS 기반이라 CRS 설정은 가능합니다. 다만 제공 프로젝트 템플릿은 UTM32/UTM33(독일)이고 어휘(Vocabulary)도 독일 체계입니다.
- **미확인**: 단면 투영 방식 상세, 높이 과장, DXF/PDF 출력 방식(매뉴얼 작성 중).
- **고고학 적합성**: **상**(개념적으로 가장 가까운 사례). 다만 현장 실시간 측량 중심이고, 기존 CSV 후처리 도구로서의 편의성은 미확인입니다.

### 2.6 survey2gis (전처리)
- **공식 URL**: https://www.survey-tools.org/ , 문서 https://s2g-docs.survey-tools.org/
- **가격/라이선스**: 무료, GPL-3.0. 독일 바덴뷔르템베르크 문화재청이 지원했고 QGIS 플러그인이 있습니다.
- **기능**: 코드가 붙은 측량 CSV/좌표를 parser 스키마로 해석해 **점·선·면(2D/3D)**으로 바꾸고 위상을 정리합니다. 출력은 SHP, DXF 등입니다(DXF는 속성을 별도 txt로 씀). **단면 기능은 없습니다.**
- **의미**: "현장 코드 → 토층선/유구선" 자동 생성 전처리의 참고 모델입니다.

---

## 3. CAD 계열

### 3.1 Autodesk Civil 3D
- **공식 URL**: https://www.autodesk.com/products/civil-3d (도움말: https://help.autodesk.com/cloudhelp/2026/ENG/Civil3D-UserGuide/ )
- **가격**: 연 구독. 공식 스토어 기준 약 **$2,870/년**(검색 요약 기준이며 지역별로 다릅니다. 한국 가격은 미확인).
- **입력**: 점 파일(CSV/TXT, PNEZD 등), DWG/DXF, SHP(Map 3D 기능), 서피스.
- **단면 기능**: 선형(Alignment)에 샘플 라인 → 단면 뷰(Section View) 다중 생성. 단면 뷰 스타일 Graph 탭에서 **수직 과장(Vertical Exaggeration)**을 설정합니다. **COGO 점을 단면/종단 뷰에 투영**할 수 있지만, 투영된 COGO 점 마커는 수직 과장이 자동 적용되지 않는다는 커뮤니티 요청이 있습니다. 데이터 밴드로 표고·오프셋 라벨을 답니다. DWG/DXF/PDF 출력은 기본입니다.
- **한국 좌표계**: MAPCSASSIGN으로 **EPSG:5186(KOREA_GRS80_CENTRAL 등)** 을 할당한 사례가 있습니다. 일부 버전에서는 라이브러리 가져오기가 필요했다는 보고가 있습니다(Autodesk 포럼).
- **장점**: 업계 표준이고 단면 자동화·라벨·밴드가 강력합니다.
- **단점**: 비싸고 무겁습니다. 짧은 유구 단면 하나에도 선형·서피스 개념이 필요해 고고학 단면에는 과합니다.
- **고고학 적합성**: **중**.

### 3.2 BricsCAD Pro V26
- **공식 URL**: https://www.bricsys.com/ (블로그: https://bricscad.octave.com/blog/turn-terrain-into-insight-civil-sections-made-simple-in-bricscad-v26-blog )
- **가격**: EU 공식 스토어 Pro **€710/년**(Lite €330/년 표기, 측량 툴셋은 Pro 이상). 영구 라이선스도 있습니다(가격은 스토어·지역별로 다름).
- **입력**: DWG/DXF, Civil Points(V26: 코드·속성 자동 매핑), GML, TIN 서피스.
- **단면 기능(V26)**: TIN·선형 기반 **섹션 라인 → 섹션 뷰**. 모델이 바뀌면 동적으로 갱신됩니다. Civil Explorer에서 **수직 과장**, 표시 항목, **오프셋·표고 라벨(연관)**을 설정합니다. 개별 측점을 단면에 버퍼 투영하는 기능은 **미확인**입니다.
- **한국 좌표계**: EPSG ID 기반 좌표계 DB(geodatabase.xml, CS-MAP 계열)가 있습니다. EPSG:5185–5188 포함 여부는 **미확인**입니다.
- **장점**: Civil 3D보다 저렴하고 영구 라이선스를 선택할 수 있습니다. DreamPlus·ArchaeoCAD가 BricsCAD를 지원합니다.
- **단점**: 고고학 관례는 직접 만들어야 합니다.
- **고고학 적합성**: **중**.

### 3.3 ArchaeoCAD (ArcTron, 독일): 고고학 전용 CAD 애드온
- **공식 URL**: https://www.arctron.de/products/archaeocad-details-en/
- **가격**: **미확인**(공개가 없음. 하드락(USB 동글) 방식이고 특정 AutoCAD/BricsCAD 버전에 묶임).
- **기능**: **코드가 붙은 토털스테이션 측정 데이터를 자동 작도**(Plandraw)해 층별 레이어로 구조화합니다. "object contours, **profiles**, **levelling values**(레벨 값), excavation boundaries"가 들어간 인쇄용 도면을 만듭니다. **평면(Plana)·단면 프레임**, 좌표격자, 도면 표제 DB, 고고학 기호 라이브러리, 측정 파일 편집기를 제공하고, **측정 평면과 단면 자동 결합**을 지원합니다.
- **미확인**: 점→단면 투영 알고리즘, 높이 과장, 한국 좌표계, 한국어 지원.
- **고고학 적합성**: **상**(기능 콘셉트상 목표에 가장 가깝습니다). 다만 유료이고 독일 발굴 관례 기반이며 국내 도입 사례는 확인하지 못했습니다.

### 3.4 TachyCAD Archaeology (kubit → FARO)
- **URL**: https://knowledge.faro.com/Software/Legacy-Software/Legacy-PointSense_and_CAD_Plugins/TachyCAD/User_Manual_for_TachyCAD_Archaeology
- **상태**: FARO 지식베이스에서 **"Legacy Software"**로 분류되어 있습니다(최신 사양서 2017-04, 한국어 사양서 있음). 신규 판매·지원 여부는 **미확인**입니다.
- **기능**: AutoCAD에서 토털스테이션·블루투스 거리계 측정을 받아 현장 CAD 기록을 합니다. 단면 스캔 워크플로는 TachyCAD Building 문서(수직 평면 준비 → 프로파일 스캔)에서만 확인했습니다.
- **고고학 적합성**: **중**(단종 가능성이 있어 신규 도입은 비추천).

### 3.5 DreamPlus (국산, AutoCAD/BricsCAD/GstarCAD 애드온)
- **공식 URL**: https://dreamcad.net/
- **가격**: 1년 라이선스만 판매. **AutoCAD용 ₩66,000/년**, AutoCAD LT·BricsCAD(V20.2.10 Pro+)·GstarCAD(2024+)용 **₩99,000/년**(VAT 포함).
- **단면 관련 기능**(공식 Q&A·기능 페이지)
  - 좌표 가져오기(CIM): 엑셀의 X,Y,Z → 점 또는 **3D 폴리선**.
  - **3D 폴리선을 횡단으로(PTCS)**: 횡단 방향으로 측량한 점열을 바로 횡단면도로 만듭니다(GNSS 횡단 측량에 맞음).
  - 삼각망(DTIN) → **지형도에서 횡단 추출(CFM)**: 직선·꺾인선·곡선, 다중 횡단, 측점 간격별, 엑셀 측점 목록 지원, 계획고·경계 표기.
  - **지형도에서 종단 추출(GRP)**.
  - 제작사 설명: "토목 전용 설계 프로그램이 아니며 여러 기능을 조합"해야 합니다.
- **미확인**: 버퍼 내 점 투영, 높이 과장 설정, X=북 관례 처리.
- **한국 좌표계**: CAD 좌표값을 그대로 씁니다(좌표계 메타데이터 처리는 없는 것으로 보이나 미확인).
- **고고학 적합성**: **중상**. 국내 측량 실무자에게 익숙한 CAD+리습 방식이고 저렴합니다.

### 3.6 CADian Pro + Dream II (국산 CAD)
- **공식 URL**: https://www.cadian.com/ (제품: https://www.cadian.com/en/product_view.php?idx=60 )
- **가격**: CADian Pro 영구 라이선스(정확한 2026 가격은 **미확인**). 2024-04 가격표(검색 요약)에서는 Dream 100만원(Pro 구매 시 무상 제공), Survey & Cogo 37.7만원, Suite 76.7만원이었습니다.
- **기능**: Dream II 무상 제공(드림플러스 기능 대체). 캐디안 블로그 기준으로 종단·횡단면도 자동 그리기, 지형도 횡단 추출, 계획고·지반고, 사면 기능이 있습니다. CADian Pro 출력 포맷에 DWG/DXF/PDF/**SVG**가 있습니다.
- **고고학 적합성**: **중**. 영구 라이선스 국산 CAD로 AutoCAD 구독을 피할 수 있습니다.

### 3.7 SitePlan CAD 플러그인 (국산)
- **공식 URL**: https://siteplan.kr/products/cad
- **가격**: 측량 ₩99,000/12개월, 엔터프라이즈 ₩149,000/12개월. 15일 무료 체험.
- **기능**: 횡단면도(SSC), 대횡단(SSCM), 등고선, 현황도, SHP 입출력, 좌표 내보내기, **Bessel↔GRS80·원점 변환**.
- **미확인**: 지원 CAD 호스트, 점 투영 방식, 높이 과장, 단면 출력 상세.

### 3.8 이지소프트 종횡단 프로그램 (국산)
- **공식 URL**: https://eazysoft.co.kr/
- **가격**: 페이지에 ₩3,000,000과 "원래 ₩2,100,000 / 현재 ₩1,800,000"이 함께 표기되어 있어 **확정 불가**. AutoCAD 2007–2024, 1라이선스.
- **기능**: TIN(Delaunay), 종단·횡단 도면 생성, 토공량(엑셀). 골프장 설계에 특화되어 있습니다.
- **고고학 적합성**: **하**.

---

## 4. 사진측량 / 포인트클라우드

### 4.1 CloudCompare
- **공식 URL**: https://www.cloudcompare.org/ , https://github.com/CloudCompare/CloudCompare
- **가격/라이선스**: 무료, GPL.
- **기능**: Tools > Segmentation > **Extract Sections**에서 폴리라인을 그리거나 가져와 **두께(thickness) 안의 점을 슬라이스**하고, 하부/상부/양쪽 **단면 프로파일 폴리라인**을 추출합니다. 경로를 따라 **일정 간격 직교 단면 자동 생성**과 **폴리라인을 따라 펼치기(unfold)**도 됩니다. 폴리라인은 **DXF**(여러 개를 한 파일로)로 저장할 수 있습니다. 섹션 추출 결과의 SHP 내보내기는 검색 요약 기준입니다. Cross Section 도구로 슬라이스 윤곽도 추출할 수 있습니다.
- **높이 과장/레벨 표기/PDF**: **미확인**(도면화 기능은 없다고 보는 게 안전합니다).
- **한국 좌표계**: 좌표계(CRS) 메타데이터 처리 기능은 이번 조사에서 확인하지 못했습니다(**미확인**). 큰 좌표는 Global Shift로 다룹니다.
- **고고학 적합성**: **상**(사진측량/스캔 토층벽 단면 추출용). 다만 도면 마감은 CAD/QGIS에서 해야 합니다.

### 4.2 Agisoft Metashape Professional
- **공식 URL**: https://www.agisoft.com/ (도움말: https://agisoft.freshdesk.com/support/solutions/articles/31000148884-dem-based-measurements )
- **가격**: Professional **$3,499**(노드락 영구), Standard $179(측정·DEM 기능은 Pro 전용).
- **기능**: DEM/모델 위 폴리라인(또는 폴리곤)의 **Measure → Profile 탭**. 프로파일을 **KML, SHP, DXF** 벡터나 래스터 이미지로 저장합니다. Shape Report(PDF/HTML)에 프로파일을 넣을 수 있습니다.
- **미확인**: 높이 과장, 수직 단면벽 모델의 단면, EPSG:5185–5188 목록.
- **고고학 적합성**: **중**. 정사영상·DEM 생성 후 단면 확인용이고, 도면은 다른 도구로 만들어야 합니다.

---

## 5. 기타 상용 GIS

### 5.1 Global Mapper (Blue Marble)
- **공식 URL**: https://www.bluemarblegeo.com/ (Path Profile: https://www.bluemarblegeo.com/knowledgebase/global-mapper/Path_Profile/PathProfile_settings.htm )
- **가격**: 구독 Pro 등급 **$2,199/년부터**. 과거 영구 Pro $1,750(노드락) 등(검색 요약 기준, 공식 페이지 직접 확인은 못함).
- **기능**: Path Profile에서 **수직 과장 지정**, 거리·고도 축척 일치. **File > Save Profile to Vector/PDF → DXF**(v15.1+). DXF를 CAD에서 다시 축척해야 한다는 포럼 지적이 있습니다.
- **미확인**: 점 레이어 버퍼 투영, EPSG:5185–5188.
- **고고학 적합성**: **중**.

---

## 6. 실무 추천 (2~3개)

1. **QGIS(3.40 LTR 또는 4.x) 내장 Elevation Profile + 레이아웃**: 기본 추천
   - 이유: 무료이고 한국 좌표계를 공식 지원합니다. CSV/SHP/DXF 측점을 그대로 쓰고, **tolerance 버퍼로 점을 단면에 투영**합니다. 벡터 PDF, 2D 단면 DXF, 인쇄 레이아웃 단면 그래프까지 됩니다. 4.0부터는 높이 과장 축척비를 지정할 수 있습니다.
   - 사용 팁: CSV 불러오기에서 X 필드=동(Y열), Y 필드=북(X열)으로 지정해 축 순서를 확인하세요. 레이어 Elevation 탭에서 Z 필드를 지정합니다. 단면선은 선 레이어(A–A′ 속성)로 관리하세요.
2. **QGIS Profile Tool**: 빠른 단면과 CAD 이관용 보조
   - 이유: 점 레이어 Search buffer 투영이 단순합니다. **DXF 2D(거리-표고)**로 바로 CAD에 넘겨 토층선·라벨을 마감할 수 있습니다. SVG/PDF 출력도 됩니다.
3. **CAD 마감 환경이 필요하면 DreamPlus(AutoCAD ₩66,000/년, 또는 BricsCAD ₩99,000/년)**, 사진측량 토층벽이면 **CloudCompare** 병행
   - 이유: 국내 측량 실무자에게 익숙한 CAD 리습 방식이고 매우 저렴합니다. GNSS 횡단 측량 → 3D 폴리선 → 횡단(PTCS), TIN → 횡단/종단(CFM/GRP)이 됩니다. CloudCompare는 무료로 포인트클라우드 단면 폴리라인을 DXF로 냅니다.
   - 고급 대안: 예산이 있고 독일식 고고학 도면 체계를 받아들일 수 있으면 **ArchaeoCAD**(가격 문의 필요)를 검토할 만합니다.

---

## 7. 기존 도구로 부족한 점: 별도 단면뷰어를 만든다면 채울 틈

| # | 틈 | 현재 상황 | 단면뷰어에서 할 일 |
|---|---|---|---|
| 1 | **고고학 단면 도면 관례** | QGIS/CAD 모두 수작업. ArchaeoCAD(유료·독일)만 단면 프레임·레벨 값 지원 | 수평 기준선 + "EL. 45.230 m" 표기, A–A′ 단면 기호, 방위(W→E), 토층 번호 원문자, 1:10/1:20/1:40 축척 프리셋, 표제란 |
| 2 | **점 코드 → 토층선/유구선 자동 연결** | survey2gis(평면)·ArchaeoCAD(독일 코드)만 있음 | 국내 현장 코드(예: 토층번호·유구번호) 기반으로 단면 위 점을 순서대로 이어 층 경계선·유구 윤곽 생성 |
| 3 | **한국 측량 관례 처리** | GIS는 X=동 가정이 기본이고, EPSG:5186 공식 축은 북·동. 바뀌면 단면이 엉뚱하게 나옴 | CSV 헤더·값 범위로 **X=북/Y=동 자동 판별과 경고**, EPSG:5185–5188 원점(서·중·동·동해) 자동 추정, GNSS 컨트롤러 출력 포맷 프리셋 |
| 4 | **투영 결과의 투명성** | QGIS/Profile Tool은 버퍼 안 점을 표시만 함 | 각 점의 **단면선 이격거리(offset)·좌/우 표시**, 버퍼 밖 근접점 경고, 단면선 방향 뒤집기 |
| 5 | **높이 과장 + 정확한 눈금** | QGIS 3.x는 1:1 잠금만, Profile Tool은 임의 줌, Civil 3D는 COGO 마커 과장 문제 | 수평/수직 축척을 독립 지정(예: H 1:100, V 1:20)하고 눈금·라벨은 실제 표고로 유지 |
| 6 | **구조화된 DXF 출력** | Profile Tool DXF는 폴리라인만, QGIS DXF는 지오메트리 위주 | 레이어 분리(지표선·토층선·측점·라벨·기준선·프레임), 한글 텍스트, 실제 도면 단위(mm) 축척 반영 DXF + 벡터 PDF/SVG |
| 7 | **단면 사진(정사영상) 겹치기** | profileAAR/TachyGIS(독일, QGIS 숙련 필요), CloudCompare 별도 | 단면 기준점 2~3개로 정사영상을 단면 좌표계(거리-표고)에 맞춰 측점과 겹쳐 트레이싱 |
| 8 | **다중 단면·교차 단면** | TachyGIS v2만 "미러링+조립" 지원 | 십자 단면(유구 장축/단축) 자동 배치, 여러 단면 일괄 출력 |
| 9 | **유구 폴리곤과 단면 교차** | qProf(지질용)만 지원 | 유구 경계 SHP/DXF와 단면선 교차 → 단면에 유구 범위 자동 표시 |
| 10 | **비GIS 실무자용 단순 UI·오프라인** | QGIS/CAD 학습 부담, 상용 고가 | 파일 열기 → 2점 단면선 → 버퍼 → 미리보기 → 출력의 4단계 UI, 설치형 오프라인 |

---

## 8. 출처 (주요)
- QGIS Elevation Profile 문서: https://docs.qgis.org/3.44/en/docs/user_manual/map_views/elevation_profile.html
- QGIS 레이아웃 단면 아이템(3.30): https://changelog.qgis.org/id/entry/2437 , https://docs.qgis.org/3.44/en/docs/user_manual/print_layout/layout_items/layout_elevation_profile.html
- QGIS 단면 내보내기 changelog: https://changelog.qgis.org/en/entry/2441
- QGIS 4.0 거리:고도 축척비: https://github.com/qgis/QGIS/pull/62794 / 4.0 출시: https://blog.qgis.org/2026/03/09/qgis-4-0-norrkoping-is-released/
- Profile Tool: https://plugins.qgis.org/plugins/profiletool/ , 소스 tools/dataReaderTool.py · ui/ptdockwidget.py · tools/plottingtool.py (GitHub PANOimagen/profiletool)
- qProf: https://plugins.qgis.org/plugins/qProf/ , help: https://github.com/mauroalberti/qProf/blob/master/help/help.html
- profileAAR: https://plugins.qgis.org/plugins/profileAAR/
- TachyGIS: https://tachygis.github.io/ , v2 플라이어 https://tachygis.github.io/TachyGIS_v2_Flyer.pdf
- survey2gis: https://www.survey-tools.org/
- Civil 3D 단면/과장: https://help.autodesk.com/cloudhelp/2026/ENG/Civil3D-UserGuide/files/GUID-25D796D5-F8C8-4E56-88D5-2F76C783ACFE.htm , COGO 투영 과장 이슈: https://forums.autodesk.com/t5/civil-3d-ideas/vertical-exaggeration-for-projected-cogopoints/idi-p/7588594 , EPSG:5186: https://forums.autodesk.com/t5/civil-3d-forum/i-cannot-assign-a-new-mapcslibrary-epsg-5186/td-p/11838346
- BricsCAD V26 단면: https://bricscad.octave.com/blog/turn-terrain-into-insight-civil-sections-made-simple-in-bricscad-v26-blog , 스토어: https://bricscad.octave.com/store/bricscad , CRS: https://help.bricsys.com/en-us/document/bricscad/drawing-accurately/coordinate-reference-system?version=V26
- ArchaeoCAD: https://www.arctron.de/products/archaeocad-details-en/
- TachyCAD Archaeology(Legacy): https://knowledge.faro.com/Software/Legacy-Software/Legacy-PointSense_and_CAD_Plugins/TachyCAD/Technical_Specification_Sheet_for_TachyCAD_Archaeology
- DreamPlus: https://dreamcad.net/bbs/board.php?bo_table=qna&page=286&wr_id=2324 , https://dreamcad.net/bbs/board.php?bo_table=product&wr_id=4 , 가격 https://www.dreamcad.net/theme/s007/index/sale_01.php
- CADian: https://www.cadian.com/en/product_view.php?idx=60
- SitePlan: https://siteplan.kr/products/cad , https://siteplan.kr/products/cad/features
- 이지소프트: https://eazysoft.co.kr/product/%ec%a2%85%ed%9a%a1%eb%8b%a8-%ed%94%84%eb%a1%9c%ea%b7%b8%eb%9e%a8/
- CloudCompare Extract Sections: https://www.cloudcompare.org/doc/wiki/index.php/Extract_Sections
- Metashape: https://agisoft.freshdesk.com/support/solutions/articles/31000148884-dem-based-measurements , 스토어 https://www.agisoft.com/buy/online-store
- Global Mapper Path Profile: https://www.bluemarblegeo.com/knowledgebase/global-mapper/Path_Profile/PathProfile_settings.htm
- EPSG:5186 축 순서: https://epsg.org/crs_5186/KGD2002-Central-Belt-2010.html
