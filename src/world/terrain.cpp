#include "terrain.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

namespace mc {

const char* terrainModeName(TerrainMode m) { static const char* n[] = { "Land", "Floating islands", "Underground" }; return n[std::clamp((int)m, 0, 2)]; }
const char* terrainEdgeName(TerrainEdge e) { static const char* n[] = { "Cliffs all round", "A drop into the void", "Open sea" }; return n[std::clamp((int)e, 0, 2)]; }

// ------------------------------------------------------------------ noise (gradient noise, hashed: the same seed, the same world)
static inline uint32_t hsh(int x, int y, int z, uint32_t s) {
    uint32_t h = s * 0x9E3779B1u ^ (uint32_t)x * 0x85EBCA77u ^ (uint32_t)y * 0xC2B2AE3Du ^ (uint32_t)z * 0x27D4EB2Fu;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15; return h;
}
static inline double h01(int x, int y, int z, uint32_t s) { return (hsh(x, y, z, s) & 0xFFFFFF) / 16777216.0; }
static inline double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
static inline int fl(double v) { int i = (int)v; return v < i ? i - 1 : i; }
static double noise2(double x, double z, uint32_t s) {   // about -1 .. 1
    int ix = fl(x), iz = fl(z); double fx = x - ix, fz = z - iz;
    auto g = [&](int cx, int cz, double dx, double dz) { double a = h01(cx, 0, cz, s) * 6.283185307179586; return std::cos(a) * dx + std::sin(a) * dz; };
    double u = fade(fx), v = fade(fz);
    double a = g(ix, iz, fx, fz), b = g(ix + 1, iz, fx - 1, fz), c = g(ix, iz + 1, fx, fz - 1), d = g(ix + 1, iz + 1, fx - 1, fz - 1);
    return (a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v) * 1.41;
}
static double noise3(double x, double y, double z, uint32_t s) {   // about -1 .. 1
    static const double G[12][3] = { { 1, 1, 0 }, { -1, 1, 0 }, { 1, -1, 0 }, { -1, -1, 0 }, { 1, 0, 1 }, { -1, 0, 1 }, { 1, 0, -1 }, { -1, 0, -1 }, { 0, 1, 1 }, { 0, -1, 1 }, { 0, 1, -1 }, { 0, -1, -1 } };
    int ix = fl(x), iy = fl(y), iz = fl(z); double fx = x - ix, fy = y - iy, fz = z - iz;
    auto g = [&](int cx, int cy, int cz, double dx, double dy, double dz) { const double* q = G[hsh(cx, cy, cz, s) % 12]; return q[0] * dx + q[1] * dy + q[2] * dz; };
    double u = fade(fx), v = fade(fy), w = fade(fz);
    auto L = [](double a, double b, double t) { return a + (b - a) * t; };
    double x00 = L(g(ix, iy, iz, fx, fy, fz), g(ix + 1, iy, iz, fx - 1, fy, fz), u), x10 = L(g(ix, iy + 1, iz, fx, fy - 1, fz), g(ix + 1, iy + 1, iz, fx - 1, fy - 1, fz), u);
    double x01 = L(g(ix, iy, iz + 1, fx, fy, fz - 1), g(ix + 1, iy, iz + 1, fx - 1, fy, fz - 1), u), x11 = L(g(ix, iy + 1, iz + 1, fx, fy - 1, fz - 1), g(ix + 1, iy + 1, iz + 1, fx - 1, fy - 1, fz - 1), u);
    return L(L(x00, x10, v), L(x01, x11, v), w);
}
static double fbm2(double x, double z, int oct, double gain, uint32_t s) {
    double sum = 0, amp = 1, norm = 0;
    for (int i = 0; i < oct; i++) { sum += noise2(x, z, s + i * 1013) * amp; norm += amp; amp *= gain; x = x * 2.03 + 17.1; z = z * 2.03 - 9.7; }
    return sum / norm * 3.0;   // (about -1 .. 1 for most places: the sum of octaves averages out)
}
static double sstep(double a, double b, double x) { double t = std::clamp((x - a) / (b - a), 0.0, 1.0); return t * t * (3 - 2 * t); }
static int floorDivI(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// ------------------------------------------------------------------ the presets
const std::vector<std::string>& terrainPresetNames() {
    static const std::vector<std::string> n = { "Overgrown", "Frozen", "Volcanic", "Desert ruins", "Corrupted", "Floating islands", "Underground" };
    return n;
}
TerrainSettings terrainPreset(const std::string& type) {
    TerrainSettings t; t.type = type;
    if (type == "Frozen") {   // icy terraced cliffs, snow on top, frozen lakes, crystal spikes
        t.layers = { { "snow", 2, 1 }, { "frozen rock", 3, 1 } }; t.rock = "stone"; t.bands = { "stone", "frozen rock" }; t.bandThick = 3;
        t.base = 8; t.hills = 9; t.hillSize = 60; t.mountains = 46; t.mountainSize = 200; t.mountainShare = 0.45f; t.ridged = 0.4f; t.roughness = 0.45f;
        t.terrace = 5; t.terraceSharp = 0.75f; t.liquid = "water"; t.liquidLevel = 2; t.liquidTop = "ice";
        t.spikeBlock = "ice crystal"; t.spikes = 0.12f; t.spikeHeight = 10; t.caves = 0.25f; t.openings = 0.2f;
        t.veins = { { "ice crystal", 0.015f, 3, -64, 40 } };
    } else if (type == "Volcanic") {   // dark basalt plateaus, lava lakes, lots of caves
        t.layers = { { "ash", 1, 1 }, { "basalt", 2, 1 } }; t.rock = "basalt"; t.bands = { "basalt", "basalt", "dark rock" }; t.bandThick = 2;
        t.base = 4; t.hills = 7; t.hillSize = 50; t.mountains = 38; t.mountainSize = 180; t.mountainShare = 0.35f; t.ridged = 0.6f; t.roughness = 0.7f;
        t.terrace = 4; t.terraceSharp = 0.5f; t.liquid = "lava"; t.liquidLevel = 0;
        t.caves = 0.6f; t.caverns = 0.35f; t.openings = 0.6f; t.veins = { { "magma rock", 0.05f, 3, -64, 30 } };
        t.spikeBlock = "basalt"; t.spikes = 0.08f; t.spikeHeight = 7;
    } else if (type == "Desert ruins") {   // striped mesas, canyons, dunes
        t.layers = { { "sand", 2, 2 }, { "sandstone", 5, 2 } }; t.rock = "stone"; t.bands = { "sandstone", "red sandstone", "sandstone", "red sand", "red sandstone" }; t.bandThick = 3;
        t.base = 2; t.hills = 5; t.hillSize = 90; t.mountains = 30; t.mountainSize = 200; t.mountainShare = 0.32f; t.ridged = 0; t.roughness = 0.35f;
        t.terrace = 6; t.terraceSharp = 0.9f; t.liquid = ""; t.caves = 0.15f; t.openings = 0.15f;
        t.spikeBlock = "red sandstone"; t.spikes = 0.05f; t.spikeHeight = 12;
    } else if (type == "Corrupted") {   // jagged spires, glowing blue veins, strange soil
        t.layers = { { "corrupted soil", 1, 0 }, { "dark rock", 2, 1 } }; t.rock = "dark rock"; t.bands = { "dark rock", "stone", "dark rock", "dark rock" }; t.bandThick = 2;
        t.base = 4; t.hills = 8; t.hillSize = 60; t.mountains = 45; t.mountainSize = 160; t.mountainShare = 0.35f; t.ridged = 0.9f; t.roughness = 0.8f;
        t.liquid = "water"; t.liquidLevel = -6; t.veins = { { "phazon crystal", 0.05f, 2.5f, -64, 60 } };
        t.spikeBlock = "dark rock"; t.spikes = 0.25f; t.spikeHeight = 14; t.caves = 0.4f; t.openings = 0.5f;
    } else if (type == "Floating islands") {   // chunks of land hanging in the sky
        t.mode = TerrainMode::Islands; t.layers = { { "grass", 1, 0 }, { "dirt", 3, 1 } }; t.rock = "stone"; t.bands = { "stone", "cobblestone", "stone" }; t.bandThick = 2;
        t.base = 0; t.hills = 6; t.hillSize = 50; t.mountains = 0; t.roughness = 0.5f; t.islands = 0.5f; t.islandY = 30; t.islandThick = 16;
        t.liquid = ""; t.caves = 0.1f; t.openings = 0.2f;
    } else if (type == "Underground") {   // solid rock, huge caverns, pools in them
        t.mode = TerrainMode::Underground; t.layers = { { "grass", 1, 0 }, { "dirt", 3, 1 } }; t.rock = "stone"; t.bands = { "stone", "dark rock", "stone" }; t.bandThick = 4;
        t.base = 0; t.hills = 4; t.mountains = 0; t.ceiling = 48; t.liquid = "water"; t.liquidLevel = -24;
        t.caves = 0.6f; t.caverns = 0.55f; t.openings = 0; t.veins = { { "phazon crystal", 0.01f, 2, -64, 40 }, { "magma rock", 0.01f, 3, -64, -20 } };
    } else {   // "Overgrown": rolling hills, deep valleys, lakes, mossy rock
        t.type = "Overgrown"; t.layers = { { "grass", 1, 0 }, { "dirt", 3, 1 } }; t.rock = "stone"; t.bands = { "stone", "mossy stone", "stone" }; t.bandThick = 4;
        t.base = 3; t.hills = 10; t.hillSize = 70; t.mountains = 34; t.mountainSize = 220; t.mountainShare = 0.32f; t.ridged = 0.15f; t.roughness = 0.5f;
        t.liquid = "water"; t.liquidLevel = -1; t.caves = 0.35f; t.openings = 0.3f;
    }
    return t;
}
std::vector<std::string> terrainBlocksUsed(const TerrainSettings& t) {
    std::vector<std::string> v;
    auto add = [&](const std::string& n) { if (!n.empty() && std::find(v.begin(), v.end(), n) == v.end()) v.push_back(n); };
    for (auto& l : t.layers) add(l.block);
    add(t.rock); for (auto& b : t.bands) add(b); for (auto& vn : t.veins) add(vn.block);
    add(t.bottomBlock); add(t.liquid); add(t.liquidTop); if (t.spikes > 0) add(t.spikeBlock);
    return v;
}

// ------------------------------------------------------------------ saving
static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == ' ') o += "%20"; else if (c == '%') o += "%25"; else o += c; } return o.empty() ? "-" : o; }
static std::string unesc(const std::string& s) {
    if (s == "-") return ""; std::string o;
    for (size_t i = 0; i < s.size(); i++) { if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16); i += 2; } else o += s[i]; }
    return o;
}
std::string terrainText(const TerrainSettings& t) {
    std::ostringstream f; f.precision(8);
    f << "tg type " << esc(t.type) << "\ntg seed " << t.seed << "\ntg mode " << (int)t.mode << "\n";
    f << "tg shape " << t.base << " " << t.hills << " " << t.hillSize << " " << t.mountains << " " << t.mountainSize << " " << t.mountainShare << " " << t.ridged << " " << t.roughness << " " << t.terrace << " " << t.terraceSharp << "\n";
    f << "tg layers " << t.layers.size(); for (auto& l : t.layers) f << " " << esc(l.block) << " " << l.thick << " " << l.vary; f << "\n";
    f << "tg rock " << esc(t.rock) << " " << esc(t.bottomBlock) << " " << t.bottom << "\n";
    f << "tg bands " << t.bandThick << " " << t.bands.size(); for (auto& b : t.bands) f << " " << esc(b); f << "\n";
    f << "tg veins " << t.veins.size(); for (auto& v : t.veins) f << " " << esc(v.block) << " " << v.amount << " " << v.size << " " << v.minY << " " << v.maxY; f << "\n";
    f << "tg liquid " << esc(t.liquid) << " " << t.liquidLevel << " " << esc(t.liquidTop) << "\n";
    f << "tg caves " << t.caves << " " << t.caverns << " " << t.openings << "\n";
    f << "tg spikes " << esc(t.spikeBlock) << " " << t.spikes << " " << t.spikeHeight << "\n";
    f << "tg islands " << t.islands << " " << t.islandY << " " << t.islandThick << "\n";
    f << "tg ceiling " << t.ceiling << "\ntg area " << t.size << " " << (int)t.edge << "\n";
    return f.str();
}
bool terrainLine(TerrainSettings& t, const std::string& k, std::istream& f) {
    std::string a, b, c; int n = 0;
    if (k == "type") { f >> a; t.type = unesc(a); }
    else if (k == "seed") f >> t.seed;
    else if (k == "mode") { f >> n; t.mode = (TerrainMode)std::clamp(n, 0, (int)TerrainMode::COUNT - 1); }
    else if (k == "shape") f >> t.base >> t.hills >> t.hillSize >> t.mountains >> t.mountainSize >> t.mountainShare >> t.ridged >> t.roughness >> t.terrace >> t.terraceSharp;
    else if (k == "layers") { size_t m = 0; f >> m; t.layers.clear(); for (size_t i = 0; i < m && i < 16; i++) { TerrainLayer l; f >> a >> l.thick >> l.vary; l.block = unesc(a); t.layers.push_back(l); } }
    else if (k == "rock") { f >> a >> b >> t.bottom; t.rock = unesc(a); t.bottomBlock = unesc(b); }
    else if (k == "bands") { size_t m = 0; f >> t.bandThick >> m; t.bands.clear(); for (size_t i = 0; i < m && i < 16; i++) { f >> a; t.bands.push_back(unesc(a)); } }
    else if (k == "veins") { size_t m = 0; f >> m; t.veins.clear(); for (size_t i = 0; i < m && i < 8; i++) { TerrainVein v; f >> a >> v.amount >> v.size >> v.minY >> v.maxY; v.block = unesc(a); t.veins.push_back(v); } }
    else if (k == "liquid") { f >> a >> t.liquidLevel >> b; t.liquid = unesc(a); t.liquidTop = unesc(b); }
    else if (k == "caves") f >> t.caves >> t.caverns >> t.openings;
    else if (k == "spikes") { f >> a >> t.spikes >> t.spikeHeight; t.spikeBlock = unesc(a); }
    else if (k == "islands") f >> t.islands >> t.islandY >> t.islandThick;
    else if (k == "ceiling") f >> t.ceiling;
    else if (k == "area") { f >> t.size >> n; t.edge = (TerrainEdge)std::clamp(n, 0, (int)TerrainEdge::COUNT - 1); }
    else return false;
    return true;
}

