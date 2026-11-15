#pragma once
// Visors (Player editor > Visors): Combat, Scan, Thermal, X-Ray. V switches between the ones you have. Each visor has its
// own look (a post-processing pass over the world picture, never over the HUD) and the HUD chooses per element which
// visors show it. The Scan Visor scans objects that have a scan entry: what was scanned goes into the scan index (the
// Logbook in the pause menu). Plain C++, tested.
#include <cstdint>
#include <string>
#include <vector>

namespace mc {

enum class VisorKind { Combat, Scan, Thermal, XRay, COUNT };
const char* visorName(VisorKind v);
constexpr int VISORS = (int)VisorKind::COUNT;
constexpr uint8_t ALL_VISORS = (1 << VISORS) - 1;

struct VisorFx {   // the look over the world picture
    uint32_t tint = 0xFFFFFF; float tintAmount = 0;        // colours the picture (0: not at all, 1: only this colour, by brightness)
    float saturation = 1, contrast = 1, brightness = 1;     // 0 grey .. 1 as it is .. more: stronger
    float vignette = 0;                                     // darker edges
    float lines = 0, lineCount = 240, lineSpeed = 0;        // scan lines: how strong, how many down the screen, how fast they move
    float noise = 0;                                        // flickering grain
    uint32_t edgeColor = 0xFFFFFF; float edges = 0;         // outlines where brightness changes sharply
    float chroma = 0;                                       // colour fringes (pixels)
    float heat = 0;                                         // 0..1: a heat palette by brightness (bright = hot)
};

struct VisorSettings {
    VisorFx fx[VISORS];
    float scanRange = 24, scanTime = 1.2f;                  // how far the Scan Visor reaches (blocks), seconds to scan (an object can have its own)
    uint32_t markColor = 0xFF9A3A, importantColor = 0xFF3030, doneColor = 0x5CA8FF;   // scan markers: not scanned / important / scanned
    float switchFlash = 0.25f;                              // seconds of a bright flash when switching visors
};
VisorSettings defaultVisors();
bool saveVisors(const std::string& path, const VisorSettings& v);
bool loadVisors(const std::string& path, VisorSettings& v);

// ---- the scan index: what has been scanned (one entry per name: several objects can share an entry)
struct ScanEntry { std::string name, category, text, model; bool important = false; };
struct ScanLog {
    std::vector<ScanEntry> entries;                         // in the order they were scanned
    bool has(const std::string& name) const;
    bool add(const ScanEntry& e);                           // true if it was new
    std::vector<std::string> categories() const;            // in the order they first came
    void clear() { entries.clear(); }
};
// text with line breaks and spaces, kept on one line of a file
std::string escText(const std::string& s);
std::string unescText(const std::string& s);

} // namespace mc
