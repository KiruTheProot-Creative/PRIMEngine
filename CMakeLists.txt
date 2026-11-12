cmake_minimum_required(VERSION 3.16)
project(PRIMEngine CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# World + game logic: plain C++, no Windows code (also builds and tests on Linux).
add_library(primcore STATIC
    src/core/png.cpp src/core/noise.cpp src/world/library.cpp src/world/blocks.cpp src/world/shapes.cpp src/world/storage.cpp src/world/world.cpp src/world/mesher.cpp
    src/game/game.cpp src/game/demo.cpp src/game/weather.cpp src/game/environment.cpp src/game/project.cpp)
find_package(Threads REQUIRED)
target_link_libraries(primcore PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(primcore PUBLIC /W3 /utf-8 /MP)
    target_compile_definitions(primcore PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

enable_testing()
foreach(t world_test mesher_test game_test library_test shapes_test)
    add_executable(${t} tests/${t}.cpp)
    target_link_libraries(${t} primcore)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

if(WIN32)
    set(IMGUI third_party/imgui)
    add_executable(PRIMEngine WIN32
        src/app/main.cpp
        ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp ${IMGUI}/imgui_stdlib.cpp
        ${IMGUI}/backends/imgui_impl_win32.cpp ${IMGUI}/backends/imgui_impl_dx11.cpp)
    target_include_directories(PRIMEngine PRIVATE ${IMGUI})
    target_link_libraries(PRIMEngine primcore d3d11 dxgi d3dcompiler comdlg32 shell32)
    add_custom_command(TARGET PRIMEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:PRIMEngine>/shaders)
endif()
