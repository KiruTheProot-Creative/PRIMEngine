// Terrain generator test: every preset generates, is deterministic, respects the area, and smooths edges correctly.
#include "../src/terrain.h"
#include <chrono>
#include <cstdio>
using namespace px;
int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)
int main() {
    for (int k = 0; k < TK_COUNT; k++) {
        TerrainParams p; p.seed = 42; terrainPreset(k, p);
        Room r; r.newFlat(16);
        auto t0 = std::chrono::steady_clock::now(); generateTerrain(r, p);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        size_t n = r.count(), slopes = 0, lava = 0, chozo = 0, holesInBottom = 0;
        for (size_t i = 0; i < r.cells.size(); i++) { slopes += r.cells[i] == B_SLOPE || r.cells[i] == B_CORNER || r.cells[i] == B_INNER; lava += r.cells[i] && r.mats[i] == MAT_LAVA; chozo += r.cells[i] && r.mats[i] == MAT_CHOZO; }
        for (int z = r.oz; z < r.oz + r.sz; z++) for (int x = r.ox; x < r.ox + r.sx; x++) holesInBottom += !r.solid(x, r.oy, z);
        Room r2; r2.newFlat(16); generateTerrain(r2, p);
        MeshData m; auto t1 = std::chrono::steady_clock::now(); r.buildMesh(m);
        double mms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        printf("%-12s %6zu blocks, %4zu slopes, %5zu tris, gen %.0f ms, mesh %.0f ms\n", kTerrainNames[k], n, slopes, m.i.size() / 3, ms, mms);
        CHECK(n > 10000 && holesInBottom == 0, "%s: solid ground with a closed bottom", kTerrainNames[k]);
        CHECK(r2.cells == r.cells && r2.mats == r.mats && r2.rots == r.rots, "%s: same seed gives the same terrain", kTerrainNames[k]);
        CHECK(slopes > 50, "%s: edges are smoothed with slopes (%zu)", kTerrainNames[k], slopes);
        if (k == TK_LAVA) CHECK(lava > 30, "lava cave has lava (%zu blocks)", lava);
        if (k == TK_RUINS) CHECK(chozo > 30, "ruins have Chozo stone (%zu blocks)", chozo);
        if (k == TK_CAVERN) { size_t roof = 0; for (int z = r.oz; z < r.oz + r.sz; z++) for (int x = r.ox; x < r.ox + r.sx; x++) roof += r.solid(x, r.oy + r.sy - 1, z); CHECK(roof > (size_t)(r.sx * r.sz * 0.9), "cavern has a ceiling (%zu)", roof); }
        // slopes rise toward a higher neighbour: the block in front of a slope's tall side must be solid
        size_t badSlope = 0;
        for (int y = r.oy; y < r.oy + r.sy; y++) for (int z = r.oz; z < r.oz + r.sz; z++) for (int x = r.ox; x < r.ox + r.sx; x++) {
            Block b = r.block(x, y, z); if (b.type != B_SLOPE && b.type != B_INNER) continue;
            // the slope's full side(s) should face solid ground
            for (int d : { 0, 1, 4, 5 }) if (r.fullFace(x, y, z, d)) { V3 v = dirVec(d); if (!r.solid(x + (int)v.x, y, z + (int)v.z)) badSlope++; }
        }
        CHECK(badSlope == 0, "%s: every slope leans against higher ground (%zu wrong)", kTerrainNames[k], badSlope);
    }
    { TerrainParams p; p.seed = 1; TerrainParams q = p; q.seed = 2; Room a; a.newFlat(16); Room b; b.newFlat(16); generateTerrain(a, p); generateTerrain(b, q);
      CHECK(a.cells != b.cells, "a different seed gives different terrain"); }
    { Room a; a.newFlat(16); a.set(20, 5, 20, (uint8_t)B_CUBE); Room before = a; TerrainParams p; p.wholeRoom = false; p.x0 = -5; p.z0 = -5; p.x1 = 5; p.z1 = 5; generateTerrain(a, p);
      size_t outside = 0; for (int y = a.oy; y < a.oy + a.sy; y++) for (int z = a.oz; z < a.oz + a.sz; z++) for (int x = a.ox; x < a.ox + a.sx; x++)
        if ((x < -5 || x > 5 || z < -5 || z > 5) && !(a.block(x, y, z) == before.block(x, y, z))) outside++;
      CHECK(outside == 0, "an area-limited generation leaves everything outside the box alone (%zu changed)", outside); }
    { Room a; a.newFlat(16); TerrainParams p; generateTerrain(a, p); a.save("/tmp/terrain.room"); Room b; std::string e; b.load("/tmp/terrain.room", e);
      CHECK(b.cells == a.cells && b.mats == a.mats && b.rots == a.rots && b.sizes == a.sizes, "terrain with materials survives save/load"); }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
