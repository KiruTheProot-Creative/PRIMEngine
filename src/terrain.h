// terrain.h : generates natural ground, caves and ruins into a room, as ordinary blocks you can keep editing.
#pragma once
#include "room.h"

namespace px {

enum TerrainKind { TK_HILLS, TK_CAVERN, TK_CANYON, TK_CRATER, TK_ICE, TK_LAVA, TK_RUINS, TK_COUNT };
extern const char* const kTerrainNames[TK_COUNT];

struct TerrainParams {
    int kind = TK_HILLS;
    uint32_t seed = 1;
    // shape (heights in blocks above the bottom of the room)
    float base = 4, height = 8, scale = 22, rough = 0.5f, cliffs = 0, overhang = 0;
    // caves
    int caves = 2; float caveSize = 2.f; int tunnels = 2;          // tunnels are 1 block wide: morph ball only
    // ceiling (caverns)
    bool ceiling = false; float ceilHeight = 15;
    // fluid (lava) filling low ground; < 0 = none
    float fluidLevel = -1; uint8_t fluidMat = MAT_LAVA;
    // layers
    uint8_t top = MAT_MOSS, mid = MAT_DIRT, deep = MAT_ROCK; int midDepth = 3;
    // extras
    bool smooth = true;       // slopes and corners on terrain edges
    int spikes = 0;           // ice spikes
    int ruins = 0;            // broken Chozo walls and pillars
    // canyon / crater shape
    float featDepth = 10, featSize = 7;
    // area (inclusive block coordinates); whole room when wholeRoom
    bool wholeRoom = true; int x0 = -16, z0 = -16, x1 = 15, z1 = 15;
};

void terrainPreset(int kind, TerrainParams& p);          // keeps seed and area, sets everything else
void generateTerrain(Room& room, const TerrainParams& p);   // replaces everything inside the area

} // namespace px
