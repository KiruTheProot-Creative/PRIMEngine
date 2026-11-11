// room.h : a room is a grid of 1 m blocks (Minecraft scale). Platform-independent, no D3D.
//   - 13 block shapes (Copycats+ style), each with 24 orientations and an adjustable size
//   - storage + save/load (".room" text file, run-length encoded)
//   - mesh building with Minecraft-style corner shading baked into vertex colour
//   - ray casting (which block is under the crosshair) and box collision with step-up (walk up stairs/slopes)
#pragma once
#include "scene.h"
#include <string>
#include <vector>

namespace px {

enum BlockType : uint8_t {
    B_AIR = 0, B_CUBE, B_SLAB, B_STAIRS, B_SLOPE, B_CORNER, B_INNER, B_BEAM, B_PILLAR, B_PANEL, B_STEP, B_FRAME, B_ROUND, B_ARCH,
    B_COUNT
};

struct ShapeInfo {
    const char* name;
    bool upright;          // true: stands on the floor and faces away from you (stairs); false: attaches to the clicked face (slab)
    uint8_t def, mn, mx;   // size steps (mn == mx: no size)
    const char* sizeWhat;  // what the size changes
    int sizeDiv;           // size / sizeDiv = fraction of a block (8 = eighths, 16 = sixteenths, 0 = a count)
};
const ShapeInfo& shapeInfo(uint8_t type);

// orientation: which direction the shape's "up" points (0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z) and a quarter-turn spin
inline uint8_t makeRot(int up, int spin) { return (uint8_t)(up * 4 + (spin & 3)); }
inline int rotUp(uint8_t r) { return r / 4; }
inline int rotSpin(uint8_t r) { return r & 3; }
V3 dirVec(int d);
// Picks an orientation the way Minecraft does: attach to the clicked face, or stand upright facing away from the player.
uint8_t chooseRot(uint8_t type, int clickNx, int clickNy, int clickNz, V3 playerForward, int spinOffset, bool flip);

// materials: which texture a block shows on its top, sides and bottom (like Minecraft grass), plus shine and glow
enum MaterialId : uint8_t { MAT_METAL = 0, MAT_ROCK, MAT_DIRT, MAT_MOSS, MAT_SAND, MAT_SNOW, MAT_ICE, MAT_BASALT, MAT_LAVA, MAT_CHOZO, MAT_COUNT };
struct MaterialInfo { const char* name; uint8_t top, side, bottom; float env, glow; float swatch[3]; };
const MaterialInfo& materialInfo(uint8_t m);

struct Block {
    uint8_t type = B_AIR, rot = 8, size = 8, mat = MAT_METAL;
    bool operator==(const Block& o) const { return type == o.type && rot == o.rot && size == o.size && mat == o.mat; }
};
struct SubMesh { uint8_t tex = 0, mat = 0; float env = 0, glow = 0; MeshData mesh; };
struct Box3 { V3 mn, mx; };

struct RayHit { int x, y, z; int nx, ny, nz; float t; bool onGround; };   // onGround: hit the floor plane, not a block

struct Room {
    std::string name = "Untitled room";
    int sx = 64, sy = 32, sz = 64;          // size in blocks
    int ox = -32, oy = -1, oz = -32;        // world position of cell (0,0,0)'s minimum corner
    std::vector<uint8_t> cells, rots, sizes, mats;   // per cell: type, orientation, size, material

    void resize(int x, int y, int z, int oX, int oY, int oZ);
    void newFlat(int half = 16);
    bool inBounds(int x, int y, int z) const { return x >= ox && y >= oy && z >= oz && x < ox + sx && y < oy + sy && z < oz + sz; }
    uint8_t get(int x, int y, int z) const { return inBounds(x, y, z) ? cells[idx(x, y, z)] : (uint8_t)B_AIR; }
    Block block(int x, int y, int z) const { if (!inBounds(x, y, z)) return {}; size_t i = idx(x, y, z); return { cells[i], rots[i], sizes[i], mats[i] }; }
    bool set(int x, int y, int z, uint8_t type) { Block b; b.type = type; b.size = shapeInfo(type).def; return set(x, y, z, b); }
    bool set(int x, int y, int z, const Block& b) {
        if (!inBounds(x, y, z)) return false;
        size_t i = idx(x, y, z);
        cells[i] = b.type; rots[i] = b.type ? b.rot : 8; sizes[i] = b.type ? b.size : 8; mats[i] = b.type ? b.mat : 0; return true;
    }
    bool solid(int x, int y, int z) const { return get(x, y, z) != B_AIR; }
    bool fullFace(int x, int y, int z, int worldDir) const;   // does this cell's block fully cover its face in that direction?
    bool pointSolid(V3 p) const;
    size_t count() const;

    bool save(const std::string& path) const;
    bool load(const std::string& path, std::string& err);

    void buildMeshes(std::vector<SubMesh>& out) const;   // one sub-mesh per (material, top/side/bottom) texture
    void buildMesh(MeshData& out) const;                  // everything in one mesh (tests, tools)
    bool raycast(V3 origin, V3 dir, float maxT, RayHit& hit) const;

    // Moves an upright box (feet position, half width, height) by vel*dt against the blocks. stepUp lets it climb
    // small ledges (stairs, slopes) while on the ground. Zeroes the blocked parts of vel.
    V3 moveBox(V3 feet, V3& vel, float halfW, float height, float dt, bool& onGround, float stepUp = 0.f) const;
    bool boxFree(V3 feet, float halfW, float height) const;

private:
    size_t idx(int x, int y, int z) const { return ((size_t)(y - oy) * sz + (z - oz)) * sx + (x - ox); }
};

// world-space collision boxes of one block (for shapes these approximate slopes and curves with small steps)
void shapeBoxes(const Block& b, int x, int y, int z, std::vector<Box3>& out);

} // namespace px
