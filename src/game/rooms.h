// rooms.h : the Room Manager's rooms. The game finds them by itself: a room is the air closed in by walls and doors
// (doors are where rooms end - every room has at least one). A room without a roof goes by its floor: the walkable
// layer of air over the ground, closed in by walls and doors, up to the height of its walls. Rooms can also be marked
// by hand (boxes: their air is the room). Each room has a name, a colour, its own ambient light and its own music; the
// map reveals a room at once when you walk in (and Map Stations reveal the rooms they are set to).
// Plain C++, tested on Linux.
#pragma once
#include "cook.h"
#include "objects.h"
#include "sound.h"
#include <array>
#include <climits>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace mc {

struct RoomBits {   // one chunk's blocks of a room (16 x 16 x 16 bits, x fastest, then z, then y)
    std::array<uint64_t, CS3 / 64> b{};
    static int at(int lx, int ly, int lz) { return (ly * CS + lz) * CS + lx; }
    bool has(int i) const { return (b[i >> 6] >> (i & 63)) & 1; }
    bool set(int i) { uint64_t m = 1ull << (i & 63); if (b[i >> 6] & m) return false; b[i >> 6] |= m; return true; }
};

struct RoomShape {
    bool roofless = false;          // found by its floor (no roof)
    bool partial = false;           // it reached chunks that were not loaded (found again when they are)
    I3 lo{ 0, 0, 0 }, hi{ -1, -1, -1 };
    size_t cells = 0;
    std::unordered_map<I3, RoomBits, I3Hash> bits;
    std::vector<int> doors;         // the doors it touches (their place in the doors list it was found with)
    std::vector<uint32_t> doorObjs; // ... and their objects
    bool empty() const { return cells == 0; }
    bool has(int x, int y, int z) const;
    bool add(int x, int y, int z);
    void clear() { *this = RoomShape{}; }
    void forEach(const std::function<void(int, int, int)>& f) const;
};

using RoomChunkAt = std::function<const Chunk*(I3)>;
using RoomCellFn = std::function<bool(int, int, int)>;

struct RoomRules {
    size_t maxCells = 1500000;      // a room bigger than this is the outside
    int maxSpan = 400;              // ... or wider / taller than this (blocks)
    int skyScan = 96;               // air this far straight up: the sky
    int band = 3;                   // no roof: the air up to this high over the ground is the floor's layer
    size_t maxFloorCells = 400000;  // ... and a floor bigger than this is the outside
    int maxHeight = 48;             // ... a roofless room reaches up to its walls' top, at most this high over its floor
};

struct RoomWorld {   // what a search looks at (all on the calling thread)
    const BlockRegistry* reg = nullptr;
    RoomChunkAt chunkAt;            // null: not loaded (a wall for now, the room is partial)
    std::vector<CookDoor> doors;    // door openings: walls for rooms (they are where rooms end)
    RoomCellFn through;             // goes through anyway (breakable blocks: the room goes on behind them)
    RoomCellFn blocked;             // a wall anyway (the boxes of rooms marked by hand)
    std::vector<std::pair<I3, I3>> edges;   // room edges (invisible boxes, only rooms see them): walls for the search - an open room's sea side, a canyon's mouth
    std::function<bool()> tired;    // true: stop this step here, go on next time (reading chunks from disk took this frame's share)
};

