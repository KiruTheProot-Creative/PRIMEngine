#include "environment.h"
#include <algorithm>
#include <cmath>

namespace mc {

const std::vector<EnvType>& envTypes() {
    auto mk = [](std::initializer_list<float> v) { EnvLook l{}; float* p = &l.dayTop[0]; for (float x : v) *p++ = x; return l; };
    // dayTop dayHorizon lowHorizon | glowDay glowLow | sunHigh sunLow | ambDay ambLow | atmosphere haze clouds shimmer planet cold | particle kind amount colour
    static const std::vector<EnvType> t = {
        { "temperate", "Temperate", mk({ 0.10f, 0.27f, 0.80f,  0.50f, 0.67f, 0.95f,  1.05f, 0.55f, 0.28f,   0.9f, 0.75f, 0.5f,  1.5f, 0.6f, 0.2f,
                                         3.1f, 2.9f, 2.55f,  2.2f, 1.0f, 0.45f,   0.38f, 0.48f, 0.70f,  0.45f, 0.36f, 0.38f,
                                         1, 0, 1, 0, 0, 0,   0, 0, 1, 1, 1 }) },
        { "desert", "Desert (hot)", mk({ 0.16f, 0.36f, 0.82f,  0.88f, 0.76f, 0.58f,  1.2f, 0.6f, 0.25f,   1.2f, 0.9f, 0.55f,  1.8f, 0.7f, 0.2f,
                                         3.8f, 3.4f, 2.8f,  2.6f, 1.2f, 0.4f,   0.50f, 0.48f, 0.45f,  0.55f, 0.40f, 0.30f,
                                         1, 0.0010f, 0.3f, 1, 0, 0,   2, 0.25f, 0.9f, 0.75f, 0.55f }) },
        { "arctic", "Arctic (icy)", mk({ 0.12f, 0.30f, 0.85f,  0.62f, 0.78f, 0.98f,  0.95f, 0.65f, 0.6f,   0.8f, 0.8f, 0.9f,  1.2f, 0.6f, 0.5f,
                                         2.6f, 2.7f, 2.9f,  1.8f, 1.0f, 0.7f,   0.40f, 0.52f, 0.80f,  0.45f, 0.45f, 0.60f,
                                         1, 0.0012f, 0.8f, 0, 0, 1,   4, 0.35f, 0.9f, 0.95f, 1.0f }) },
        { "space", "Space (no air)", mk({ 0, 0, 0,  0, 0, 0,  0, 0, 0,   0, 0, 0,  0, 0, 0,
                                         4.0f, 3.9f, 3.7f,  4.0f, 3.9f, 3.7f,   0.03f, 0.035f, 0.05f,  0.03f, 0.035f, 0.05f,
                                         0, 0, 0, 0, 1, 0,   0, 0, 1, 1, 1 }) },
        { "volcanic", "Volcanic", mk({ 0.25f, 0.08f, 0.05f,  0.55f, 0.22f, 0.10f,  0.7f, 0.25f, 0.08f,   1.2f, 0.4f, 0.1f,  1.5f, 0.4f, 0.1f,
                                         1.6f, 0.8f, 0.4f,  1.2f, 0.4f, 0.15f,   0.35f, 0.18f, 0.12f,  0.35f, 0.15f, 0.10f,
                                         1, 0.009f, 1.2f, 0.6f, 0, 0,   3, 0.4f, 1.0f, 0.45f, 0.1f }) },
        { "alien", "Alien", mk({ 0.04f, 0.50f, 0.32f,  0.50f, 0.95f, 0.45f,  0.9f, 0.4f, 0.9f,   0.9f, 0.6f, 1.1f,  1.3f, 0.4f, 1.3f,
                                         2.8f, 2.5f, 3.2f,  2.0f, 0.8f, 1.8f,   0.30f, 0.55f, 0.40f,  0.40f, 0.30f, 0.45f,
                                         1, 0.0035f, 0.9f, 0, 0, 0,   3, 0.25f, 0.5f, 1.0f, 0.45f }) },
        { "martian", "Martian (dusty)", mk({ 0.45f, 0.30f, 0.20f,  0.85f, 0.62f, 0.42f,  0.45f, 0.55f, 0.85f,   0.9f, 0.75f, 0.6f,  0.5f, 0.75f, 1.4f,
                                         2.4f, 2.2f, 2.0f,  1.6f, 1.4f, 1.5f,   0.48f, 0.36f, 0.27f,  0.40f, 0.36f, 0.40f,
                                         1, 0.0016f, 0.15f, 0.3f, 0, 0,   2, 0.3f, 0.85f, 0.55f, 0.35f }) },
    };
    return t;
}
int findEnv(const std::string& key) { for (size_t i = 0; i < envTypes().size(); i++) if (envTypes()[i].key == key) return (int)i; return -1; }

EnvLook lerp(const EnvLook& a, const EnvLook& b, float t) {
    EnvLook o; const float* pa = &a.dayTop[0]; const float* pb = &b.dayTop[0]; float* po = &o.dayTop[0];
    for (size_t i = 0; i < sizeof(EnvLook) / sizeof(float); i++) po[i] = pa[i] + (pb[i] - pa[i]) * t;
    o.particleKind = t < 0.5f ? a.particleKind : b.particleKind;   // the kind switches; the amount fades
    if (a.particleKind != b.particleKind) o.particleAmount = t < 0.5f ? a.particleAmount * (1 - 2 * t) : b.particleAmount * (2 * t - 1);
    return o;
}

bool EnvZone::contains(V3 p) const {
    return p.x >= lo.x && p.x < hi.x + 1 && p.y >= lo.y && p.y < hi.y + 1 && p.z >= lo.z && p.z < hi.z + 1;
}

void EnvState::update(double dt, V3 pos, const std::string& worldEnv, const std::vector<EnvZone>& zones, bool instant) {
    // the smallest zone you are in wins (a station inside a bigger area)
    const EnvZone* zone = nullptr; double best = 1e300;
    for (const EnvZone& z : zones) {
        if (!z.contains(pos) || findEnv(z.env) < 0) continue;
        double vol = (double)(z.hi.x - z.lo.x + 1) * (z.hi.y - z.lo.y + 1) * (z.hi.z - z.lo.z + 1);
        if (vol < best) { best = vol; zone = &z; }
    }
    inZone = zone != nullptr; zoneName = zone ? zone->name : "";
    int e = findEnv(zone ? zone->env : worldEnv); if (e < 0) e = 0;
    active = e;
    float k = instant ? 1.f : 1 - (float)std::exp(-dt / 1.6);   // about 5 seconds to fully change
    now = lerp(now, envTypes()[e].look, k);
}

} // namespace mc
