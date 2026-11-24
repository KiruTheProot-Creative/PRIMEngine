// cook.h : "cooking" a game for export - first step: finding the spaces of the world.
// The air of a chosen area is sorted into the outside (air the sky reaches) and rooms (closed-off air: halls, corridors,
// caves). Doors are where rooms meet (portals); zones can make a room by hand. Later steps put reflection probes and baked
// light into each room, and use the portals to skip drawing rooms you can't see.
#pragma once
#include "../world/world.h"
#include "environment.h"
#include "../core/math.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace mc {

struct CookArea {                     // chunk columns from (cx0, cz0) to (cx1, cz1), all included, between the heights y0 .. y1 (blocks, y1 not included)
    int cx0 = 0, cz0 = 0, cx1 = 0, cz1 = 0;
    int y0 = 0, y1 = 128;
    int columns() const { return (cx1 - cx0 + 1) * (cz1 - cz0 + 1); }
};
struct CookDoor { I3 lo, hi; std::string name; uint32_t obj = 0; };   // (obj: the door object)   // a door's opening (blocks, hi included): the air stops there, the rooms on both sides meet
struct CookRoom {
    int id = 0;                       // 1, 2, 3 ... (0: no room / solid)
    bool outside = false;             // the air the sky reaches (there is one outside, id 1, if any)
    std::string zone;                 // made by a zone (its name), else ""
    I3 lo, hi;                        // its blocks' bounds
    int cells = 0;                    // how many air blocks
    bool pocket() const { return !outside && zone.empty() && cells < 8; }   // a tiny gap in a wall (no probes for it)
};
struct CookPortal { int a = 0, b = 0; I3 lo, hi; std::string via; };   // where two spaces meet (a door's name, or "zone" where a zone's edge is open)

struct CookRooms {
    std::vector<CookRoom> rooms;      // rooms[i].id == i + 1
    std::vector<CookPortal> portals;
    std::unordered_map<I3, std::vector<uint16_t>, I3Hash> cells;   // per chunk of the area: the room of each block (16 x 16 x 16, x fastest), 0 = solid
    int roomAt(int x, int y, int z) const;                         // 0: solid, outside the area, or not cooked
    int count(bool outsideToo = true) const;
};

bool cookOpen(const World& w, BlockId v);
bool cookOpenReg(const BlockRegistry& reg, BlockId v);   // air for cooking: not a full opaque block (glass, plants, slopes, water: open)
// sorts the air of the area (its chunks are loaded first). Zones win over everything (their air is their room); then the outside
// floods from every block the sky reaches; what is left falls into rooms (blocks touching through a face are one room)
CookRooms findRooms(World& w, const CookArea& a, const std::vector<CookDoor>& doors, const std::vector<EnvZone>& zones);
bool saveCookRooms(const std::string& path, const CookArea& a, const CookRooms& r);   // the cook's results (cook/rooms.txt)

// ---- baked light: what the lamp blocks give each block of air in the area (shadows from the blocks between, like their real light)
struct CookLamp { V3 c, half; double range = 0; V3 col; };   // a lamp light: a glowing box (centre, half size; half 0: a point), its reach, its colour
struct CookLit {
    I3 lo; int X = 0, Y = 0, Z = 0;   // the grid's corner (blocks) and size
    std::vector<float> rgb, dir;      // 3 floats per block (x fastest, then z, then y): the light, and where it mostly comes from
    size_t at(int x, int y, int z) const { return ((size_t)y * Z + z) * X + x; }
};
CookLit bakeLight(World& w, const CookArea& a, const std::vector<CookLamp>& lamps, int threads = 1, size_t maxCells = 24u << 20);

// ---- reflection probes: where each room's picture is taken (the middle of an air block of the room, near its centre).
// The outside and tiny gaps get none; the biggest rooms first, up to maxProbes. (room id, the spot)
std::vector<std::pair<int, V3>> probeSpots(const CookRooms& r, int maxProbes = 64);

// ---- what can be seen: each chunk's rooms (its air and the air just around it), and which rooms show through the portals
std::unordered_map<I3, std::vector<uint16_t>, I3Hash> chunkRooms(const CookRooms& r);
// vp: the camera's view-projection (camera-relative, row vectors); cam: where it is. Starts in the camera's room, goes through
// each portal that is on screen and inside what the room before showed of the screen. vis[id]: 1 seen (size rooms + 1)
void visibleRooms(const CookRooms& r, const float vp[16], V3 cam, std::vector<uint8_t>& vis);

} // namespace mc