// ------------------------------------------------------------------ making it
TerrainGen::TerrainGen(const TerrainSettings& st, const BlockRegistry& r) : s(st), seed(st.seed * 2654435761u + 12345u) {
    BlockId stone = r.find("stone"); if (stone == AIR) for (size_t i = 1; i < r.count(); i++) if (r.has((BlockId)i) && r.get((BlockId)i).solid) { stone = (BlockId)i; break; }
    auto solid = [&](const std::string& n) { BlockId b = n.empty() ? AIR : r.find(n); return b != AIR ? b : stone; };   // (a block the game does not have: stone)
    for (auto& l : s.layers) if (l.thick > 0) layers.push_back({ solid(l.block), l.thick, std::max(0, l.vary) });
    rock = solid(s.rock); bottomB = solid(s.bottomBlock);
    for (auto& b : s.bands) bands.push_back(solid(b));
    for (auto& v : s.veins) if (v.amount > 0) veins.push_back({ solid(v.block), v.amount, std::max(0.5f, v.size), v.minY, v.maxY });
    liquid = s.liquid.empty() ? AIR : r.find(s.liquid);
    if (liquid == AIR && !s.liquid.empty()) liquid = r.liquidBlock(s.liquid == "lava" ? 2 : 1);
    liquidTop = s.liquidTop.empty() ? AIR : r.find(s.liquidTop);
    spikeB = s.spikes > 0 ? solid(s.spikeBlock.empty() ? s.rock : s.spikeBlock) : AIR;
    s.bandThick = std::max(1, s.bandThick); s.hillSize = std::max(4.f, s.hillSize); s.mountainSize = std::max(8.f, s.mountainSize);
}

