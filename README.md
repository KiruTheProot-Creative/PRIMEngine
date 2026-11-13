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
- Each game is a project: a folder in `projects` next to the exe with `game.txt` (name, start world, player rules), `blocks.txt`, `textures/*.png` and `worlds/`. Its worlds share its blocks and textures. Even the starter blocks are PNGs there. Repaint one in any paint program, save it, and the engine updates within a second.
- The start screen is the project manager. Worlds are made, opened, renamed and deleted inside the editor (Outliner > Worlds). The old `game` and `worlds` folders from before projects move into a project called "My First Game" by themselves.

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

## Tools (T)

In the editor, **T** swaps the hotbar between blocks and tools. Slot 1 is the **terrain brush**:
- **Q** opens its options: 6 shapes (sphere, cube, cylinder, dome, diamond, pyramid) and 10 things it does (add, remove, paint, replace, smooth, raise, lower, flatten, fill, roughen), with size, height, strength, paint depth, lump size and the material.
- Left click uses it, right click does the opposite (add/remove, raise/lower). Hold to keep going. Shift + wheel or + / - changes the size. One stroke is one undo step.
- Turning blocks by hand moved: R turns, **Shift+R** tips over (T is now the tools).

## Objects (G2)

- **Block models:** box blocks with the Selection box (T, slot 2) and press "Make into object". Place them from Assets > Models, as often as you like, turned and sized freely.
- **Imported models:** Blockbench (.bbmodel, cube models), .obj (+ .mtl and its picture), glTF (.gltf / .glb) and FBX (text and binary, with their animations, e.g. a door's "open" and "close"). Use Assets > Models > "Import models...", File > "Import 3D models..." or drop the file on the window.
- **3D viewer:** double-click a model. Shows the model on a grid next to the player (1.8 tall) and the morph ball (1 block), with its size; set the height of imported models there.
- Collision is made from the model itself: block models from their blocks, imported models from their shape (filled with small cubes, merged into boxes).

## Doors (G2)

- Any object can be a door (Inspector > Kind). Models with an "open" animation become doors by themselves.
- Width x Height in blocks: the model is stretched to fill it (1 x 1 for the morph ball, 2 x 3, 4 x 2 ...).
- Forcefield: a glowing field in the door's opening, in a colour you pick. In Play mode, click to shoot the test beam: the field dissolves from where it was hit, the door plays its "open" animation, stays open (longer while you stand in it), plays "close" and the field comes back. Without a forcefield the door opens when you walk up to it.
- Collision follows: the door halves and the field block you while it is shut; the frame always does.

## Platforms and elevators (G2)

- Any object can be a platform (Inspector > Kind): build one from blocks and shapes ("Make into object") or use an imported model; collision comes from it as always.
- Platform path tool (T, slot 4): click an object to pick it, then click the blocks where it should go; right click removes the last point.
- Back and forth, round and round, or once; always moving, when you stand on it (elevators: step off and on again to send it back, or let it come back by itself), or when shot. Speed, waiting at stops, stopping at every point, copies spread along the path, or a spawner that sends a new one every few seconds.
- You ride along on top. "Try it here" moves them in the editor.

## Pickups, checkpoints, teleporters, trigger zones (G2)

- Objects tool (T, slot 3) > Add: Health, Key, Gem, Checkpoint, Teleporter, Trigger zone. Each has a built-in look; any model can be used instead.
- Pickups: health, keys (doors can need a key: Door > Needs key) and collectibles that are counted. They turn and bob, and can come back after a while.
- Checkpoints: touch one and you come back there after dying.
- Teleporters: to an object (by name) in this world or in another world of the game; the pickups you took stay taken.
- Trigger zones: invisible boxes that show a message, kill, heal or set a checkpoint when you walk in (G3 will let them start any event).

## Lights and particles (G2)

- Objects tool > Add > Light: point or spot lights (colour, brightness, reach, cone, tilt, flicker: candle, broken, pulse). They light blocks and objects in real time (up to 32 near you).
- Objects tool > Add > Particles: an emitter with presets (sparks, fire, smoke, steam, magic, drips, embers, beam trail, impact) and every setting: how many, bursts, where from, life, direction, spread, speed, gravity, drag, spin, hitting blocks (bounce or stop), shape (soft dot, streak, smoke puff, pixel, ring, flare), glowing or soft, size and colour from start to end, and lighting its surroundings.
- The engine uses the same particles: the test beam has a glowing tail and its own light, and throws sparks where it hits (in the forcefield's colour when it hits a door).

## Event cards (G3)

The game's logic, without code. The **Events** panel (a tab next to Assets) holds cards. Each card reads:

- **When** something happens: the game starts, the player walks into / out of a zone, touches or shoots an object, picks something up, a door opens or closes, the player dies, every few seconds, after some seconds, a counter reaches a number.
- **Only if** (optional): the player has / has not got a key, a counter is at least, a flag is on / off, health is below.
- **Do**, in order: show a message, open / close / lock / unlock a door, move a platform (set its "Starts" to "By an event card"), show / hide an object, switch a light (or emitter) on / off, a burst of particles, teleport, heal / hurt / kill, set a checkpoint, give / take a key, add to a counter, turn a flag on / off, **wait** (the actions after it come later), win.

Objects are found **by name**: every object called "Hall light" switches together. "Only once" cards run a single time per play. In Play mode a card lights up when it runs. Cards are saved per world in `events.txt`. Start from **+ From an example...** to see how they work.

## Player editor, part 1: the HUD

Open it with the **Player** button in the toolbar (or Game > Player editor). It's saved for the whole game in `hud.txt`.

- **Planes (2D):** flat sheets in front of the camera, anywhere you like: nearer or farther than the 3D parts, so in front of or behind them. They can **bend** side to side and top to bottom, like the inside of a visor. Each plane holds **elements**: panels, bars (cut into pieces for energy tanks), text (`{value}` and `{max}` show a value), pictures (put in the project's `hud` folder) and circles. Bars and text can show health, a counter (missiles...) or whether you have a key.
- **3D parts:** models fixed to the camera (your helmet). **Mirror copy** makes the other side, **Mirror** flips one, **Duplicate** (Ctrl+D) copies; this works for planes and elements too.
- **Sway:** when you turn, the HUD stays behind a little and springs back, like in Metroid Prime. Strength, smoothness, walk bob, lean when turning; every plane and part has its own sway amount.
- The editor shows the HUD from around the camera (drag to turn), a camera preview over the world, and the picked plane laid out flat: drag elements to move them and their corner to size them (Shift snaps).
- Event cards can **show, hide and flash** HUD elements by name (there's a "Low energy: flash the HUD" example).

### Door forcefields and animations (update)

- The forcefield floats a little **in front of the door** (and/or behind it): "Field on" both sides / front / back, and how far in front. Or tick **"On the door's parts"** and choose which moving parts shimmer, for a field shaped like the door itself.
- FBX doors: parts inside moving parts now move along with them (groups in groups), and a door with only an **Open** animation plays it backwards to close. If you imported a door before this update, import it again.
- HUD: it has its own **view angle** (Sway and view), so it stays where you put it whatever field of view the player picks. **Landing shake**: the faster you fall, the bigger the dip and shake.

## Weapons (Samus controls, part 1)

Player editor > **Weapons**. Every beam is its own **arm cannon model** with three points you drag in its 3D view:
**pivot** (where it sits and turns), **muzzle** (the flash, with an arrow showing where it aims) and **emitter** (where shots start, with an arrow).
In Play mode shots fly from the emitter to wherever the crosshair points. Left mouse fires the beam (hold, if "Hold to keep firing" is on),
right mouse fires a missile (ammo is a counter, "Missile" by default, so the HUD can show it). Keys 1-9 or the mouse wheel switch beams.

- Each beam: colour, trail / hit / muzzle-flash looks (particles), speed, shots per second, damage, range, thickness, its firing animation and whether the next shot waits for it.
- Missiles: their own animation per beam, "wait until it is done" (on by default), speed, rate, colour.
- Idle breathing, walk bob, landing dip and recoil are made by the engine (strength sliders); the firing animations come from the cannon model.
- A beam found later: switch off "Have it from the start"; a key pickup with the beam's name gives it.
- Doors: **Opened by** any shot, one beam, or missiles.
