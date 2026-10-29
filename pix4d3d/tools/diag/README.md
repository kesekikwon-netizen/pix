# 진단 도구(빌드 대상 아님)
1.2.0 제주 신고(「단면선이 지나가는 곳이 정확히 잘리지 않는다」) 조사에 쓴 것. 직접 컴파일:

    g++ -O2 -std=c++17 -Icore/include -Ithird_party tools/diag/diag_plan_height.cpp \
        build-linux/libarchsection.a build-linux/libasec_stb.a build-linux/libopenctm.a -lpthread -o /tmp/diag_plan
    /tmp/diag_plan scene.3mx CX CY HALF RES out [LODRES]     # 잎 LOD 정사영상 + 최고 Z 래스터(out_ortho.png, out_height.png/.f32)
    /tmp/diag_cmp scene.3mx AX AY BX BY BACK [MESHRES]       # computeSection 단면(sec.csv) vs 잎 연직 피킹 1 cm(pick.csv)

좌표는 실좌표(EPSG). 결과 이미지는 dist/screens/40–46 참고.