TerrainGen::Column TerrainGen::column(int x, int z) const {
    Column c{ 0, -1e9, -100000, true };
    double hills = fbm2(x / (double)s.hillSize, z / (double)s.hillSize, 4, 0.45 + 0.25 * s.roughness, seed + 1) * s.hills;
    double rough = fbm2(x / 9.0, z / 9.0, 2, 0.5, seed + 2) * s.roughness * 2.2;
    double h = s.base + hills + rough;
    if (s.mode == TerrainMode::Islands) {
        double m = fbm2(x / 90.0, z / 90.0, 3, 0.5, seed + 5) * 0.5 + 0.5, thr = 0.78 - std::clamp((double)s.islands, 0.0, 1.0) * 0.4;
        if (m < thr) { c.any = false; return c; }
        double k = std::min(1.0, (m - thr) / std::max(0.05, 1 - thr) * 2.2);
        c.h = s.islandY + hills * 0.6 + rough + k * 6;
        c.islandBottom = c.h - s.islandThick * (0.15 + std::pow(k, 0.7)) - (fbm2(x / 7.0, z / 7.0, 2, 0.5, seed + 6) * 0.5 + 0.5) * 4;
    } else if (s.mode == TerrainMode::Underground) {
        c.h = s.ceiling + hills * 0.3 + rough;
    }
    if (s.mode == TerrainMode::Land) {
        if (s.mountains > 0) {
            double m = fbm2(x / (double)s.mountainSize, z / (double)s.mountainSize, 4, 0.5, seed + 3) * 0.5 + 0.5;
            double share = std::clamp((double)s.mountainShare, 0.0, 1.0), mask = sstep(1 - share - 0.06, 1 - share + 0.14, m);
            double ridge = 1 - std::fabs(fbm2(x / (s.mountainSize * 0.4), z / (s.mountainSize * 0.4), 5, 0.5, seed + 4));
            double peak = (1 - s.ridged) * sstep(1 - share - 0.06, 1, m) * 1.3 + s.ridged * ridge * ridge * 1.25;
            h += s.mountains * mask * peak;
        }
        if (s.terrace >= 1) {   // steps: flat ledges, cliffs between them
            double t = h / s.terrace, f = t - std::floor(t), a = std::clamp((double)s.terraceSharp, 0.0, 1.0) * 0.49;
            h = (std::floor(t) + sstep(a, 1 - a, f)) * s.terrace;
        }
        if (s.size > 0) {   // the area's edge
            double d = std::sqrt((double)x * x + (double)z * z) + fbm2(x / 40.0, z / 40.0, 2, 0.5, seed + 9) * s.size * 0.08;
            double e = sstep(s.size * 0.85, s.size, d);
            if (s.edge == TerrainEdge::Cliffs) h += e * (s.mountains * 0.6 + 40);
            else if (s.edge == TerrainEdge::Sea) h = h + ((s.liquid.empty() ? s.bottom + 6 : s.liquidLevel - 14) - h) * e;
            else if (d > s.size) { c.any = false; return c; }
        }
        c.h = h;
    }
    h = c.h;
    if (s.spikes > 0 && spikeB != AIR) {   // pillars / crystals: at most one in a cell of 14 x 14
        const int C = 14; int cx = floorDivI(x, C), cz = floorDivI(z, C);
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            int gx = cx + dx, gz = cz + dz; if (h01(gx, 7, gz, seed + 11) > s.spikes * 0.6) continue;
            double px = gx * C + 2 + h01(gx, 1, gz, seed + 12) * (C - 4), pz = gz * C + 2 + h01(gx, 2, gz, seed + 12) * (C - 4);
            double r = 1.2 + h01(gx, 3, gz, seed + 12) * 2.2, H = s.spikeHeight * (0.6 + 0.8 * h01(gx, 4, gz, seed + 12));
            double d = std::hypot(x + 0.5 - px, z + 0.5 - pz); if (d >= r) continue;
            c.spikeTop = std::max(c.spikeTop, (int)std::floor(h + H * std::pow(1 - d / r, 0.8)));
        }
    }
    return c;
}

