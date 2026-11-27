// The world generator: the same seed makes the same world, chunks match block by block, every world type makes
// what it says (hills, water, ice, islands, caverns, an edge), a dry place to start, saving, the terrain kit's blocks.
#include "../src/world/world.h"
#include "../src/world/terrain.h"
#include <chrono>
#include <cstdio>
#include <sstream>
using namespace mc;
int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)

int main() {
    BlockRegistry reg; registerBuiltinBlocks(reg);
    // the kit: every block the world types need
    {
        size_t before = reg.count(); std::vector<std::string> texs;
        for (const std::string& n : terrainKitNames()) addTerrainKitBlock(reg, n, &texs);
        bool all = true; for (const std::string& n : terrainKitNames()) all = all && reg.find(n) != AIR;
        BlockId again = addTerrainKitBlock(reg, "snow");
        CHECK(all && reg.count() == before + terrainKitNames().size() && texs.size() >= terrainKitNames().size() && again == reg.find("snow"), "the terrain kit's %zu blocks are added once, with %zu textures", terrainKitNames().size(), texs.size());
        reg.resolve();
        bool used = true; for (const std::string& t : terrainPresetNames()) for (const std::string& b : terrainBlocksUsed(terrainPreset(t))) used = used && reg.find(b) != AIR;
        CHECK(used, "every world type's blocks exist (with the kit)");
        CHECK(reg.get(reg.find("phazon crystal")).glow[F_PY].on() && reg.get(reg.find("magma rock")).glow[0].lights > 0, "phazon crystals and magma glow");
    }
    // the same seed: the same world; chunks match blockAt
    {
        TerrainSettings s = terrainPreset("Overgrown"); s.seed = 7; TerrainGen a(s, reg), b(s, reg); s.seed = 8; TerrainGen c(s, reg);
        int same = 0, diff = 0;
        for (int i = 0; i < 400; i++) { int x = i * 37 - 5000, z = i * 53 - 3000; if (a.surfaceY(x, z) == b.surfaceY(x, z)) same++; if (a.surfaceY(x, z) != c.surfaceY(x, z)) diff++; }
        CHECK(same == 400 && diff > 200, "the same seed makes the same land; another seed another (%d of 400 differ)", diff);
        int bad = 0;
        for (I3 p : { I3{ 0, 0, 0 }, I3{ -3, -1, 2 }, I3{ 5, 1, -7 }, I3{ -40, -3, 60 }, I3{ 2, 2, 2 } }) {
            Chunk ch; ch.pos = p; Generator g; g.kind = Generator::Terrain; g.terrain = std::make_shared<TerrainGen>(a); g.fill(ch);
            for (int k = 0; k < CS3; k += 7) { int lx = k % CS, ly = (k / CS) % CS, lz = k / (CS * CS); if (ch.b[Chunk::idx(lx, ly, lz)] != a.blockAt(p.x * CS + lx, p.y * CS + ly, p.z * CS + lz)) bad++; }
        }
        CHECK(bad == 0, "a chunk matches the generator block by block (%d wrong)", bad);
        Generator g; g.kind = Generator::Terrain; g.terrain = std::make_shared<TerrainGen>(a);
        auto t0 = std::chrono::steady_clock::now(); int n = 0;
        for (int cx = 0; cx < 8; cx++) for (int cz = 0; cz < 8; cz++) for (int cy = -3; cy <= 3; cy++) { Chunk ch; ch.pos = { cx, cy, cz }; g.fill(ch); n++; }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(ms / n < 3.0, "fast enough: %.2f ms a chunk", ms / n);
    }
    // each world type
    for (const std::string& type : terrainPresetNames()) {
        TerrainSettings s = terrainPreset(type); s.seed = 3; TerrainGen g(s, reg);
        int lo = 1 << 30, hi = -(1 << 30), empty = 0, liquid = 0, ice = 0, caveAir = 0, below = 0, spikes = 0;
        for (int z = -300; z <= 300; z += 6) for (int x = -300; x <= 300; x += 6) {
            int top = g.surfaceY(x, z); if (top < -10000) { empty++; continue; }
            lo = std::min(lo, top); hi = std::max(hi, top);
            if (g.surfaceY(x, z) > (int)std::floor(g.groundHeight(x, z))) spikes++;
            BlockId above = g.blockAt(x, top + 1, z); if (reg.get(above).liquid) liquid++; if (g.blockAt(x, s.liquidLevel, z) == reg.find("ice")) ice++;
        }
        for (int i = 0; i < 4000; i++) { int x = (i * 97) % 600 - 300, z = (i * 61) % 600 - 300, y = -40 + (i * 13) % 70; double h = g.groundHeight(x, z); if (h < -1e8 || y > h - 6) continue;
            below++; BlockId b = g.blockAt(x, y, z); if (b == AIR || reg.get(b).liquid) caveAir++; }
        V3 sp = g.spawnPoint(); int sx = (int)std::floor(sp.x), sy = (int)std::floor(sp.y), sz = (int)std::floor(sp.z);
        bool stand = g.blockAt(sx, sy - 1, sz) != AIR && !reg.get(g.blockAt(sx, sy - 1, sz)).liquid && g.blockAt(sx, sy, sz) == AIR && g.blockAt(sx, sy + 1, sz) == AIR;
        printf("      %-16s heights %d..%d, empty %d, liquid %d, ice %d, spikes %d, caves %d of %d below ground, start %.0f %.0f %.0f\n", type.c_str(), lo, hi, empty, liquid, ice, spikes, caveAir, below, sp.x, sp.y, sp.z);
        CHECK(stand, "%s: a dry place to start, on the ground with room to stand", type.c_str());
        if (type != "Underground") CHECK(hi - lo >= 8, "%s: not flat (%d blocks from lowest to highest)", type.c_str(), hi - lo);
        if (type == "Overgrown") CHECK(liquid > 20 && caveAir > 10, "Overgrown: lakes and caves");
        if (type == "Frozen") CHECK(ice > 10 && spikes > 0, "Frozen: frozen lakes and crystal spikes");
        if (type == "Volcanic") CHECK(liquid > 5 && caveAir > 60, "Volcanic: lava and many caves");
        if (type == "Floating islands") CHECK(empty > 300 && empty < 9000, "Floating islands: islands with sky between them");
        if (type == "Underground") CHECK(caveAir > 100, "Underground: caverns in the rock");
        if (type == "Desert ruins") CHECK(liquid == 0, "Desert ruins: dry");
    }
    // an area with an edge
    {
        TerrainSettings s = terrainPreset("Overgrown"); s.size = 120; s.edge = TerrainEdge::Void; TerrainGen v(s, reg);
        CHECK(v.surfaceY(0, 0) > -10000 && v.surfaceY(200, 0) < -10000 && v.surfaceY(0, -190) < -10000, "an area with a drop into the void: nothing past its edge");
        s.edge = TerrainEdge::Cliffs; TerrainGen c(s, reg); double in = 0, out = 0;
        for (int a = 0; a < 36; a++) { double r = a * 0.1745; in += c.groundHeight((int)(std::cos(r) * 30), (int)(std::sin(r) * 30)); out += c.groundHeight((int)(std::cos(r) * 125), (int)(std::sin(r) * 125)); }
        CHECK(out / 36 > in / 36 + 25, "an area with cliffs all round: the edge stands high (%.0f vs %.0f)", out / 36, in / 36);
        s.edge = TerrainEdge::Sea; TerrainGen se(s, reg); int wet = 0; for (int a = 0; a < 36; a++) { double r = a * 0.1745; int x = (int)(std::cos(r) * 125), z = (int)(std::sin(r) * 125); if (reg.get(se.blockAt(x, se.surfaceY(x, z) + 1, z)).liquid) wet++; }
        CHECK(wet > 30, "an area in the open sea: water all round (%d of 36)", wet);
    }
    // saving
    {
        TerrainSettings s = terrainPreset("Volcanic"); s.seed = 99; s.size = 300; s.edge = TerrainEdge::Sea; s.layers.push_back({ "red sand", 4, 2 }); s.liquidTop = "ice";
        std::string txt = terrainText(s); TerrainSettings b; std::istringstream in(txt); std::string tg, key;
        while (in >> tg >> key) { std::string rest; std::getline(in, rest); std::istringstream l(rest); terrainLine(b, key, l); }
        bool same = b.seed == 99 && b.type == "Volcanic" && b.layers.size() == s.layers.size() && b.layers.back().block == "red sand" && b.layers.back().vary == 2 && b.bands == s.bands
                 && b.veins.size() == 1 && b.veins[0].block == "magma rock" && b.liquid == "lava" && b.liquidTop == "ice" && b.size == 300 && b.edge == TerrainEdge::Sea && b.terrace == s.terrace && b.ridged == s.ridged;
        CHECK(same, "the generator's settings save and load (names with spaces too)");
        TerrainGen g1(s, reg), g2(b, reg); int d = 0; for (int i = 0; i < 200; i++) if (g1.blockAt(i * 7, i % 30 - 10, i * 3) != g2.blockAt(i * 7, i % 30 - 10, i * 3)) d++;
        CHECK(d == 0, "... and make the same world");
    }
    // a world in the World class: streaming it in, nothing breaks
    {
        World w(reg); w.gen.kind = Generator::Terrain; w.gen.terrain = std::make_shared<TerrainGen>(terrainPreset("Frozen"), reg);
        w.stream({ 0, 0, 0 }, 3, 2, 100000);
        V3 sp = w.gen.terrain->spawnPoint(); BlockId under = w.get((int)std::floor(sp.x), (int)std::floor(sp.y) - 1, (int)std::floor(sp.z));
        CHECK(w.loadedCount() > 100 && under != AIR, "a Frozen world streams in (%zu chunks); the start stands on %s", w.loadedCount(), reg.get(under).name.c_str());
        CHECK(!w.gen.uniform(), "terrain chunks differ (the far copies make each one)");
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
