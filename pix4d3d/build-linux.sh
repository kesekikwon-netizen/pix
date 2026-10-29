#!/bin/bash
# Linux 개발 빌드 + 단위 시험 + 합성 3MX
set -euo pipefail
cd "$(dirname "$0")"
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DASEC_BUILD_APP=ON >/dev/null
ninja -C build-linux
./build-linux/asec_tests
./build-linux/asec-make-synthetic /tmp/asec-synthetic
