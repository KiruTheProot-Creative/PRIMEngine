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

## The editor
PRIMEngine opens as an engine editor, like Unity or Godot:

| Panel | What it is for |
| --- | --- |
| Viewport | The game, drawn live. Editing: hold right mouse + WASD/QE to fly (Shift = fast). Playing: click to control, Esc to release |
| Hierarchy | What is in the scene: the room, the player, the arm cannon and its parts |
| Inspector | Settings of whatever is selected |
| Assets | Your files in the `assets` folder. Double-click a model to use it |
| Animation | Every clip of the model: play, rename, loop, speed, transitions |
| Console | What the engine did, and any errors |

**Play / Stop** (F5) is in the middle of the menu bar. F6 pauses, F11 hides the editor, Ctrl+S saves. Panels can be dragged and docked anywhere; View > Reset layout puts them back.

## Building rooms (creative mode)
**File > New room** asks for a size (Small 64x32x64 up to Huge 512x96x512, or your own up to 512x128x512) and whether to start with a floor (one block = 1 metre, Minecraft scale). The Inspector can resize a room later; blocks stay in place. Then **click the viewport** to step into the room, Minecraft creative style:

| Key | Does |
| --- | --- |
| WASD, mouse | Walk and look |
| Space | Jump; **double-tap** to fly on/off (flying: Space up, Shift down) |
| Ctrl / Shift | Sprint / sneak |
| Left click | Break the block in the crosshair (hold to keep breaking) |
| Right click | Place the selected block (hold to keep placing) |
| Middle click | Pick the block you look at |
| R / F | Rotate / flip the shape you are about to place (an orange outline previews it) |
| + / - | Change the shape's size (slab thickness, number of steps, slope height, beam width ...) |
| 1-9, mouse wheel | Choose a hotbar slot |
| E or Esc | Mouse back to the editor panels (click a block in Inventory to put it in the hotbar) |
| Ctrl+Z / Ctrl+Y | Undo / redo |

**Shapes** (Inventory panel, or the hotbar): Cube, Slab, Stairs, Slope, Outer corner, Inner corner, Beam, Pillar, Panel, Step, Frame, Round edge, Arch. Attaching shapes (slab, beam, pillar, frame) stick to the face you click; standing shapes (stairs, slopes, panels) stand up and face away from you, and hang upside down when placed on a ceiling. Samus walks up stairs and slopes.

**Materials**: pick one for the selected hotbar slot in the Inventory panel: Metal, Rock, Dirt, Moss, Sand, Snow, Ice, Basalt (glowing cracks), Lava (glowing) and Chozo stone. Moss and Snow have their own top and side textures.

**Terrain** panel: choose a preset (Hills, Cavern, Canyon, Crater, Ice field, Lava cave, Chozo ruins), press **Preview**, drag the sliders and watch the room change, then **Apply** or **Cancel**. One Ctrl+Z removes applied terrain. Edges are smoothed with slopes and corners, caves and 1-block morph ball tunnels are carved, and you can limit it to a box.

**Lights**: Inventory > Lights has presets (warm lamp, cool lamp, Phazon blue, lava orange, Chozo gold, spore green, white, red alarm). Put one in the hotbar, right-click to place, left-click a light to remove it, middle-click to select it and edit colour, brightness, range, shadows and glow in the Inspector. Light is baked into the blocks with real shadows; lava and basalt light their surroundings by themselves.

**Room mood** panel: presets (Tallon rain, Tallon sunny, Phendrana snow, Magmoor caverns, Chozo ruins, Phazon mines, Space station) and settings for haze, ambient light, sun, bloom, floating particles (dust, spores, snow, embers, rain) and sky (gradient, clouds, stars). Lighting re-bakes a few chunks per frame, nearest first.

**Play** (F5) starts the game with Samus right where you are. Rooms autosave every 30 seconds into `assets/rooms`; Ctrl+S saves now. The last room you had open reopens on start.

## Your models and animations
1. In Blockbench: **File > Export > Export FBX**, save into the `assets` folder next to `prime_d3d11.exe`.
2. Double-click it in the **Assets** panel. Every animation in the file appears in the **Animation** panel.
3. Set which clips loop, rename them, change speed, choose the blend type and time, and try transitions by pressing **Play** on another clip.
4. Select the arm cannon in the **Hierarchy** and place it with the **Inspector**.

Your choices are saved automatically next to the model as `<name>.fbx.meta`. Re-exporting the FBX from Blockbench re-imports it automatically and keeps your settings.
