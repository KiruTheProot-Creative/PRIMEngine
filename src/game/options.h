#pragma once
// The player's options (Sound, Graphics, Controls): one list that both the in-visor Options page and the editor's
// Settings window are drawn from, with how to read, change and show each one. Plain C++, tested.
#include "game.h"
#include <string>
#include <vector>

namespace mc {

enum class OptType { Slider, Toggle, Choice, Key, Reset, Preset };   // Preset: the game's quality presets (sets many options at once)

struct OptionDef {
    std::string key, label, group, section, tip;   // group: "Sound", "Graphics", "Controls"; section: a heading inside it
    OptType type = OptType::Slider;
    float lo = 0, hi = 1, step = 0.05f;            // Slider
    std::string fmt = "%.2f";                       // Slider: how the number shows ("%" : as a percentage)
    std::string zero;                               // Slider: what it says at the bottom (e.g. "off")
    std::vector<std::string> choices;               // Choice: the names ...
    std::vector<float> values;                      // ... and what each one sets (empty: 0, 1, 2...)
    int act = -1;                                   // Key: which Act
    std::string needs;                              // shown only while this option is on (above 0)
    bool inGame = true;                             // false: only in the editor's Settings window (time of day...)
};

const std::vector<OptionDef>& optionDefs();
const std::vector<std::string>& optionGroups();     // "Sound", "Graphics", "Controls"
int findOption(const std::string& key);             // -1: none

float optionGet(const Settings& s, const OptionDef& d);          // Choice: the index of the current choice
void optionSet(Settings& s, const OptionDef& d, float v);        // kept inside its range / choices
void optionStep(Settings& s, const OptionDef& d, int dir);       // one step left (-1) or right (+1): sliders and choices stop at the ends, a toggle flips
std::string optionText(const Settings& s, const OptionDef& d);   // "70", "On", "2048", "75%"... (not for keys)
bool optionShown(const Settings& s, const OptionDef& d, bool inGame);
void optionReset(Settings& s, const std::string& group);         // the group's options back to how they start

// ---- quality presets: named sets of the heavy graphics options. Each game has its own (the game maker can rename them and
// set them from their own settings); the options list shows the one that matches, or "Custom".
struct QualityPreset { std::string name; std::vector<std::pair<std::string, float>> values; };
const std::vector<std::string>& presetKeys();                   // the options a preset sets
std::vector<QualityPreset> defaultPresets();                    // Very low, Low, Medium, High, Ultra, Prime
QualityPreset presetFrom(const Settings& s, const std::string& name);   // the current settings as a preset
void applyPreset(Settings& s, const QualityPreset& p);
int matchingPreset(const Settings& s, const std::vector<QualityPreset>& ps);   // -1: none (Custom)
bool savePresets(const std::string& path, const std::vector<QualityPreset>& ps);
bool loadPresets(const std::string& path, std::vector<QualityPreset>& ps);
extern std::vector<QualityPreset>* gamePresets;                  // the game's own (set by the app; the Preset option shows and steps through these)

} // namespace mc
