#include "particles.h"
#include <algorithm>
#include <cmath>

namespace mc {

const char* shapeName(PShape s) {
    static const char* n[] = { "Soft dot", "Spark (streak)", "Smoke puff", "Pixel", "Ring", "Flare (star)" };
    return n[std::clamp((int)s, 0, (int)PShape::COUNT - 1)];
}
const char* areaName(PArea a) { static const char* n[] = { "A point", "A ball", "A box", "A disc (flat)" }; return n[std::clamp((int)a, 0, (int)PArea::COUNT - 1)]; }
const char* flickerName(Flicker f) { static const char* n[] = { "Steady", "Candle", "Broken (flickers off)", "Pulse" }; return n[std::clamp((int)f, 0, 3)]; }

std::vector<std::string> particlePresets() { return { "Sparks", "Fire", "Smoke", "Steam", "Magic", "Drips", "Embers", "Beam trail", "Impact" }; }
ParticleSettings particlePreset(const std::string& n) {
    ParticleSettings p;
    if (n == "Sparks") { p.rate = 0; p.burst = 25; p.burstEvery = 1.2f; p.life = 0.6f; p.lifeVar = 0.3f; p.spread = 70; p.speed = 6; p.speedVar = 2.5f; p.gravity = -14; p.drag = 0.6f;
        p.size0 = 0.07f; p.size1 = 0.03f; p.color0 = 0xFFF0B0; p.color1 = 0xFF6010; p.glow = 4; p.shape = PShape::Spark; p.collide = true; p.bounce = true; }
    else if (n == "Fire") { p.rate = 45; p.life = 0.9f; p.lifeVar = 0.3f; p.spread = 12; p.speed = 1.6f; p.speedVar = 0.5f; p.gravity = 1.5f; p.drag = 0.8f; p.size0 = 0.45f; p.size1 = 0.1f;
        p.color0 = 0xFFC040; p.color1 = 0xC02008; p.glow = 2.5f; p.area = PArea::Disc; p.areaSize = { 0.35, 0, 0.35 }; p.spin = 0.5f; p.light = true; p.lightRange = 7; p.lightIntensity = 2.2f; }
    else if (n == "Smoke") { p.rate = 12; p.life = 4; p.lifeVar = 1; p.spread = 15; p.speed = 0.9f; p.speedVar = 0.3f; p.gravity = 0.3f; p.drag = 0.4f; p.size0 = 0.5f; p.size1 = 2.2f;
        p.color0 = 0x707070; p.color1 = 0x505050; p.alpha0 = 0.55f; p.alpha1 = 0; p.glow = 1; p.additive = false; p.shape = PShape::Smoke; p.area = PArea::Disc; p.areaSize = { 0.3, 0, 0.3 }; p.spin = 0.15f; }
    else if (n == "Steam") { p.rate = 30; p.life = 1.6f; p.lifeVar = 0.4f; p.spread = 8; p.speed = 3.5f; p.speedVar = 0.8f; p.gravity = 0.5f; p.drag = 1.2f; p.size0 = 0.2f; p.size1 = 1.2f;
        p.color0 = 0xF0F4F8; p.color1 = 0xE0E8F0; p.alpha0 = 0.45f; p.alpha1 = 0; p.glow = 1; p.additive = false; p.shape = PShape::Smoke; p.spin = 0.3f; }
    else if (n == "Magic") { p.rate = 25; p.life = 2; p.lifeVar = 0.5f; p.spread = 180; p.speed = 0.6f; p.speedVar = 0.3f; p.gravity = 0.4f; p.drag = 0.3f; p.size0 = 0.12f; p.size1 = 0;
        p.color0 = 0xC0A0FF; p.color1 = 0x40E0FF; p.glow = 3; p.shape = PShape::Flare; p.area = PArea::Sphere; p.areaSize = { 0.8, 0.8, 0.8 }; p.spin = 1; }
    else if (n == "Drips") { p.rate = 3; p.life = 2; p.lifeVar = 0.2f; p.spread = 0; p.dir = { 0, -1, 0 }; p.speed = 0.2f; p.speedVar = 0; p.gravity = -12; p.drag = 0; p.size0 = 0.06f; p.size1 = 0.06f;
        p.color0 = 0x80B8FF; p.color1 = 0x80B8FF; p.alpha0 = 0.8f; p.alpha1 = 0.8f; p.glow = 1.2f; p.shape = PShape::Spark; p.area = PArea::Box; p.areaSize = { 0.6, 0, 0.6 }; p.collide = true; p.bounce = false; }
    else if (n == "Embers") { p.rate = 8; p.life = 3; p.lifeVar = 1; p.spread = 40; p.speed = 0.8f; p.speedVar = 0.4f; p.gravity = 0.6f; p.drag = 0.5f; p.size0 = 0.05f; p.size1 = 0.02f;
        p.color0 = 0xFFB040; p.color1 = 0xFF3000; p.glow = 4; p.shape = PShape::Pixel; p.area = PArea::Disc; p.areaSize = { 1, 0, 1 }; }
    else if (n == "Beam trail") { p.rate = 120; p.life = 0.25f; p.lifeVar = 0.1f; p.spread = 180; p.speed = 0.3f; p.speedVar = 0.2f; p.drag = 2; p.size0 = 0.12f; p.size1 = 0;
        p.color0 = 0xB0F0FF; p.color1 = 0x2080FF; p.glow = 3; p.shape = PShape::Dot; }
    else if (n == "Impact") { p.rate = 0; p.burst = 30; p.burstEvery = 0; p.life = 0.45f; p.lifeVar = 0.2f; p.spread = 80; p.speed = 5; p.speedVar = 2; p.gravity = -10; p.drag = 1;
        p.size0 = 0.06f; p.size1 = 0.02f; p.color0 = 0xE0FFFF; p.color1 = 0x2090FF; p.glow = 4; p.shape = PShape::Spark; p.collide = true; }
    return p;
}

static V3 turn(V3 v, double yawDeg) { double a = yawDeg * 3.14159265358979 / 180, c = std::cos(a), s = std::sin(a); return { v.x * c + v.z * s, v.y, -v.x * s + v.z * c }; }

void ParticleEmitter::spawn(V3 o, double yaw) {
    if ((int)ps.size() >= std::max(1, s.maxParticles)) return;
    Particle p;
    V3 off{ 0, 0, 0 };
    switch (s.area) {
    case PArea::Sphere: { double u = rand01() * 6.2831853, v = rand01() * 2 - 1, r = std::cbrt(rand01()); double q = std::sqrt(1 - v * v); off = { std::cos(u) * q * r * s.areaSize.x, v * r * s.areaSize.y, std::sin(u) * q * r * s.areaSize.z }; break; }
    case PArea::Box: off = { (rand01() - 0.5) * s.areaSize.x, (rand01() - 0.5) * s.areaSize.y, (rand01() - 0.5) * s.areaSize.z }; break;
    case PArea::Disc: { double u = rand01() * 6.2831853, r = std::sqrt(rand01()); off = { std::cos(u) * r * s.areaSize.x, 0, std::sin(u) * r * s.areaSize.z }; break; }
    default: break;
    }
    p.pos = o + turn(off, yaw);
    // a direction inside the cone around dir
    V3 d = s.dir; double l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z); d = l > 1e-9 ? d * (1 / l) : V3{ 0, 1, 0 };
    double cone = std::clamp((double)s.spread, 0.0, 180.0) * 3.14159265358979 / 180;
    double cz = 1 - rand01() * (1 - std::cos(cone)), sz = std::sqrt(std::max(0.0, 1 - cz * cz)), ph = rand01() * 6.2831853;
    V3 a = std::fabs(d.y) < 0.9 ? V3{ 0, 1, 0 } : V3{ 1, 0, 0 };
    V3 t{ a.y * d.z - a.z * d.y, a.z * d.x - a.x * d.z, a.x * d.y - a.y * d.x }; double tl = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z); t = t * (1 / tl);
    V3 b{ d.y * t.z - d.z * t.y, d.z * t.x - d.x * t.z, d.x * t.y - d.y * t.x };
    V3 dir = d * cz + t * (sz * std::cos(ph)) + b * (sz * std::sin(ph));
    double sp = std::max(0.0, (double)s.speed + (rand01() * 2 - 1) * s.speedVar);
    p.vel = turn(dir, yaw) * sp;
    p.life = std::max(0.02f, s.life + (rand01() * 2 - 1) * s.lifeVar);
    p.rot = rand01() * 6.2831853f; p.spin = (rand01() * 2 - 1) * s.spin * 6.2831853f; p.size = 0.8f + rand01() * 0.4f; p.seed = rng;
    ps.push_back(p);
}

