// chunk.h : 16 x 16 x 16 blocks. The world is an unlimited sparse grid of these.
#pragma once
#include "../core/math.h"
#include "blocks.h"
#include <array>

namespace mc {

constexpr int CS = 16;                 // chunk size
constexpr int CS3 = CS * CS * CS;

inline I3 chunkOf(int x, int y, int z) { return { floorDiv(x, CS), floorDiv(y, CS), floorDiv(z, CS) }; }

struct Chunk {
    I3 pos;                            // chunk coordinates (block = pos * 16 + local)
    std::array<BlockId, CS3> b{};      // x fastest, then z, then y
    int nonAir = 0;
    bool modified = false;             // differs from what the generator makes: must be saved
    uint32_t version = 0;              // bumped on every change (stale meshes are thrown away)
    int emitters = 0;                  // glowing blocks in it (kept by World): light is only worked out near these

    static int idx(int lx, int ly, int lz) { return (ly * CS + lz) * CS + lx; }
    BlockId get(int lx, int ly, int lz) const { return b[idx(lx, ly, lz)]; }
    void set(int lx, int ly, int lz, BlockId id) {
        BlockId& c = b[idx(lx, ly, lz)];
        if (c == id) return;
        nonAir += (id != AIR) - (c != AIR);
        c = id; version++;
    }
    void recount() { nonAir = 0; for (auto v : b) nonAir += v != AIR; }
};

} // namespace mc
