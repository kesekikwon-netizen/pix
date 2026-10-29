cmake_minimum_required(VERSION 3.16)
project(ExcavSection VERSION 1.2.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/utf-8)  # PIX4D 개조: 한글 주석 소스(UTF-8)를 MSVC가 제대로 읽게
endif()

option(ASEC_BUILD_TESTS "Build unit tests" ON)
option(ASEC_BUILD_APP "Build Qt desktop app" ON)
option(ASEC_BUILD_TOOLS "Build CLI tools" ON)
if(WIN32 AND NOT MSVC)
  add_link_options(-static)  # MinGW 런타임(libstdc++/libgcc/winpthread) 정적 연결 → 추가 DLL 불필요
endif()
set(ASEC_QT_WIN_DIR "" CACHE PATH "Qt for MinGW (Windows cross build, manual linking, no moc)")

# ---- OpenCTM (C, zlib 라이선스) ----
file(GLOB CTM_SRC third_party/openctm/*.c third_party/openctm/liblzma/*.c)
add_library(openctm STATIC ${CTM_SRC})
target_include_directories(openctm PUBLIC third_party/openctm PRIVATE third_party/openctm/liblzma)
target_compile_definitions(openctm PUBLIC OPENCTM_STATIC PRIVATE OPENCTM_BUILD LZMA_PREFIX_CTM)
set_property(TARGET openctm PROPERTY POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(openctm PRIVATE -O2 -w)
endif()

# ---- archsection 코어 (Qt 무관, MDL 재사용 대상) ----
add_library(archsection STATIC
  core/src/tmx.cpp core/src/srs.cpp core/src/vdatum.cpp core/src/section.cpp core/src/lod.cpp core/src/stream.cpp core/src/schedule.cpp core/src/pick.cpp core/src/raster.cpp
  core/src/dxf.cpp core/src/export.cpp core/src/tiff.cpp core/src/pointcloud.cpp core/src/las.cpp core/src/obj.cpp core/src/engine.cpp core/src/sheet.cpp)
target_include_directories(archsection PUBLIC core/include PRIVATE third_party)
find_package(ZLIB REQUIRED)
target_link_libraries(archsection PUBLIC openctm ZLIB::ZLIB)
if(NOT MSVC)
  target_compile_options(archsection PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
endif()
if(WIN32)
  target_compile_options(archsection PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

# ---- 합성 데이터 생성(시험·도구) ----
add_library(asec_stb STATIC tools/stb_impl.c)
target_include_directories(asec_stb PUBLIC third_party)
if(NOT MSVC)
  target_compile_options(asec_stb PRIVATE -O2 -w)
endif()
add_library(asec_synth STATIC tools/synth.cpp)
target_include_directories(asec_synth PUBLIC tools third_party)
target_link_libraries(asec_synth PUBLIC archsection asec_stb)

if(ASEC_BUILD_TOOLS)
  add_executable(asec-make-synthetic tools/make_synthetic.cpp)
  target_link_libraries(asec-make-synthetic asec_synth)
  add_executable(asec-section tools/section_cli.cpp)
  target_link_libraries(asec-section asec_synth)
  add_executable(asec-info tools/info.cpp)
  target_link_libraries(asec-info archsection asec_stb)
endif()

if(ASEC_BUILD_TESTS)
  enable_testing()
  add_executable(asec_tests tests/test_section.cpp tests/test_stitch.cpp tests/test_tmx.cpp tests/test_dxf.cpp tests/test_raster.cpp
                 tests/test_pipeline.cpp tests/test_tiff.cpp tests/test_points.cpp tests/test_cache_parallel.cpp tests/test_stream.cpp tests/test_schedule.cpp tests/test_srs.cpp tests/test_vdatum.cpp tests/test_layers.cpp tests/test_pick.cpp tests/test_joingaps.cpp tests/test_real.cpp tests/test_sheet.cpp tests/test_cutplace.cpp tests/test_backdepth.cpp tests/test_las.cpp third_party/catch_amalgamated.cpp)
  target_include_directories(asec_tests PRIVATE third_party)
  target_link_libraries(asec_tests asec_synth)
  add_test(NAME core COMMAND asec_tests)
endif()

if(ASEC_BUILD_APP)
  add_subdirectory(app)
endif()
