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

// a placed light: baked into the blocks around it (with shadows), optionally drawn as a glow
struct RoomLight { V3 p{ 0, 0, 0 }; V3 color{ 1, 0.78f, 0.5f }; float intensity = 1.3f, radius = 8; bool shadows = true, glow = true; };

// the atmosphere of a room: haze, ambient light, sun, bloom, floating particles, sky
enum Particles { PT_NONE, PT_DUST, PT_SPORES, PT_SNOW, PT_EMBERS, PT_RAIN, PT_COUNT };
enum SkyType { SKY_NONE, SKY_GRADIENT, SKY_CLOUDS, SKY_STARS, SKY_COUNT };
struct RoomMood {
    V3 haze{ 0.045f, 0.085f, 0.10f }; float hazeStart = 18, hazeEnd = 170, hazeAmount = 0.85f;
    V3 skyAmbient{ 0.74f, 0.80f, 0.82f }, groundAmbient{ 0.34f, 0.32f, 0.30f }; float ambient = 1.f;
    bool sun = false; float sunElev = 50, sunAzim = 35; V3 sunColor{ 1, 0.94f, 0.82f }; float sunStrength = 0.9f;
    float bloom = 0.85f, glowLight = 1.f;            // glowLight: how much lava / glowing blocks light their surroundings
    int particles = PT_DUST; float particleAmount = 0.4f; V3 particleColor{ 0.6f, 0.85f, 0.8f };
    int sky = SKY_NONE; V3 skyTop{ 0.03f, 0.06f, 0.09f }, skyHorizon{ 0.09f, 0.15f, 0.17f };
};
extern const char* const kParticleNames[PT_COUNT];
extern const char* const kSkyNames[SKY_COUNT];
enum MoodPreset { MP_DEFAULT, MP_TALLON_RAIN, MP_TALLON_SUN, MP_PHENDRANA, MP_MAGMOOR, MP_RUINS, MP_PHAZON, MP_STATION, MP_COUNT };
extern const char* const kMoodNames[MP_COUNT];
void moodPreset(int preset, RoomMood& m);
V3 sunDirection(const RoomMood& m);   // pointing from the ground toward the sun

struct Room {
    std::string name = "Untitled room";
    int sx = 64, sy = 32, sz = 64;          // size in blocks
    int ox = -32, oy = -1, oz = -32;        // world position of cell (0,0,0)'s minimum corner
    std::vector<uint8_t> cells, rots, sizes, mats;   // per cell: type, orientation, size, material
    std::vector<RoomLight> lights;
    RoomMood mood;

    void resize(int x, int y, int z, int oX, int oY, int oZ);
    void newFlat(int half = 16);                 // 64 x 32 x 64 with a (2*half)^2 metal floor (tests)
    void create(int w, int h, int d, bool floor, uint8_t floorMat);   // centred on x/z, floor under y = 0
    void resizeKeep(int w, int h, int d);         // new size, blocks kept in place (centred on x/z, same bottom)
    static constexpr int kMaxW = 512, kMaxH = 128;
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
    void buildMeshesBox(int x0, int y0, int z0, int x1, int y1, int z1, std::vector<SubMesh>& out) const;   // cells in [x0,x1) etc. (chunks)
    void buildMesh(MeshData& out) const;                  // everything in one mesh (tests, tools)
    bool raycast(V3 origin, V3 dir, float maxT, RayHit& hit) const;
    bool blocked(V3 from, V3 to, int skipX, int skipY, int skipZ) const;   // is anything between two points (ignoring one cell)?
    V3 lightAt(V3 p, V3 n, float ao, int cx, int cy, int cz, const std::vector<std::pair<V3, int>>& glowers) const;   // baked light at a surface point

    // Moves an upright box (feet position, half width, height) by vel*dt against the blocks. stepUp lets it climb
    // small ledges (stairs, slopes) while on the ground. Zeroes the blocked parts of vel.
    V3 moveBox(V3 feet, V3& vel, float halfW, float height, float dt, bool& onGround, float stepUp = 0.f) const;
    bool boxFree(V3 feet, float halfW, float height) const;

private:
    bool rayCore(V3 o, V3 d, float maxT, RayHit& hit, int skx, int sky, int skz, bool ground) const;
    size_t idx(int x, int y, int z) const { return ((size_t)(y - oy) * sz + (z - oz)) * sx + (x - ox); }
};

// world-space collision boxes of one block (for shapes these approximate slopes and curves with small steps)
void shapeBoxes(const Block& b, int x, int y, int z, std::vector<Box3>& out);

} // namespace px
