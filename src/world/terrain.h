// terrain.h : worlds made by a generator - hills, mountains, terraces, cliffs of coloured bands, caves, lakes of water or
// lava, spikes, veins, floating islands or a world of solid rock with caverns - from your own stack of layers.
// The settings are plain (block names, numbers) so they save with the world; TerrainGen is them made ready (block ids,
// the noise) and fills chunks. The same seed always makes the same world. Plain C++, tested on Linux.
#pragma once
#include "blocks.h"
#include "chunk.h"
#include <array>
#include <cstdint>
#include <istream>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

namespace mc {

struct TerrainLayer { std::string block; int thick = 1, vary = 0; };   // from the surface down: this block, this many blocks deep (give or take vary)
struct TerrainVein { std::string block; float amount = 0.03f, size = 3; int minY = -64, maxY = 64; };   // blobs of a block inside the rock
enum class TerrainMode { Land, Islands, Underground, COUNT };
enum class TerrainEdge { Cliffs, Void, Sea, COUNT };
const char* terrainModeName(TerrainMode m);
const char* terrainEdgeName(TerrainEdge e);

struct TerrainSettings {
    std::string type = "Overgrown";       // the preset it started from (only a name)
    uint32_t seed = 1;
    TerrainMode mode = TerrainMode::Land;
    // the shape of the land
    float base = 4;                       // the ground's middle height (y)
    float hills = 9, hillSize = 70;       // rolling hills: how high, how wide (blocks)
    float mountains = 34, mountainSize = 260, mountainShare = 0.3f;   // mountains: how high, how wide, how much of the land
    float ridged = 0;                     // 0 round mountains .. 1 sharp ridges and spires
    float roughness = 0.5f;               // small bumps (0 smooth .. 1 rugged)
    float terrace = 0, terraceSharp = 0.6f;   // steps every this many blocks (0: none); 1: sheer cliffs between them
    // what it is made of
    std::vector<TerrainLayer> layers = { { "grass", 1, 0 }, { "dirt", 3, 1 } };   // from the top down
    std::string rock = "stone";           // below the layers
    std::vector<std::string> bands;       // stripes in the rock (cliffs show them), in order; empty: none
    int bandThick = 3;
    std::vector<TerrainVein> veins;
    std::string bottomBlock = "stone"; int bottom = -64;   // the floor of the world (nothing below it)
    // lakes and seas
    std::string liquid = "water"; int liquidLevel = -2;    // filled up to this height ("": none)
    std::string liquidTop;                // its top frozen into this ("": open) - ice
    // caves
    float caves = 0.35f;                  // tunnels (0 none .. 1 many)
    float caverns = 0;                    // big rooms (0 none .. 1 many)
    float openings = 0.3f;                // how often caves break out at the surface (0 never .. 1 often)
    // spikes: pillars or crystals standing on the ground
    std::string spikeBlock; float spikes = 0, spikeHeight = 9;
    // floating islands
    float islands = 0.45f; int islandY = 30; float islandThick = 16;
    // underground: solid rock up to here
    int ceiling = 48;
    // the area: endless (0), or this far from the middle with an edge
    int size = 0; TerrainEdge edge = TerrainEdge::Cliffs;
};
TerrainSettings terrainPreset(const std::string& type);   // "Overgrown", "Frozen", "Volcanic", "Desert ruins", "Corrupted", "Floating islands", "Underground"
const std::vector<std::string>& terrainPresetNames();
std::vector<std::string> terrainBlocksUsed(const TerrainSettings& t);   // every block name it needs
// saving: "tg <key> <values>" lines (into world.txt)
std::string terrainText(const TerrainSettings& t);
bool terrainLine(TerrainSettings& t, const std::string& key, std::istream& in);   // one line's key and the rest of it; false: not a terrain key

class TerrainGen {
public:
    TerrainGen(const TerrainSettings& s, const BlockRegistry& r);
    void fill(Chunk& c) const;                       // (blocks only; the chunk is cleared first by the caller)
    double groundHeight(int x, int z) const;         // the land's height there (before caves and spikes; -1e9: nothing - void, between islands)
    int surfaceY(int x, int z) const;                // the top solid block there (with spikes, without caves); a very low number: none
    bool liquidAt(int x, int y, int z) const;        // water / lava fills this spot (if nothing solid is there)
    V3 spawnPoint() const;                           // a dry place to stand near the middle (feet)
    const TerrainSettings& settings() const { return s; }
    BlockId blockAt(int x, int y, int z) const;      // one block (slow: for tests and previews)
    std::vector<BlockId> blocksUsed() const;
private:
    struct Layer { BlockId b; int thick, vary; };
    struct Vein { BlockId b; float amount, size; int minY, maxY; };
    TerrainSettings s; uint32_t seed;
    std::vector<Layer> layers; BlockId rock, bottomB, liquid, liquidTop, spikeB; std::vector<BlockId> bands; std::vector<Vein> veins;
    struct Column { double h; double islandBottom; int spikeTop; bool any; };
    Column column(int x, int z) const;
    BlockId solidAt(const Column& col, int x, int y, int z) const;   // what is there if nothing carves it
    bool carved(const Column& col, int x, int y, int z) const;       // a cave
};

// previews for the editor (RGBA, W x H): the land from above (around cx, cz; perPixel blocks a pixel), and a cut through it
// along x at z = cz (heights yLow .. yHigh). The palette holds the blocks' colours, so a preview can be made on another
// thread while the game's blocks change.
struct TerrainPalette { std::unordered_map<BlockId, std::array<float, 3>> top, side; std::unordered_set<BlockId> liquid; };
TerrainPalette terrainPalette(const TerrainGen& g, const BlockRegistry& r);
std::vector<uint8_t> terrainPreviewMap(const TerrainGen& g, const TerrainPalette& pal, int W, int H, double cx, double cz, double perPixel);
std::vector<uint8_t> terrainPreviewCut(const TerrainGen& g, const TerrainPalette& pal, int W, int H, double cx, double cz, double perPixel, int yLow, int yHigh);

} // namespace mc
