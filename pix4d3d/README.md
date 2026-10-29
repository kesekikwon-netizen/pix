# 발굴 단면뷰어 (Excavation Section Viewer)

3MX 실사 메시(iTwin Capture / ContextCapture)를 열어 평면에서 단면선을 긋고, 10 cm 레벨선이 있는 단면도를
바로 보고 DXF · PNG/TIFF/GeoTIFF · 점군(XYZ/LAS)으로 내보내는 Windows 데스크톱 프로그램.

- 언어/도구: C++17, Qt 6.8 Widgets + OpenGL, OpenCTM(C). Linux 에서 MinGW-w64 로 Windows x64 교차 빌드.
- 앱은 moc 없이(Q_OBJECT 미사용) 작성 → 교차 빌드에 Qt 호스트 도구 불필요.
- `core/` (`archsection` 정적 라이브러리)는 Qt 무관 순수 C++ — 3MX 로더, 단면 절단·이어붙이기·정리, 레벨선,
  입면 영상 래스터라이저, DXF/TIFF/GeoTIFF/LAS 작성. MicroStation/Descartes MDL 플러그인에서 재사용 가능.

## 구조
```
core/include/asec, core/src   Qt 무관 코어 (tmx, section, lod, stream, schedule, pick, srs, raster, dxf, export, tiff, pointcloud, obj, engine)
app/                          Qt 앱 (mainwindow[작업·내보내기], mainwindow_ui[화면 구성·시작 화면·단면 목록·되돌리기],
                              sheetexport[도면 창·PDF], planview[OpenGL], sectionview[QPainter], theme[Strata QSS·아이콘])
tools/                        asec-section(명령줄 단면), asec-info(3MX 점검·성능), asec-make-synthetic(합성 3MX), synth
tests/                        Catch2 단위 시험 + dxf_audit.py(ezdxf)
packaging/                    아이콘·NSIS·라이선스·리소스
third_party/                  OpenCTM, stb, nlohmann/json, Catch2
```

## 빌드
- Linux 개발: `./build-linux.sh` (Qt6 dev, cmake, ninja, zlib)
- Windows: `./build-win.sh` → `dist/SectionViewer-<버전>-Setup.exe`, `dist/SectionViewer-<버전>-portable-win64.zip`, `dist/SHA256SUMS.txt`(이전 버전 줄은 유지)
  (필요: g++-mingw-w64-x86-64-posix, libz-mingw-w64-dev, nsis, zip, Qt 6.8.3 win64_mingw — `QT_WIN` 환경 변수로 경로 지정)

## 명령줄 자동화(시험·캡처용)
`SectionViewer <파일.3mx> --line AX AY BX BY [--local] [--front m] [--back m] [--scale N] [--dpi N]
 [--export-png|tiff|geotiff|dxf|plan|xyz|las|csv 파일] [--area whole|band|view] [--spacing m] [--shot 파일.png] [--log 파일]
 [--pick X Y]... [--hover] [--perf-log 파일.csv] [--wheel-test] [--depth-fade on|off] [--quit]`
- `--front/--back m`: 두께 띠(0–5 m, 뒤 기본 3 m). 입면 영상은 뒤 깊이까지 그림. 단면선(잘린 면)은 미리보기 때도 언제나 최고 해상도 잎으로 자름. `--depth-fade on|off`: 깊이 음영
- `--wheel-test`: 평면·단면에 실제 휠 이벤트(화면 비중심 지점)를 보내 애니메이션이 끝난 뒤 커서 아래 지점 이동(px),
  애니메이션 시간, 프레임 최악 시간, LOD 깊이·스트리밍 대기 여부와 레벨선 간격(화면/1:20·1:40·1:100 내보내기)을 로그
- `--pick X Y`: 실좌표(또는 `--local`)에서 잎 메시 연직 정밀 피킹 → 로그에 Z·출처·시간
- `--hover`: 평면 보기 가운데 마우스 이동 흉내 → 좌표줄 Z 가 대략(화면 LOD) → 잎 표면으로 바뀌는지 로그
- `--perf-log`: 카메라 경로(맞춤→확대→이동→축소, 240프레임) + 단면선 끌기(60프레임 ~16 ms) 재생, 프레임마다
  시간·그린 노드·업로드·대기·GPU MB·최대 깊이를 CSV 로, 요약(미리보기 수, 놓은 뒤 최종까지 ms)은 로그로

