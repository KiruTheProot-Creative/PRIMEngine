#include "glowanim.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace mc {

const char* glowAnimName(GlowAnim a) {
    static const char* n[] = { "Steady", "Slow pulse", "Fast pulse", "Blink", "Double blink (beacon)", "Flicker (broken neon)", "Candle", "Strobe", "Heartbeat", "Off" };
    return n[std::clamp((int)a, 0, (int)GlowAnim::COUNT - 1)];
}

static double frac(double x) { return x - std::floor(x); }
static double bump(double x, double c, double w) { double d = (x - c) / w; return std::exp(-d * d); }

// (kept in step with glowCurve in shaders/block.hlsl)
float glowCurve(GlowAnim a, double t) {
    const double TAU = 6.283185307179586;
    switch (a) {
    case GlowAnim::Steady: return 1;
    case GlowAnim::SlowPulse: return (float)(0.5 + 0.5 * std::sin(t * TAU * 0.3));
    case GlowAnim::FastPulse: return (float)(0.5 + 0.5 * std::sin(t * TAU * 1.2));
    case GlowAnim::Blink: return frac(t * 0.8) < 0.5 ? 1.f : 0.f;
    case GlowAnim::DoubleBlink: { double p = frac(t * 0.5); return (p < 0.06 || (p > 0.14 && p < 0.2)) ? 1.f : 0.f; }
    case GlowAnim::Flicker: { double k = std::sin(t * 1.7) + std::sin(t * 4.3 + 2) + std::sin(t * 9.1); return k > 1.6 ? 0.f : k > 1.3 ? 0.45f : 1.f; }
    case GlowAnim::Candle: return (float)std::clamp(0.72 + 0.14 * std::sin(t * 11.3) + 0.09 * std::sin(t * 23.7 + 1.3) + 0.05 * std::sin(t * 41.1), 0.0, 1.0);
    case GlowAnim::Strobe: return frac(t * 3) < 0.12 ? 1.f : 0.f;
    case GlowAnim::Heartbeat: { double p = frac(t * 1.1); return (float)std::min(1.0, bump(p, 0.06, 0.035) + 0.7 * bump(p, 0.24, 0.04)); }
    case GlowAnim::Off: return 0;
    default: return 1;
    }
}
float glowPhase(uint32_t seed) { return (float)((seed % 1000u) * 0.137); }
float glowAt(const GlowAnimSettings& s, double time, uint32_t seed) {
    if (s.kind == GlowAnim::Steady) return 1;
    double t = time * std::max(0.01f, s.speed) + (s.outOfStep ? glowPhase(seed) : 0);
    float low = std::clamp(s.low, 0.f, 1.f);
    return low + (1 - low) * glowCurve(s.kind, t);
}
uint32_t glowSeedOf(int x, int y, int z) { return ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^ ((uint32_t)z * 83492791u); }

std::string glowAnimText(const GlowAnimSettings& s) {
    std::ostringstream o; o << (int)s.kind << " " << s.speed << " " << s.low << " " << (s.outOfStep ? 1 : 0); return o.str();
}
bool glowAnimFrom(const std::string& text, GlowAnimSettings& s) {
    std::istringstream i(text); int k = 0, st = 1; GlowAnimSettings r;
    if (!(i >> k >> r.speed >> r.low >> st)) return false;
    r.kind = (GlowAnim)std::clamp(k, 0, (int)GlowAnim::COUNT - 1); r.speed = std::clamp(r.speed, 0.05f, 20.f); r.low = std::clamp(r.low, 0.f, 1.f); r.outOfStep = st != 0;
    s = r; return true;
}

float weaponGlowAt(const WeaponGlow& w, double time, double sinceShot, float charge) {
    float k = glowAt(w.idle, time, 0);
    if (w.shotFlash > 0 && sinceShot >= 0 && sinceShot < w.shotFade) { double f = 1 - sinceShot / std::max(0.01f, w.shotFade); k += w.shotFlash * (float)(f * f); }
    if (w.chargeGlow > 0 && charge > 0) { float c = std::clamp(charge, 0.f, 1.f); k += w.chargeGlow * c * c * (c >= 1 ? 0.85f + 0.15f * (float)std::sin(time * 14) : 1.f); }   // (full: it throbs)
    return k;
}

} // namespace mc
