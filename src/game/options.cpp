#include "options.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mc {

namespace {
struct Field { float* f = nullptr; int* i = nullptr; bool* b = nullptr; };
Field field(Settings& s, const std::string& k) {
    Field r;
    if (k == "volume") r.f = &s.soundVolume; else if (k == "vol_weapons") r.f = &s.volWeapons; else if (k == "vol_movement") r.f = &s.volMovement;
    else if (k == "vol_enemies") r.f = &s.volEnemies; else if (k == "vol_world") r.f = &s.volWorld;
    else if (k == "fullscreen") r.b = &s.fullscreen; else if (k == "vsync") r.b = &s.vsync; else if (k == "brightness") r.f = &s.brightness;
    else if (k == "fov") r.f = &s.fov; else if (k == "ui_scale") r.f = &s.uiScale;
    else if (k == "render_distance") r.i = &s.renderDistance; else if (k == "vertical_distance") r.i = &s.verticalDistance;
    else if (k == "shadows") r.i = &s.shadowQuality; else if (k == "shadow_res") r.i = &s.shadowRes; else if (k == "shadow_distance") r.f = &s.shadowDistance;
    else if (k == "shadow_soft") r.i = &s.shadowSoft; else if (k == "shadow_softness") r.f = &s.shadowSoftness;
    else if (k == "clouds") r.f = &s.cloudCover; else if (k == "cloud_shadows") r.b = &s.cloudShadows; else if (k == "fog") r.f = &s.groundFog;
    else if (k == "bloom") r.f = &s.bloom; else if (k == "reflections") r.i = &s.reflections; else if (k == "ao") r.i = &s.ambientOcclusion; else if (k == "ao_strength") r.f = &s.aoStrength;
    else if (k == "swaying") r.b = &s.swaying; else if (k == "hud_glow") r.b = &s.hudGlow;
    else if (k == "bumps") r.f = &s.bumps; else if (k == "coat") r.f = &s.coat; else if (k == "materials") r.b = &s.materials; else if (k == "guess_materials") r.b = &s.guessMaterials;
    else if (k == "time_of_day") r.f = &s.timeOfDay; else if (k == "day_cycle") r.b = &s.dayCycle; else if (k == "block_preview") r.b = &s.blockPreview;
    else if (k == "sensitivity") r.f = &s.mouseSensitivity; else if (k == "invert_y") r.b = &s.invertY; else if (k == "lock_mode") r.i = &s.lockMode; else if (k == "cave_culling") r.b = &s.caveCulling;
    return r;
}
float readField(const Field& f) { return f.f ? *f.f : f.i ? (float)*f.i : f.b ? (*f.b ? 1.f : 0.f) : 0.f; }
void writeField(const Field& f, float v) { if (f.f) *f.f = v; else if (f.i) *f.i = (int)std::lround(v); else if (f.b) *f.b = v > 0.5f; }

OptionDef slider(const char* key, const char* label, const char* group, const char* section, float lo, float hi, float step, const char* fmt, const char* zero = "", const char* tip = "") {
    OptionDef d; d.key = key; d.label = label; d.group = group; d.section = section; d.type = OptType::Slider; d.lo = lo; d.hi = hi; d.step = step; d.fmt = fmt; d.zero = zero; d.tip = tip; return d;
}
OptionDef toggle(const char* key, const char* label, const char* group, const char* section, const char* tip = "") {
    OptionDef d; d.key = key; d.label = label; d.group = group; d.section = section; d.type = OptType::Toggle; d.tip = tip; return d;
}
OptionDef choice(const char* key, const char* label, const char* group, const char* section, std::vector<std::string> ch, std::vector<float> vals = {}, const char* tip = "") {
    OptionDef d; d.key = key; d.label = label; d.group = group; d.section = section; d.type = OptType::Choice; d.choices = std::move(ch); d.values = std::move(vals); d.tip = tip; return d;
}
OptionDef needs(OptionDef d, const char* n) { d.needs = n; return d; }
OptionDef editorOnly(OptionDef d) { d.inGame = false; return d; }
OptionDef reset(const char* group) { OptionDef d; d.key = std::string("reset_") + group; d.label = "Back to the start values"; d.group = group; d.section = ""; d.type = OptType::Reset; return d; }
} // namespace

const std::vector<std::string>& optionGroups() { static const std::vector<std::string> g = { "Sound", "Graphics", "Controls" }; return g; }

