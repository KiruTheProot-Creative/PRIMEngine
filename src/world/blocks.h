// blocks.h : the block registry. Every block has a name, a shape, how it is drawn, and a texture per face.
// Textures are named images (any size). The renderer puts them all into one texture array at a common
// resolution, referenced by layer number. Block ids never change once given (worlds store them).
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace mc {

// A stored block value: bits 0-15 = block type, 16-25 = shape (0 = the block's own), 26-30 = rotation (0-23).
using BlockId = uint32_t;
constexpr BlockId AIR = 0;
constexpr int MAX_BLOCK_TYPES = 65535;
inline BlockId typeOf(BlockId v) { return v & 0xFFFFu; }
inline int shapeOf(BlockId v) { return (int)((v >> 16) & 0x3FFu); }
inline int rotOf(BlockId v) { return (int)((v >> 26) & 31u); }
// bit 31: a block that is not a full cube (a slope, a slab, a plant ...) standing in water - water fills the rest of its space
constexpr BlockId WET = 0x80000000u;
inline bool wetOf(BlockId v) { return (v & WET) != 0 && v != 0xFFFFFFFFu; }
// liquids keep their amount (1..16, 16 = a full block) where other blocks keep their shape
inline int liquidLevel(BlockId v) { int l = (int)((v >> 16) & 31u); return l == 0 || l > 16 ? 16 : l; }
inline BlockId makeBlock(BlockId type, int shape = 0, int rot = 0) { return (type & 0xFFFFu) | ((uint32_t)(shape & 0x3FF) << 16) | ((uint32_t)(rot & 31) << 26); }

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
    int lightOf() const { return light > 0 ? light : autoLight; }   // how far its light reaches (its own setting first)
    void lightLevels(int out[3]) const {
        int L = lightOf(); uint32_t col = light > 0 ? lightColor : autoLightColor;
        int c[3] = { (int)(col >> 16) & 255, (int)(col >> 8) & 255, (int)col & 255 }, m = std::max(1, std::max(c[0], std::max(c[1], c[2])));
        for (int i = 0; i < 3; i++) out[i] = (L * c[i] + m / 2) / m;
    }
    bool waves = false;                  // sways in the wind (leaves, plants)
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
    bool exists = true;                  // false = a deleted id (kept so old worlds stay readable)
};

struct TexImage {
    std::string name; int w = 0, h = 0; std::vector<uint8_t> rgba; bool transparent = false;
    std::string baseName; FaceGlow glow;   // a glowing face's own copy (hidden from the texture lists): the texture it copies, and how it glows
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
    std::string uniqueName(const std::string& want, BlockId except = AIR) const;
    int nextId = 1;                                         // ids below this were given out once (saved in blocks.txt)

private:
    std::vector<BlockDef> defs;
    std::vector<TexImage> textures;
    BlockDef missing;
};

inline BlockId makeLiquid(BlockId type, int level) { return makeBlock(type, level >= 16 ? 0 : level, 0); }

// The built-in starter blocks with procedural textures (written out as PNGs the first time the engine runs).
void registerBuiltinBlocks(BlockRegistry& r);
void addBuiltinLiquids(BlockRegistry& r);   // water, lava and their springs (added to older block files too)

} // namespace mc