`asec-info <파일.3mx> [--tree] [--decode] [--pick X Y]... [--section AX AY BX BY] [--front m] [--back m] [--notex] [--local] [--csv 파일]`
— 열기 시간, 좌표계 판별(수평 EPSG/높이 기준/경고), 원점·위경도·float32 정밀도, 트리(타일·노드·잎·깊이), 잎 전체
디코드(삼각형 수, 실제 Z 범위), 피킹 시간, 단면 미리보기/최종 차가움·따뜻 시간(입면 영상 포함, 텍스처 디코드 기본 켬 —
`--notex` 로 끔). GUI 없이 성능 비교용.

- 1.2 시험용: `--settings DIR`(설정을 DIR 의 INI 로 — 사용자 설정 안 건드림), `--export-pdf|sheet-png|sheet-tiff|sheet-dxf 파일`
  `[--paper A4L|A4P|A3L|A3P] [--sheet-scale N] [--split]`(도면 용지 내보내기), `--export-dialog-shot 파일`(도면 창 캡처),
  `--undo-test`(두께·반전·레벨선 바꾼 뒤 되돌리기×3 / 다시×3 이 원래 상태와 같은지), `--ctx-shot 파일`(그리는 중 지금 도구 줄),
  `--lod-shot 파일`(평면 확대 직후 디테일 카드), `--extra-line AX AY BX BY`(단면 목록에 추가, 반복), `--section-scale N`(단면 화면 1:N),
  `--start-shot 파일`(파일 없이 시작 화면)
- 1.2.1 시험용: `--vex N`(단면 화면 세로 과장 1·2·5·10 — 화면만), `--plan-cam X Y mpp`(캡처 전 평면 카메라를 실좌표·m/px 로 — 평면-단면 정합 확인).
  `--cut-check`(단면선 정확도: 1 cm 간격 단면선 윗면 ↔ 잎 연직 피킹 차이, 미리보기 ↔ 최종 차이, 화면·내보내기에서 단면선 꼭짓점이 순수 빨강으로 맨 위에 그려졌는지 기록).
  환경 변수 `SECTIONVIEWER_COARSE_PREVIEW_CUT=1`: 미리보기 단면선을 옛 방식(거친 LOD)으로 — 성능 비교용만.
  로그에 `window-title=… appended=0|1`(창 제목에 표시 이름이 덧붙는지), `vex-suggest: relief=… suggest=x…`.
  Windows 패키지: `./build-win.sh --no-package`(빌드·배포 폴더만), zip 은 `packaging/mkzip.py`(UTF-8 이름 플래그).

## 1.2.1 (2026-10-06)
뒤 깊이(입면 배경) 기본 3 m(0–5 m 조절, 칩·숫자키 0.5/1/2/3/5, 옛 0.5 기본 이관) · 잘린 면은 미리보기도 최고 해상도 잎 · 단면선 맨 위 ·
단면 화면 세로 과장(X, 화면만) · 창 제목 중복 수정 · 휴대용 zip 한글 이름 UTF-8 플래그 · `--front` 만 줄 때 뒤 깊이 유지. 전체 목록은 `CHANGELOG.md`.

