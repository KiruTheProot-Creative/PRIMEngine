cmake_minimum_required(VERSION 3.20)
project(PRIMEngine C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Engine core: platform-independent (FBX import, animation, asset settings). Also builds on Linux for tests.
add_library(prime_core STATIC
  src/model.cpp src/model.h src/assetmeta.cpp src/assetmeta.h src/room.cpp src/room.h src/terrain.cpp src/terrain.h src/scene.h
  third_party/ufbx/ufbx.c)
target_include_directories(prime_core PUBLIC src)
if(MSVC)
  target_compile_definitions(prime_core PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Standalone Windows build (Direct3D 11 + Dear ImGui editor panels). Needs the Windows SDK.
if(WIN32)
  set(IMGUI third_party/imgui)
  add_executable(prime_d3d11 WIN32 src/main_d3d11.cpp
    ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp ${IMGUI}/imgui_demo.cpp
    ${IMGUI}/imgui_stdlib.cpp ${IMGUI}/backends/imgui_impl_win32.cpp ${IMGUI}/backends/imgui_impl_dx11.cpp)
  target_include_directories(prime_d3d11 PRIVATE ${IMGUI} ${IMGUI}/backends)
  target_link_libraries(prime_d3d11 PRIVATE prime_core d3d11 d3dcompiler dxgi user32 shell32)
  target_compile_definitions(prime_d3d11 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  if(MSVC)
    target_compile_options(prime_d3d11 PRIVATE /W3 /permissive-)
  endif()
  # shaders are compiled at startup; the assets folder is where your FBX files go
  add_custom_command(TARGET prime_d3d11 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:prime_d3d11>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:prime_d3d11>/assets)
endif()

# Tests (run anywhere)
add_executable(scene_test tests/scene_test.cpp)
add_executable(model_test tests/model_test.cpp)
target_link_libraries(model_test PRIVATE prime_core)
add_executable(room_test tests/room_test.cpp)
target_link_libraries(room_test PRIVATE prime_core)
add_executable(terrain_test tests/terrain_test.cpp)
target_link_libraries(terrain_test PRIVATE prime_core)
