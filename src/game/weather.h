// weather.h : the weather of a world. Changes fade in smoothly; it can also change by itself.
#pragma once
#include "../core/math.h"
#include <cstdint>
#include <string>
#include <vector>

namespace mc {

struct WeatherLook {
    float cover = 0.5f;        // how much of the sky has clouds (0..1)
    float dark = 0;            // storm clouds: darker and thicker (0..1)
    float rain = 0, snow = 0;  // how much falls (0..1)
    float wind = 0.3f;         // 0 calm .. 2 gale
    float strikesPerMin = 0;   // lightning
    float fog = 1;             // multiplies the ground fog
    float dim = 0;             // how much sunlight the clouds swallow (0..1)
};
struct WeatherType { std::string key, name; WeatherLook look; };
const std::vector<WeatherType>& weatherTypes();
int findWeather(const std::string& key);   // -1 if unknown

class Weather {
public:
    int target = 1;                // the weather it is turning into (index into weatherTypes())
    bool autoChange = false;       // picks a new weather every few minutes
    WeatherLook now;               // what it looks like right now (fading towards the target)
    float wetness = 0, snowCover = 0;   // surfaces: 0..1, build up and dry / melt slowly
    float flash = 0;               // lightning flash brightness, fades fast
    // the last lightning bolt: where it hit (world), when (seconds ago), its random shape
    V3 boltPos{ 0, 0, 0 }; double boltAge = 99; uint32_t boltSeed = 0; int strikes = 0;

    Weather() { set(1, true); }
    void set(int index, bool instant);
    void update(double dt, V3 player);
    const WeatherType& type() const { return weatherTypes()[target]; }
    double secondsToChange() const { return timer; }
    void seed(uint32_t s) { rng = s ? s : 1; }

private:
    double timer = 300;
    uint32_t rng = 0x9E3779B9u;
    float rnd();   // 0..1
};

} // namespace mc