// one search for the room around a block of air - in steps (a budget of blocks each), so it never stutters
class RoomFlood {
public:
    enum Result { Running, Room, Outside, Solid, Unknown };   // Unknown: it reached chunks that are not loaded (try again later)
    void start(const RoomWorld& w, I3 seed, const RoomRules& rules = RoomRules{});
    Result step(size_t budget);                 // Running until it is done
    Result result() const { return res; }
    RoomShape shape;                            // a room: its blocks
    std::vector<I3> outsideCells() const;       // the outside: the floor blocks it went over (remembered: no need to search them again)
    void addOutsideTo(std::unordered_map<I3, RoomBits, I3Hash>& out) const;   // the same, straight into a chunk map (a whole chunk's bits at once: fast)
    I3 seed() const { return seed0; }
    I3 leakAt{ 0, 0, 0 }; int leak = 0;          // the outside: where its air got out (1 to the sky, 2 too big, 3 off an edge into the void, 4 into chunks not loaded)
    std::vector<I3> leakPath() const;            // from where it started to leakAt (the way the air got out)
    std::vector<I3> leakGaps(const std::vector<I3>& path);   // the holes on that way: a block-wide gap in a floor, ceiling or wall
private:
    enum Phase { P3D, PFloor, PUp, PDone };
    RoomWorld W; RoomRules R; Phase phase = PDone; Result res = Solid; I3 seed0{ 0, 0, 0 };
    std::unordered_map<I3, std::vector<int>, I3Hash> doorsByChunk;
    std::unordered_map<I3, std::array<uint8_t, CS3>, I3Hash> from;   // 3D search: the way each block was reached (1 + direction, 0: the start)
    std::unordered_map<I3, RoomBits, I3Hash> seen; std::vector<I3> q; size_t qi = 0; size_t count = 0; I3 lo, hi;
    std::unordered_map<int64_t, int> roofOf, wallTopOf;   // per column: the first wall above (INT_MAX: the sky), a wall's top
    std::unordered_map<int64_t, int> colTop;              // roofless: each floor column's highest floor-layer block
    std::vector<int64_t> cols; size_t ci = 0; int wallTop = INT_MIN, floorLo = INT_MAX;
    std::unordered_set<int> touched;
    const Chunk* lastCh = nullptr; I3 lastC{ INT_MIN, 0, 0 }; bool lastHave = false;
    uint8_t kind(int x, int y, int z, uint8_t& mask, int& door);   // 1 open, 2 wall, 3 door, 4 shape (mask: covered sides)
    bool open(int x, int y, int z) { uint8_t m; int d; uint8_t k = kind(x, y, z, m, d); return k == 1 || (k == 4 && m != 0x3F); }
    bool openOrVoid(int x, int y, int z) { if (!W.chunkAt || !W.chunkAt(chunkOf(x, y, z))) return true; return open(x, y, z); }   // (going down: chunks not loaded down there are the void)
    bool passes(int x, int y, int z, int dir, int nx, int ny, int nz);
    bool mark(int x, int y, int z);
    bool sky(int x, int y, int z);
    bool inFloorLayer(int x, int y, int z);
    int topOfWall(int x, int y, int z);
    bool beginFloor();
    void finish(Result r);
};

// ---- the rooms of a world: what you set (saved: rooms.txt in the world's folder) and what was found
struct RoomDef {
    uint32_t id = 0;                // never changes (the map, Map Stations and saves use it)
    std::string name;               // "": "Room <id>"
    uint32_t color = 0x60C0FF;
    I3 anchor{ 0, 0, 0 };           // a block of its air: where it is searched from again after changes
    std::vector<std::pair<I3, I3>> boxes;   // marked by hand: the air in these boxes is the room (lo, hi: blocks, hi included)
    bool showName = true;           // its name shows when you walk in
    bool open = false;              // an open room (outdoors, like Talon Overworld): searched with bigger limits - higher above its ground, bigger
    bool ambOn = false; AmbientSettings amb;   // its own ambient light
    int musicMode = 0;              // 0 keep what plays, 1 play its music (loops), 2 silence
    SoundUse music; float musicFade = 1.5f;
    bool marked() const { return !boxes.empty(); }
    std::string label() const { return name.empty() ? "Room " + std::to_string(id) : name; }
    bool inBoxes(int x, int y, int z) const;
};
struct RoomList {
    std::vector<RoomDef> rooms;
    uint32_t nextId = 1;
    int band = 3;                   // roofless rooms: the floor's layer (walls at least this high close a room)
    RoomDef* find(uint32_t id);
    const RoomDef* find(uint32_t id) const;
};
uint32_t roomColor(uint32_t id);    // a colour for a new room
bool saveRooms(const std::string& path, const RoomList& l);
bool loadRooms(const std::string& path, RoomList& l);

