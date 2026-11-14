#pragma once
// Materials per texture: how shiny, how metal, whether it glows, how bumpy its generated surface is and how strong
// its fine "coat" of noise is. Textures without settings of their own get a guess from their name (iron: metal,
// glass / ice: shiny, lamp / lava / glow: glowing), like integrated PBR. Plain C++, tested on Linux.
#include "../world/blocks.h"
#include <map>
#include <string>
#include <vector>

namespace mc {

struct TexMaterial {
    float shine = 0;        // 0 matt .. 1 polished (reflections, a sun highlight)
    float metal = 0;        // 0 .. 1: metal reflects in its own colour and has less plain colour
    float glow = 0;         // 0 .. 1+: lights itself up (bright pixels only, see glowFrom)
    float glowFrom = 0;     // only pixels brighter than this glow (0: all of it; 0.6: just the bright parts, like a lamp's bulb)
    float bumps = 1;        // how bumpy the generated surface is (times the global setting; 0: flat)
    float coat = 1;         // how strong the fine noise coat is (times the global setting; 0: none)
    bool operator==(const TexMaterial& o) const { return shine == o.shine && metal == o.metal && glow == o.glow && glowFrom == o.glowFrom && bumps == o.bumps && coat == o.coat; }
    bool operator!=(const TexMaterial& o) const { return !(*this == o); }
};

TexMaterial guessMaterial(const std::string& textureName);   // from its name, for textures nobody set up

struct Materials {
    std::map<std::string, TexMaterial> set;                   // textures someone set up, by name
    bool guess = true;                                         // textures nobody set up: a guess from their name (else plain)
    TexMaterial of(const std::string& textureName) const;      // its own settings, or the guess
    bool isSet(const std::string& textureName) const { return set.count(textureName) > 0; }
    void put(const std::string& textureName, const TexMaterial& m) { set[textureName] = m; }
    void reset(const std::string& textureName) { set.erase(textureName); }   // back to the guess
    // for the shaders: 8 floats per texture layer (shine, metal, glow, glowFrom, bumps, coat, 0, 0)
    std::vector<float> table(const BlockRegistry& reg) const;
};
bool saveMaterials(const std::string& path, const Materials& m);
bool loadMaterials(const std::string& path, Materials& m);

} // namespace mc