BlockId TerrainGen::solidAt(const Column& col, int x, int y, int z) const {
    int top = (int)std::floor(col.h);
    if (y > top) return y <= col.spikeTop ? spikeB : AIR;
    if (y < s.bottom) return AIR;
    if (y == s.bottom) return bottomB;
    int depth = top - y, acc = 0;
    for (size_t i = 0; i < layers.size(); i++) {
        const Layer& l = layers[i];
        int t = l.thick + (l.vary ? (int)std::lround(noise2(x / 11.0, z / 11.0, seed + 20 + (uint32_t)i) * l.vary) : 0);
        if (depth < acc + std::max(0, t)) return l.b;
        acc += std::max(0, t);
    }
    for (size_t i = 0; i < veins.size(); i++) {
        const Vein& v = veins[i]; if (y < v.minY || y > v.maxY) continue;
        double n = noise3(x / (double)v.size, y / (double)v.size, z / (double)v.size, seed + 40 + (uint32_t)i);
        if (n > 0.62 - v.amount * 3.2) return v.b;
    }
    if (!bands.empty()) {
        int w = (int)std::lround(noise2(x / 23.0, z / 23.0, seed + 30) * 2.5);
        int k = floorDivI(y + w, s.bandThick) % (int)bands.size(); if (k < 0) k += (int)bands.size();
        return bands[k];
    }
    return rock;
}

