#!/bin/bash
# Windows x64 교차 빌드 → 배포 폴더 → 휴대용 zip + NSIS 설치 파일 (모두 이 저장소 안: dist/)
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$PWD
QT=${QT_WIN:-$HOME/qt/6.8.3/mingw_64}
VER=1.2.1
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release \
      -DASEC_QT_WIN_DIR="$QT" -DASEC_BUILD_TESTS=ON -DASEC_BUILD_TOOLS=ON >/dev/null
ninja -C build-win
D=build-win/deploy/SectionViewer
rm -rf build-win/deploy && mkdir -p "$D/platforms" "$D/imageformats" "$D/styles" "$D/licenses" "$D/tools"
x86_64-w64-mingw32-strip -o "$D/SectionViewer.exe" build-win/app/SectionViewer.exe
for m in Core Gui Widgets OpenGL OpenGLWidgets; do cp "$QT/bin/Qt6$m.dll" "$D/"; done
cp "$QT/bin/libstdc++-6.dll" "$QT/bin/libgcc_s_seh-1.dll" "$QT/bin/libwinpthread-1.dll" "$QT/bin/opengl32sw.dll" "$D/"
cp "$QT/plugins/platforms/qwindows.dll" "$D/platforms/"
cp "$QT/plugins/imageformats/qico.dll" "$D/imageformats/"
cp "$QT/plugins/styles/qmodernwindowsstyle.dll" "$D/styles/"
x86_64-w64-mingw32-strip -o "$D/tools/asec-section.exe" build-win/asec-section.exe
cp packaging/assets/app.ico "$D/"
cp packaging/licenses/* "$D/licenses/"
cp packaging/읽어보기.txt "$D/"
# --no-package: 빌드·배포 폴더까지만(zip·설치 파일·SHA256SUMS 는 만들지 않음)
if [ "${1:-}" = "--no-package" ]; then echo "deploy: $ROOT/$D"; exit 0; fi
mkdir -p dist
rm -f "dist/SectionViewer-$VER-portable-win64.zip" "dist/SectionViewer-$VER-Setup.exe"
# 한글 파일 이름(읽어보기.txt)이 Windows 탐색기에서 깨지지 않게 UTF-8 플래그(0x800)를 켜는 zip(Info-ZIP 은 안 켬)
python3 packaging/mkzip.py build-win/deploy SectionViewer "$ROOT/dist/SectionViewer-$VER-portable-win64.zip"
(cd packaging && makensis -V2 -DSRCDIR="$ROOT/$D" -DOUTFILE="$ROOT/dist/SectionViewer-$VER-Setup.exe" installer.nsi)
# SHA256SUMS.txt: 이전 버전 줄은 유지하고 이번 버전 줄만 바꿈
(cd dist && { if [ -f SHA256SUMS.txt ]; then grep -v -e "SectionViewer-$VER-Setup.exe" -e "SectionViewer-$VER-portable-win64.zip" SHA256SUMS.txt || true; fi
  sha256sum "SectionViewer-$VER-Setup.exe" "SectionViewer-$VER-portable-win64.zip"; } > SHA256SUMS.new && mv SHA256SUMS.new SHA256SUMS.txt && cat SHA256SUMS.txt)
