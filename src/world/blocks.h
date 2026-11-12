// blocks.h : the block registry. Every block has a name, how it is drawn, and one texture per face.
// Textures live in one texture array (all the same size), referenced by layer number.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mc {

using BlockId = uint16_t;
constexpr BlockId AIR = 0;

enum class Render : uint8_t {
    Air,      // nothing
    Opaque,   // full solid cube (hides the faces of its neighbours)
    Cutout,   // full cube with see-through pixels (glass, leaves)
};
enum Face { F_PX = 0, F_NX, F_PY, F_NY, F_PZ, F_NZ };   // +X -X +Y(top) -Y(bottom) +Z -Z

struct BlockDef {
    std::string name;
    Render render = Render::Opaque;
    int tex[6] = { 0, 0, 0, 0, 0, 0 };   // texture layer per face
    bool solid = true;                   // you collide with it
    bool cullSame = false;               // hide faces between two blocks of this kind (glass)
};

class BlockRegistry {
public:
    static constexpr int TEX = 16;       // texture size in pixels (Minecraft-style pixel art)
    BlockRegistry();                     // block 0 is air
    int addTexture(const std::vector<uint8_t>& rgba);   // TEX*TEX*4 bytes; returns the layer
    BlockId add(const BlockDef& d);
    const BlockDef& get(BlockId id) const { return defs[id < defs.size() ? id : 0]; }
    size_t count() const { return defs.size(); }
    size_t textureCount() const { return textures.size(); }
    const std::vector<uint8_t>& texture(int layer) const { return textures[layer]; }
    BlockId find(const std::string& name) const;
    bool opaque(BlockId id) const { return get(id).render == Render::Opaque; }
private:
    std::vector<BlockDef> defs;
    std::vector<std::vector<uint8_t>> textures;
};

// The built-in placeholder blocks (procedural textures) until texture import arrives.
void registerBuiltinBlocks(BlockRegistry& r);

} // namespace mc
