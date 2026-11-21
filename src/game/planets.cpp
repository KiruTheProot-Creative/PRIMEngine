#include "planets.h"
#include "visors.h"   // escText / unescText
#include <algorithm>
#include <fstream>
#include <sstream>

namespace mc {

Planet defaultPlanet() { return planetPreset("Earth-like"); }

const std::vector<std::string>& planetPresetNames() {
    static const std::vector<std::string> n = { "Earth-like", "Desert", "Ice", "Lava", "Gas giant", "Alien jungle" };
    return n;
}
Planet planetPreset(const std::string& kind) {
    Planet p; p.name = kind;
    if (kind == "Desert") {
        p.ocean = true; p.seaLevel = 0.3f; p.oceanColor = 0x2A7A8A; p.deepColor = 0x124050; p.ice = false; p.cloudCover = 0.15f; p.atmoColor = 0xFFB878; p.landScale = 1.3f;
        p.bands = { { 0xD8B070, 0 }, { 0xC08848, 0.25f }, { 0x9A5A30, 0.55f }, { 0x6A3A22, 0.8f } };
    } else if (kind == "Ice") {
        p.seaLevel = 0.45f; p.oceanColor = 0x3A7AA8; p.deepColor = 0x163A5A; p.iceCaps = 0.55f; p.cloudCover = 0.5f; p.atmoColor = 0xA8E0FF;
        p.bands = { { 0xC8DCE8, 0 }, { 0xA8C0D0, 0.35f }, { 0xF4FAFF, 0.6f } };
    } else if (kind == "Lava") {
        p.seaLevel = 0.42f; p.oceanColor = 0xFF6A18; p.deepColor = 0xC02808; p.oceanShine = 0.1f; p.ice = false; p.cloudColor = 0x3A302C; p.cloudCover = 0.35f;
        p.atmoColor = 0xFF5A20; p.night = true; p.nightColor = 0xFF5A10; p.nightAmount = 0.8f; p.roughness = 0.75f;
        p.bands = { { 0x2A2220, 0 }, { 0x3E322C, 0.4f }, { 0x18120F, 0.75f } };
    } else if (kind == "Gas giant") {
        p.ocean = false; p.ice = false; p.landScale = 0.6f; p.roughness = 0.2f; p.cloudCover = 0.2f; p.cloudSpeed = 0.8f; p.atmoColor = 0xFFD8A0; p.rings = true;
        p.bands = { { 0xC89A68, 0 }, { 0xE8C898, 0.2f }, { 0xA87048, 0.4f }, { 0xF0DEC0, 0.55f }, { 0xB88458, 0.75f }, { 0xE0BC8C, 0.9f } };
    } else if (kind == "Alien jungle") {
        p.seaLevel = 0.48f; p.oceanColor = 0x6A2A8A; p.deepColor = 0x2A0E40; p.iceCaps = 0.08f; p.iceColor = 0xC8FFE8; p.cloudColor = 0xE0FFD8; p.atmoColor = 0x7CFF9A;
        p.night = true; p.nightColor = 0x60FFC0; p.nightAmount = 0.5f;
        p.bands = { { 0x2E8A5A, 0 }, { 0x1E6A48, 0.3f }, { 0x5AC878, 0.55f }, { 0x8A5AC8, 0.82f } };
    } else {
        p.name = "Earth-like";
        p.bands = { { 0xD8C890, 0 }, { 0x5A9A48, 0.06f }, { 0x2E6A34, 0.3f }, { 0x7A6A58, 0.6f }, { 0xF2F4F8, 0.85f } };
    }
    return p;
}

int PlanetSet::find(const std::string& name) const { for (int i = 0; i < (int)planets.size(); i++) if (planets[i].name == name) return i; return -1; }

bool savePlanets(const std::string& path, const PlanetSet& set) {
    std::ofstream f(path); if (!f) return false;
    f << "planets 1\n";
    for (const Planet& p : set.planets) {
        f << "planet " << escText(p.name) << " " << p.seed << " " << p.landScale << " " << p.roughness << " " << p.pixels << "\n";
        f << "ocean " << (p.ocean ? 1 : 0) << " " << p.oceanColor << " " << p.deepColor << " " << p.seaLevel << " " << p.oceanShine << "\n";
        for (const PlanetBand& b : p.bands) f << "band " << b.color << " " << b.from << "\n";
        f << "ice " << (p.ice ? 1 : 0) << " " << p.iceColor << " " << p.iceCaps << "\n";
        f << "clouds " << (p.clouds ? 1 : 0) << " " << p.cloudColor << " " << p.cloudCover << " " << p.cloudPuff << " " << p.cloudSpeed << " " << p.cloudShadows << "\n";
        f << "atmo " << (p.atmo ? 1 : 0) << " " << p.atmoColor << " " << p.atmoThick << " " << p.atmoGlow << "\n";
        f << "night " << (p.night ? 1 : 0) << " " << p.nightColor << " " << p.nightAmount << "\n";
        f << "rings " << (p.rings ? 1 : 0) << " " << p.ringColor << " " << p.ringColor2 << " " << p.ringInner << " " << p.ringOuter << " " << p.ringTilt << "\n";
    }
    return (bool)f;
}
bool loadPlanets(const std::string& path, PlanetSet& set) {
    std::ifstream f(path); if (!f) return false;
    set = PlanetSet{}; std::string line; Planet* cur = nullptr;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k; int on = 1;
        if (k == "planet") { Planet p; std::string n; l >> n >> p.seed >> p.landScale >> p.roughness >> p.pixels; p.name = unescText(n); p.bands.clear(); set.planets.push_back(p); cur = &set.planets.back(); continue; }
        if (!cur) continue;
        Planet& p = *cur;
        if (k == "ocean") { l >> on >> p.oceanColor >> p.deepColor >> p.seaLevel >> p.oceanShine; p.ocean = on != 0; }
        else if (k == "band") { PlanetBand b; if (l >> b.color >> b.from && (int)p.bands.size() < PLANET_BANDS) p.bands.push_back(b); }
        else if (k == "ice") { l >> on >> p.iceColor >> p.iceCaps; p.ice = on != 0; }
        else if (k == "clouds") { l >> on >> p.cloudColor >> p.cloudCover >> p.cloudPuff >> p.cloudSpeed >> p.cloudShadows; p.clouds = on != 0; }
        else if (k == "atmo") { l >> on >> p.atmoColor >> p.atmoThick >> p.atmoGlow; p.atmo = on != 0; }
        else if (k == "night") { l >> on >> p.nightColor >> p.nightAmount; p.night = on != 0; }
        else if (k == "rings") { l >> on >> p.ringColor >> p.ringColor2 >> p.ringInner >> p.ringOuter >> p.ringTilt; p.rings = on != 0; }
    }
    for (Planet& p : set.planets) { std::sort(p.bands.begin(), p.bands.end(), [](const PlanetBand& a, const PlanetBand& b) { return a.from < b.from; }); p.pixels = std::clamp(p.pixels, 0, 1024); }
    return true;
}

} // namespace mc
