// world.h : an unlimited world of chunks. Chunks are loaded around the player, generated if new,
// and saved to region files when changed. Coordinates are plain ints (about +-2 billion blocks).
#pragma once
#include "chunk.h"
#include "storage.h"
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mc {

struct Generator {
    enum Kind { Flat = 0, Void = 1 };
    int kind = Flat;
    std::vector<std::pair<BlockId, int>> layers;   // flat world: (block, thickness) from the bottom up, ending at y = -1
    void fill(Chunk& c) const;
};

// the flat surface of a mirror: points p with p[axis] == coord; sign = which way it faces (+1 / -1)
struct MirrorPlane { bool valid = false; int axis = 0, sign = 1; double coord = 0; V3 center{ 0, 0, 0 }; double dist = 0; };

struct Box3 { V3 lo, hi; };   // an axis-aligned box in world coordinates (blocks)

struct Hit { int x, y, z; int nx, ny, nz; double t; double px, py, pz; };   // block, side hit (outward), distance, exact point

class World {
public:
    explicit World(const BlockRegistry& r) : reg(r) {}
    BlockId find(const std::string& name) const { return reg.find(name); }
    const BlockRegistry& reg;
    Generator gen;
    std::unique_ptr<RegionStore> store;               // null = nothing is saved (tests, previews)

    // chunks
    Chunk* chunk(I3 c) const { auto it = chunks.find(c); return it == chunks.end() ? nullptr : it->second.get(); }
    Chunk& loadChunk(I3 c);                           // from disk, else generated
    void unloadChunk(I3 c);                           // saves it first if you changed it
    size_t loadedCount() const { return chunks.size(); }

    // blocks (world coordinates)
    BlockId get(int x, int y, int z) const { const Chunk* c = chunk(chunkOf(x, y, z)); return c ? c->get(floorMod(x, CS), floorMod(y, CS), floorMod(z, CS)) : AIR; }
    bool loaded(int x, int y, int z) const { return chunk(chunkOf(x, y, z)) != nullptr; }
    void set(int x, int y, int z, BlockId id);       // loads the chunk if needed, marks meshes to rebuild

    // loads missing chunks near `center` (nearest first, at most maxLoads per call) and unloads far ones
    void stream(I3 center, int radiusH, int radiusV, int maxLoads);
    void saveAll();                                   // every changed chunk + region files

    // chunks whose mesh must be rebuilt (filled by set / load)
    std::unordered_set<I3, I3Hash> dirty;

    // queries
    bool raycast(V3 origin, V3 dir, double maxT, Hit& hit) const;
    bool solidAt(int x, int y, int z) const;          // unloaded chunks count as solid so you never fall through
    bool boxFree(V3 feet, double halfW, double height) const;   // also checks objectSolids
    std::vector<Box3> objectSolids;                   // collision boxes of the objects in the world (doors, props ...)
    // moves a box (feet position) with collision; stepUp > 0 lets a walker climb steps that low while on the ground
    V3 moveBox(V3 feet, V3& vel, double halfW, double height, double dt, bool& onGround, double stepUp = 0) const;

    std::unordered_map<I3, std::unique_ptr<Chunk>, I3Hash> chunks;
    void recountEmitters();                           // after blocks were edited (a block may glow or mirror now)
    // the nearest mirror surface that faces the camera and is in front of it (within maxDist blocks)
    MirrorPlane nearestMirror(V3 cam, V3 forward, double maxDist) const;

    // liquids: water and lava flow (fall first, then level out sideways); the amount of liquid never changes.
    // Only liquid near something that changed is looked at, so still pools cost nothing.
    std::unordered_set<I3, I3Hash> liquidActive;
    int tickLiquids();                                // one step (call 10 times a second); returns how many blocks changed
    int liquidAt(V3 p) const;                         // 0 = not in a liquid, 1 water, 2 lava (counts how full the block is)
    double liquidTimer = 0; uint64_t liquidTick = 0;
    bool emittersNear(I3 c) const;                    // any glowing block in this chunk or the 26 around it

private:
    void markAround(I3 c);
    void markAll27(I3 c);
    void touchLiquid(int x, int y, int z);
    int countEmitters(const Chunk& c) const;
    int countMirrors(const Chunk& c) const;
    std::vector<I3> offsets; int offH = -1, offV = -1;   // cached load order (nearest first)
};

} // namespace mc
