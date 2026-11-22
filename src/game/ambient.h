// ambient.h : an ambient light fills the room it stands in. From its spot it spreads through the air (like the cook's rooms:
// air, glass, plants, liquids and shapes that aren't whole blocks let it through), up to the walls, closed doors (a door is always a wall
// for it) and its reach. Then it spills a few blocks further - through doorways and openings, doors too - fading out, so walking from
// one room into the next changes the light softly. Doors and solid blocks (any shape) are hard walls for it, the spill too.
#pragma once
#include "cook.h"
#include <functional>

namespace mc {

struct AmbientFill {
    std::vector<std::pair<I3, uint8_t>> cells;   // every block of air it reaches, and how much (255 its own room, less where it spills)
    I3 lo{ 0, 0, 0 }, hi{ -1, -1, -1 };           // their box
    bool stuck = false;                           // it stands in a solid block (nothing filled)
};
// through: blocks it goes through anyway (breakable ones: they can be gone, so the room goes on behind them)
// chunkAt: the chunks (null: not loaded) - a copy of them can be filled on another thread
using AmbChunkAt = std::function<const Chunk*(I3)>;
void fillAmbient(const BlockRegistry& reg, const AmbChunkAt& chunkAt, V3 at, float reach, int blend, const std::vector<CookDoor>& doors, AmbientFill& out,
                 size_t maxCells = 400000, const std::function<bool(int, int, int)>& through = nullptr);
inline void fillAmbient(World& w, V3 at, float reach, int blend, const std::vector<CookDoor>& doors, AmbientFill& out, size_t maxCells = 400000,
                        const std::function<bool(int, int, int)>& through = nullptr) {
    fillAmbient(w.reg, [&](I3 c) { return (const Chunk*)w.chunk(c); }, at, reach, blend, doors, out, maxCells, through);
}
// the box of blocks a fill can look at (its chunks: what a copy needs)
inline void ambientBox(V3 at, float reach, int blend, I3& lo, I3& hi) {
    int R = (int)std::ceil(std::max(1.f, reach)) + std::max(0, blend) + 2;
    lo = { (int)std::floor(at.x) - R, (int)std::floor(at.y) - R, (int)std::floor(at.z) - R }; hi = { lo.x + 2 * R, lo.y + 2 * R, lo.z + 2 * R };
}

} // namespace mc
