cmake_minimum_required(VERSION 3.16)

# 测试与性能基准独立工程：不随外层工程一起构建。
#
#   cmake -S tests -B build-tests
#   cmake --build build-tests -j
#   ctest --test-dir build-tests --output-on-failure
#
# 复用外层工程的源码与依赖（关闭 demo，只构建 ui 静态库）。
project(OpenGL_UITemplate_Tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY  ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY  ${CMAKE_BINARY_DIR}/lib)

enable_testing()

# 复用外层工程：只构建 ui 库，不构建 demo。
set(UI_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_BINARY_DIR}/project")

# ---- 回归测试 ----
add_executable(ui_offscreen_regression review_regression.cpp)
target_link_libraries(ui_offscreen_regression PRIVATE ui)
add_test(NAME ui_offscreen_regression COMMAND ui_offscreen_regression)

add_executable(ui_demo_acceptance demo_acceptance.cpp)
target_link_libraries(ui_demo_acceptance PRIVATE ui)
add_test(NAME ui_demo_acceptance COMMAND ui_demo_acceptance)

if(UNIX)
    add_executable(ui_window_regression window_regression.cpp)
    target_link_libraries(ui_window_regression PRIVATE ui ${CMAKE_DL_LIBS})
endif()

# ---- 性能基准 ----
add_executable(ui_frame_bench frame_bench.cpp)
target_link_libraries(ui_frame_bench PRIVATE ui)

# 改动前基线探针（只用公开 API，也可对旧版本手工编译，见 tests/README.md）
add_executable(ui_baseline_probe baseline_probe.cpp)
target_link_libraries(ui_baseline_probe PRIVATE ui)
