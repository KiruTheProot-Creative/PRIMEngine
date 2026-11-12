# PRIMEngine

A block world engine in C++ (Windows, Direct3D 11). Think of Minecraft built as an engine: you build your maps in creative mode, and the world has no size limit.

## Milestone 1: map editing in an unlimited world (this build)

- Worlds load around you in 16×16×16 chunks as you move, and unload behind you. Coordinates go to about ±2 billion blocks in every direction, up and down included.
- Only chunks you changed are saved, in `worlds/<name>/region`. Untouched ground is regenerated, so a huge empty world costs no disk space.
- Creative mode with Minecraft movement: walking, sprinting, sneaking, jumping and flying.

### Controls

| Key | What it does |
|---|---|
| WASD | move |
| Mouse | look around |
| Space | jump; double-tap to fly or stop flying |
| Space / Shift while flying | up / down |
| Ctrl | sprint |
| Shift | sneak (walking) |
| Left click | break a block (hold to keep breaking) |
| Right click | place a block (hold to keep placing) |
| Middle click | pick the block you look at |
| 1–9, mouse wheel | choose a hotbar slot |
| E | block inventory |
| Ctrl+Z / Ctrl+Y | undo / redo |
| Ctrl+S | save |
| F3 | debug info (position, chunks, fps) |
| F1 | hide the HUD |
| Esc | menu (settings, save and quit) |

## Milestone 2: your own textures

- Drop PNG files onto the window (or press E, then **Import images...**). Choose whether they become blocks, cross planes (like flowers) or flat planes (they stick to the side you click: ladders, vines, posters).
- Pictures with transparent pixels become see-through by themselves. Any size works; big ones are scaled to fit.
- In the **Blocks** tab, right-click a block to edit it: name, shape, see-through, collision, and a texture per side.
- Everything lives in the `game` folder next to the exe and is shared by all worlds: `game/textures/*.png` and `game/blocks.txt`. Even the starter blocks are PNGs there. Repaint one in any paint program, save it, and the engine updates within a second.

## Milestone 3: block shapes

- Every block comes in about 100 shapes, each in all 24 rotations: half and quarter blocks, plates, ramps at four slopes with corners, stairs, square and round pillars, arches, coves, balls, domes, beams, bars, railings, ladders, frames, roof pieces, pyramids and more.
- **Q** opens the shape menu for the block in your hand (with search and categories). Pick a shape and build.
- Blocks turn by themselves to face away from you. Placed against a ceiling (or the top half of a wall), stairs and ramps go upside down. Pillars stand out of the side you click, and planes lie flat on it.
- **R** turns the next block, **T** tips it over, **G** goes back to automatic. **Ctrl + middle click** copies a placed block's shape and rotation.
- A see-through preview shows where the next block goes (it can be turned off in Settings).
- You walk up half blocks, stairs and ramps by yourself, and you can aim past poles and through window frames.
- Worlds from milestones 1 and 2 are converted the first time they load.

| Key | What it does |
|---|---|
| Q | shape menu |
| R / T | turn / tip over the next block |
| G | automatic rotation again |
| Ctrl + middle click | copy shape and rotation |

## Next

- **Milestone 4:** nicer rendering in the style of Complementary shaders.

## Building

GitHub Actions builds `PRIMEngine.exe` on every push. Download it under **Actions → Build Windows → Artifacts**.

To build it yourself, use CMake with Visual Studio 2022:

```
cmake -S . -B build -A x64
cmake --build build --config Release
```

Third-party code: Dear ImGui (MIT) and stb_image (public domain / MIT).
