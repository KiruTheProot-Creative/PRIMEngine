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
inline BlockId makeBlock(BlockId type, int shape = 0, int rot = 0) { return (type & 0xFFFFu) | ((uint32_t)(shape & 0x3FF) << 16) | ((uint32_t)(rot & 31) << 26); }

enum class Render : uint8_t {
    Air,      // nothing
    Opaque,   // solid pixels only (a full cube hides the faces of its neighbours)
    Cutout,   // has see-through pixels (glass, leaves, plants)
};
enum class Shape : uint8_t {
    Cube,     // a full block
    Cross,    // two crossed planes through the middle (flowers, tall grass)
    Plane,    // one flat plane stuck to the face it was placed against (ladders, vines, posters)
};
enum Face { F_PX = 0, F_NX, F_PY, F_NY, F_PZ, F_NZ };   // +X -X +Y(top) -Y(bottom) +Z -Z

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
    void lightLevels(int out[3]) const {
        int c[3] = { (int)(lightColor >> 16) & 255, (int)(lightColor >> 8) & 255, (int)lightColor & 255 }, m = std::max(1, std::max(c[0], std::max(c[1], c[2])));
        for (int i = 0; i < 3; i++) out[i] = (light * c[i] + m / 2) / m;
    }
    bool waves = false;                  // sways in the wind (leaves, plants)
    bool exists = true;                  // false = a deleted id (kept so old worlds stay readable)
};

struct TexImage { std::string name; int w = 0, h = 0; std::vector<uint8_t> rgba; bool transparent = false; };

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
    void resolve();                                         // texture names -> layers (after any change)
    std::string uniqueName(const std::string& want, BlockId except = AIR) const;
    int nextId = 1;                                         // ids below this were given out once (saved in blocks.txt)

private:
    std::vector<BlockDef> defs;
    std::vector<TexImage> textures;
    BlockDef missing;
};

// The built-in starter blocks with procedural textures (written out as PNGs the first time the engine runs).
void registerBuiltinBlocks(BlockRegistry& r);

} // namespace mc