const std::vector<OptionDef>& optionDefs() {
    static const std::vector<OptionDef> defs = [] {
        std::vector<OptionDef> v;
        const char* S = "Sound"; const char* G = "Graphics"; const char* C = "Controls";
        v.push_back(slider("volume", "Master volume", S, "Volume", 0, 2, 0.05f, "%", "silent"));
        v.push_back(slider("vol_weapons", "Weapons", S, "Volume", 0, 2, 0.05f, "%", "silent", "Beams, charging, missiles, their hits."));
        v.push_back(slider("vol_movement", "Movement", S, "Volume", 0, 2, 0.05f, "%", "silent", "Steps, jumps, landing, the Morph Ball and its bombs."));
        v.push_back(slider("vol_enemies", "Enemies", S, "Volume", 0, 2, 0.05f, "%", "silent", "Enemies noticing you, attacking, getting hurt, dying."));
        v.push_back(slider("vol_world", "World", S, "Volume", 0, 2, 0.05f, "%", "silent", "Doors, events and everything else."));
        v.push_back(reset(S));

        v.push_back(toggle("fullscreen", "Fullscreen", G, "Screen", "Borderless, over the whole screen."));
        v.push_back(toggle("vsync", "VSync", G, "Screen", "Waits for the screen: no tearing, frame rate up to the screen's."));
        v.push_back(slider("brightness", "Brightness", G, "Screen", 0.3f, 2.5f, 0.05f, "%"));
        v.push_back(slider("fov", "Field of view", G, "Screen", 50, 110, 1, "%.0f"));
        v.push_back(slider("ui_scale", "Interface size", G, "Screen", 0.75f, 2, 0.05f, "%"));
        v.push_back(slider("render_distance", "Render distance", G, "Distance", 2, 32, 1, "%.0f chunks"));
        v.push_back(slider("vertical_distance", "Up / down distance", G, "Distance", 1, 12, 1, "%.0f chunks"));
        v.push_back(toggle("cave_culling", "Skip what is behind walls", G, "Distance", "Chunks you cannot see from where you are (rock, closed caves, the other side of walls) are not drawn. Off: everything in range is drawn."));
        v.push_back(toggle("shadows", "Shadows", G, "Shadows"));
        v.push_back(needs(choice("shadow_res", "Shadow resolution", G, "Shadows", { "1024", "1536", "2048", "3072", "4096 (heavy)" }, { 1024, 1536, 2048, 3072, 4096 },
                                 "The size of each of the four shadow maps. Higher: crisper shadows, more graphics memory."), "shadows"));
        v.push_back(needs(slider("shadow_distance", "Shadow distance", G, "Shadows", 24, 512, 8, "%.0f blocks", "", "How far shadows reach. The sharpness near you stays the same."), "shadows"));
        v.push_back(needs(choice("shadow_soft", "Shadow edges", G, "Shadows", { "Sharp", "Smooth", "Soft like sunlight" }), "shadows"));
        v.push_back(needs(slider("shadow_softness", "Shadow softness", G, "Shadows", 0.25f, 3, 0.05f, "%.2f"), "shadow_soft"));
        v.push_back(slider("clouds", "Clouds", G, "Sky", 0, 1, 0.05f, "%", "none"));
        v.push_back(toggle("cloud_shadows", "Cloud shadows", G, "Sky"));
        v.push_back(slider("fog", "Ground fog", G, "Sky", 0, 2, 0.05f, "%", "none"));
        v.push_back(slider("bloom", "Glow (bloom)", G, "Effects", 0, 1, 0.05f, "%", "off"));
        v.push_back(choice("reflections", "Reflections", G, "Effects", { "Off (only the sky)", "Normal", "High" }));
        v.push_back(choice("ao", "Ambient occlusion", G, "Effects", { "Off (only block corners)", "Normal", "High" }, {}, "Soft shade in corners, under overhangs and where things touch."));
        v.push_back(needs(slider("ao_strength", "Occlusion strength", G, "Effects", 0.2f, 3, 0.1f, "%.1f"), "ao"));
        v.push_back(toggle("swaying", "Leaves sway in the wind", G, "Effects"));
        v.push_back(toggle("hud_glow", "HUD glow", G, "Effects", "The HUD's glow, and its light on the helmet."));
        v.push_back(slider("bumps", "Bumpy textures", G, "Surfaces", 0, 2, 0.05f, "%", "off", "Each texture's light and dark pixels become little bumps that catch the light."));
        v.push_back(slider("coat", "Texture coat", G, "Surfaces", 0, 2, 0.05f, "%", "off", "A fine noise inside each pixel, so big flat pixels look like stone, dirt, wood..."));
        v.push_back(toggle("materials", "Materials (shiny, metal, glowing)", G, "Surfaces"));
        v.push_back(editorOnly(toggle("guess_materials", "Guess materials from texture names", G, "Surfaces",
                                      "Textures you did not set up get a material from their name's words (iron: metal, glass: shiny, lava: glowing...).")));
        v.push_back(editorOnly(slider("time_of_day", "Time of day", G, "Editor", 0, 24, 0.1f, "%.1f h")));
        v.push_back(editorOnly(toggle("day_cycle", "The sun moves (a day is 20 minutes)", G, "Editor")));
        v.push_back(editorOnly(toggle("block_preview", "Show where the next block goes", G, "Editor")));
        v.push_back(reset(G));

        v.push_back(slider("sensitivity", "Mouse sensitivity", C, "Mouse", 0.1f, 3, 0.05f, "%.2f"));
        v.push_back(toggle("invert_y", "Invert up / down", C, "Mouse", "Moving the mouse up looks down."));
        v.push_back(choice("lock_mode", "Lock-on button", C, "Mouse", { "As the game says", "Hold it", "Press to lock, press to let go" }));
        for (int a = 0; a < (int)Act::COUNT; a++) { OptionDef d; d.key = std::string("key_") + actKey((Act)a); d.label = actName((Act)a); d.group = C; d.section = "Keys"; d.type = OptType::Key; d.act = a; v.push_back(d); }
        v.push_back(reset(C));
        return v;
    }();
    return defs;
}

