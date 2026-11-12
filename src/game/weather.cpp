#include "weather.h"
#include <algorithm>
#include <cmath>

namespace mc {

const std::vector<WeatherType>& weatherTypes() {
    //                                                      cover dark  rain snow  wind  strikes fog   dim
    static const std::vector<WeatherType> t = {
        { "clear",         "Clear",          { 0.15f, 0.0f, 0.0f, 0.0f, 0.2f, 0,      0.7f, 0.0f } },
        { "partly_cloudy", "Partly cloudy",  { 0.50f, 0.0f, 0.0f, 0.0f, 0.35f, 0,     1.0f, 0.05f } },
        { "overcast",      "Overcast",       { 0.92f, 0.3f, 0.0f, 0.0f, 0.5f, 0,      1.4f, 0.65f } },
        { "foggy",         "Foggy",          { 0.75f, 0.1f, 0.0f, 0.0f, 0.1f, 0,      5.0f, 0.5f } },
        { "drizzle",       "Drizzle",        { 0.88f, 0.3f, 0.3f, 0.0f, 0.4f, 0,      1.8f, 0.6f } },
        { "rain",          "Rain",           { 0.96f, 0.5f, 0.7f, 0.0f, 0.7f, 0,      2.2f, 0.75f } },
        { "thunderstorm",  "Thunderstorm",   { 1.00f, 0.75f, 0.85f, 0.0f, 1.1f, 3,    2.5f, 0.85f } },
        { "intense_storm", "Intense storm",  { 1.00f, 0.95f, 1.0f, 0.0f, 2.0f, 14,   3.5f, 0.93f } },
        { "snow",          "Snow",           { 0.92f, 0.25f, 0.0f, 0.7f, 0.35f, 0,    2.0f, 0.6f } },
        { "blizzard",      "Blizzard",       { 1.00f, 0.6f, 0.0f, 1.0f, 2.0f, 0,      6.0f, 0.85f } },
    };
    return t;
}
int findWeather(const std::string& key) {
    for (size_t i = 0; i < weatherTypes().size(); i++) if (weatherTypes()[i].key == key) return (int)i;
    return -1;
}

float Weather::rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.f; }

void Weather::set(int i, bool instant) {
    target = std::max(0, std::min((int)weatherTypes().size() - 1, i));
    if (instant) now = weatherTypes()[target].look;
    timer = 180 + rnd() * 420;
}

void Weather::update(double dt, V3 player) {
    // fade towards the target (about 20 seconds for a full change)
    const WeatherLook& g = weatherTypes()[target].look;
    float k = 1 - (float)std::exp(-dt / 7.0);
    auto mv = [&](float& a, float b) { a += (b - a) * k; };
    mv(now.cover, g.cover); mv(now.dark, g.dark); mv(now.rain, g.rain); mv(now.snow, g.snow); mv(now.wind, g.wind);
    mv(now.strikesPerMin, g.strikesPerMin); mv(now.fog, g.fog); mv(now.dim, g.dim);

    // by itself: every 3 to 10 minutes, mostly to a "neighbouring" weather (clear -> cloudy -> rain -> storm ...)
    if (autoChange && (timer -= dt) <= 0) {
        static const int next[10][4] = { { 1, 1, 0, 3 }, { 0, 2, 2, 4 }, { 1, 4, 5, 8 }, { 1, 2, 1, 0 }, { 5, 2, 1, 5 },
                                         { 4, 6, 2, 6 }, { 5, 7, 5, 2 }, { 6, 6, 5, 6 }, { 2, 9, 8, 1 }, { 8, 8, 2, 8 } };
        int n = next[target][std::min(3, (int)(rnd() * 4))];
        set(n, false);
    }

    // surfaces get wet in rain and dry afterwards; snow builds up and melts
    wetness = std::min(1.f, std::max(0.f, wetness + (float)dt * (now.rain > 0.1f ? now.rain / 25 : -1.f / 90)));
    snowCover = std::min(1.f, std::max(0.f, snowCover + (float)dt * (now.snow > 0.1f ? now.snow / 90 : -1.f / 240)));

    // lightning
    flash *= (float)std::exp(-dt * 7);
    boltAge += dt;
    if (now.strikesPerMin > 0.05f && rnd() < now.strikesPerMin / 60.0 * dt) {
        float a = rnd() * 6.2831853f, d = 40 + rnd() * 220;
        boltPos = { player.x + std::cos(a) * d, player.y - 2, player.z + std::sin(a) * d };
        boltAge = 0; boltSeed = (uint32_t)(rnd() * 4e9f) | 1; strikes++;
        flash = std::max(flash, 0.6f + 0.4f * rnd() * (1 - d / 300));
    }
}

} // namespace mc
