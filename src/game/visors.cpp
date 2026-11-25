#include "visors.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <sstream>

namespace mc {

const char* visorName(VisorKind v) {
    static const char* n[] = { "Combat Visor", "Scan Visor", "Thermal Visor", "X-Ray Visor" };
    int i = (int)v; return i >= 0 && i < VISORS ? n[i] : "?";
}

VisorSettings defaultVisors() {
    VisorSettings s;
    VisorFx& sc = s.fx[(int)VisorKind::Scan];   // cool blue, calmer colours, slow scan lines, outlines
    sc.tint = 0x6CC8FF; sc.tintAmount = 0.35f; sc.saturation = 0.55f; sc.contrast = 1.1f; sc.vignette = 0.25f;
    sc.lines = 0.2f; sc.lineCount = 220; sc.lineSpeed = 1.5f; sc.edges = 0.35f; sc.edgeColor = 0x9FE8FF;
    VisorFx& th = s.fx[(int)VisorKind::Thermal];   // bright = hot
    th.heat = 1; th.contrast = 1.2f; th.noise = 0.04f; th.lines = 0.08f; th.lineCount = 300; th.vignette = 0.3f;
    VisorFx& xr = s.fx[(int)VisorKind::XRay];   // grey-white, hard outlines
    xr.saturation = 0; xr.tint = 0xDDEEFF; xr.tintAmount = 0.3f; xr.contrast = 1.6f; xr.brightness = 0.9f; xr.edges = 0.8f; xr.noise = 0.05f; xr.vignette = 0.3f;
    return s;
}

static void writeFx(std::ostream& f, int i, const VisorFx& x) {
    f << "fx " << i << " " << x.tint << " " << x.tintAmount << " " << x.saturation << " " << x.contrast << " " << x.brightness << " " << x.vignette << " "
      << x.lines << " " << x.lineCount << " " << x.lineSpeed << " " << x.noise << " " << x.edgeColor << " " << x.edges << " " << x.chroma << " " << x.heat << "\n";
}
bool saveVisors(const std::string& path, const VisorSettings& v) {
    std::ofstream f(path); if (!f) return false;
    f << "visors 1\n";
    f << "scan " << v.scanRange << " " << v.scanTime << " " << v.markColor << " " << v.importantColor << " " << v.doneColor << " " << v.switchFlash << "\n";
    for (int i = 0; i < VISORS; i++) writeFx(f, i, v.fx[i]);
    return (bool)f;
}
bool loadVisors(const std::string& path, VisorSettings& v) {
    std::ifstream f(path); if (!f) return false;
    v = defaultVisors(); std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k;
        if (k == "scan") { VisorSettings t = v; if (l >> t.scanRange >> t.scanTime >> t.markColor >> t.importantColor >> t.doneColor >> t.switchFlash) v = t; }
        else if (k == "fx") {
            int i = -1; VisorFx x; l >> i;
            if (l >> x.tint >> x.tintAmount >> x.saturation >> x.contrast >> x.brightness >> x.vignette >> x.lines >> x.lineCount >> x.lineSpeed >> x.noise >> x.edgeColor >> x.edges >> x.chroma >> x.heat && i >= 0 && i < VISORS) v.fx[i] = x;
        }
    }
    v.scanRange = std::clamp(v.scanRange, 1.f, 500.f); v.scanTime = std::clamp(v.scanTime, 0.f, 30.f);
    return true;
}

bool ScanLog::has(int id) const {
    for (const ScanEntry& e : entries) if (e.id == id) return true;
    return false;
}
bool ScanLog::add(const ScanEntry& e) {
    if (e.id <= 0 || has(e.id)) return false;
    entries.push_back(e); return true;
}

