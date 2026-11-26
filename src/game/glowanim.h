#pragma once
// Glow animations: how something's glow (an emission map, glowing pixels, a light) changes over time - steady, pulsing, blinking,
// flickering ... One list for everything: textures (blocks), models, placed objects, lights and the arm cannon.
// The same curves run in the block shader (shaders/block.hlsl: glowCurve), so a glowing block and its light stay together.
#include <cstdint>
#include <string>

namespace mc {

enum class GlowAnim { Steady, SlowPulse, FastPulse, Blink, DoubleBlink, Flicker, Candle, Strobe, Heartbeat, Off, COUNT };
const char* glowAnimName(GlowAnim a);

struct GlowAnimSettings {
    GlowAnim kind = GlowAnim::Steady;
    float speed = 1;          // times its own pace
    float low = 0;            // how dim it gets at its lowest (0 dark .. 1 never dims)
    bool outOfStep = true;    // each one at its own moment (a row of lights doesn't blink all together)
    bool on() const { return kind != GlowAnim::Steady; }
    bool operator==(const GlowAnimSettings& o) const { return kind == o.kind && speed == o.speed && low == o.low && outOfStep == o.outOfStep; }
    bool operator!=(const GlowAnimSettings& o) const { return !(*this == o); }
};

float glowCurve(GlowAnim a, double t);                                    // 0..1 at time t (seconds, at its own pace)
float glowPhase(uint32_t seed);                                           // a thing's own moment (seconds), from its seed
float glowAt(const GlowAnimSettings& s, double time, uint32_t seed);      // low..1: how bright now
uint32_t glowSeedOf(int x, int y, int z);                                 // a block's seed (from its place; the shader works it out the same way)

std::string glowAnimText(const GlowAnimSettings& s);                      // "kind speed low step" (for saving)
bool glowAnimFrom(const std::string& text, GlowAnimSettings& s);

// the arm cannon's glow: its idle animation, a flash when a shot leaves, brighter while a charge builds
struct WeaponGlow {
    GlowAnimSettings idle;
    float shotFlash = 2;      // how much brighter the moment a shot leaves (0: no flash)
    float shotFade = 0.3f;    // ... gone after this long (seconds)
    float chargeGlow = 1.5f;  // how much brighter when the charge is full (it builds up while charging; 0: none)
    bool operator==(const WeaponGlow& o) const { return idle == o.idle && shotFlash == o.shotFlash && shotFade == o.shotFade && chargeGlow == o.chargeGlow; }
};
// how bright the cannon glows (1 = its own glow): idle animation, plus the flash (sinceShot: seconds since the last shot), plus the charge (0..1)
float weaponGlowAt(const WeaponGlow& w, double time, double sinceShot, float charge);

} // namespace mc
