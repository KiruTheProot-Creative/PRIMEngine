// blocks.h : the block registry. Every block has a name, a shape, how it is drawn, and a texture per face.
// Textures are named images (any size). The renderer puts them all into one texture array at a common
// resolution, referenced by layer number. Block ids never change once given (worlds store them).
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace mc {

// A stored block value: bits 0-15 = block type, 16-24 = shape (0 = the block's own), 25 = HOT (see below), 26-30 = rotation (0-23), 31 = WET.
using BlockId = uint32_t;
constexpr BlockId AIR = 0;
constexpr int MAX_BLOCK_TYPES = 65535;
inline BlockId typeOf(BlockId v) { return v & 0xFFFFu; }
inline int shapeOf(BlockId v) { return (int)((v >> 16) & 0x1FFu); }
inline int rotOf(BlockId v) { return (int)((v >> 26) & 31u); }
// bit 31: a block that is not a full cube (a slope, a slab, a plant ...) standing in water - water fills the rest of its space
constexpr BlockId WET = 0x80000000u;
inline bool wetOf(BlockId v) { return (v & WET) != 0 && v != 0xFFFFFFFFu; }   // standing in water or lava
constexpr BlockId HOT = 1u << 25;        // with WET: the block stands in lava, not water (shapes use bits 16-24)
constexpr BlockId SOAK = WET | HOT;
inline int soakOf(BlockId v) { return wetOf(v) ? ((v & HOT) ? 2 : 1) : 0; }   // 0 dry, 1 in water, 2 in lava
// liquids keep their amount (1..16, 16 = a full block) where other blocks keep their shape
inline int liquidLevel(BlockId v) { int l = (int)((v >> 16) & 31u); return l == 0 || l > 16 ? 16 : l; }
inline BlockId makeBlock(BlockId type, int shape = 0, int rot = 0) { return (type & 0xFFFFu) | ((uint32_t)(shape & 0x1FF) << 16) | ((uint32_t)(rot & 31) << 26); }

enum class Render : uint8_t {
    Air,      // nothing
    Opaque,   // solid pixels only (a full cube hides the faces of its neighbours)
    Cutout,   // has see-through pixels (glass, leaves, plants)
    Liquid,   // water / lava: drawn and simulated as a liquid
};
enum class Shape : uint8_t {
    Cube,     // a full block
    Cross,    // two crossed planes through the middle (flowers, tall grass)
    Plane,    // one flat plane stuck to the face it was placed against (ladders, vines, posters)
};
enum Face { F_PX = 0, F_NX, F_PY, F_NY, F_PZ, F_NZ };   // +X -X +Y(top) -Y(bottom) +Z -Z

// one face glowing on its own (Blocks > edit a block > the glow button under a texture): only this block's face, not every
// block using that texture. It gets its own copy of the texture (a hidden "@glow" layer) with these glow settings.
struct FaceGlow {
    int mode = 0;                   // 0 off (the texture's own material), 1 the whole face glows, 2 only its bright parts, 3 an emission picture
    uint32_t color = 0xFFFFFF;      // tint (white: its own colours)
    float strength = 1.0f;          // how bright
    float from = 0.5f;              // bright parts: only pixels brighter than this
    int lights = 0;                 // lights up around it (0 no .. 15 like a lamp)
    std::string map;                // emission picture: its file in the project's emission folder
    bool on() const { return mode > 0; }
    bool operator==(const FaceGlow& o) const { return mode == o.mode && color == o.color && strength == o.strength && from == o.from && lights == o.lights && map == o.map; }
    bool operator!=(const FaceGlow& o) const { return !(*this == o); }
};

// a face that varies from block to block (Blocks > edit a block > Vary under a texture): turned, mirrored and / or another texture.
// What each block gets comes from its place in the world, so the same spot always looks the same (no flicker, same after loading).
struct FaceVary {
    int turns = 0;                       // 0 never, 1 half turns (0 or 180 degrees: bricks, panels), 2 quarter turns (stone, dirt, sand)
    bool mirror = false;                 // half of them mirrored too
    float ownWeight = 1;                 // how often its own texture comes up, against the others' weights
    std::vector<std::string> others;     // other textures it can be
    std::vector<float> weights;          // ... how often each comes up
    uint32_t seed = 0;                   // Shuffle: another pick everywhere
    bool on() const { return turns > 0 || mirror || !others.empty(); }
    bool operator==(const FaceVary& o) const { return turns == o.turns && mirror == o.mirror && ownWeight == o.ownWeight && others == o.others && weights == o.weights && seed == o.seed; }
    bool operator!=(const FaceVary& o) const { return !(*this == o); }
};

// a face that shows one tile of a bigger picture (Blocks > edit a block > Multi under a texture): blocks side by side show the whole
// picture, w tiles across and h down (a 48 x 48 picture at 3 x 3: nine 16 x 16 tiles), repeating every w x h blocks
struct FaceMulti {
    int w = 1, h = 1;
    bool on() const { return w > 1 || h > 1; }
    bool operator==(const FaceMulti& o) const { return w == o.w && h == o.h; }
    bool operator!=(const FaceMulti& o) const { return !(*this == o); }
};
// which way a face's texture runs in the world (textures stay world-aligned): +u along axis ua (sign us), +v along axis va (sign vs)
inline void faceAxes(int face, int& ua, int& us, int& va, int& vs) {
    switch (face) {
    case 0: ua = 2; us = -1; va = 1; vs = -1; break;   // +X: u toward -Z, v down
    case 1: ua = 2; us = 1; va = 1; vs = -1; break;    // -X: u toward +Z
    case 4: ua = 0; us = 1; va = 1; vs = -1; break;    // +Z: u toward +X
    case 5: ua = 0; us = -1; va = 1; vs = -1; break;   // -Z: u toward -X
    default: ua = 0; us = 1; va = 2; vs = 1; break;    // top and bottom: u toward +X, v toward +Z
    }
}

struct BlockDef {
    std::string name;
    Render render = Render::Opaque;
    Shape shape = Shape::Cube;
    std::string faces[6];                // texture name per face (planes use faces[0])
    int tex[6] = { 0, 0, 0, 0, 0, 0 };   // texture layer per face (filled by resolve())
    bool solid = true;                   // you bump into it
    bool cullSame = false;               // hide faces between two blocks of this kind (glass)
    int light = 0;                       // glows: 0 = no, 1..15 = how far its light reaches (lamp = 15)
    uint32_t lightColor = 0xFFB873;      // colour of that light, 0xRRGGBB (warm lamp orange)
    // light levels (0..15) per colour channel a glowing block starts with: its brightest channel = light
    int autoLight = 0; uint32_t autoLightColor = 0;   // (not saved) light from its textures' emission maps, set by the editor
    bool autoReal = true;                            // (not saved) ... a real-time light or the classic spreading one (the texture's material says)
    bool isReal() const { return light > 0 ? realLight : autoReal; }   // its light is a real-time one (its own Glows setting first, else its emission map's)
    int lightOf() const { return light > 0 ? light : autoLight; }   // how far its light reaches (its own setting first)
    void lightLevels(int out[3]) const {
        int L = lightOf(); uint32_t col = light > 0 ? lightColor : autoLightColor;
        int c[3] = { (int)(col >> 16) & 255, (int)(col >> 8) & 255, (int)col & 255 }, m = std::max(1, std::max(c[0], std::max(c[1], c[2])));
        for (int i = 0; i < 3; i++) out[i] = (L * c[i] + m / 2) / m;
    }
    bool waves = false;                  // sways in the wind (leaves, plants)
    bool connects = false;               // its texture creeps over its neighbours' faces (any block): a jagged, speckled border instead of a straight seam
    int connectWidth = 3;                // ... this many pixels deep (1..8)
    bool grass = false;                  // grass blades grow on its top (pixel-wide, from its top texture's colours)
    float grassHeight = 0.4f;            // ... how tall (blocks)
    float grassDensity = 0.6f;           // ... how many of the top's pixels have one (0..1)
    float shine = 0;                     // how much it reflects: 0 matt (dirt) .. 1 polished (metal, ice)
    bool realLight = true;               // its glow is a real light (lights by direction, casts shadows) when the game's "Lamp blocks" says so; off: the classic spreading light (liquids always are)
    float roughness = -1;                // how blurry its reflections are: 0 sharp .. 1 very soft; below 0: its textures' own (Materials)
    bool mirror = false;                 // reflects like a mirror
    bool texTurns = true;                // a turned block turns its textures too (false: they stay lined up with the world)
    int liquid = 0;                      // 0 not a liquid, 1 water, 2 lava
    bool spring = false;                 // a liquid that never runs dry (keeps pouring out)
    std::string category = "Building";   // folder in the asset browser
    std::string surface;                 // what it is made of, for Samus's steps on it (Surfaces tab): Rock, Metal, Water ... ("" none)
    FaceGlow glow[6];                    // faces that glow on their own
    FaceVary vary[6];                    // faces that vary from block to block
    std::string overlay[6];              // a texture laid over a face (its see-through parts show the face below): never turned or varied - grass hanging over a dirt side that varies
    int overlayTex[6] = { 0, 0, 0, 0, 0, 0 };   // (its layer: filled by resolve(); 0 none)
    std::vector<std::pair<int, float>> varyTex[6];   // (not saved) the other textures' layers and weights (filled by resolve())
    FaceMulti multi[6];                  // faces that show a tile of a bigger picture (never turned or varied)
    bool multiWorld = false;             // ... the picture lines up with the world grid (false: with the corner of each group of these blocks)
    int multiTex[6] = { 0, 0, 0, 0, 0, 0 };   // (its first tile's layer, the tiles row by row: filled by resolve(); 0 none)
    bool hasMulti() const { for (int f = 0; f < 6; f++) if (multi[f].on() && multiTex[f] > 0) return true; return false; }
    bool varies() const { for (const FaceVary& v : vary) if (v.on()) return true; return false; }
    bool exists = true;                  // false = a deleted id (kept so old worlds stay readable)
};

struct TexImage {
    std::string name; int w = 0, h = 0; std::vector<uint8_t> rgba; bool transparent = false;
    std::string baseName; FaceGlow glow;   // a glowing face's own copy (hidden from the texture lists): the texture it copies, and how it glows
    bool multiOnly = false;              // (only cut into tiles: its own size does not set the texture array's)
    bool variant() const { return !baseName.empty(); }
};

class BlockRegistry {
public:
    static constexpr int TEX = 16;       // size of the built-in pixel-art textures
    BlockRegistry();                     // block 0 is air, texture 0 is the "missing" checkerboard

    // textures
    int setTexture(const std::string& name, int w, int h, std::vector<uint8_t> rgba);   // adds or replaces; returns the layer
    int addTexture(const std::vector<uint8_t>& rgba16);    // unnamed TEX x TEX texture (tests)
    bool removeTexture(const std::string& name);
    int textureIndex(const std::string& name) const;       // -1 if there is none
    size_t textureCount() const { return textures.size(); }
    const TexImage& image(int layer) const { return textures[layer]; }
    int layerSize() const;                                 // common resolution of the texture array
    std::vector<uint8_t> layer(int layer, int size) const; // the texture scaled to size x size (RGBA)

    // blocks
    BlockId add(const BlockDef& d);                         // a new id, never one used before (0 if full)
    void put(BlockId id, const BlockDef& d);                // at a fixed id (loading, editing)
    void remove(BlockId id);
    const BlockDef& get(BlockId v) const { BlockId id = typeOf(v); return id < defs.size() && defs[id].exists ? defs[id] : missing; }
    bool has(BlockId v) const { BlockId id = typeOf(v); return id < defs.size() && defs[id].exists; }
    size_t count() const { return defs.size(); }            // ids are 0 .. count()-1 (check has())
    BlockId find(const std::string& name) const;
    BlockId liquidBlock(int kind) const;                    // the (flowing, not spring) block of that liquid; 0 if none
    void resolve();                                         // texture names -> layers (after any change); glowing faces get their own copies
    static std::string glowLayerName(BlockId id, int face);
    static std::string multiTileName(const std::string& tex, const FaceMulti& m, int tile, const std::string& glowKey = "");
    void resolveVary(BlockDef& d) const;                   // a varying face's other textures: names -> layers
    std::string uniqueName(const std::string& want, BlockId except = AIR) const;
    int nextId = 1;                                         // ids below this were given out once (saved in blocks.txt)

private:
    std::vector<BlockDef> defs;
    std::vector<TexImage> textures;
    BlockDef missing;
};

// what a varying face looks like at a block in the world: its texture layer (base: the face's own) and its texture coordinates turned / mirrored in place
inline uint32_t varyHash(int x, int y, int z, int face, uint32_t seed) {
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u ^ (uint32_t)(face + 1) * 2654435761u ^ (seed + 1) * 0x9E3779B9u;
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16; return h;
}
inline int varyFace(const BlockDef& d, int face, int x, int y, int z, int base, float& u, float& v, int* turnsOut = nullptr, bool* mirrorOut = nullptr) {   // turnsOut / mirrorOut: how it was turned (an overlay is turned back)
    const FaceVary& fv = d.vary[face]; if (!fv.on()) return base;
    uint32_t h = varyHash(x, y, z, face, fv.seed); int layer = base;
    const auto& o = d.varyTex[face];
    if (!o.empty()) {   // another texture, as often as the weights say
        float total = std::max(0.f, fv.ownWeight); for (const auto& e : o) total += e.second;
        if (total > 0) {
            float r = ((h >> 8) & 0xFFFF) / 65536.f * total;
            if (r >= std::max(0.f, fv.ownWeight)) { r -= std::max(0.f, fv.ownWeight); for (const auto& e : o) { if (r < e.second) { layer = e.first; break; } r -= e.second; layer = e.first; } }
        }
    }
    if (fv.mirror && (h >> 2) & 1) u = 1 - u;
    int k = fv.turns >= 2 ? (int)(h & 3) : fv.turns == 1 ? (int)(h & 1) * 2 : 0;
    if (turnsOut) *turnsOut = k; if (mirrorOut) *mirrorOut = fv.mirror && ((h >> 2) & 1);
    for (int i = 0; i < k; i++) { float t = u; u = 1 - v; v = t; }   // a quarter turn about the middle
    return layer;
}

inline BlockId makeLiquid(BlockId type, int level) { return makeBlock(type, level >= 16 ? 0 : level, 0); }

// The built-in starter blocks with procedural textures (written out as PNGs the first time the engine runs).
void registerBuiltinBlocks(BlockRegistry& r);
void addBuiltinLiquids(BlockRegistry& r);   // water, lava and their springs (added to older block files too)

} // namespace mc
