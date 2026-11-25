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

// ---- scan entries: written once (scans.txt), with an ID; any number of objects point to one (scan the door once: every
// door with that ID counts as scanned)
struct ScanDef {
    int id = 0;
    std::string name, category = "Research", text;
    bool important = false;                                 // a red marker: not to be missed
    float time = 0;                                         // seconds to scan (0: the visor's own time)
};
struct ScanLibrary {
    std::vector<ScanDef> defs;
    ScanDef* find(int id);
    const ScanDef* find(int id) const;
    int add(ScanDef d);                                     // gives it the next free ID (if it has none or a taken one); returns the ID
    const ScanDef* findName(const std::string& name) const;
};
bool saveScanLibrary(const std::string& path, const ScanLibrary& l);
bool loadScanLibrary(const std::string& path, ScanLibrary& l);

// ---- the scan index: what has been scanned (one entry per ID)
struct ScanEntry { int id = 0; std::string name, category, text, model; bool important = false; };
struct ScanLog {
    std::vector<ScanEntry> entries;                         // in the order they were scanned
    bool has(int id) const;
    bool add(const ScanEntry& e);                           // true if it was new (by ID)
    std::vector<std::string> categories() const;            // in the order they first came
    void clear() { entries.clear(); }
};
// What is on the visor's glass (not its look): steam fogging it up, frost creeping in from the edges, splats that slide off,
// static from a shock, your own face in a bright flash - and how the rain drops on it react to where you look and how you turn.
enum class GlassHit { GooSplat, PhazonSplat, Static, FogUp, Frost, Face, COUNT };
const char* glassHitName(GlassHit h);
std::vector<std::string> glassHitNames();
int findGlassHit(const std::string& name);   // by name (event cards); -1 unknown
struct VisorGlass {
    float fog = 0, frost = 0;            // 0..1: fogged up (steam), frost from the edges (the cold)
    float splat = 0, splatAge = 99;      // a splat: how much of it is left (0..1), seconds since it hit
    uint32_t splatColor = 0x7BC84A, splatSeed = 1; bool splatGlows = false;
    float shock = 0, face = 0;           // static (0..1), your face in the glass (0..1)
    float lookUp = 0, turn = 0;          // the rain on it: how far up you look (-1..1), how fast the view turns sideways (-1..1, smoothed)
    void hit(GlassHit h, float strength, uint32_t seed);
    // steam: how deep the eye is in steam (0..1); cold: how cold the air is here (0..1); flash: a bright flash now (0..1)
    void update(double dt, float steam, float cold, float flash, bool underwater, float pitchDeg, float yawRateDeg);
    void clear() { float l = lookUp, t = turn; *this = VisorGlass{}; lookUp = l; turn = t; }
};

// text with line breaks and spaces, kept on one line of a file
std::string escText(const std::string& s);
std::string unescText(const std::string& s);

} // namespace mc