void ParticleEmitter::burstNow(int n, V3 origin, double yaw) { for (int i = 0; i < n; i++) spawn(origin, yaw); }

void ParticleEmitter::update(double dt, V3 origin, double yaw, const World* w, bool emitting) {
    if (emitting) {
        if (!started) { started = true; if (s.burst > 0) burstNow(s.burst, origin, yaw); }
        else if (s.burst > 0 && s.burstEvery > 0) { burstT += (float)dt; while (burstT >= s.burstEvery) { burstT -= s.burstEvery; burstNow(s.burst, origin, yaw); } }
        if (s.rate > 0) { accum += (float)(s.rate * dt); while (accum >= 1) { accum -= 1; spawn(origin, yaw); } }
    }
    double drag = std::exp(-std::max(0.f, s.drag) * dt);
    for (Particle& p : ps) {
        p.age += (float)dt;
        p.vel.y += s.gravity * dt; p.vel = p.vel * drag;
        V3 next = p.pos + p.vel * dt;
        if (s.collide && w && w->solidAt((int)std::floor(next.x), (int)std::floor(next.y), (int)std::floor(next.z))) {
            if (!s.bounce) { p.age = p.life; continue; }
            // bounce off the side it came through
            int cx = (int)std::floor(p.pos.x), cy = (int)std::floor(p.pos.y), cz = (int)std::floor(p.pos.z);
            if ((int)std::floor(next.y) != cy && w->solidAt(cx, (int)std::floor(next.y), cz)) p.vel.y = -p.vel.y * 0.35;
            else if ((int)std::floor(next.x) != cx) p.vel.x = -p.vel.x * 0.35;
            else p.vel.z = -p.vel.z * 0.35;
            p.vel = p.vel * 0.7; next = p.pos;
        }
        p.pos = next; p.rot += p.spin * (float)dt;
    }
    ps.erase(std::remove_if(ps.begin(), ps.end(), [](const Particle& p) { return p.age >= p.life; }), ps.end());
}