bool TerrainGen::carved(const Column& col, int x, int y, int z) const {
    if (s.caves <= 0 && s.caverns <= 0) return false;
    if (y <= s.bottom + 1) return false;
    int top = (int)std::floor(col.h), depth = top - y;
    if (y > top) return false;   // (spikes are never hollow)
    if (s.mode == TerrainMode::Islands && y < col.islandBottom + 3) return false;
    if (depth < 5) {   // a skin of ground over the caves, broken now and then (an opening)
        if (s.openings <= 0) return false;
        if (noise2(x / 45.0, z / 45.0, seed + 50) * 0.5 + 0.5 < 1 - s.openings * 0.45) return false;
    }
    if (!s.liquid.empty() && s.mode == TerrainMode::Land && top < s.liquidLevel + 1 && depth < 7) return false;   // (no holes under the sea's floor)
    if (s.caves > 0) {   // tunnels: where two noises are both near zero
        double a = noise3(x / 26.0, y / 18.0, z / 26.0, seed + 60), b = noise3(x / 26.0, y / 18.0, z / 26.0, seed + 61), w = 0.03 + s.caves * 0.075;
        if (a * a + b * b < w * w) return true;
    }
    if (s.caverns > 0) {   // big rooms, mostly well below the surface
        double c = noise3(x / 60.0, y / 30.0, z / 60.0, seed + 62) * 0.7 + noise3(x / 22.0, y / 14.0, z / 22.0, seed + 63) * 0.3;
        if (c > 0.42 - s.caverns * 0.3 + (depth < 12 ? (12 - depth) * 0.03 : 0)) return true;
    }
    return false;
}

