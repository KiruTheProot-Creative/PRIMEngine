# G2: Objects (concept)

## Every object has
- Name, folder in the Outliner ("Objects", with foldable categories)
- Position, rotation, size. Snaps to the block grid by default, free placement with a key held.
- Look: block model (built from blocks), imported model (Blockbench, .obj / .glb), flat sprite, or invisible
- Collision made from the geometry by itself (see below)
- Its own settings, by kind

## Kinds
- Props: decoration (machines, pipes, consoles, rocks, furniture)
- Interactive: doors, switches / buttons / levers / pressure plates, moving platforms and elevators (follow a drawn path), breakable walls (bomb, shot, touch)
- Pickups: health, ammo, keys, upgrades; the game remembers what was collected
- Helpers (invisible while playing): extra spawn points, checkpoints / save stations, trigger zones (for G3 logic), teleporters / world exits (link worlds), point and spot lights, particle emitters
- Later (G5): enemies and NPCs on the same system

## Doors
- Energy forcefield in the doorway: glowing, see-through, coloured by what opens it
- Shot: the field dissolves, stays open a few seconds (or while the player is near), then fades back in
- Doors are fitted and centred in their frame automatically; the door has Width and Height settings (in blocks)
- Shooting before G5's weapons: a simple test beam in Play mode

## 3D viewer (props, doors, imported models)
- Orbit camera, floor grid, lit like the game
- Height reference next to the model: the player (standing) and the 1-block morph ball
- Size in blocks shown on every axis; scale by number or by dragging; "make it N blocks tall"
- Origin at the bottom centre by default
- Shows the collision boxes

## Collision from the geometry
- Block models: the exact block / shape boxes, merged into as few boxes as possible
- Imported models: voxelised at 1/16 block, merged into boxes; recomputed when rotated or scaled
- Per object: Exact (from geometry), One box, or None

## Tool slot 2: Selection box
- Drag a box over blocks: copy, paste, fill, delete, "Make into object"

## Build order (attempts)
1. Object system: objects in the world (save / load), place / move / rotate / scale, Outliner + Inspector, Selection box tool, block models, collision from geometry, drawn with shadows and AO
2. 3D viewer, model import (Blockbench, .obj / .glb), height reference, scaling
3. Interactive: forcefield doors (+ test beam), switches, moving platforms / elevators, breakable walls
4. Pickups, checkpoints, teleporters / world exits, trigger zones (+ upgrades)
5. Lights and particle emitters