ParticleLook lookOf(const ParticleEmitter& e, const Particle& p) {
    const ParticleSettings& s = e.s; float t = e.alive01(p);
    auto ch = [](uint32_t c, int sh) { return std::pow(((c >> sh) & 255) / 255.f, 2.2f); };
    ParticleLook l;
    l.pos = p.pos; l.vel = p.vel; l.size = (s.size0 + (s.size1 - s.size0) * t) * p.size; l.rot = p.rot;
    l.r = ch(s.color0, 16) + (ch(s.color1, 16) - ch(s.color0, 16)) * t; l.g = ch(s.color0, 8) + (ch(s.color1, 8) - ch(s.color0, 8)) * t; l.b = ch(s.color0, 0) + (ch(s.color1, 0) - ch(s.color0, 0)) * t;
    l.a = s.alpha0 + (s.alpha1 - s.alpha0) * t;
    if (t < 0.08f) l.a *= t / 0.08f;   // fade in a little, no popping
    return l;
}

float flickerAt(Flicker f, double time, uint32_t seed) {
    double t = time + (seed % 1000) * 0.137;
    switch (f) {
    case Flicker::Candle: return (float)std::clamp(0.82 + 0.1 * std::sin(t * 11.3) + 0.06 * std::sin(t * 23.7 + 1.3) + 0.04 * std::sin(t * 41.1), 0.0, 1.0);
    case Flicker::Broken: { double k = std::sin(t * 1.7) + std::sin(t * 4.3 + 2) + std::sin(t * 9.1); return k > 1.6 ? 0.05f : (k > 1.3 ? 0.5f : 1.f); }
    case Flicker::Pulse: return (float)(0.55 + 0.45 * std::sin(t * 2.4));
    default: return 1;
    }
}

} // namespace mc