bool TerrainGen::liquidAt(int x, int y, int z) const {
    if (liquid == AIR || s.mode == TerrainMode::Islands || y > s.liquidLevel || y <= s.bottom) return false;
    Column c = column(x, z); if (!c.any) return false;
    if (s.mode == TerrainMode::Underground) return y <= (int)std::floor(c.h) && carved(c, x, y, z);
    return y > (int)std::floor(c.h);
}

BlockId TerrainGen::blockAt(int x, int y, int z) const {
    Column c = column(x, z); if (!c.any) return AIR;
    BlockId b = AIR;
    if (!(s.mode == TerrainMode::Islands && y < c.islandBottom)) { b = solidAt(c, x, y, z); if (b != AIR && carved(c, x, y, z)) b = AIR; }
    if (b == AIR && liquidAt(x, y, z)) b = (y == s.liquidLevel && liquidTop != AIR && s.mode == TerrainMode::Land) ? liquidTop : liquid;
    return b;
}

void TerrainGen::fill(Chunk& ch) const {
    // the columns: kept for the chunks above and below (a few per thread)
    struct Cache { const TerrainGen* g = nullptr; uint32_t sd = 0; int cx = 0, cz = 0; std::array<Column, CS * CS> col; };
    thread_local std::array<Cache, 8> cache; thread_local int next = 0;
    int x0 = ch.pos.x * CS, y0 = ch.pos.y * CS, z0 = ch.pos.z * CS;
    Cache* cc = nullptr;
    for (auto& k : cache) if (k.g == this && k.sd == seed && k.cx == ch.pos.x && k.cz == ch.pos.z) { cc = &k; break; }
    if (!cc) {
        cc = &cache[next]; next = (next + 1) % (int)cache.size();
        cc->g = this; cc->sd = seed; cc->cx = ch.pos.x; cc->cz = ch.pos.z;
        for (int lz = 0; lz < CS; lz++) for (int lx = 0; lx < CS; lx++) cc->col[lz * CS + lx] = column(x0 + lx, z0 + lz);
    }
    int topMax = -100000;
    for (const Column& c : cc->col) if (c.any) topMax = std::max(topMax, std::max((int)std::floor(c.h), c.spikeTop));
    int wet = (liquid != AIR && s.mode != TerrainMode::Islands) ? s.liquidLevel : -100000;
    if (y0 > std::max(topMax, wet) || y0 + CS <= s.bottom) return;   // all air
    for (int lz = 0; lz < CS; lz++) for (int lx = 0; lx < CS; lx++) {
        const Column& c = cc->col[lz * CS + lx]; if (!c.any) continue;
        int x = x0 + lx, z = z0 + lz, top = std::max((int)std::floor(c.h), c.spikeTop);
        for (int ly = 0; ly < CS; ly++) {
            int y = y0 + ly; if (y < s.bottom) continue;
            if (y > top && y > wet) break;
            BlockId b = AIR;
            if (y <= top && !(s.mode == TerrainMode::Islands && y < c.islandBottom)) {
                b = solidAt(c, x, y, z);
                if (b != AIR && carved(c, x, y, z)) b = (s.mode == TerrainMode::Underground && liquid != AIR && y <= s.liquidLevel) ? liquid : AIR;
            }
            if (b == AIR && liquid != AIR && y <= s.liquidLevel && y > (int)std::floor(c.h) && s.mode == TerrainMode::Land) b = (y == s.liquidLevel && liquidTop != AIR) ? liquidTop : liquid;
            if (b != AIR) ch.b[Chunk::idx(lx, ly, lz)] = b;
        }
    }
}

