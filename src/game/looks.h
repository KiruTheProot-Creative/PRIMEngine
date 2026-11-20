#pragma once
// Looks: how the picture is "developed", like a film or a colour grade - the tone curve (how bright light becomes screen
// colour), exposure and how the eye adapts to dark and bright, bloom, colours in the shadows and highlights, warmth,
// lens fringes, grain, vignette. Presets ("Neon night", "Toxic", "Inferno" ...) and your own (looks.txt). A world has a
// look; an environment zone can have its own (it fades in when you walk in). Plain C++, tested.
#include <cstdint>
#include <string>
#include <vector>

namespace mc {

enum class ToneCurve { Classic, Film, Neutral, COUNT };   // Classic: the old filmic curve; Film: bright colours stay colourful (AgX); Neutral: true colours
const char* toneCurveName(ToneCurve t);

struct ColorLook {
    std::string name = "Natural";
    ToneCurve curve = ToneCurve::Film;
    float exposure = 0;                       // stops: + brighter, - darker
    float adapt = 0.5f, adaptSpeed = 1.5f;    // how much the eye adapts to dark and bright places (0: not at all), how fast
    float contrast = 1, saturation = 1;
    float warmth = 0;                         // -1 cold (blue) .. 1 warm (orange)
    uint32_t shadows = 0x3050A0; float shadowsAmount = 0;      // a colour in the dark parts
    uint32_t highlights = 0xFFB070; float highlightsAmount = 0; // a colour in the bright parts
    float bloom = 1, bloomSize = 1;           // how strong the glow around bright things is (times the setting), how wide it spreads
    float fringes = 0;                        // lens colour fringes toward the edges
    float grain = 0;                          // film grain
    float vignette = 0.18f;                   // darker corners
    // the air: fog of this look (on top of the weather's ground fog) and how much lights glow in it (beams, halos)
    float fog = 0;                            // how thick (0: none)
    float fogHeight = 64, fogThin = 0;        // it thins out above this height (world Y) over this many blocks (0: the same everywhere)
    uint32_t fogColor = 0xB0B8C8; float fogGlow = 0;   // its colour, and how much it glows by itself (city haze)
    float lightFog = 0.35f;                   // lights in the air: their glow and beams (0: none)
    float bounce = 1;                         // bounce light: lit and glowing surfaces light the ones near them (0: none)
};
ColorLook lerp(const ColorLook& a, const ColorLook& b, float t);
const std::vector<ColorLook>& lookPresets();   // Natural, Classic, Neon night, Toxic, Inferno, Chozo dusk, Frozen, Noir

struct LookSet {
    std::vector<ColorLook> looks;              // your own (and edited presets)
    const ColorLook* find(const std::string& name) const;   // your own first, then the presets; nullptr: none
    ColorLook get(const std::string& name) const;            // (unknown: Natural)
    std::vector<std::string> names() const;                 // yours, then the presets you have not made your own
};
bool saveLooks(const std::string& path, const LookSet& s);
bool loadLooks(const std::string& path, LookSet& s);

// the look around you: a new one fades in over a second and a half
class LookState {
public:
    ColorLook now; std::string active;
    void update(double dt, const ColorLook& target, const std::string& key, bool instant = false);
private:
    ColorLook from, to; float t = 1;
};

} // namespace mc
