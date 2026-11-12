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