// ------------------------------------------------------------------ previews (the editor's map and side cut)
std::vector<BlockId> TerrainGen::blocksUsed() const {
    std::vector<BlockId> v; auto add = [&](BlockId b) { if (b != AIR && std::find(v.begin(), v.end(), b) == v.end()) v.push_back(b); };
    for (auto& l : layers) add(l.b); add(rock); add(bottomB); add(liquid); add(liquidTop); add(spikeB); for (BlockId b : bands) add(b); for (auto& vn : veins) add(vn.b);
    return v;
}
TerrainPalette terrainPalette(const TerrainGen& g, const BlockRegistry& r) {
    TerrainPalette p;
    auto avg = [&](BlockId b, int face) {
        std::array<float, 3> out{ 0.5f, 0.5f, 0.5f }; const BlockDef& d = r.get(b); int ti = d.tex[face];
        if (ti <= 0 || ti >= (int)r.textureCount()) return out;
        const TexImage& im = r.image(ti); double sum[3] = { 0, 0, 0 }; int n = 0;
        for (size_t i = 0; i + 3 < im.rgba.size(); i += 4) { if (im.rgba[i + 3] < 128) continue; for (int k = 0; k < 3; k++) sum[k] += im.rgba[i + k]; n++; }
        if (n) for (int k = 0; k < 3; k++) out[k] = (float)(sum[k] / n / 255.0);
        return out;
    };
    for (BlockId b : g.blocksUsed()) { p.top[b] = avg(b, F_PY); p.side[b] = avg(b, F_PX); if (r.get(b).liquid) p.liquid.insert(b); }
    return p;
}
std::vector<uint8_t> terrainPreviewMap(const TerrainGen& g, const TerrainPalette& pal, int W, int H, double cx, double cz, double perPixel) {
    std::vector<uint8_t> px((size_t)W * H * 4, 0);
    std::vector<int> top((size_t)(W + 1) * (H + 1));
    for (int y = 0; y <= H; y++) for (int x = 0; x <= W; x++) top[(size_t)y * (W + 1) + x] = g.surfaceY((int)std::floor(cx + (x - W * 0.5) * perPixel), (int)std::floor(cz + (H * 0.5 - y) * perPixel));
    const TerrainSettings& s = g.settings();
    auto colOf = [&](BlockId b) { auto it = pal.top.find(b); return it != pal.top.end() ? it->second : std::array<float, 3>{ 0.5f, 0.5f, 0.5f }; };
    auto isLiquid = [&](BlockId b) { return pal.liquid.count(b) > 0; };
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int wx = (int)std::floor(cx + (x - W * 0.5) * perPixel), wz = (int)std::floor(cz + (H * 0.5 - y) * perPixel), t = top[(size_t)y * (W + 1) + x];
        uint8_t* p = &px[((size_t)y * W + x) * 4];
        if (t < -10000) { p[0] = 12; p[1] = 14; p[2] = 22; p[3] = 255; continue; }   // nothing: sky / void
        BlockId b = g.blockAt(wx, t, wz), above = g.blockAt(wx, t + 1, wz); std::array<float, 3> c = colOf(b);
        double sx = top[(size_t)y * (W + 1) + x + 1] - t, sz = top[(size_t)(y + 1) * (W + 1) + x] - t; sx = std::max(-30.0, std::min(30.0, sx)); sz = std::max(-30.0, std::min(30.0, sz));
        double shade = std::clamp(1.0 + (-sx * 0.7 + sz * 0.7) / perPixel * 0.09, 0.45, 1.35) * (0.78 + std::clamp((t - s.base) / 120.0, -0.2, 0.3));
        if (above != AIR && isLiquid(above)) {   // under water / lava: its colour over the ground
            int depth = 0; for (int k = t + 1; k <= t + 40 && isLiquid(g.blockAt(wx, k, wz)); k++) depth++;
            std::array<float, 3> l = colOf(above); double a = std::min(0.9, 0.45 + depth * 0.05); shade = 1;
            for (int k = 0; k < 3; k++) c[k] = (float)(c[k] * (1 - a) + l[k] * a);
        } else if (above != AIR) {   // ice on a lake: the ice, the water under it showing a little
            c = colOf(above);
        }
        for (int k = 0; k < 3; k++) p[k] = (uint8_t)std::clamp(c[k] * shade * 255.0, 0.0, 255.0); p[3] = 255;
    }
    return px;
}
std::vector<uint8_t> terrainPreviewCut(const TerrainGen& g, const TerrainPalette& pal, int W, int H, double cx, double cz, double perPixel, int yLow, int yHigh) {
    std::vector<uint8_t> px((size_t)W * H * 4, 0);
    for (int x = 0; x < W; x++) {
        int wx = (int)std::floor(cx + (x - W * 0.5) * perPixel), wz = (int)std::floor(cz);
        for (int y = 0; y < H; y++) {
            int wy = (int)std::floor(yHigh - (y + 0.5) * (yHigh - yLow) / (double)H);
            BlockId b = g.blockAt(wx, wy, wz); uint8_t* p = &px[((size_t)y * W + x) * 4];
            if (b == AIR) { float k = (float)y / H; p[0] = (uint8_t)(30 + 40 * k); p[1] = (uint8_t)(40 + 50 * k); p[2] = (uint8_t)(60 + 60 * k); p[3] = 255; continue; }
            auto it = pal.side.find(b); std::array<float, 3> c = it != pal.side.end() ? it->second : std::array<float, 3>{ 0.5f, 0.5f, 0.5f };
            for (int k = 0; k < 3; k++) p[k] = (uint8_t)std::clamp(c[k] * 255.0, 0.0, 255.0); p[3] = 255;
        }
    }
    return px;
}

