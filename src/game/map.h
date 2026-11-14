#pragma once
// The map: built from what Samus has actually seen. Each frame a few hundred rays go out from the eyes; the first
// solid block each one hits is marked as seen (with a small patch of the same surface around it, so grazing looks
// do not leave it speckled). The floor under your feet is marked as you walk. Map cameras (Map Stations) look all
// around from where they are and mark everything they see. The map is drawn from the seen blocks' open faces,
// merged into big panels. Plain C++, tested on Linux.
#include "../world/world.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mc {

struct MapQuad {          // one panel of the map (a merged rectangle of block faces)
    float p[4][3];        // corners (world), around the rectangle
    float n[3];           // which way it faces (out of the block, into the open)
    float w, h;           // its size in blocks (for the grid lines)
    float born;           // when this part of the map was seen (for the fade-in)
};

struct SeenMap {
    struct ChunkBits { uint64_t bits[CS3 / 64] = {}; double changed = 0; int count = 0; };
    std::unordered_map<I3, ChunkBits, I3Hash> chunks;
    std::unordered_set<I3, I3Hash> dirty;     // chunks whose panels need building again
    std::unordered_set<uint32_t> doors;       // doors (by object id) that have been seen: drawn in their colour and size
    size_t total = 0;                         // blocks seen

    bool seen(int x, int y, int z) const;
    bool mark(int x, int y, int z, double now);           // true if it was new
    void clear() { chunks.clear(); dirty.clear(); doors.clear(); total = 0; }

    // a look along a ray: the first solid block (within range) is seen, and `patch` blocks around it on the same surface
    bool look(const World& w, V3 eye, V3 dir, double range, int patch, double now);
    // the eyes: `rays` rays spread over the view (yaw / pitch in radians, fov vertical, aspect), shuffled by `frame`
    int lookAround(const World& w, V3 eye, double yaw, double pitch, double fovY, double aspect, int rays, double range, uint32_t frame, double now);
    void floorUnder(const World& w, V3 feet, int radius, double now);   // the ground you walk on
    int revealFrom(const World& w, V3 at, double range, int rays, double now);   // a map camera: all around
};

// nothing solid between the eye and that point (within range): it can be seen (doors are checked this way)
bool canSee(const World& w, V3 eye, V3 target, double range);

// the panels of one chunk's seen blocks: their faces that open into air, merged into rectangles
std::vector<MapQuad> mapPanels(const World& w, const SeenMap& m, I3 chunk);

bool saveSeenMap(const std::string& path, const SeenMap& m);
bool loadSeenMap(const std::string& path, SeenMap& m);

} // namespace mc
