cmake_minimum_required(VERSION 3.20)
project(combat_roster LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the DLL must not depend on a redistributable being installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(IMGUI ${CMAKE_SOURCE_DIR}/third_party/imgui)

add_library(combat_roster SHARED
    src/assets.cpp
    src/dllmain.cpp
    src/fontloader.cpp
    src/gon.cpp
    src/gpak.cpp
    src/loc.cpp
    src/swf.cpp
    src/game.cpp
    src/hooks.cpp
    src/log.cpp
    src/overlay.cpp
    src/panel.cpp
    src/roster.cpp
    ${IMGUI}/imgui.cpp
    ${IMGUI}/imgui_draw.cpp
    ${IMGUI}/imgui_tables.cpp
    ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/backends/imgui_impl_win32.cpp
    ${IMGUI}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(combat_roster PRIVATE
    src
    third_party
    ${IMGUI}
    ${IMGUI}/backends
)

target_compile_definitions(combat_roster PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
)

# /EHa (not the default /EHsc) so __try/__except and C++ code can share frames safely.
string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
target_compile_options(combat_roster PRIVATE /EHa /W3 /utf-8)
target_link_libraries(combat_roster PRIVATE opengl32 user32)

# Offline test tool for the SWF rasteriser (not part of the mod).
add_executable(swf_test tools/swf_test.cpp src/gpak.cpp src/swf.cpp)
target_include_directories(swf_test PRIVATE src third_party)
target_compile_options(swf_test PRIVATE /utf-8 /EHsc /O2)
add_executable(gon_test tools/gon_test.cpp src/gpak.cpp src/gon.cpp)
target_include_directories(gon_test PRIVATE src)
target_compile_options(gon_test PRIVATE /utf-8 /EHsc /O2)
add_executable(font_test tools/font_test.cpp src/fontloader.cpp src/gpak.cpp src/swf.cpp
    ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp)
target_include_directories(font_test PRIVATE src third_party ${IMGUI})
target_compile_definitions(font_test PRIVATE STB_IMAGE_WRITE_IMPLEMENTATION)
target_compile_options(font_test PRIVATE /utf-8 /EHsc /O2)
add_executable(glyph_test tools/glyph_test.cpp src/gpak.cpp src/swf.cpp)
target_include_directories(glyph_test PRIVATE src third_party)
target_compile_definitions(glyph_test PRIVATE STB_IMAGE_WRITE_IMPLEMENTATION)
target_compile_options(glyph_test PRIVATE /utf-8 /EHsc /O2)
