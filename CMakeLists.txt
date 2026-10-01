cmake_minimum_required(VERSION 3.20)
project(combat_roster LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the DLL must not depend on a redistributable being installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(IMGUI ${CMAKE_SOURCE_DIR}/third_party/imgui)

add_library(combat_roster SHARED
    src/dllmain.cpp
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
