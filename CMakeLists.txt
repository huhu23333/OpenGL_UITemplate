cmake_minimum_required(VERSION 3.16)

project(OpenGL_UITemplate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 输出目录（相对构建目录，不依赖绝对路径）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY  ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ===== 依赖（全部通过 pkg-config / find_package 从系统路径解析，无绝对路径）=====
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)
pkg_check_modules(GLES REQUIRED glesv2)
pkg_check_modules(EGL  REQUIRED egl)
pkg_check_modules(FREETYPE REQUIRED freetype2)
pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
find_package(OpenCV REQUIRED)

# ===== UI 核心库（静态库）=====
add_library(ui STATIC
    src/RenderContext.cpp
    src/Texture.cpp
    src/Renderer2D.cpp
    src/Component.cpp
    src/Widgets.cpp
    src/CanvasGroup.cpp
    src/UIApplication.cpp
    src/TextEngine.cpp
    src/common/FrameRateCounter.cpp
)

target_include_directories(ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GLFW_INCLUDE_DIRS}
    ${GLES_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
    ${HARFBUZZ_INCLUDE_DIRS}
    ${FONTCONFIG_INCLUDE_DIRS}
)
target_compile_options(ui PRIVATE
    ${GLFW_CFLAGS_OTHER}
    ${GLES_CFLAGS_OTHER}
    ${EGL_CFLAGS_OTHER}
    ${FREETYPE_CFLAGS_OTHER}
    ${HARFBUZZ_CFLAGS_OTHER}
    ${FONTCONFIG_CFLAGS_OTHER}
)
target_link_libraries(ui PUBLIC
    ${GLFW_LIBRARIES}
    ${GLES_LIBRARIES}
    ${EGL_LIBRARIES}
    ${FREETYPE_LIBRARIES}
    ${HARFBUZZ_LIBRARIES}
    ${FONTCONFIG_LIBRARIES}
    ${OpenCV_LIBS}
)

# ===== 应用实例 =====
add_executable(demo examples/demo.cpp)
target_link_libraries(demo PRIVATE ui)

# ===== 回归测试（可选）=====
option(UI_BUILD_TESTS "Build regression tests" ON)
if(UI_BUILD_TESTS)
    enable_testing()
    add_executable(ui_offscreen_regression tests/review_regression.cpp)
    target_link_libraries(ui_offscreen_regression PRIVATE ui)
    add_test(NAME ui_offscreen_regression COMMAND ui_offscreen_regression)
    add_executable(ui_demo_acceptance tests/demo_acceptance.cpp)
    target_link_libraries(ui_demo_acceptance PRIVATE ui)
    add_test(NAME ui_demo_acceptance COMMAND ui_demo_acceptance)
    if(UNIX)
        add_executable(ui_window_regression tests/window_regression.cpp)
        target_link_libraries(ui_window_regression PRIVATE ui ${CMAKE_DL_LIBS})
    endif()
endif()
