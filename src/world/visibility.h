#pragma once
// Cave culling (like Minecraft's): for each chunk, which of its six sides are joined by air (or anything you can see
// through) inside it; then, from the camera's chunk, a walk through the chunks that only goes on through such openings
// and always away from the camera. Chunks it never reaches are behind walls and are not drawn. Plain C++, tested.
#include "../core/math.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace mc {

// sides: 0 -X, 1 +X, 2 -Y, 3 +Y, 4 -Z, 5 +Z
constexpr uint16_t SEE_ALL = 0x7FFF;      // every side joined to every other (air, or not known yet)
int seePair(int a, int b);                 // which of the 15 bits joins sides a and b (a != b)
inline bool seeConnects(uint16_t mask, int a, int b) { return a == b || ((mask >> seePair(a, b)) & 1); }
// solid: CS^3 cells (x fastest, then z, then y, like Chunk), non-zero = you cannot see through it
uint16_t chunkSeeMask(const uint8_t* solid);

// the chunks you could see from cam (a chunk position): within r sideways and v up / down, inView(c) false for chunks
// outside the view (the walk does not go through those either); see(c): a chunk's mask (SEE_ALL when not known)
void visibleChunks(I3 cam, int r, int v, const std::function<uint16_t(I3)>& see, const std::function<bool(I3)>& inView, std::vector<I3>& out);

} // namespace mc
