#include "demo.h"
#include "../world/shapes.h"

namespace mc {

void buildDemo(World& w, const BlockRegistry& r) {
    const Shapes& S = Shapes::get();
    auto B = [&](const char* n) { return r.find(n); };
    auto shape = [&](const char* block, const char* sh, int rot = 0) { return makeBlock(B(block), S.find(sh), rot); };
    auto facing = [&](const char* sh, int look, bool upside = false) { return Shapes::placeRot(S.info(S.find(sh)).place, upside ? F_NY : F_PY, look, false); };
    auto box = [&](int x0, int y0, int z0, int x1, int y1, int z1, BlockId v) { for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) w.set(x, y, z, v); };

    // ---- a small house: cobblestone base, plank walls, log corners, glass windows, a stair roof
    box(2, 0, 4, 10, 0, 12, B("cobblestone"));
    box(2, 1, 4, 10, 3, 12, B("planks"));
    box(3, 1, 5, 9, 3, 11, AIR);
    for (int x : { 2, 10 }) for (int z : { 4, 12 }) box(x, 0, z, x, 4, z, B("log"));
    box(5, 1, 4, 6, 2, 4, AIR);                                                   // door
    w.set(5, 3, 4, shape("planks", "arch", facing("arch", F_PZ)));
    w.set(6, 3, 4, shape("planks", "arch", facing("arch", F_PZ)));
    for (int z : { 6, 7, 9, 10 }) { w.set(2, 2, z, B("glass")); w.set(10, 2, z, B("glass")); }
    for (int x : { 8, 9 }) w.set(x, 2, 4, B("glass"));
    for (int k = 0; k <= 4; k++) {                                                // roof rows climbing to the ridge
        for (int x = 1; x <= 11; x++) {
            w.set(x, 4 + k, 3 + k, shape("bricks", "stairs", facing("stairs", F_PZ)));
            w.set(x, 4 + k, 13 - k, shape("bricks", "stairs", facing("stairs", F_NZ)));
        }
        if (k < 4) for (int x : { 2, 10 }) box(x, 4 + k, 4 + k, x, 4 + k, 12 - k, B("planks"));
    }
    for (int x = 1; x <= 11; x++) w.set(x, 8, 8, shape("bricks", "half"));
    box(3, 4, 5, 9, 4, 11, B("planks"));                                          // ceiling
    w.set(6, 3, 8, B("lamp"));

    // ---- a tree
    box(-6, 0, 6, -6, 4, 6, B("log"));
    for (int y = 3; y <= 7; y++) for (int z = 3; z <= 9; z++) for (int x = -9; x <= -3; x++) {
        int dx = x + 6, dy = y - 5, dz = z - 6; if (dx * dx + dy * dy * 2 + dz * dz <= 9 && !(dx == 0 && dz == 0 && y <= 4)) w.set(x, y, z, B("leaves"));
    }

    // ---- an arch wall
    box(-5, 0, -4, 5, 3, -4, B("bricks"));
    for (int x : { -3, 0, 3 }) { box(x, 0, -4, x, 1, -4, AIR); w.set(x, 2, -4, shape("bricks", "arch", facing("arch", F_PZ))); }
    for (int x = -5; x <= 5; x++) w.set(x, 4, -4, shape("stone", "half"));

    // ---- a cobblestone path with half-block edges
    for (int z = -3; z <= 3; z++) for (int x = 4; x <= 7; x++) w.set(x, -1, z, B("cobblestone"));
    for (int z = -3; z <= 3; z++) { w.set(3, 0, z, shape("stone", "quarter_slab")); w.set(8, 0, z, shape("stone", "quarter_slab")); }

    // ---- lamp posts
    for (int x : { 3, 8 }) { w.set(x, 1, 2, shape("metal", "pole4")); w.set(x, 2, 2, shape("metal", "pole4")); w.set(x, 3, 2, B("lamp")); }

    // ---- a shape garden
    const char* garden[] = { "ball", "dome", "cone", "pyramid", "cylinder_dummy", "ramp", "stairs", "arch", "quarter_cylinder", "spike", "hexagon", "ibeam" };
    int i = 0;
    for (const char* g : garden) {
        int id = S.find(g); if (!id) id = S.find("pole16");
        int x = 14 + (i % 4) * 3, z = 2 + (i / 4) * 3; i++;
        w.set(x, 0, z, makeBlock(B("stone"), id, Shapes::placeRot(S.info(id).place, F_PY, F_NZ, false)));
        w.set(x, -1, z, B("metal"));
    }
    box(13, 0, 12, 20, 0, 12, shape("metal", "railing", facing("railing", F_PZ)));
    for (int k = 0; k < 3; k++) { w.set(22, k, 4 + k, shape("cobblestone", "stairs", facing("stairs", F_PZ))); box(22, 0, 4 + k, 22, k - 1, 4 + k, B("cobblestone")); }
    box(22, 0, 7, 24, 2, 9, B("cobblestone"));
    w.set(23, 3, 8, shape("glass", "ball"));
}

std::vector<DemoShot> demoShots() {
    return {
        { "overview_morning", { -12, 10, -16 }, { 6, 2, 6 }, 9.5f },
        { "house_noon", { 6, 3.5, -6 }, { 6, 3, 6 }, 12.5f },
        { "arches_afternoon", { 0, 2.2, -12 }, { 0, 1.5, -4 }, 16.0f },
        { "garden", { 17, 4, -6 }, { 18, 0, 5 }, 14.0f },
        { "tree_and_house_sunset", { -14, 4, 0 }, { 4, 3, 8 }, 18.6f },
        { "night", { -10, 8, -14 }, { 6, 2, 6 }, 23.0f },
    };
}

} // namespace mc
