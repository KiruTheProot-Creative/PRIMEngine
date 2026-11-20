#pragma once
// Surfaces: what a block is made of, for Samus's sounds on it. Each block has a surface tag (Rock, Metal, Water ...);
// each surface has its own steps, jump, landing and Morph Ball rolling - played on top of the character's own
// (or instead of them). Kept in the project's surfaces.txt.
#include "sound.h"
#include <string>
#include <vector>

namespace mc {

struct Surface {
    std::string name = "Surface";
    SoundSet step, jump, land, roll;    // roll: a loop while rolling on it (louder and higher the faster, like the ball's own)
    float volume = 1;
    bool replace = false;               // instead of the character's own steps / jump / landing / rolling (water, snow ...)
};
struct SurfaceSet {
    std::vector<Surface> list;
    Surface* find(const std::string& name);
    const Surface* find(const std::string& name) const;
};
SurfaceSet defaultSurfaces();           // Metal, Grate, Rock, Dirt, Sand, Wood, Glass, Organic, Snow, Ice, Water (no sounds yet)
bool saveSurfaces(const std::string& path, const SurfaceSet& s);
bool loadSurfaces(const std::string& path, SurfaceSet& s);

}
