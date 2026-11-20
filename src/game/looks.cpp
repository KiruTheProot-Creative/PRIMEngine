#include "looks.h"
#include "visors.h"   // escText / unescText
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace mc {

const char* toneCurveName(ToneCurve t) {
    static const char* n[] = { "Classic", "Film (bright colours stay colourful)", "Neutral (true colours)" };
    int i = (int)t; return i >= 0 && i < (int)ToneCurve::COUNT ? n[i] : "?";
}

static uint32_t mixColor(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int s) { float x = ((a >> s) & 255) + (((b >> s) & 255) - (float)((a >> s) & 255)) * t; return (uint32_t)std::clamp((int)std::lround(x), 0, 255) << s; };
    return ch(16) | ch(8) | ch(0);
}
ColorLook lerp(const ColorLook& a, const ColorLook& b, float t) {
    t = std::clamp(t, 0.f, 1.f); auto L = [&](float x, float y) { return x + (y - x) * t; };
    ColorLook o = t < 0.5f ? a : b;   // (the name and tone curve switch halfway)
    o.exposure = L(a.exposure, b.exposure); o.adapt = L(a.adapt, b.adapt); o.adaptSpeed = L(a.adaptSpeed, b.adaptSpeed);
    o.contrast = L(a.contrast, b.contrast); o.saturation = L(a.saturation, b.saturation); o.warmth = L(a.warmth, b.warmth);
    o.shadows = mixColor(a.shadows, b.shadows, t); o.shadowsAmount = L(a.shadowsAmount, b.shadowsAmount);
    o.highlights = mixColor(a.highlights, b.highlights, t); o.highlightsAmount = L(a.highlightsAmount, b.highlightsAmount);
    o.bloom = L(a.bloom, b.bloom); o.bloomSize = L(a.bloomSize, b.bloomSize);
    o.fringes = L(a.fringes, b.fringes); o.grain = L(a.grain, b.grain); o.vignette = L(a.vignette, b.vignette);
    o.fog = L(a.fog, b.fog); o.fogHeight = L(a.fogHeight, b.fogHeight); o.fogThin = L(a.fogThin, b.fogThin);
    o.fogColor = mixColor(a.fogColor, b.fogColor, t); o.fogGlow = L(a.fogGlow, b.fogGlow); o.lightFog = L(a.lightFog, b.lightFog);
    return o;
}

const std::vector<ColorLook>& lookPresets() {
    static const std::vector<ColorLook> v = [] {
        std::vector<ColorLook> p;
        ColorLook n; p.push_back(n);                                                                    // Natural
        ColorLook c; c.name = "Classic"; c.curve = ToneCurve::Classic; c.adapt = 0; p.push_back(c);         // as it was
        ColorLook neon; neon.name = "Neon night"; neon.exposure = -0.3f; neon.adapt = 0.35f; neon.contrast = 1.15f; neon.saturation = 1.2f; neon.warmth = -0.15f;
        neon.shadows = 0x2A3A8C; neon.shadowsAmount = 0.45f; neon.highlights = 0xFF6AC8; neon.highlightsAmount = 0.25f;
        neon.bloom = 1.6f; neon.bloomSize = 1.35f; neon.fringes = 0.35f; neon.grain = 0.25f; neon.vignette = 0.32f;
        neon.fog = 0.02f; neon.fogColor = 0x6A4AB0; neon.fogGlow = 0.12f; neon.lightFog = 1.4f; p.push_back(neon);
        ColorLook tox; tox.name = "Toxic"; tox.exposure = -0.2f; tox.contrast = 1.1f; tox.saturation = 1.1f; tox.shadows = 0x1E3A12; tox.shadowsAmount = 0.5f;
        tox.highlights = 0xC8FF5A; tox.highlightsAmount = 0.35f; tox.bloom = 1.4f; tox.bloomSize = 1.25f; tox.grain = 0.15f; tox.vignette = 0.3f;
        tox.fog = 0.03f; tox.fogColor = 0x4A8A2A; tox.fogGlow = 0.08f; tox.lightFog = 1.0f; p.push_back(tox);
        ColorLook inf; inf.name = "Inferno"; inf.contrast = 1.12f; inf.saturation = 1.15f; inf.warmth = 0.45f; inf.shadows = 0x4A1408; inf.shadowsAmount = 0.4f;
        inf.highlights = 0xFFC060; inf.highlightsAmount = 0.3f; inf.bloom = 1.5f; inf.bloomSize = 1.2f; inf.vignette = 0.28f;
        inf.fog = 0.015f; inf.fogColor = 0xC05020; inf.fogGlow = 0.1f; inf.lightFog = 0.8f; p.push_back(inf);
        ColorLook dusk; dusk.name = "Chozo dusk"; dusk.warmth = 0.3f; dusk.saturation = 0.95f; dusk.shadows = 0x3A2C5A; dusk.shadowsAmount = 0.3f;
        dusk.highlights = 0xFFD08A; dusk.highlightsAmount = 0.3f; dusk.bloom = 1.2f; dusk.bloomSize = 1.1f; dusk.vignette = 0.22f; p.push_back(dusk);
        ColorLook fr; fr.name = "Frozen"; fr.exposure = 0.1f; fr.warmth = -0.4f; fr.saturation = 0.85f; fr.contrast = 0.95f; fr.shadows = 0x2C4C7A; fr.shadowsAmount = 0.35f;
        fr.highlights = 0xDDF4FF; fr.highlightsAmount = 0.2f; fr.bloom = 1.1f; p.push_back(fr);
        ColorLook noir; noir.name = "Noir"; noir.saturation = 0.1f; noir.contrast = 1.3f; noir.grain = 0.4f; noir.vignette = 0.45f; noir.bloom = 0.8f; p.push_back(noir);
        return p;
    }();
    return v;
}