ScanDef* ScanLibrary::find(int id) { for (ScanDef& d : defs) if (d.id == id) return &d; return nullptr; }
const ScanDef* ScanLibrary::find(int id) const { for (const ScanDef& d : defs) if (d.id == id) return &d; return nullptr; }
const ScanDef* ScanLibrary::findName(const std::string& name) const { for (const ScanDef& d : defs) if (d.name == name) return &d; return nullptr; }
int ScanLibrary::add(ScanDef d) {
    if (d.id <= 0 || find(d.id)) { int m = 0; for (const ScanDef& x : defs) m = std::max(m, x.id); d.id = m + 1; }
    defs.push_back(d); return d.id;
}
bool saveScanLibrary(const std::string& path, const ScanLibrary& l) {
    std::ofstream f(path); if (!f) return false;
    f << "scans 1\n";
    for (const ScanDef& d : l.defs) f << "scan " << d.id << " " << escText(d.name) << " " << escText(d.category) << " " << escText(d.text) << " " << (d.important ? 1 : 0) << " " << d.time << "\n";
    return (bool)f;
}
bool loadScanLibrary(const std::string& path, ScanLibrary& l) {
    std::ifstream f(path); if (!f) return false;
    l = ScanLibrary{}; std::string line;
    while (std::getline(f, line)) {
        std::istringstream s(line); std::string k; s >> k; if (k != "scan") continue;
        ScanDef d; std::string n, c, t; int imp = 0;
        if (s >> d.id >> n >> c >> t >> imp >> d.time) { d.name = unescText(n); d.category = unescText(c); d.text = unescText(t); d.important = imp != 0; d.time = std::max(0.f, d.time); if (d.id > 0 && !l.find(d.id)) l.defs.push_back(d); }
    }
    return true;
}
std::vector<std::string> ScanLog::categories() const {
    std::vector<std::string> c;
    for (const ScanEntry& e : entries) if (std::find(c.begin(), c.end(), e.category) == c.end()) c.push_back(e.category);
    return c;
}

std::string escText(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '%') o += "%25"; else if (c == ' ') o += "%20"; else if (c == '\n') o += "%0A"; else if (c == '\r') {} else if (c == '\t') o += "%09"; else o += c; }
    return o.empty() ? "-" : o;
}
std::string unescText(const std::string& s) {
    if (s == "-") return "";
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) { o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16); i += 2; }
        else o += s[i];
    }
    return o;
}

// ---------------------------------------------------------------- the visor's glass
const char* glassHitName(GlassHit h) {
    static const char* n[] = { "Goo splat", "Phazon splat", "Static", "Fog up", "Frost", "Face in the glass" };
    int i = (int)h; return i >= 0 && i < (int)GlassHit::COUNT ? n[i] : "?";
}
std::vector<std::string> glassHitNames() { std::vector<std::string> v; for (int i = 0; i < (int)GlassHit::COUNT; i++) v.push_back(glassHitName((GlassHit)i)); return v; }
int findGlassHit(const std::string& name) { for (int i = 0; i < (int)GlassHit::COUNT; i++) if (name == glassHitName((GlassHit)i)) return i; return -1; }
void VisorGlass::hit(GlassHit h, float strength, uint32_t seed) {
    float k = std::clamp(strength, 0.f, 1.f);
    switch (h) {
    case GlassHit::GooSplat: case GlassHit::PhazonSplat:
        splat = std::max(splat, std::max(0.3f, k)); splatAge = 0; splatSeed = seed ? seed : 1;
        splatGlows = h == GlassHit::PhazonSplat; splatColor = splatGlows ? 0x2E8BFF : 0x7BC84A; break;
    case GlassHit::Static: shock = std::max(shock, std::max(0.2f, k)); break;
    case GlassHit::FogUp: fog = std::max(fog, k); break;
    case GlassHit::Frost: frost = std::max(frost, k); break;
    case GlassHit::Face: face = std::max(face, k); break;
    default: break;
    }
}
void VisorGlass::update(double dt, float steam, float cold, float flash, bool underwater, float pitchDeg, float yawRateDeg) {
    float t = (float)dt; steam = std::clamp(steam, 0.f, 1.f); cold = std::clamp(cold, 0.f, 1.f);
    if (steam > fog) fog += (steam - fog) * std::min(1.f, t * 2.5f);     // fogs up within a second ...
    else fog = std::max(0.f, fog - t / 5);                                // ... clears in a few (from the edges in: the picture does that)
    if (frost < cold) frost = std::min(cold, frost + t * cold / 25);     // frost creeps in slowly where it is cold ...
    else frost = std::max(cold, frost - t / 6);                           // ... and melts when it isn't
    frost = std::max(0.f, frost - steam * t * 0.8f);                      // (steam melts it)
    splatAge += t; if (splatAge > 2.5f) splat = std::max(0.f, splat - t / 5);   // a splat hangs on, then slides off
    shock = std::max(0.f, shock - t * 1.3f);
    face = std::max(0.f, face - t * 2.2f);
    if (flash > 0.8f) face = std::max(face, (flash - 0.8f) * 2.5f);      // a very bright flash: your face shows in the glass
    if (underwater) { fog = 0; frost = std::max(0.f, frost - t / 2); splat = std::max(0.f, splat - t * 1.5f); }   // the water washes it
    float k = 1 - std::exp(-t * 6);
    lookUp += (std::clamp(pitchDeg / 70.f, -1.f, 1.f) - lookUp) * k;
    turn += (std::clamp(yawRateDeg / 180.f, -1.f, 1.f) - turn) * k;
}

} // namespace mc
