// lod.h : far chunks as simpler copies (levels of detail). Each chunk is first boiled down to 8 x 8 x 8 cells of 2 x 2 x 2 blocks
// (its "summary": which block mostly fills each cell). A level-k group is 2^k chunks a side, drawn as 16 x 16 x 16 cells of 2^k
// blocks: level 1 = 2 x 2 x 2 chunks (cells of 2), level 2 = 4 x 4 x 4 chunks (cells of 4), level 3 = 8 x 8 x 8 chunks (cells of 8).
// A cell is solid when at least half of the cells it is made of are (level by level, so a floor one block thick stays a floor);
// its look is the block most common in its top half (grass on top of a hill, not the dirt under it).
#pragma once
#include "mesher.h"
#include <functional>

namespace mc {

struct LodSummary {
    uint8_t kind = 0;               // 0 all air, 1 every cell is `uni`, 2 `cells`
    uint16_t uni = 0;
    std::vector<uint16_t> cells;    // 8^3 block types (0 air), x fastest, then z, then y
    uint16_t at(int x, int y, int z) const { return kind == 0 ? 0 : kind == 1 ? uni : cells[((size_t)y * 8 + z) * 8 + x]; }
};
constexpr uint16_t LOD_UNKNOWN = 0xFFFF;   // not summarized (yet)
LodSummary lodSummarize(const BlockRegistry& reg, const Chunk& c);

// the cells of a level-k group with a one-cell border round it: 18^3 (index ((y + 1) * 18 + (z + 1)) * 18 + (x + 1), x/y/z in -1..16).
// get(chunk position) -> its summary, or null when unknown (those cells: LOD_UNKNOWN)
void lodGroupCells(const BlockRegistry& reg, int k, I3 group, const std::function<const LodSummary*(I3)>& get, std::vector<uint16_t>& out);
// its triangles (positions relative to the group's corner, in blocks); liquids show as their flat top. false: nothing to draw
bool lodMesh(const BlockRegistry& reg, int k, const std::vector<uint16_t>& cells, MeshData& out);

inline int lodCellIdx(int x, int y, int z) { return ((y + 1) * 18 + (z + 1)) * 18 + (x + 1); }

} // namespace mc
