#pragma once
// Particles (G2 attempt 5): emitters you place (fire, smoke, sparks, steam ...) and the engine's own effects
// (beam tails, impact sparks). Simulated here (plain C++, tested on Linux), drawn by the app as glowing or soft quads.
#include "game.h"
#include <cstdint>
#include <string>
#include <vector>

namespace mc {

enum class PShape { Dot, Spark, Smoke, Pixel, Ring, Flare, COUNT };   // how one particle looks (Spark: stretched along its flight)
enum class PArea { Point, Sphere, Box, Disc, COUNT };                 // where new ones appear
const char* shapeName(PShape s);
const char* areaName(PArea a);

struct ParticleSettings {
    float rate = 20;                 // new particles per second (0: only bursts)
    int burst = 0;                   // this many at once ...
    float burstEvery = 0;            // ... every so many seconds (0: once, at the start)
    float life = 1.5f, lifeVar = 0.3f;
    V3 dir{ 0, 1, 0 };               // where they fly (turns with the object)
    float spread = 25;               // degrees around dir (180: every way)
    float speed = 3, speedVar = 1;   // blocks per second
    float gravity = 0;               // blocks per second per second (down is negative)
    float drag = 0.5f;               // slows them down
    float size0 = 0.25f, size1 = 0.05f;   // at birth, at the end (blocks)
    uint32_t color0 = 0xFFD070, color1 = 0xFF4010;
    float alpha0 = 1, alpha1 = 0;
    float glow = 2;                  // how bright (glowing particles light up and bloom)
    bool additive = true;            // glowing (added) or soft (smoke)
    PShape shape = PShape::Dot;
    PArea area = PArea::Point; V3 areaSize{ 0.3, 0.3, 0.3 };
    float spin = 0;                  // turns per second (smoke)
    bool collide = false, bounce = true;   // hit the blocks: bounce, or stop there
    bool light = false; float lightRange = 4; float lightIntensity = 1.5f;   // the emitter also lights its surroundings (fire)
    int maxParticles = 2000;
};
ParticleSettings particlePreset(const std::string& name);
std::vector<std::string> particlePresets();   // "Sparks", "Fire", "Smoke", ...

struct Particle { V3 pos, vel; float age = 0, life = 1, rot = 0, spin = 0, size = 1; uint32_t seed = 0; };

class ParticleEmitter {
public:
    ParticleSettings s;
    std::vector<Particle> ps;
    explicit ParticleEmitter(const ParticleSettings& set = {}, uint32_t seed = 1) : s(set), rng(seed ? seed : 1) {}
    // moves them, makes new ones at `origin` (turned by yaw degrees); world: for collisions (may be null)
    void update(double dt, V3 origin, double yaw, const World* w, bool emitting = true);
    void burstNow(int n, V3 origin, double yaw);
    float alive01(const Particle& p) const { return p.life > 0 ? std::min(1.f, p.age / p.life) : 1; }
private:
    uint32_t rng; float accum = 0, burstT = 0; bool started = false;
    float rand01() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.f; }
    void spawn(V3 origin, double yaw);
};

// one particle as it should be drawn now
struct ParticleLook { V3 pos, vel; float size, rot, r, g, b, a; };
ParticleLook lookOf(const ParticleEmitter& e, const Particle& p);

// lights (objects, glowing emitters, shots)
enum class LightType { Point, Spot };
enum class Flicker { None, Candle, Broken, Pulse };
struct LightSettings {
    LightType type = LightType::Point;
    uint32_t color = 0xFFE0B0;
    float intensity = 2, range = 8;  // range: blocks
    float cone = 35, tilt = 45;      // spotlights: half-angle, and how far it looks down (degrees; it turns with the object)
    Flicker flicker = Flicker::None;
};
const char* flickerName(Flicker f);
float flickerAt(Flicker f, double time, uint32_t seed);   // 0..1 brightness

// an impact sphere: a glowing see-through shell that grows fast from where something went off, then fades (explosions, missiles, charged shots)
struct ImpactSphere {
    V3 pos{ 0, 0, 0 }; float t = 0, life = 0.45f, radius = 1.5f; uint32_t color = 0xFFC060;
    float radiusNow() const;   // grows quickly at first, slowly at the end
    float alphaNow() const;    // bright at first, gone at the end
    bool done() const { return t >= life; }
};

} // namespace mc
