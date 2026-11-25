# PRIMEngine notes: ideas for later

- **Liquid editor:** make your own liquids (colour, how thick / fast they flow, glow, what they turn into when they touch other liquids).
- **Voxel ray tracing** (optional setting; Kiru has an RTX 2070): bounced light, soft lamp shadows, reflections of everything (also behind you), traced through a 3D copy of the nearby blocks. Works on any DirectX 11 card. Real RTX (DirectX 12 / DXR) maybe much later.
- **Sound:** thunder, rain, footsteps, water.
- **Polish:** desert and Mars still too pale; puddles very glassy; metal floor reflection faint; more detail in clouds up close.
- **Blockbench shapes:** import custom block shapes.
- **Samus:** first person, morph ball (one block tall, so tunnels are one block), weapons.
- Several true mirrors at once.

## Custom block types and shapes from FBX files
- FBX models can be imported as objects now (G2). Still to do: use them as new block shapes (and whole block types), next to the built-in shape library.

## Objects
- Lamps inside block models do not glow or light things yet (the model mesh has no light sources).
- Undo / redo for object changes (place, move, delete, make into object).

## Performance and VR (the SourceVox road)
- **VR rendering benchmark** (not a working VR mode): the same 30-second benchmark flight, but drawn the way a headset needs it. That means two views each frame (left and right eye, about 6.4 cm apart), each at a headset-like size (around 2000 x 2200 per eye, with a size setting), and hard frame budgets: 11.1 ms (90 Hz), 8.3 ms (120 Hz), 6.9 ms (144 Hz). The result shows how many frames missed the budget. Shown on the monitor side by side. Later it can test VR-only speed-ups: drawing both eyes in one pass, shared shadows and lists for both eyes, and fewer pixels at the edges of the view.
- Then: a thin rendering layer, an experimental DirectX 12 branch, and PCVR through OpenXR (PC only, not standalone Quest).
- Goal on the RTX 2070 laptop: a steady 144 fps on the monitor, with the slowest 1% of frames at 120 fps or faster. VR needs more headroom than that.
- Animated emission (arm cannon idle pulse, charge glow, pickup pulse): after the speed work.
- Open world: map regions outdoors, a terrain generator, far enemies asleep, fast streaming, music by area.