const ColorLook* LookSet::find(const std::string& name) const {
    for (const ColorLook& l : looks) if (l.name == name) return &l;
    for (const ColorLook& l : lookPresets()) if (l.name == name) return &l;
    return nullptr;
}
ColorLook LookSet::get(const std::string& name) const { const ColorLook* l = find(name); return l ? *l : lookPresets()[0]; }
std::vector<std::string> LookSet::names() const {
    std::vector<std::string> v; for (const ColorLook& l : looks) v.push_back(l.name);
    for (const ColorLook& l : lookPresets()) if (std::find(v.begin(), v.end(), l.name) == v.end()) v.push_back(l.name);
    return v;
}

bool saveLooks(const std::string& path, const LookSet& s) {
    std::ofstream f(path); if (!f) return false;
    f << "looks 1\n";
    for (const ColorLook& l : s.looks)
        f << "look " << escText(l.name) << " " << (int)l.curve << " " << l.exposure << " " << l.adapt << " " << l.adaptSpeed << " " << l.contrast << " " << l.saturation << " " << l.warmth << " "
          << l.shadows << " " << l.shadowsAmount << " " << l.highlights << " " << l.highlightsAmount << " " << l.bloom << " " << l.bloomSize << " " << l.fringes << " " << l.grain << " " << l.vignette
          << " " << l.fog << " " << l.fogHeight << " " << l.fogThin << " " << l.fogColor << " " << l.fogGlow << " " << l.lightFog << "\n";
    return (bool)f;
}
bool loadLooks(const std::string& path, LookSet& s) {
    std::ifstream f(path); if (!f) return false;
    s = LookSet{}; std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k, n; int c = 1; l >> k; if (k != "look") continue;
        ColorLook o;
        if (!(l >> n >> c >> o.exposure >> o.adapt >> o.adaptSpeed >> o.contrast >> o.saturation >> o.warmth >> o.shadows >> o.shadowsAmount >> o.highlights >> o.highlightsAmount >> o.bloom >> o.bloomSize >> o.fringes >> o.grain >> o.vignette)) continue;
        { ColorLook a; if (l >> a.fog >> a.fogHeight >> a.fogThin >> a.fogColor >> a.fogGlow >> a.lightFog) { o.fog = a.fog; o.fogHeight = a.fogHeight; o.fogThin = a.fogThin; o.fogColor = a.fogColor; o.fogGlow = a.fogGlow; o.lightFog = a.lightFog; } }   // (newer files)
        o.name = unescText(n); o.curve = (ToneCurve)std::clamp(c, 0, (int)ToneCurve::COUNT - 1);
        bool dup = false; for (const ColorLook& x : s.looks) dup |= x.name == o.name;
        if (!dup && !o.name.empty()) s.looks.push_back(o);
    }
    return true;
}

void LookState::update(double dt, const ColorLook& target, const std::string& key, bool instant) {
    if (instant || active.empty()) { now = from = to = target; active = key; t = 1; return; }
    if (key != active) { from = now; active = key; t = 0; }
    to = target;   // (edits show right away)
    t = std::min(1.f, t + (float)dt / 1.5f);
    float s = t * t * (3 - 2 * t);
    now = t >= 1 ? to : lerp(from, to, s);
}

} // namespace mc
