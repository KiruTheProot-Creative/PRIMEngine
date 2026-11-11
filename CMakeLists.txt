cmake_minimum_required(VERSION 3.20)
project(PrimeStyleRenderer CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Standalone Windows build (Direct3D 11). Needs the Windows 10/11 SDK, which ships with Visual Studio.
if(WIN32)
  add_executable(prime_d3d11 WIN32 src/main_d3d11.cpp src/scene.h)
  target_link_libraries(prime_d3d11 PRIVATE d3d11 d3dcompiler dxgi user32)
  target_compile_definitions(prime_d3d11 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  if(MSVC)
    target_compile_options(prime_d3d11 PRIVATE /W3 /permissive-)
  endif()
  # copy the .hlsl files next to the exe so the demo finds them (they are compiled at startup)
  add_custom_command(TARGET prime_d3d11 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:prime_d3d11>/shaders)
endif()

# Platform-independent content test (runs anywhere): builds the demo scene and checks winding/determinism.
add_executable(scene_test tests/scene_test.cpp)
