cmake_minimum_required(VERSION 3.21)
project(pix4d_edit VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(pix4d_edit src/main.cpp)

enable_testing()
add_test(NAME pix4d_edit_runs COMMAND pix4d_edit)
