// environment.h : the atmosphere of a world or of a zone in it (temperate, desert, arctic, space, volcanic ...).
// Colours of the sky and light, haze, heat shimmer, things floating in the air, what weather can do there.
#pragma once
#include "../core/math.h"
#include <string>
#include <vector>

namespace mc {

struct EnvLook {
    float dayTop[3], dayHorizon[3], lowHorizon[3];   // sky by day; the horizon when the sun is low
    float glowDay[3], glowLow[3];                    // glow around the sun
    float sunHigh[3], sunLow[3];                     // sunlight (high / low sun)
    float ambDay[3], ambLow[3];                      // light from the sky
    float atmosphere;      // 1 = air, 0 = vacuum (black sky, no fog, clouds or weather)
    float haze;            // extra dust in the air everywhere (fog density)
    float clouds;          // multiplies the cloud cover
    float shimmer;         // heat shimmer over hot ground
    float planet;          // a planet in the sky
    float cold;            // 1 = rain falls as snow
    float particleKind;    // 0 none, 2 dust, 3 embers / spores (rise, glow), 4 ice sparkles
    float particleAmount;
    float particleColor[3];
};
struct EnvType { std::string key, name; EnvLook look; };
const std::vector<EnvType>& envTypes();
int findEnv(const std::string& key);   // -1 if unknown
EnvLook lerp(const EnvLook& a, const EnvLook& b, float t);

// a box in the world with its own environment (both corners included)
struct EnvZone { std::string name, env; I3 lo, hi; bool contains(V3 p) const; };

// which environment is around you, fading smoothly when you walk into another one
class EnvState {
public:
    EnvLook now = envTypes()[0].look;
    int active = 0;                      // index of the environment you are in
    bool inZone = false; std::string zoneName;   // the zone you are in (none = the world's own environment)
    void update(double dt, V3 pos, const std::string& worldEnv, const std::vector<EnvZone>& zones, bool instant = false);
};

} // namespace mc