## 1.2 화면(Strata) · 사용자 중심 개선 P0 + 단면 목록
- 디자인: `research/ui-ref/UI-SPEC.md` §4 토큰(ground #FAF9F5, ink #141413, 주 단추 흙색 #B5573A 은 「도면」 하나), 본문 13 px 고딕,
  리본 탭 파일·홈·보기·측정·내보내기(옛 분석→측정, 추출→내보내기), 44 px 타일. 영어 병기는 기본 끔(보기 › English). 고대비(보기).
- 높이 배지: 단면 머리 + 상태줄. ok「높이 EGM96 EPSG:5773 · 지정함」/ warn「▲ 높이 타원체고 표기 · 확인」「▲ 높이 기준 모름」/ error「● 좌표계 없음」.
  누르면 높이 기준 지정. 좌표계 경고는 리본 아래 한 줄 알림(「높이 기준 지정…」「자세히」 ×).
- 핵심 흐름: 최근 파일(파일 › 최근, Ctrl+Shift+O, 시작 화면 「이어서 열기」 — 모델별 마지막 단면선·두께·화면·단면 목록 복원),
  뒤 깊이 칩 0.5/1/2/3/5 m = 숫자키 1–5(1.2.1~ 기본 3 m — 입면도용 배경, 1.2.0 은 0.5 m), 평행 이동 [ ] 0.1 m · { } 1 m, 그리는 중 지금 도구 줄(길이·방위·Shift/Enter/Esc).
- LOD: 평면 왼쪽 아래 「디테일 불러오는 중 n / N」 카드, 단면 정보 띠 「미리보기(거친) → 최종 계산 중…」/「✓ 최종(잎)」, 상태줄 Z 출처.
- 평면 단면선: 흰 3 px 테두리 위 빨강(#FF0000) — 어떤 영상 위에서도 보임. A/A′ 흰 칩.
- 되돌리기 Ctrl+Z / 다시 Ctrl+Y(Ctrl+Shift+Z): 단면선·두께(1.5 초 안 연속 변경은 하나로)·표시 켜기/끄기·높이 기준·현재 단면 (QUndoStack, 200단계).
- 도면(Ctrl+P, `app/sheetexport.cpp`, 코어 `asec/sheet.hpp`): A4/A3 가로/세로, 1:10/20/40/100/맞춤, 실제 비율 미리보기, 넘침 경고와 추천
  (같은 용지에 들어가는 표준 축척 · A3 · 방향 · N장 나눠 붙이기), 표제란(도면명·축척·용지·보는 방향·날짜·수평 EPSG·높이 기준), 축척 막대,
  기준선 EL, 넣을 것(단면선·영상·레벨선·표제란), PDF(QPdfWriter, 여러 쪽)/DXF(모델 공간 1:N)/PNG/TIFF(나누면 _1, _2 …).
  인쇄 단추는 없음(QtPrintSupport 미사용 — PDF 로 인쇄).
- 시작 화면: 이어서 하기 카드, 최근 모델 표(좌표계·높이·단면 수·마지막으로 연 때, 원본 없으면 ●), 끌어 놓기, 세 걸음·자주 쓰는 키.
- 단면 목록(P1-1, 왼쪽 232 px): A–A′, B–B′ … 이름·메모·썸네일(메모리에만), 누르면 그 단면, 두 번 = 이름, N = 새 단면.
  QSettings `model/<경로 해시>/`(모델 폴더가 읽기 전용이어도 됨), 파일 › 단면 목록 내보내기/가져오기 = `.sections.json`.
- 성능: 평면 타일 텍스처 업로드를 프레임당 예산(기본 3 MB · 5 ms, `view/uploadBudgetKB`, 환경 `SECTIONVIEWER_UPLOAD_KB`, 0 = 옛 방식)으로
  나눔, 밉맵은 작업 스레드에서, 같은 크기 텍스처 재사용 풀(96 MB). `plan/PERF-M0.md` 1.2 절.

## 입면·레벨선·휠 (1.1.1)
- 입면 깊이 0–5 m(「5 m」 단추 + ▾ 0.5/1/2/3/5 m). 그리기 순서 레벨선 → 입면 영상 → 단면선. 끄는 동안은 화면 LOD
  미리보기, 놓으면 잎 메시 최종(백그라운드). 래스터라이저(`asec/raster.hpp` `renderElevation`)는 행 띠 병렬
  (최대 8스레드, 2만 삼각형 미만은 1스레드), 메시 상자·깊이 띠 밖 삼각형 미리 버림. 깊이 음영(`depthFade`, 기본 0.55):
  깊이 d 에서 흰색 쪽으로 fade·min(1, d / max(뒤깊이, 2 m)). 화면·PNG/TIFF/GeoTIFF·DXF 영상 모두 같은 깊이·음영.
- 레벨선 간격(`asec::planLevels`): 기본 선 10 cm · 숫자 50 cm. 선 간격이 화면 4 px(인쇄 0.5 mm) 미만이면 선 50 cm → 1 m …,
  숫자 높이×1.35 미만이면 숫자 1 m → 5 m …. 10 cm 보다 촘촘하게는 안 그림. DXF 는 축척과 무관하게 10/50 cm·1 m 레이어 모두 기록.
- 휠 확대/축소: 평면·단면 모두 커서 아래 지점 고정, 16 ms 간격 애니메이션(남은 배율의 40 %씩, 설정 `view/smoothZoom`
  기본 켬), 터치패드 pixelDelta 지원. 평면은 애니메이션 중에도 매 프레임 LOD 요청이 갱신됨.

## 시험
`./build-linux/asec_tests` (Catch2). 실제 3MX 시험은 선택:
`ASEC_REAL_3MX=/경로/Production_2.3mx [ASEC_REAL_EXPECT_LABEL="수평 EPSG:5186 / 높이 타원체고(GRS80)"] ./build-linux/asec_tests "[real]"`
(변수가 없으면 건너뜀. 실제 데이터는 저장소에 넣지 않는다.)

## 좌표·높이
실좌표 = 로컬 + SRSOrigin (3MX 는 x=동 E, y=북 N). 높이는 모델 SRS 값 그대로 — 지오이드 변환 없음.
- SRS 해석(`asec/srs.hpp`): `EPSG:n`, `EPSG:h+v`, `ENU:lat,lon`, WKT1(PROJCS/COMPD_CS), WKT2(PROJCRS/COMPOUNDCRS/
  BOUNDCRS/VERTCRS). 수평 EPSG 는 ID/AUTHORITY → REMARK("Promoted to 3D from EPSG:5186") → 이름 표 → TM 매개변수 순.
  높이 기준은 VERTCRS 또는 'ellipsoidal height' 축으로 판별. 화면에는 항상 `수평 EPSG:5186 / 높이 타원체고(GRS80)` 형태로
  표시하고, SRS 없음·모름·metadata.xml 불일치·원점 없음·축 순서 의심·float32 정밀도(로컬 > 8192 m)는 노란 경고.
- 높이 기준 식별(`asec/vdatum.hpp`): EGM96(EPSG:5773), EGM2008(3855), KVD1964 인천만 평균해수면(5193), KNGeoid, 타원체고 —
  복합 EPSG 번호, `EPSG:5186+EGM96` 같은 글자 표기, WKT VDATUM·GEOIDMODEL·PROJ4_GRIDS·PARAMETERFILE 에서.
- 「높이 기준 지정」(리본 홈 › 좌표계): Z 값은 그대로 두고 높이 기준 이름표만 지정(EGM96/EGM2008/KVD1964/KNGeoid/타원체고).
  화면·DXF 제목·GeoTIFF 수직 GeoKey 4096·LAS·JSON(`vertical_declared_by_user`, `vertical_srs_original`)에 반영.
  모델별로 QSettings(`heightDatum/<경로 해시>`)에 저장. iTwin 이 2D EPSG 를 '3D 로 승격'해 '타원체고'로 표기한 모델은
  노란 안내(측량값을 그대로 넣었다면 Z 는 측량 높이 기준, 예: EGM96)를 띄우고, 지정한 적 있으면 마지막 지정값(`height/lastDeclared`)을 기본으로 적용.
  자동화: `--height-datum srs|ellipsoidal|egm96|egm2008|kvd1964|kngeoid`(저장 안 함).
- 지오이드 변환 틀(GeoidModel/GridGeoidModel(GTX)/GeoidRegistry/convertHeight)은 있으나 UI·자동 변환 없음, 격자 미동봉
  (후보: EGM96 15′ 격자 — NGA 공개, 재배포 조건 미검증).
- 복합 EPSG 는 수평/수직을 나눠 GeoTIFF GeoKey 3072/4096, LAS GeoKeyDirectory VLR 에 기록.
- 커서/측정 Z(`asec/pick.hpp`): 최고 해상도 잎 메시에서 CPU double 광선 교차(로컬 + 원점). 화면 LOD 값은 '대략'으로
  먼저 보이고 잎 결과가 오면 바뀜. 좌표줄 옆에 Z 출처 표시.

## 성능 구조(M0)
- 타일 캐시: 타일 단위 잠금, 잎 병렬 디코드(최대 8 스레드).
- 평면 보기: 시점·확대 기반 LOD 스트리밍(LodStreamer, 비동기 로드 + GPU 예산 `view/gpuBudgetMB` 기본 768 MB).
- 단면: 끄는 동안 최신 요청만 남기는 CoalescingWorker — 미리보기는 화면 해상도에 맞는 거친 LOD, 놓으면 잎 메시로 최종.
- 병합 3MX: 같은 SRS·원점의 meshPyramid 레이어 전부 사용(다르면 빼고 경고).
