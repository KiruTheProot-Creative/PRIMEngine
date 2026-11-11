// room.h : a room is a grid of 1 m blocks (Minecraft scale). Platform-independent, no D3D.
//   - storage + save/load (".room" text file, run-length encoded)
//   - mesh building with Minecraft-style corner shading (ambient occlusion) baked into vertex colour
//   - ray casting (which block is under the mouse) and box collision (the player walking around)
#pragma once
#include "scene.h"
#include <string>
#include <vector>

namespace px {

enum BlockType : uint8_t { B_AIR = 0, B_CUBE = 1 };

struct RayHit { int x, y, z; int nx, ny, nz; float t; bool onGround; };   // onGround: hit the floor plane, not a block

struct Room {
    std::string name = "Untitled room";
    int sx = 64, sy = 32, sz = 64;          // size in blocks
    int ox = -32, oy = -1, oz = -32;        // world position of cell (0,0,0)'s minimum corner
    std::vector<uint8_t> cells;             // B_AIR or a block type, x fastest, then z, then y

    void resize(int x, int y, int z, int oX, int oY, int oZ);
    void newFlat(int half = 16);            // one layer of blocks under y = 0, (2*half)^2 big
    bool inBounds(int x, int y, int z) const { return x >= ox && y >= oy && z >= oz && x < ox + sx && y < oy + sy && z < oz + sz; }
    uint8_t get(int x, int y, int z) const { return inBounds(x, y, z) ? cells[idx(x, y, z)] : (uint8_t)B_AIR; }
    bool set(int x, int y, int z, uint8_t b) { if (!inBounds(x, y, z)) return false; cells[idx(x, y, z)] = b; return true; }
    bool solid(int x, int y, int z) const { return get(x, y, z) != B_AIR; }
    size_t count() const;

    bool save(const std::string& path) const;
    bool load(const std::string& path, std::string& err);

    void buildMesh(MeshData& out) const;    // visible faces only, AO in vertex colour, 1 texture repeat per block
    bool raycast(V3 origin, V3 dir, float maxT, RayHit& hit) const;

    // Moves an upright box (feet position, half width, height) by vel*dt against the blocks.
    // Returns the moved feet position; zeroes the blocked parts of vel; onGround = standing on something.
    V3 moveBox(V3 feet, V3& vel, float halfW, float height, float dt, bool& onGround) const;
    bool boxFree(V3 feet, float halfW, float height) const;

private:
    size_t idx(int x, int y, int z) const { return ((size_t)(y - oy) * sz + (z - oz)) * sx + (x - ox); }
};

} // namespace px
