// planets.h : planets you design in layers (ocean, terrain bands, ice, clouds, atmosphere, night lights, rings),
// drawn by one shader: a spinning world behind the main menu ("Space" backgrounds)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mc {

struct PlanetBand { uint32_t color = 0x6A9A4A; float from = 0; };   // a terrain layer: its colour, from this height above the sea (0..1)
constexpr int PLANET_BANDS = 8;

struct Planet {
    std::string name = "Planet";
    uint32_t seed = 1;
    // the land: continents from noise
    float landScale = 1.6f;     // continent size (bigger: more, smaller continents)
    float roughness = 0.5f;     // how ragged the coasts and the bands' edges are (0 smooth .. 1 rough)
    // the ocean
    bool ocean = true; uint32_t oceanColor = 0x1E5A8C, deepColor = 0x0A2240; float seaLevel = 0.52f, oceanShine = 0.6f;
    std::vector<PlanetBand> bands;   // terrain layers, low to high (up to 8)
    // ice caps
    bool ice = true; uint32_t iceColor = 0xEAF4FF; float iceCaps = 0.16f;   // how far down from the poles
    // clouds
    bool clouds = true; uint32_t cloudColor = 0xFFFFFF; float cloudCover = 0.45f, cloudPuff = 0.5f, cloudSpeed = 0.3f, cloudShadows = 0.5f;   // speed: faster (+) or slower (-) than the ground turns
    // the atmosphere: a glowing rim
    bool atmo = true; uint32_t atmoColor = 0x6CB8FF; float atmoThick = 0.08f, atmoGlow = 1;
    // the night side
    bool night = false; uint32_t nightColor = 0xFFB050; float nightAmount = 0.6f;   // city lights / lava glow on the dark half
    // rings
    bool rings = false; uint32_t ringColor = 0xC8B090, ringColor2 = 0x7A6450; float ringInner = 1.4f, ringOuter = 2.2f, ringTilt = 18;   // radii (the planet is 1), tilt (degrees)
    // the look
    int pixels = 0;             // 0 smooth, else pixelated: this many pixels across the planet
};
Planet defaultPlanet();         // an Earth-like world: ocean, sand, grass, forest, rock, snow
Planet planetPreset(const std::string& kind);   // "Earth-like", "Desert", "Ice", "Lava", "Gas giant", "Alien jungle"
const std::vector<std::string>& planetPresetNames();

struct PlanetSet {
    std::vector<Planet> planets;
    int find(const std::string& name) const;   // -1: none
};
bool savePlanets(const std::string& path, const PlanetSet& set);
bool loadPlanets(const std::string& path, PlanetSet& set);   // false: no file (the set stays empty)

} // namespace mc
