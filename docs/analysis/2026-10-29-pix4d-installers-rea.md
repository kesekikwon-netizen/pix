# 분석 기록: PIX4D 설치 파일 2개 REA 뜯어보기

- 날짜: 2026-10-29
- 대상: `Pix4Dmapper-4.9.0.msi`, `PIX4Dmatic-Setup-2.10.4.exe`
- 방법: REA MCP + 읽기 전용 머리말 확인 (설치·실행 안 함)
- 관련 요청: `docs/intent/2026-10-29-desca-pix4d-3d-editor.md`

## 결론 세 줄
- 두 파일은 모두 설치 묶음이다. 안에 든 3D 처리 본체는 이번에 뜯지 않았다.
- `PIX4Dmatic-Setup-2.10.4.exe`는 WiX 묶음 겉껍질이며 서명은 정상이다. 속은 압축되어 겉으로 안 보인다.
- `Pix4Dmapper-4.9.0.msi`는 파일 1059개짜리 구형 MSI 묶음이다. 속 기술 갈래가 글자로 보인다.

## 파일 지문 (확인)
| 파일 | 크기 | SHA-256 | 전체 뒤섞임도 |
|---|---|---|---|
| `Pix4Dmapper-4.9.0.msi` | 442580992 | `909bfd4f1402683c5851fe6031bfc2de1e065a473934c29412ef24b6b21f59d4` | 7.9974 |
| `PIX4Dmatic-Setup-2.10.4.exe` | 3239583528 | `b7db329035ff68fe082db734c8a4cdec867f2265b1c700885a2b042476e42e8b` | 7.9997 |

- 뒤섞임도가 8에 가깝다. 압축된 설치 묶음이라는 뜻이다 (추측 아님: 압축과 암호는 겉보기로 구분 안 됨).
- 증거: `ev_bc938d148807c2488d3dfb173e694946387599b405b162fa0a13733e590b04eb`, `ev_437f29d4dd665741acaa696c697bcc1f61e0ca846715d6dde87a11e243d1dee0`, `ev_6ae248904aad2f38e1a3f7effe22286482a6cd544d03066fb78f006cbb6681ff`, `ev_31fe68db69c73033f69c86de3c6984333220418425e2dc0b390b3462ddde65ab`.

## 겉껍질 모양 (확인)
- MSI 첫 8바이트 `d0cf11e0a1b11ae1` → OLE 묶음(MSI) 맞다.
- EXE 첫 2바이트 `MZ`, PE 표시 `PE\0\0`, 기계 `0x8664`(64비트), 구간 8개, 시간표시 `1728078343`이다.
- EXE는 `PE32+`이며 구간 이름에 `.wixburn`이 있다 → WiX Burn으로 묶은 겉껍질이다.
- 구간: `.text` `.rdata` `.data` `.pdata` `.didat` `.wixburn` `.rsrc` `.reloc`.
- EXE 앞 4MB에서 `Pix4D`·`matic`·`MSI` 글자가 보인다. `NSIS`·`Inno`·`Electron`·`Qt5`·`Qt6` 글자는 안 보인다.

## 제품 정보 (확인)
- EXE 버전: `2.10.4`, 제품명 `PIX4Dmatic`, 회사 `Pix4D SA`, 설명 `PIX4Dmatic`.
- EXE 서명: `Valid`, 서명자 `CN=Pix4D SA`, 발급자 `DigiCert Trusted G4 Code Signing`.
- MSI 안: 속성표 16줄, 파일표 1059개, 표 39개 (읽기 전용으로 셈).
- REA 세션: EXE를 `pe`·`x86_64`로 여는 데 성공했고(`open_binary`), 겉껍질만 보고 세션을 닫았다. MSI는 형식 미지원으로 못 열었다.

## Mapper 속 기술 갈래 (MSI 글자에서 확인)
- 본체: `pix4dmapper.exe`.
- 화면: Qt5 (`qgif`, `qtgeoservices_*`), `d3d`(Direct3D), OSG 83번(`osg`, 3D 보기).
- 사진·지도: `exiv2`, `gdal303`(GDAL 3), `geolocation`.
- 3D 계산: `ceres`(묶음조정), `cudart64_110`(CUDA 11), `opencv`, PCL 43번, `mkl`·`tbb`.
- 바탕: `boost_*` 수십 개, `concrt140`·`msvcp`(MSVC), 자체 모듈(`calibration`, `camera`, `datamodel`, `dsm`, `dtm`, `features`, `foundation`, `bundle`).
- MSI 글자 186만 개 중 셈. 압축된 속은 안 보이고 파일표 글자만 보인다.

