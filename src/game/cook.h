// cook.h : "cooking" a game for export - first step: finding the spaces of the world.
// The air of a chosen area is sorted into the outside (air the sky reaches) and rooms (closed-off air: halls, corridors,
// caves). Doors are where rooms meet (portals); zones can make a room by hand. Later steps put reflection probes and baked
// light into each room, and use the portals to skip drawing rooms you can't see.
#pragma once
#include "../world/world.h"
#include "environment.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace mc {

struct CookArea {                     // chunk columns from (cx0, cz0) to (cx1, cz1), all included, between the heights y0 .. y1 (blocks, y1 not included)
    int cx0 = 0, cz0 = 0, cx1 = 0, cz1 = 0;
    int y0 = 0, y1 = 128;
    int columns() const { return (cx1 - cx0 + 1) * (cz1 - cz0 + 1); }
};
struct CookDoor { I3 lo, hi; std::string name; };   // a door's opening (blocks, hi included): the air stops there, the rooms on both sides meet
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
    std::unordered_map<I3, std::vector<uint16_t>, I3Hash> cells;   // per chunk of the area: the room of each block (32 x 32 x 32, x fastest), 0 = solid
    int roomAt(int x, int y, int z) const;                         // 0: solid, outside the area, or not cooked
    int count(bool outsideToo = true) const;
};

bool cookOpen(const World& w, BlockId v);   // air for cooking: not a full opaque block (glass, plants, slopes, water: open)
// sorts the air of the area (its chunks are loaded first). Zones win over everything (their air is their room); then the outside
// floods from every block the sky reaches; what is left falls into rooms (blocks touching through a face are one room)
CookRooms findRooms(World& w, const CookArea& a, const std::vector<CookDoor>& doors, const std::vector<EnvZone>& zones);
bool saveCookRooms(const std::string& path, const CookArea& a, const CookRooms& r);   // the cook's results (cook/rooms.txt)

} // namespace mc
