#pragma once
// The map: built from the rooms you have been in. Walking into a room (Room Manager) puts its walls, floor and ceiling on
// the map at once; Map Stations put the rooms they are set to on it (downloaded: not visited yet). The map is drawn from
// those blocks' open faces, merged into big panels. Plain C++, tested on Linux.
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
    bool mine;            // seen with your own eyes (false: only downloaded at a Map Station, not visited yet)
};

struct SeenMap {
    struct ChunkBits { uint64_t bits[CS3 / 64] = {}; uint64_t mine[CS3 / 64] = {}; double changed = 0; int count = 0, mineCount = 0; };
    std::unordered_map<I3, ChunkBits, I3Hash> chunks;
    std::unordered_set<I3, I3Hash> dirty;     // chunks whose panels need building again
    std::unordered_set<uint32_t> doors;       // doors (by object id) that have been seen: drawn in their colour and size
    size_t total = 0;                         // blocks seen

    size_t mineTotal = 0;                     // of those, seen with your own eyes

    bool seen(int x, int y, int z) const;                 // on the map (seen or downloaded)
    bool mine(int x, int y, int z) const;                 // seen with your own eyes
    bool mark(int x, int y, int z, double now, bool own = true);   // true if the map changed (new, or now seen yourself)
    void clear() { chunks.clear(); dirty.clear(); doors.clear(); total = mineTotal = 0; }

};

// nothing solid between the eye and that point (within range): it can be seen (doors are checked this way)
bool canSee(const World& w, V3 eye, V3 target, double range);

// the panels of one chunk's seen blocks: their faces that open into air, merged into rectangles (yours and downloaded apart)
std::vector<MapQuad> mapPanels(const World& w, const SeenMap& m, I3 chunk);

bool saveSeenMap(const std::string& path, const SeenMap& m);
bool loadSeenMap(const std::string& path, SeenMap& m);

} // namespace mc
