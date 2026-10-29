# M0 성능 측정 기록 (2026-10-06, 실제 모델 jeju1 Production_2, 139 MB / 타일 265 / 잎 220)

환경: Linux 상자, Xvfb + **llvmpipe(소프트웨어 GL)** — 실제 GPU 수치 아님. 실제 PC 에서 다시 재야 함.

| 항목 | 값 |
|---|---|
| 열기(루트 머리) | 25–39 ms |
| 첫 화면 | 228–239 ms |
| 잎 전체 디코드(지오메트리) | 3,668,495 삼각형, ~575 ms (8 스레드, 텍스처 제외) |
| 카메라 240프레임(맞춤→확대→이동→축소) | 평균 21.9–32.8 ms, 최악 126–159 ms(이동 중 큰 텍스처 업로드 1건) — 실행마다 차이 |
| 단면 끌기 60프레임(~16 ms 간격) | 미리보기 56–58회(29–37/s), 화면 그리기 평균 4.1–4.6 ms·최악 18–32 ms |
| 놓은 뒤 최종(잎) 단면 | 363–628 ms (445k–524k 삼각형) |
| 잎 피킹 | 0.3–0.5 ms(asec-info, 디코드 후), 앱 14–53 ms(첫 접근 시 텍스처 디코드 포함) |

다시 재는 법:
```
xvfb-run -a ./build-linux/app/SectionViewer <파일.3mx> --perf-log perf.csv --log perf.log --quit   # 또는 실제 PC 에서 xvfb 없이
./build-linux/asec-info <파일.3mx> --tree --decode --pick X Y --section AX AY BX BY --csv info.csv
```
Windows: `SectionViewer.exe <파일.3mx> --perf-log perf.csv --log perf.log --quit` (실제 GPU 로 측정)

발견·수정: 1.0 의 단면 틈 잇기(joinGaps)가 O(n³) 라 실제 모델 최종 단면(조각 수천 개)이 수 분 멈춤 → 3aa68cf 에서 수정.
남은 것: GPU 업로드를 프레임당 예산으로 나누기(이동 중 159 ms 프레임), 실제 GPU 프레임 예산 조정.

Windows 1.1.0 exe 를 wine(소프트웨어 GL)에서 같은 모델로 실행: 열기·좌표계·피킹 Z=56.8852·단면·DXF·GeoTIFF 동일 결과, asec_tests.exe 89건 통과.

## 1.1.1 입면 5 m · 휠 확대 (2026-10-06, 같은 모델, 단면 38 m y=98129.5, 상자 부하 큼 — load 30–90)
| 항목 | 값 |
|---|---|
| 최종 단면 뒤 5 m 차가움(43타일, 786,955 삼각형) | 병렬 전 704 ms(영상 295 ms) → 461–531 ms(영상 99–117 ms) |
| 최종 단면 뒤 5 m 따뜻 | 병렬 전 936 ms(영상 438 ms) → 102–142 ms |
| 미리보기 뒤 5 m | 차가움 ~66 ms, 따뜻 ~6 ms |
| 앱 끌기 60프레임(뒤 5 m) | 미리보기 58회(32.5/s), 그리기 평균 4.5 ms·최악 22 ms, 놓은 뒤 최종 489 ms(부하 높을 때 1.9 s) |
| 부하 낮을 때(load ~2) 다시 잼 | 최종 뒤 5 m 차가움 346 ms(영상 101) · 따뜻 97 ms, 뒤 0.5 m 254 / 31 ms, 미리보기 69 / 7 ms; 앱 끌기 37.7/s, 놓은 뒤 최종 722 ms |
| 휠 확대 6칸(평면) | 애니메이션 ~720 ms, 커서 고정 오차 0.000 px, 최악 프레임 16–43 ms(새 LOD 텍스처 업로드 1건, llvmpipe), 끝난 뒤 LOD 깊이 2·대기 없음 |
| 휠 확대 8칸 / 축소 14칸(단면) | ~550 / ~950 ms, 오차 0.000 px |

다시 재는 법: `SectionViewer <3mx> --line … --back 5 --wheel-test --perf-log perf.csv --log run.log --quit`,
`asec-info <3mx> --section AX AY BX BY --back 5`.

Windows 1.1.1 휴대용 zip 을 wine 에서: 열기·피킹 Z=56.7652(잎)·단면·뒤 5 m 입면·휠 시험(오차 0 px)·PNG/DXF(ezdxf 감사 오류 0) 정상, asec_tests.exe 92건(91 통과·1 건너뜀).

## 1.2.0 평면 텍스처 업로드 프레임 예산 (2026-10-06, 같은 모델, Xvfb + llvmpipe, load 2–4)
업로드를 프레임마다 예산(3 MB · 5 ms)으로 나누고, 밉맵은 작업 스레드에서 만들고, 밉 거르기를 `glTexImage2D` 전에 정해
Mesa 의 단계별 재할당을 없애고, 내보낸 같은 크기 텍스처를 다시 씀(풀 96 MB). 새 텍스처 할당이 생기면 그 프레임 예산은 끝.
| 항목 | 1.1.1(옛 방식) | 1.2.0 |
|---|---|---|
| `--wheel-test` 평면 확대 6칸 최악 프레임(애니+정착) | 38.5 ms | **9.7 / 14.5 / 15.0 ms**(3회), 축소 10.1–14.6 ms, 커서 고정 오차 0 px |
| `--perf-log` 평면 경로 240프레임 paintGL(업로드 포함) 최악 | 222 ms(벽시계) | **19.1 / 22.3 / 26.8 ms**, p99 18–24 ms |
| 같은 경로 벽시계 프레임 최악 | 222 ms | 60.2 / 64.8 / 69.7 ms — 그 프레임들의 paintGL 은 2–6 ms, 나머지는 paintGL 밖(Xvfb·llvmpipe 창 합성, 중앙값 15–18 ms) |
| 한 프레임 업로드 최대 | — | 15–23 ms(새 텍스처 할당 1건, llvmpipe 0 채움) |
| 단면선 끌기 60프레임 | — | 미리보기 56–58회(26–35/s), 그리기 평균 4.5–4.9 ms·최악 14.5–17.8 ms, 놓은 뒤 최종 516–532 ms |

실제 GPU(Windows)에서는 할당·합성이 훨씬 싸므로 더 낮을 것으로 보지만 **실제 GPU 측정은 아직 안 함**.
다시 재는 법: `SectionViewer <3mx> --settings /tmp/s --line … --back 5 --wheel-test --perf-log perf.csv --log run.log --quit`
(CSV 열 `paint_ms` = paintGL, `upload_kb/upload_ms/staging` = 그 프레임 업로드).
