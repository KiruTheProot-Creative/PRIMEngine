#pragma once
// Materials per texture: how shiny, how metal, whether it glows, how bumpy its generated surface is and how strong
// its fine "coat" of noise is. Textures without settings of their own get a guess from their name (iron: metal,
// glass / ice: shiny, lamp / lava / glow: glowing), like integrated PBR. Plain C++, tested on Linux.
#include "../world/blocks.h"
#include <map>
#include <string>
#include <vector>

namespace mc {

// fake shine (like Metroid Prime's arm cannon): highlights painted on, not worked out from real lights - they always
// glint, even in the dark, and slide over the model as you turn. Either a shine picture (black with bright spots,
// looked up by which way the surface faces - a "matcap") or up to three glint lights that only make highlights.
enum class FakeShine { Off, Picture, Glints, COUNT };
struct Glint {
    bool on = false;
    float yaw = 0, pitch = 30;          // where it shines from (degrees): world (0 yaw: from the north) or, fixed to the camera, from the viewer
    uint32_t color = 0xFFFFFF;
    float sharp = 0.6f, power = 1;      // 0 a soft sheen .. 1 a tiny sharp dot; how bright
    bool operator==(const Glint& o) const { return on == o.on && yaw == o.yaw && pitch == o.pitch && color == o.color && sharp == o.sharp && power == o.power; }
};
const char* shinePresetName(int i);    // the built-in shine pictures (0 .. SHINE_PRESETS-1); SHINE_PRESETS: your own picture
constexpr int SHINE_PRESETS = 4;

struct TexMaterial {
    float shine = 0;        // 0 matt .. 1 polished (reflections, a sun highlight)
    float metal = 0;        // 0 .. 1: metal reflects in its own colour and has less plain colour
    float glow = 0;         // 0 .. 1+: lights itself up (bright pixels only, see glowFrom)
    float glowFrom = 0;     // only pixels brighter than this glow (0: all of it; 0.6: just the bright parts, like a lamp's bulb)
    float bumps = 1;        // how bumpy the generated surface is (times the global setting; 0: flat)
    float coat = 1;         // how strong the fine noise coat is (times the global setting; 0: none)
    // an emission map: a second picture (same layout as the texture) of what glows and in which colour (black: nothing)
    std::string emitMap;    // its file in the textures folder ("": none; a file called <texture>_e / _emission / _emissive / _glow .png is found by itself)
    float emit = 1.5f;      // how bright it glows
    float lights = 6;       // it lights up what is around it: blocks built from it shine this far (0 none .. 15 like a lamp), models a light this far
    // fake shine (models)
    FakeShine fake = FakeShine::Off;
    float fakeStrength = 1; uint32_t fakeTint = 0xFFFFFF; bool fakeBeam = false;   // tint: a colour, or the current beam's colour
    bool fakeWorld = true;              // slides when you turn (fixed to the world, like Prime) / stays put (fixed to the camera)
    int fakePreset = 0;                 // the shine picture: a built-in one, or SHINE_PRESETS = fakeMap
    std::string fakeMap, fakeMask;      // your own shine picture; a mask (laid out like the model's picture: white shines, black not)
    Glint glints[3] = { { true, -40, 45, 0xFFFFFF, 0.7f, 1 }, { true, 70, 15, 0xA8D8FF, 0.4f, 0.5f }, {} };
    bool operator==(const TexMaterial& o) const {
        return shine == o.shine && metal == o.metal && glow == o.glow && glowFrom == o.glowFrom && bumps == o.bumps && coat == o.coat && emitMap == o.emitMap && emit == o.emit && lights == o.lights
            && fake == o.fake && fakeStrength == o.fakeStrength && fakeTint == o.fakeTint && fakeBeam == o.fakeBeam && fakeWorld == o.fakeWorld && fakePreset == o.fakePreset && fakeMap == o.fakeMap && fakeMask == o.fakeMask
            && glints[0] == o.glints[0] && glints[1] == o.glints[1] && glints[2] == o.glints[2];
    }
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
    // for the shaders: 8 floats per texture layer (shine, metal, glow, glowFrom, bumps, coat, emit, 0); emit is only used where an emission map is loaded
    std::vector<float> table(const BlockRegistry& reg) const;
};
bool saveMaterials(const std::string& path, const Materials& m);
bool loadMaterials(const std::string& path, Materials& m);

} // namespace mc