// the rooms found so far, and the searching (call tick every frame)
class RoomSet {
public:
    RoomList list;
    struct Live {
        RoomShape shape;
        std::unordered_map<I3, uint32_t, I3Hash> boxVer; uint64_t boxSig = 0;   // a marked room: each chunk's version its blocks were taken at (only changed chunks again)
        uint64_t sig = 0;           // the chunks and doors it was found with (changes: search again)
        bool searched = false;
        bool open = false;          // it is not closed any more (part of the outside)
        uint32_t joined = 0;        // it is one room with this one now (a wall or door between them is gone)
        int retry = 0;              // (not all of it loaded: checks to wait before trying again)
        int leak = 0; I3 leakAt{ 0, 0, 0 }; std::vector<I3> leakPath, gaps;   // not closed (or no roof): where its air gets out, the way, the holes on it
    };
    std::unordered_map<uint32_t, Live> live;
    struct Leak { I3 seed; int leak = 0; I3 at; std::vector<I3> path, gaps; };   // beside a door: air that got out (a room that should be closed, or just the outside)
    std::vector<Leak> leaks;
    uint32_t generation = 0;        // goes up when any room's blocks change
    bool listChanged = false;       // a room was found (save the list)
    uint32_t roomAt(int x, int y, int z) const;   // 0: none (the outside, solid, or not found yet)
    const Live* liveOf(uint32_t id) const { auto it = live.find(id); return it == live.end() ? nullptr : &it->second; }
    bool busy() const { return jobOn; }
    double maxMs = 1e9;   // a search's time a tick at most (as well as its step budget): some steps cost far more than others
    std::string busyText() const;
    // seeds: blocks to find rooms from (beside the doors, where you are); chunkVer: a chunk's version + 1 (0: not loaded);
    // checkNow: look whether found rooms changed (every half second or so); budget: blocks searched in this call
    void tick(const RoomWorld& w, const std::vector<I3>& seeds, const std::function<uint32_t(I3)>& chunkVer, bool checkNow, size_t budget);
    void forget() { live.clear(); outside.clear(); failed.clear(); jobOn = false; byChunk.clear(); generation++; }
    void forgetOutside() { outside.clear(); failed.clear(); leaks.clear(); }
    void researchAll() { for (auto& kv : live) kv.second.searched = false; forgetOutside(); }
    void edited() { forgetOutside(); for (auto& kv : live) if (kv.second.open || kv.second.shape.empty()) kv.second.searched = false; }   // blocks were changed somewhere
    static std::vector<I3> doorSeeds(const std::vector<CookDoor>& doors);   // a block on each side of each door
private:
    RoomFlood job; bool jobOn = false; uint32_t jobFor = 0; I3 jobSeed{ 0, 0, 0 };   // (0: finding a new room)
    std::unordered_map<I3, RoomBits, I3Hash> outside; std::unordered_set<I3, I3Hash> failed;
    std::unordered_map<I3, std::vector<uint32_t>, I3Hash> byChunk;
    uint64_t sigOf(const RoomShape& s, const std::function<uint32_t(I3)>& chunkVer, uint64_t doorsSig) const;
    uint64_t doorsSig(const RoomWorld& w) const;
    void index();
    void apply(uint32_t id, RoomFlood::Result r, RoomShape&& s, const std::function<uint32_t(I3)>& chunkVer, uint64_t dsig);
    uint64_t sigFor(const RoomDef& d, const RoomShape& s, const std::function<uint32_t(I3)>& chunkVer, uint64_t dsig) const;
    bool isOutside(I3 c) const;
};

} // namespace mc