double TerrainGen::groundHeight(int x, int z) const { Column c = column(x, z); return c.any ? c.h : -1e9; }
int TerrainGen::surfaceY(int x, int z) const { Column c = column(x, z); return c.any ? std::max((int)std::floor(c.h), c.spikeTop) : -100000; }

V3 TerrainGen::spawnPoint() const {
    for (int r = 0; r <= 320; r += 4)   // rings outward from the middle: the first dry, fairly flat spot
        for (int i = 0; i < std::max(1, r * 2); i++) {
            double a = r ? i * 3.14159265358979 / r : 0; int x = (int)std::lround(std::cos(a) * r), z = (int)std::lround(std::sin(a) * r);
            Column c = column(x, z); if (!c.any || c.spikeTop > (int)std::floor(c.h)) continue;
            int top = (int)std::floor(c.h);
            if (liquid != AIR && s.mode == TerrainMode::Land && top < s.liquidLevel + 1) continue;
            if (std::fabs(groundHeight(x + 2, z) - c.h) > 2.5 || std::fabs(groundHeight(x, z + 2) - c.h) > 2.5) continue;
            return { x + 0.5, (double)top + 1, z + 0.5 };
        }
    return { 0.5, (double)surfaceY(0, 0) + 1, 0.5 };
}

} // namespace mc
