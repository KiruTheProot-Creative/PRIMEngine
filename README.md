# Prime-style renderer (Wii / GameCube / PS2 look)

A small, dependency-free renderer that imitates the look of Metroid Prime 3 / Wii-era games.
**The shipping target is a standalone Windows exe on Direct3D 11. There is no web runtime in it.**
WebGL2 is only a test harness, with the same pipeline and the same shader math.

```
web/index.html        WebGL2 test harness (open in any browser, no server needed)
shaders/world.hlsl    material + particle shaders   (Shader Model 5)
shaders/post.hlsl     bloom chain + composite       (Shader Model 5)
src/scene.h           engine content, platform-independent C++17: procedural textures, mesh builder + baked lighting, demo room, particles, collision
src/main_d3d11.cpp    Win32 + Direct3D 11 renderer
tests/                scene_test.cpp (runs on any OS), shot.py / js_fingerprint.py (screenshot + JS-vs-C++ parity check)
```

## Build (Windows)
Install Visual Studio (Desktop C++ workload) or the Windows SDK + CMake, then:
```
cmake -B build -A x64
cmake --build build --config Release
build\Release\prime_d3d11.exe
```
Shaders are compiled at startup from `shaders\` (copied next to the exe), so you can edit an .hlsl file and just relaunch.

Controls: click = capture mouse, WASD, mouse look, LMB/Space fire, Esc release.
F1 bloom, F2 dither, F3 haze, F4 nearest upscale, F5 dynamic lights, 1-5 internal resolution (240/360/480/720/native), -/+ bloom strength.

## How the look is made (and how it maps to GX hardware)
| Wii/GC trait | Here |
|---|---|
| Renders ~640x480 into the EFB, then upscaled | Scene goes to a low-res RGBA8 target (default 480p), bilinear (or nearest) upscale to the window |
| Gamma-space shading, no HDR | Everything stays in gamma space, 8-bit targets, clamp before grading |
| Baked lightmaps + vertex colour | `Scene::bake` writes hemisphere ambient, corner AO and static lights into vertex colour (subdivided ~1 m grid) |
| Up to 8 hardware lights, per-vertex (Gouraud) | 4 dynamic lights evaluated **in the vertex shader** |
| TEV: texture * rasterised colour, + extra stages | `albedo * (baked + dynamic)`, `+ env * mask`, `+ albedo * alpha(glow)` |
| Sphere-map env (armour, metal) | Normal in view space -> sphere-map UV, shared procedural env texture |
| Glow mask in texture alpha | Alpha channel = emissive mask |
| Signature bloom | Bright-pass at 1/4 res, 9-tap separable blur, second wider blur at 1/8 res, added back |
| RGB565 / RGB6 dithering | Optional 4x4 Bayer + 6-bit quantise in the composite |
| Additive particles, coloured haze | Billboard quads, ONE/ONE blend, per-pixel haze fade |

All textures are generated in code, so there are no asset files. Drop in your own art later.

## Conventions (same in both back-ends)
Left-handed, Y up, metres. Clockwise triangles are front faces. Vertex = pos(3) normal(3) uv(2) baked-colour(4), 48 bytes.
HLSL uses DirectXMath row-major / `mul(v, M)`; the GLSL port uses column-major / `M * v`.

## Status / what was verified
- WebGL2 harness: rendered and checked in headless Chrome (see screenshot).
- `scene.h`: compiled and run on Linux; its textures and meshes match the JS harness exactly (same sums for every texture and mesh).
- **`main_d3d11.cpp` and the .hlsl files were NOT compiled or run in my sandbox (no Windows SDK there).** They are a direct port, but expect to fix a typo or two on first build. Compile errors from the shaders appear in a message box with the line number.

## Vulkan / DX12 later
Keep the .hlsl as the source of truth. Compile it with DXC to DXIL for D3D12, or to SPIR-V (`dxc -spirv`) for Vulkan.
The D3D11 code is already shaped like a small RHI: a few constant buffers, 2 vertex formats, one low-res target plus a bloom chain, so moving to Vulkan/D3D12 means rewriting `main_d3d11.cpp`'s `App`, not the shaders or `scene.h`.

## Not done yet (next steps toward a Prime fan game)
- UV2 lightmap atlas (the vertex-colour bake is the stand-in)
- Visor/HUD layer, scan-visor tint, heat-haze/distortion pass, blob shadows
- Model/animation loading (glTF), audio, level streaming/doors, proper collision

## Getting the .exe without compiling
Every push builds the game on GitHub. Open the repository's **Actions** tab, click the newest green "Build Windows" run, and download **PRIMEngine-windows** at the bottom. Unzip it and run `prime_d3d11.exe`.