## Matic 속 (확인한 만큼)
- EXE 전체에서 `MSCF`(CAB) 18곳을 찾았으나 머리말 검사에서 전부 우연 겹침으로 판명. 진짜 상자 0개.
- `pix4d` 글자는 서명 증명서 2곳에만 있다. 앱 파일 이름은 압축 속에 숨어 안 보인다.
- 결론: Matic 속 파일 목록은 겉읽기로 못 뽑는다. 풀려면 묶음 풀기 실행이 필요한데 무겁고 위험해서 안 했다.

## 못 한 것 (정직하게)
- 전체 디컴파일은 못 했다. Ghidra 미설치, 3GB 입력 상한, `objdump` 없음 때문이다.
- MSI 속성값 글자는 COM 읽기 한계로 비어 보였다. 개수만 믿을 수 있다.
- 두 번 같은 방식으로 실패한 MSI 글자 읽기는 더 파지 않고 중단했다.
- Matic 묶음 풀기(실행)는 안 했다. 3GB를 푸는 것은 오래 걸리고 컴퓨터에 변화를 준다.

## 3D 처리 방식 (디컴파일로 확인)
뜯어낸 것: `work/decomp_meshio`(1919함수), `work/decomp_pcio`(485함수), `work/decomp_wavefront`(107함수).

### 메쉬 자료형 (공통)
- 정점: `vector<Eigen::Vector3f>`, 삼각형: `vector<pix4d::geom3d::TTriangle<int>>`, UV: `vector<Eigen::Vector2f>`, 겉그림(`texfile`) 경로.
- 중심 클래스: `pix4d::mesh::datamodel::Mesh` (정점수, 정점, 삼각형, UV, 겉그림 유무).
- 입출력: `pix4d::mesh::io::loadMesh` / `saveMesh` (+`LinearUnits` 단위) → `read` / `write` / `writeTiled`.

### 메쉬 파일 형식표 (확인)
| 끝맺음 | 내부번호 | 쓰는 도구 |
|---|---|---|
| `.p4b` | 0 | 자체 P4B (`MeshReaderP4B`·`MeshWriterP4B`) |
| `.obj` | 1 | 이 길에서는 안 됨 ("Unsupported") |
| `.ply` | 2 | 이 길에서는 안 됨 ("Unsupported") |
| `.fbx` | 3 | FBX SDK (`MeshReaderFBX`·`MeshWriterFBX`) |
| `.dae` | 4 | FBX SDK (읽기만, 쓰기는 거부) |
| `.dxf` | 5 | FBX SDK |
| `.osgb` | — | OSG (`osgDB`, `Mesh::save` 길) |
| `.slpk`·`.spk` | — | Esri 장면(`FUN_180019f00`, `Mesh::save` 길) |

### 점구름 형식표 (확인)
- `.xyz`→1, `.csv`→6, `.asc`, `.ply`→2, `.las`→3, `.laz`→4, `.p4b`→5.
- P4B 점구름은 칸칸 구조: 머리말, 옮김값(`offset`), 상자(`box`), 점, 법선, 색, 밀도, 분류(`classIds`), 무리(`groupIds`), UV, 삼각형, 겉그림파일.
- 흐르듯 읽기·쓰기(`ReadNextPoint`·`WritePoint`, PCL 길)도 있다.

### 타일·LOD 내보내기 (확인)
- `writeTiled`(타일 저장), `saveLODToPath`(계층`Hierarchy`+`LODExportParameters`+일꾼`Worker`, "Exporting LOD", 실패하면 "Error e0802c").
- 큰 3D를 잘게 나누는 길은 이미 이 구조다. `desca`에 옮길 때 참고한다.

### 여는 프로젝트 파일 (MSI에서 확인)
- Mapper는 `.p4d`, Matic은 `.p4m`·`.opf`를 연다.
- Matic은 Qt6, Mapper는 Qt5 바탕이다 (아래 기술 갈래 참고).

## 개발에 쓸 점
- `desca`에 PIX4D 결과 읽기를 붙일 때 위 형식표가 설계도다. `.las`·`.laz`·`.ply`·`.obj`·`.fbx`·`.osgb`·`.slpk` 순서로 붙이면 된다 (의견).
- P4B는 PIX4D 자체 형식이라 파일 1개가 있어야 구조를 확정할 수 있다. 시험 파일 1개가 다음 열쇠다.
- Mapper 기술 갈래(Qt5, OSG, GDAL, PCL, Ceres, CUDA)는 참고용으로만 둔다. 베껴 쓰지 않는다.
- Matic 기술 갈래(Qt6, CUDA 12, LibTorch, PDAL, GDAL, FBX SDK, FFmpeg 7)도 참고용으로만 둔다.
- 다음 순서: `desca` 가져오기 → PIX4D 실제 결과 파일 1개 구하기 → 그 파일 열기부터 만든다.

## 화면에서 확인할 것
1. 이 파일이 `docs/analysis/` 폴더에 있다.
2. 위 표의 크기와 SHA 앞 8자리가 탐색기·기록과 같다.
3. EXE 버전 `2.10.4`가 기록에 적혀 있다.
