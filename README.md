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
F fires a missile (the right mouse button is kept for lock-on) (ammo is a counter, "Missile" by default, so the HUD can show it). Keys 1-9 or the mouse wheel switch beams.

- Each beam: colour, trail / hit / muzzle-flash looks (particles), speed, shots per second, damage, range, thickness, its firing animation and whether the next shot waits for it.
- **Missiles** are one supply for all beams (MISSILES in the list). More kinds can be added and found later (a key pickup with the kind's name), like an Ice Missile or a Super Missile (the starter set has one that uses 5 missiles). Each kind: its 3D model (built-in `@missile`, or any model, flying nose first along +Z), length, colour, how many missiles it uses, speed, rate, damage, trail / hit looks, impact sphere, launch and hit sounds. Each beam only sets its cannon's missile animation and "wait until it is done" (on by default).
- **Switching** (All weapons > Switching): freely (1-9 / mouse wheel for beams, Q for the missile kind), never (always the best one you have: the last one in the list, like Prime 3), or in the pause menu (Esc, like Super Metroid).
- **Shots look like** a glowing 3D orb (default: a smooth bright ball with energy streaming over it and a soft halo; charged ones crackle more) or particles only.
- **Charge animations and sounds:** the cannon's "charging" animation plays while it fills, "charged" loops while it is full, "charged shot" fires it. The charging sounds (Charging timeline) stop the moment it is full; then a "full" sound plays once together with a loop that hums until you let go. Charged shot sounds: the Charged shot timeline. Hit sounds for normal and charged shots.
- Idle breathing, walk bob, landing dip and recoil are made by the engine (strength sliders); the firing animations come from the cannon model.
- A beam found later: switch off "Have it from the start"; a key pickup with the beam's name gives it.
- Doors: **Opened by** any shot, one beam, missiles (any kind), or one missile kind (a Super Missile door).
- **Impact spheres** glow with moving plasma and light up the world around them while they last.
- **Charge shot:** a tap fires a normal shot; hold and it charges at the muzzle (particles and light); let go when it is full for a bigger, stronger shot (time, damage, size, speed and look per beam). The HUD can show it (Shows > Beam charge).
- **The cannon's layer:** it has its own depth, so it never cuts into the HUD: behind the HUD (default, like a visor) or "Cannon in front of the HUD".

## Textures: bumps, coat and materials (like Complementary)

- **Bumpy textures** (Settings > Look, generated normals): each texture's light and dark pixels become little bumps and dents with a bevel at the pixel edges, so the sun and lights catch them. Nothing to paint.
- **Texture coat** (Settings > Look, coated textures): a fine noise inside every pixel (4 x 4 specks), so big flat pixels look like stone, dirt or wood. Both fade out where pixels get smaller than the screen's.
- **Materials per texture** (Blocks & textures > Textures: click a texture): shiny, metal (reflects in its own colour), glows (only its bright pixels if you like: a lamp's bulb, lava's cracks), and its own bumps and coat strength. Textures nobody set up get a guess from their name (iron / gold / metal, glass / ice, lamp / glow / lava...). Saved in the project's `materials.txt`.
- **3D models** (FBX and built-in) get the bumps and coat from their own picture's pixels too, and a **material per model** (shiny, metal, glow...): in the object Inspector, a HUD part, a beam's cannon or a missile kind, under "Material of ...". It counts everywhere that model is used.
- **HUD glow:** each 2D plane gets a soft **Bloom** around what is drawn on it, and **Lights the helmet**: its light (in its colours; bigger and brighter elements give more) falls a little on the helmet's 3D parts. Settings > Look > HUD glow switches both.

## Changing between animations (like the first prototype)

When one animation takes over from another, or a model goes back to rest, its parts don't jump. In the 3D viewer (models with animations) under **Changing between animations**:
- **Inertialization** (default): the parts keep moving the way they were and settle onto the new animation along a smooth curve. It stays smooth even when a new one starts in the middle of a change.
- **Crossfade**: a mix from what was on screen to the new one, along a curve (linear, ease in, ease out, smooth).
- **Cut**: no blending.
- **Blend time** in seconds. Saved per model in `anim_blend.txt` and used everywhere the model is used: arm cannons (fire, charging, charged loop, missile, back to rest), doors (opening turning into closing) and the viewer.

## Folders you make (assets and objects)

Textures, models and blocks (Assets panel) and objects (Outliner) can go into folders you make yourself, folders inside folders too ("Weapons/Beams"). Right-click "All ..." (or Objects) for a new folder; right-click a folder to make one inside it, rename it or delete it (what is in it moves up one). Drag things onto a folder to put them in it, drag them onto "All ..." to take them out, and drag folders into each other. Clicking a folder shows what is in it and in its sub-folders. Folders are only for sorting: the files stay where they are, so nothing breaks. Saved in the project's `asset_folders.txt` (blocks and objects keep theirs on themselves).

Also: **speed per animation** (3D viewer: "Speed of ...", and next to each cannon animation in the Weapons tab), everywhere that animation plays; and the **charge** now grows an energy orb at the muzzle with particles sucked into it (particles: "Sucked in").

## The game's own sounds (Player > Sounds)

Besides steps, jumps, the morph ball and bombs, the Sounds tab has the sounds of the helmet and the game around you (each can have several: a random one plays):

- **Visors and scanning:** switching visors (one for all, or each visor its own), onto something scannable, scanning (a loop that rises as it fills), scan done, a new entry, the scan text coming up and going away.
- **Menu and map:** the pause menu opening / closing, the map opening / closing. (Picking and using buttons in the menus: the game's interface sounds, Screens > Sound & timing.)
- **Weapons:** another beam, another missile, firing with none left (missiles, power bombs), locking on, letting go.
- **Pickups and alerts:** energy, missiles / bombs picked up (from enemies too), an item (upgrades, tanks), the game saved, a message, getting hurt, the low-energy alarm (below the energy you set, every so many seconds), dying.

## The Assets panel (like Unity's Project window)

Everything the game is made of lives in **one folder structure you make yourself**: blocks, textures, models, sounds and music, pictures, videos, enemy types, bosses, screens, scan entries, worlds and prefabs, side by side (a `Bosses/Kraid` folder can hold his model, roar, music, enemy type and boss). The folders are the editor's own (`assets.txt`): the files stay where the engine keeps them, so sorting never breaks anything. New things you make or import elsewhere start in a folder of their kind; things you make in the panel go into the folder that is open. (Older projects: the old block / texture / model folders are carried over.)

- **Left:** favourites, your folders (drag folders into folders), and **Built in** (the engine's models and the block shapes).
- **Right:** the way to the folder (each step is a drop target too), search (`kraid t:sound`; `t:` + any kind), a kinds filter, **Create**, and the size slider (all the way down: a list). Every kind has its own picture: blocks, textures, 3D models, a sound's waveform, pictures and GIFs, a video's frame, enemy types (bosses wear a crown), a screen in miniature, worlds, prefabs.
- **Click** picks it (the Inspector shows it), **Ctrl / Shift + click** picks several, **double-click** opens it (a folder, the 3D viewer, listening to a sound, the enemy / boss / screen editor, another world, placing a prefab). **Drag** things into folders to move them.
- **Right-click:** open, place, rename (F2), duplicate (Ctrl+D), move to, favourite, **find where it is used** (in all worlds, enemy types, bosses, screens, weapons, the HUD ...), **show in Explorer**, delete (Del; files go into the project's `_trash` folder). Renaming a sound, picture or video renames the file and fixes every place that uses it.
- **Importing:** drop files from Windows Explorer onto the panel (or Create > Import files): models, sounds and music, videos, and pictures (you pick: block textures, screen pictures or HUD pictures). They land in the open folder. Textures, sounds, pictures and prefabs changed on disk (painted, cut) are read again by themselves.
- **Drag from the panel:** into the viewport to place a model, an enemy type or a prefab (click to put it down); onto any sound, picture, video or model field to use it there.
- **Prefabs:** objects saved with all their settings (a red missile door, a save room, an enemy with its scan): drag an object or a whole Outliner folder into the panel, or "Save as a prefab" on an object. Place it like a model (R turns it, paths and zones turn along). Placed prefabs are copies: changing the prefab later does not change them.

## Culling: only what you can see is drawn

- **Hidden sides**: a block side touching a solid block is never built; glass-like blocks can hide the sides between each other.
- **Off screen**: chunks and object parts (each 16 x 16 x 16 piece of a model: doors, platforms, pickups, enemies...) outside the view are skipped, for the camera, each shadow map and the reflection. Objects past the fog are skipped too.
- **Behind walls** (cave culling, like Minecraft's): each chunk remembers which of its sides are joined by air inside it; from the camera a walk goes chunk to chunk only through those openings and only away from you. Rock, closed caves and rooms behind walls are not drawn (nor the objects in them). Options > Graphics > "Skip what is behind walls" turns it off.
- **F3** shows the counts: chunks drawn and hidden behind walls, object parts drawn and skipped.

## Sun shadows: four cascades

The sun's shadows use four shadow maps around you: the nearest covers about 13 blocks and is very sharp, each next one reaches farther with bigger pixels, and where two meet they blend smoothly (no line). How far shadows reach is its own setting (**Settings > Look > Shadow distance**, 24 to 512 blocks), so the sharpness near you no longer depends on the render distance. Soft edges keep the same size in blocks in every map; very soft ones are taken from a coarser map so they stay smooth.

## Player editor: Body and movement, Items

- **Body and movement** (was the Game panel's player rules): size, walking, sprint, jump, gravity, the Morph Ball's size, speed and model, health and dying.
- **Items**: everything Samus can find, from across the Metroid games. Tick what is in your game, choose "from the start" or "found later", and press **Place pickup** to put its pickup in front of you (or give it with an event card). Beams and missile kinds (from the Weapons tab), the Morph Ball and Bombs are listed too.
  - Working now: **Energy Tanks** (more health, and they fill you up), **Missile / Power Bomb Expansions**, **Power Bombs** (rolled up: F; break "Power Bomb" and "Bomb" blocks, open Power Bomb doors), **Varia / Gravity / Phazon Suits** and your own (damage taken, lava damage, moving freely in water and lava; the best one you have counts), **Hi-Jump Boots**, **Space Jump Boots** (jumps in the air), **Speed Booster** (run a while, then much faster; breaks "Speed Booster" blocks), **Spring Ball** (jump while rolled up), **Charge Beam** (no charging until found).
  - Coming (already in the list, as pickups): Screw Attack, Grapple Beam, Spider Ball, Boost Ball, Shinespark, Wall Jump, Scan / Thermal / X-Ray Visor.
  - Saved in the project's `items.txt`.

## Sounds, timelines and free aim

- **Sounds:** WAV files (PCM 8/16/24/32-bit or float, any rate). Import them in Player editor > **Sounds** (or from any sound picker); they go into the project's `sounds` folder. Every sound use has volume, pitch, random pitch (so steps don't sound the same) and a cut (start into the sound, how long). Settings > Sound volume for everything.
- **The player's sounds:** steps (one every so many blocks walked), jump, landing (silent below a falling speed, full volume at another). Sounds in the world get quieter with distance and come from their side.
- **Timelines** (Weapons > Timeline): for each beam's Fire, Charging, Charged shot and Missile: drag the end of the animation to change its speed, drag the yellow mark to set when the shot leaves, add sounds and drag them (their edges cut them). Play tries it in the preview. Charging sounds stop when you let go.
- **Free aim** (Weapons > Aiming, Wii style): the mouse moves a cursor; the cannon and the shots follow it; past the dead zone the camera turns, faster toward the edge (dead zone, turning speed, cursor speed, how much the cannon follows).
- Later, for exporting whole games: WAV to OGG.

## The pause menu (Player > Menu): tabs of bent panes

Esc while playing opens the menu inside the helmet: **pages shown as tabs** (Q / E or the tab bar switch them, a new page slides in), each made of one or more slightly bent **panes** (turned toward you for a wrap-around look), over everything else (the HUD elements and parts set to "stay when paused" stay around it). Pick with the mouse (it follows the bends), or W / S and Enter; lists scroll with the wheel.

The starter menu has **Main** (keep playing, start over, map, settings, stop), **Inventory** (the items you found, "???" for the rest; beams and missiles to pick) and **Logbook** (the scan index: entries by category, the picked one's text and its model turning). Player editor > **Menu** changes all of it: add pages (tab or not), panes (where, turn, size, bend, colour, glass, glow, frame) and items: Panel, Text (with {energy} {missiles} {scans} {game} {world} filled in), Image, Button (keep playing, start over, map, settings, stop, go to a page, back), Tab bar, Item list, Beam list, Scan list, Scan text, Scan model. Drag items on the flat layout of a pane; the camera preview shows the page as in the game.

## Screens (toolbar > Screens): main menu, options, death screen, credits

The game's own flat screens outside of play. A new game starts with four: **Main menu** (New game, Continue, Options, Credits, Quit), **Options** (the options list: Sound, Graphics, Controls), **Death screen** (Try again, Main menu, over the greyed, blurred game) and **Credits** (a roll). Add your own (or more presets) with "+ Screen"; each screen can be *the* main menu / options / death screen / credits.

- **Layout**: items on a 2D canvas, drawn as they will look: panels, text, pictures (and GIFs), buttons (new game, continue, options, credits, quit, go to a screen, back, try again, main menu), the options list (all three groups as tabs, or just one) and a credits roll ("# " lines are headings). Drag to move, the corner to resize (Ctrl: on a grid). **Every colour** is yours: text, accent, picked, dim, fill.
- **Background**: a colour (or top-to-bottom gradient), a picture or animated GIF, a **video** (anything Windows can play: MP4, WMV, AVI, MOV...), **a world seen through an object you place in it** (turned the way to look, slowly turning if you like), or **the game behind** (paused). Files go into the game's "screens" folder ("Import..." copies them there).
- **Compositor**: post-processing as a **node graph, like Blender's**. Background and UI feed nodes - Alpha Over, Mix (mix, add, multiply, screen, overlay, difference, lighten, darken), Blur, Glare, Color Balance (lift / gamma / gain, each tintable), Hue / Saturation, Bright / Contrast, Invert, Gradient Map, Vignette, Chromatic, Lens Distort, Pixelate, Grain, Scanlines, Sharpen - wired into an Output. Right click adds a node, drag from a node's Image to an input, right / middle drag moves around, the wheel zooms, Delete removes, M mutes. A wire that would make a loop is refused.
- **F6** (or "From the menu" in the toolbar) plays the game the way a player starts it: the main menu, then New game into the start world. Stopping from the pause menu goes back to the main menu; F5 goes back to the editor. Dying shows the death screen (if there is one). Esc goes back a screen.

### Sound, music, splash screens and intros (Screens > Sound & timing)

- **Interface sounds**: picking (the mouse or W / S moves to another button), using (a click / Enter, changing an option) and going back - for the whole game (every screen and the pause menu), and per screen if it wants its own. Options > Sound > **Menus** sets their volume.
- **Music** per screen: keep what is playing (the Options screen keeps the main menu's), play this (it loops, fading in over the seconds you set), or silence. WAV, **MP3, M4A, WMA, FLAC** (through Windows). Starting a game fades it out. Options > Sound > **Music**.
- **Splash screens and intros**: a screen can go on by itself after some seconds or when its video ends, to the screen you pick (or the main menu); a key or click skips (if allowed). The screen with the role **Splash (shown first)** is where F6 starts; the one with the role **Intro of a new game** (the story) comes after picking an empty file, then the game. Picking a file (or Continue) plays the **game-start sound** and fades to black - as long as the sound, or the seconds you set - and the game fades in. Items have **timing**: when they appear and go, how long they fade, **typed text** letter by letter with a sound per letter (like Super Metroid's intro), slow drifting and growing. New games come with a **Splash** (the game's name fading in and out, before the main menu) and a **Story** (typed text, after picking an empty file); "+ Screen" adds more.

### The stack (intros: Screens > Layout, "Made as: a stack")

An intro (like Super Metroid's) is a list of steps, played top to bottom; the timing works itself out:

- **Picture**: it **stays** (the next step, a text say, starts right after it faded in) or **zooms in slowly** (closer, to the size and over the seconds you set; the next step starts when the zoom is done). The whole screen, or the top part with the text under it. A new picture starts a new page: the picture and text before it fade away as it comes.
- **Text**: typed letter by letter (a sound per letter), it stays a while, then the next step; a new text replaces it (it fades first). Bottom, middle or top; size, colour, lined up.
- A picture can also sit in **the middle** (a logo); a text can fade in over its own time and **come with the text before** (a title and a line under it: "under the middle").
- **Wait** (seconds), **Sound** (plays; then waits, or not), **Clear** (everything fades away).

Add steps with the buttons above the list (they go after the picked one), drag them (or the arrows) to change the order, right-click to duplicate or delete. Picking a step shows it on the canvas (faded in, typed) and its settings on the right. Play plays it all with its sounds. The screen lasts as long as its steps take, then goes on. The Story and Splash presets are stacks; a screen can be switched to a timeline (and back) at any time. Switching a timeline screen to a stack turns its texts, pictures and sounds into steps (at about the same times); buttons and the rest stay items.

### The timeline (Screens > Layout, under the canvas)

Every item of a screen is a row (back to front), with a bar from when it appears to when it goes; slopes at its ends are its fade in / fade out, a white line under a text is how long its typing takes. The **playhead** (yellow) is what the canvas shows: click or drag in the ruler to move it, **Play** plays from there with the sounds (typing, sound cues). Drag a bar to move it, its ends to change them; the red line is the screen's end (drag it; right-click the ruler: end here / never ends). Snapping to the playhead, other bars' ends and 0.05 s (Shift: none). Ctrl + wheel zooms, Shift + wheel moves along. Right-click a bar: starts / ends at the playhead, stays, keyframe here, copy, delete.

- **+ Text / + Picture / + Sound** add one at the playhead. A **Sound** item is a sound cue: it plays once at its time (a boom, a voice line).
- **Keyframes** (diamonds on a bar): where an item is, how big and how see-through at a time; in between it moves on its own (smooth: easing in and out). Add one at the playhead (right-click its bar, or the item's Keyframes), move the playhead, drag the item on the canvas: that makes the next one. A picture gliding across, a slow zoom, a text sliding in.
- Texts line up left / middle / right and top / middle / bottom inside their box, wrapped at spaces; typed letters appear where they will stay.
- Texts (and buttons, credits, stack texts) can have **thicker letters** and an **outline**: a line all around the outside of the letters in a colour you pick (the letters stay whole; it is not a stroke drawn over their edges).

## Save files (like Metroid Prime)

Games save **only at Save Stations** (Objects: "@save-station"): stand on its pad and press E - it fills up your energy (and missiles / bombs, each can be turned off) and asks "Save your progress?". The game saves into the file being played. Main menu > New game opens the **Files** screen: three files by default (the Save files item sets how many), each card showing the world and station, time played, energy and the items found (%). An empty file starts a new game, a used one goes on from its last save (where you stood, what you carry, what you took / defeated / scanned / mapped in every world, the game's flags); Delete twice erases one. **Continue** goes on from the newest file; on the death screen, **Last save** loads this file's last save. Playing from the editor (F5) saves into a test file of its own. Files live in the game's "saves" folder.

### Options (in the visor)

The **Options** tab of the pause menu (and Main's Options button) holds the player's own options, on the same bent glass: **Sound** (master, weapons, movement, enemies, world), **Graphics** (fullscreen, VSync, brightness, field of view, interface size, render distance, shadows with their **resolution** 1024 to 4096, distance and edges, clouds, fog, glow, reflections, ambient occlusion, swaying, the HUD glow, bumps, coat, materials) and **Controls** (mouse sensitivity, invert up / down, hold or toggle lock-on, and **every key**: two per action, any key or mouse button). W / S pick, A / D change, Enter flips or sets a key (then press the new key; Esc: never mind; Delete: none), the mouse clicks and drags sliders; each group has "back to the start values". They are kept for this computer (settings.txt), not in the game. The editor's Settings window shows the same list (plus the editor's own: time of day, the moving sun, the block preview). Menus made before get the Options page added; in Player > Menu an **Options** item can show all three groups as tabs or just one, so you can lay them out on panes of your own ("+ Options page" puts the page back if you took it out).

## Visors (V) and scanning

**V** switches between the visors you have: Combat, **Scan**, Thermal, X-Ray (Items tab: the Scan Visor is there from the start). Beams and missiles only fire with the Combat Visor. Player editor > **Visors** sets each visor's look over the world picture (never the HUD or the helmet's 3D parts; the arm cannon does get it): tint, colourfulness, contrast, brightness, heat colours, scan lines, grain, outlines, colour fringes, dark edges, and a flash when switching; the preview shows it live. Thermal and X-Ray are only looks for now. Each HUD element and 3D part chooses which visors show it ("Shown in visors").

Give any object a **scan entry** (its inspector: Scan). Scan entries are written once and have an **ID** (kept in scans.txt): a name, a category, text, "important" (a red marker) and their own scan time; any number of objects can show the same entry (pick it in the list), and changing it changes it for all of them. Scan one door and every door with that entry counts as scanned (blue markers). "Give this one its own copy" splits one off. Player editor > Visors > Scan Visor lists all entries and how many objects use each. With the Scan Visor, everything scannable in reach gets a marker and a glowing outline (orange, red if important, blue once scanned); aim at one and **hold the right mouse button** until the ring fills. Its text shows on a pane (long texts scroll: mouse wheel, W / S or the arrow keys; click, E or Enter to go on) and it goes into the scan index. In the Logbook, long texts scroll with the wheel over them (or Page Up / Page Down).

## Enemies (Player > Enemies)

Enemy **types** are made once (enemies.txt, each with an ID) and placed as **Enemy** objects (Tools > Objects > Add > Enemy, then pick the type in its inspector). The starter types are placeholders with their own little models: **Zoomer** (crawls over blocks: over the top, down the sides, around the bottom, up walls), **Hornoad** (hops), **Reo** (flies, weak to the Wave Beam), **Trooper** (walks, shoots, weak to missiles and charged shots), **Skree** (hangs from the ceiling, drops when you walk under it, digs in and bursts into shrapnel) and **Turret** (stands, shoots; only missiles, charged shots and bombs hurt it).

A type has: its model (any model, fitted to its size) and colour; health, how much touching it hurts, knock-back; **what hurts it** (weaknesses: a beam or missile by name, Any beam, Missile, Charged, Bomb, Power Bomb, each times some amount; "only its weaknesses" makes everything else bounce off with grey sparks); **how it moves** (stands, walks, crawls, flies, hops, drops) with speed, chasing, how far it sees, hop height, flying height, drop reach, shrapnel; **shooting** (how often, damage, speed, reach, colour); **when it is defeated**: it **explodes** into little cubes of its own model, **fractures** into a few big chunks, plays its **die animation**, or just **bursts** into sparks, and **drops** energy or missiles (each with a chance); **comes back** never, when you come back, or after some seconds; its **animations** (standing, moving, attacking, hurt, dying: an imported model's clips); **sounds** (notices you, attacks, is hurt, dies); and a **scan entry** (Logbook > Creatures).

In play your beams, missiles and bombs hit them (a white flash, a knock-back), and they hurt you by touching and by shooting (a short moment after each hit you cannot be hurt again). Event cards: "when an enemy is defeated" (by its name, or its type's name) and "when every enemy in a zone is defeated" (to open a door after a fight).

## Bosses (Player > Bosses)

A boss is an **enemy type** made a boss (bosses.txt): pick the type on the left, or start from the **Kraid-like example** (it makes the enemy type too). No code anywhere:

- **Boss:** the name on the boss bar, health, the bar's colour, whether shots on its body hurt it (off: only its weak points). The type's weaknesses still count (only missiles, say).
- **Weak points and armour:** parts of it that take hits differently. A part is a moving part of its imported model (it follows the animation: a mouth that opens) or a box on its body (left..right, bottom..top, back..front, in %). **Weak point:** more damage, and it can open and close (only hittable while open). **Armour:** shots glance off. **Breakable armour:** has its own health and breaks off; a phase can start when it does.
- **Phases:** the first starts with the fight; later ones start when its health drops below a %, a part breaks, some seconds pass, or a flag turns on. Each has its own speed, a glow colour, its music (keep or change), and its **moves**, picked in order, at random (how often each) or by your distance.
- **Moves:** wait, walk side to side, chase you, move to a spot, hover, jump at you, hop, dash (a wall ends it with a thud), shoot, slam (a shockwave along the floor: jump over it), spawn enemies, shield (nothing hurts it), open / close a weak point, roar. Each takes some seconds and can play an animation of its model and a sound. **Shooting** patterns: aimed at you, a fan, straight ahead (Kraid's belly spikes), a ring all around, rain from above you, homing, lobbed in an arc (Kraid's claws); how many, how fast, damage, size, colour, again every few seconds, and where they leave it.
- **The fight:** an **arena** zone starts it when you walk in (no arena: when it sees you or you shoot it) and the doors around it **seal** until it is defeated. An intro first (it roars, can't be hurt, the bar fills up), its **music**, then a **death**: explosions all over it, the doors open, the music fades, and a flag ("Kraid defeated", or your own) turns on for event cards.
- **Fight it now** in the tab: play, standing in front of the first one placed, and the fight starts.

In play the boss bar shows at the top (a white part catches up after each hit). If you die the fight starts over. Screen shake (roars, slams, explosions) can be turned down in Options > Graphics > Screen shake.

Event cards for bosses: **when** a boss fight starts / a boss reaches phase N / the countdown runs out; **do** start the boss fight of, shake the screen, start a countdown (a label and seconds, big at the top, red under 10 s: Mother Brain's escape is "when Mother Brain is defeated: start a countdown 'Escape!' 60 s" plus "when the countdown runs out: kill the player"), stop the countdown, play music, stop the music. Statues: "when Kraid is defeated: turn a flag on", and a phase or a door that waits for it.

## Lock-on (right mouse button)

Like Prime's L button. Four small corners show the enemy a press would pick (the one nearest the crosshair, in reach and in sight). Hold the right mouse button: the view swings onto it and stays there, with turning arrowheads around it. **A / D circle it** at the same distance, W / S walk to and from it, your shots go straight to it, missiles (and a little the beam) **steer after it**, and **jumping while circling is a dash** to the side. It lets go when you let go of the button, when the enemy is defeated (holding on picks the next one), or when it stays out of sight for a moment. In the **Scan Visor** holding to scan also keeps the view on what you scan.

Player > Weapons > Lock-on: on / off, hold or press-to-toggle, reach, how far from the crosshair it picks, how quickly the view swings, how long out of sight it may be, shots to the target, how hard beam shots and missiles steer, circling, the dash and its speed, the Scan Visor, "nothing to lock: hold the view" (like Prime, to strafe straight), and the two colours. Each enemy type has "Can be locked onto".

## The map (M): a hologram of what you have seen

- The map is built only from blocks Samus has actually looked at: every frame a few hundred rays go out from her eyes, and the first solid block each one hits is charted (with a small patch of the same surface, so grazing looks are not speckled). The floor you walk on is charted too.
- Press **M** in play (or **Map** in the pause menu). It is a 3D hologram: drag to turn it, right-drag to move it, the wheel zooms, **C** goes back to you, **M** or **Esc** closes it. You are the orange arrow.
- **Doors** you have seen show as panels in the door's own colour and at the door's size.
- **Map Stations** and **map cameras** (Tools > Objects > Add): both have an **ID**. Stand at a station and press **E** (a prompt shows when you face it): it wakes every map camera with the same ID; each looks all around from where it stands (as far as its "Sees" range) and charts everything it can see, doors included. Then the map opens and builds up. Map cameras are invisible in the game. A station with no camera of its ID looks around from itself. Each station downloads once per play; after that E just shows the map.
- Colours: **blue** is what you have seen yourself, **orange** (see-through, no fill) is what a Map Station showed you but you have not visited yet; it turns blue as soon as you see it. You are the pink arrow.
- On the map screen the **visor stays**: every HUD element and 3D helmet part has "Stays when paused" and "Stays on the map (M)" (Player > HUD). The starter visor keeps its frame, energy and struts on the map and hides the aim and the minimap there. The arm cannon is put away on the map.
- **Minimap on the HUD**: Player > HUD, add element > **Minimap** (the starter visor has one, bottom right). The same map, see-through, around you. Settings: how far around you it shows, how steeply it looks down (90: straight down), and whether it turns with you (you always point up) or keeps north up.
- The map is kept per world for the play session (it starts empty each time you press Play).

## Morph ball, bombs, breakable blocks

- **Morph ball** (C in Play mode): you roll up into a ball of the ball size (Game settings > Player rules), seen from behind; it rolls up to speed and keeps rolling when you let go. C again stands up (if there is room). From the start, or found later (a key pickup called "Morph Ball"). Its model: built in, or any model.
- **Bombs** (the fire button while rolled up): up to 3 at once, they go off after a short fuse, break blocks marked for bombs, open bomb doors (door "Opened by": Bomb), and throw the ball up (bomb jump height). Found later: a "Bombs" pickup.
- **Breakable blocks:** the new tool (T, slot 5) marks blocks (left click, hold and sweep to mark many; right click unmarks), or mark a whole selection box. Each mark says what breaks it: any shot (or bomb / missile), bombs only, missiles only, charged shots only, or one beam. The editor shows them with a coloured outline; in the game they look like any other block. Broken blocks come back when Play ends.
- **Editing:** doors cannot be opened while you edit, so you go straight through them.
- Sounds for the morph ball, bombs and breaking blocks: Player editor > Sounds.