int findOption(const std::string& key) {
    const auto& d = optionDefs(); for (int i = 0; i < (int)d.size(); i++) if (d[i].key == key) return i; return -1;
}

float optionGet(const Settings& s, const OptionDef& d) {
    Field f = field(const_cast<Settings&>(s), d.key); float v = readField(f);
    if (d.type == OptType::Choice && !d.values.empty()) {
        int best = 0; for (int i = 1; i < (int)d.values.size(); i++) if (std::fabs(d.values[i] - v) < std::fabs(d.values[best] - v)) best = i;
        return (float)best;
    }
    return v;
}

void optionSet(Settings& s, const OptionDef& d, float v) {
    Field f = field(s, d.key);
    switch (d.type) {
    case OptType::Slider: writeField(f, std::clamp(v, d.lo, d.hi)); break;
    case OptType::Toggle: writeField(f, v > 0.5f ? 1.f : 0.f); break;
    case OptType::Choice: {
        int n = (int)d.choices.size(); int i = std::clamp((int)std::lround(v), 0, std::max(0, n - 1));
        writeField(f, d.values.empty() ? (float)i : d.values[i]); break; }
    default: break;
    }
}

void optionStep(Settings& s, const OptionDef& d, int dir) {
    float v = optionGet(s, d);
    switch (d.type) {
    case OptType::Slider: { float n = std::round((v + dir * d.step - d.lo) / d.step) * d.step + d.lo; optionSet(s, d, n); break; }
    case OptType::Toggle: optionSet(s, d, v > 0.5f ? 0.f : 1.f); break;
    case OptType::Choice: optionSet(s, d, v + dir); break;
    default: break;
    }
}

std::string optionText(const Settings& s, const OptionDef& d) {
    float v = optionGet(s, d); char b[64];
    switch (d.type) {
    case OptType::Toggle: return v > 0.5f ? "On" : "Off";
    case OptType::Choice: { int i = (int)std::lround(v); return i >= 0 && i < (int)d.choices.size() ? d.choices[i] : "?"; }
    case OptType::Slider:
        if (!d.zero.empty() && v <= d.lo + 1e-4f) return d.zero;
        if (d.fmt == "%") { std::snprintf(b, sizeof b, "%d%%", (int)std::lround(v * 100)); return b; }
        std::snprintf(b, sizeof b, d.fmt.c_str(), v); return b;
    default: return "";
    }
}

bool optionShown(const Settings& s, const OptionDef& d, bool inGame) {
    if (inGame && !d.inGame) return false;
    if (!d.needs.empty()) { int n = findOption(d.needs); if (n >= 0 && optionGet(s, optionDefs()[n]) <= 0) return false; }
    return true;
}

void optionReset(Settings& s, const std::string& group) {
    Settings fresh;
    for (const OptionDef& d : optionDefs()) {
        if (d.group != group || !d.inGame) continue;
        if (d.type == OptType::Key) s.keys.key[d.act][0] = fresh.keys.key[d.act][0], s.keys.key[d.act][1] = fresh.keys.key[d.act][1];
        else if (d.type != OptType::Reset) writeField(field(s, d.key), readField(field(fresh, d.key)));
    }
}

} // namespace mc
