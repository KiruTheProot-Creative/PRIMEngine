#pragma once
// Terrain brushes (editor tools): shape + what it does. Plain C++, tested on Linux.
#include "game.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mc {

enum class BrushShape { Sphere, Cube, Cylinder, Dome, Diamond, Pyramid, COUNT };
enum class BrushOp { Add, Remove, Paint, Replace, Smooth, Raise, Lower, Flatten, Fill, Roughen, COUNT };

struct BrushInfo { const char* name; const char* help; };
const BrushInfo& shapeInfo(BrushShape s);
const BrushInfo& opInfo(BrushOp o);
BrushOp opposite(BrushOp o);              // right click: Add <-> Remove, Raise <-> Lower ...

struct BrushSettings {
    BrushShape shape = BrushShape::Sphere;
    BrushOp op = BrushOp::Raise;
    int radius = 4;                       // 0 .. 32 (0 = one block)
    int height = 4;                       // cylinder / cube: half height
    float strength = 0.5f;                // smooth, raise, lower, roughen: 0 gentle .. 1 strong
    int paintDepth = 1;                   // paint: how deep under the surface
    float noiseScale = 0.25f;             // roughen: small = big lumps
    BlockId replaceFrom = 0;              // replace: only this block type (0 = anything)
    bool liquidsToo = false;              // remove / smooth also take water and lava
};

bool insideBrush(const BrushSettings& b, int dx, int dy, int dz);   // offset from the brush centre
// what one dab of the brush at `c` changes (nothing is changed yet). flattenY: the height for Flatten / Fill.
std::vector<Edit> brushEdits(const World& w, const BrushSettings& b, I3 c, BlockId material, int flattenY, uint32_t seed = 1);
// outline segments of the brush shape around (0,0,0), in blocks, for the preview
std::vector<std::pair<V3, V3>> brushOutline(const BrushSettings& b);

// one mouse press = one stroke = one undo step, however many dabs it makes
struct BrushStroke {
    std::vector<Edit> edits; std::unordered_map<I3, size_t, I3Hash> index;
    void apply(World& w, const std::vector<Edit>& dab);   // sets the blocks, remembers what was there first
    std::vector<Edit> finish();                           // the whole stroke, for EditHistory::push
    bool empty() const { return edits.empty(); }
};

} // namespace mc
