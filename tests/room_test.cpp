// Room core test: flat room, mesh, ray cast, save/load, collision (walking, falling, jumping, walls).
#include "../src/room.h"
#include <cstdio>
using namespace px;
int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)
int main() {
    Room r; r.newFlat(16);
    CHECK(r.count() == 32 * 32, "flat room has %zu blocks", r.count());
    MeshData m; r.buildMesh(m);
    CHECK(m.i.size() / 6 == 32 * 32 + 4 * 32 + 32 * 32, "flat room: %zu faces (top + rim + bottom)", m.i.size() / 6);
    size_t bad = 0; for (size_t i = 0; i < m.i.size(); i += 3) { auto& a = m.v[m.i[i]]; auto& b = m.v[m.i[i + 1]]; auto& c = m.v[m.i[i + 2]];
        V3 pa{ a.p[0], a.p[1], a.p[2] }, pb{ b.p[0], b.p[1], b.p[2] }, pc{ c.p[0], c.p[1], c.p[2] }; if (dot(cross(pb - pa, pc - pa), V3{ a.n[0], a.n[1], a.n[2] }) <= 0) bad++; }
    CHECK(bad == 0, "winding: %zu wrong", bad);
    // corner shading: a block placed on the floor darkens the floor next to it
    r.set(0, 0, 0, B_CUBE); r.buildMesh(m);
    float darkest = 1; for (auto& v : m.v) if (v.n[1] > 0.5f && v.p[1] == 0.f && v.p[0] >= 1 && v.p[0] <= 1 && v.p[2] >= 0 && v.p[2] <= 1) darkest = std::min(darkest, v.c[0]);
    CHECK(darkest < 0.8f, "floor next to a block is shaded (%.2f)", darkest);
    // ray cast from above
    RayHit h; bool hit = r.raycast({ 0.5f, 5.f, 0.5f }, { 0, -1, 0 }, 50, h);
    CHECK(hit && h.x == 0 && h.y == 0 && h.z == 0 && h.ny == 1, "ray down hits the placed block's top (%d %d %d n %d %d %d)", h.x, h.y, h.z, h.nx, h.ny, h.nz);
    hit = r.raycast({ 3.5f, 0.5f, 0.5f }, { -1, 0, 0 }, 50, h);
    CHECK(hit && h.x == 0 && h.nx == 1, "ray sideways hits its +X face");
    Room e; e.resize(16, 8, 16, -8, -1, -8);
    hit = e.raycast({ 0.5f, 3.f, 0.5f }, { 0.1f, -1, 0 }, 50, h);
    CHECK(hit && h.onGround && h.y + h.ny == -1, "empty room: ray lands on the floor plane (place at y=%d)", h.y + h.ny);
    // save / load
    r.name = "Test \"room\""; CHECK(r.save("/tmp/t.room"), "saved");
    Room r2; std::string err; CHECK(r2.load("/tmp/t.room", err) && r2.cells == r.cells && r2.name == r.name, "loaded identical (%s)", err.c_str());
    // collision: standing, walking into a wall, falling off the edge, jumping onto a block
    V3 feet{ 5.5f, 0.5f, 5.5f }, vel{ 0, 0, 0 }; bool g = false;
    for (int i = 0; i < 60; i++) { vel.y -= 22.f / 60; feet = r.moveBox(feet, vel, 0.35f, 1.9f, 1 / 60.f, g); }
    CHECK(g && std::fabs(feet.y) < 0.01f, "falls and lands on the floor (y=%.3f)", feet.y);
    feet = { 3.5f, 0.f, 0.5f }; vel = { -4, 0, 0 };
    for (int i = 0; i < 60; i++) { vel.x = -4; vel.y -= 22.f / 60; feet = r.moveBox(feet, vel, 0.35f, 1.9f, 1 / 60.f, g); }
    CHECK(std::fabs(feet.x - 1.35f) < 0.01f, "walking into the block stops at its face (x=%.3f)", feet.x);
    vel = { 0, 7.5f, 0 }; float maxY = 0;
    for (int i = 0; i < 90; i++) { vel.x = -4; vel.y -= 22.f / 60; feet = r.moveBox(feet, vel, 0.35f, 1.9f, 1 / 60.f, g); maxY = std::max(maxY, feet.y); }
    CHECK(maxY > 1.f && std::fabs(feet.y) < 0.02f && feet.x < 0, "jumps over a 1-block step (peak %.2f m) and lands past it (x=%.2f)", maxY, feet.x);
    feet = { 15.5f, 0.f, 0.5f }; vel = { 4, 0, 0 };
    for (int i = 0; i < 60; i++) { vel.x = 4; vel.y -= 22.f / 60; feet = r.moveBox(feet, vel, 0.35f, 1.9f, 1 / 60.f, g); }
    CHECK(feet.y < -2.f, "walks off the edge and falls (y=%.2f)", feet.y);
    // ---- shapes: every type x 24 orientations x sizes, one block alone in the air
    {
        int notClosed = 0, badVol = 0, badWind = 0, total = 0; double worstVol = 0;
        for (int t = B_CUBE; t < B_COUNT; t++) {
            const ShapeInfo& si = shapeInfo((uint8_t)t);
            for (int sz = si.mn; sz <= si.mx; sz++) for (int rot = 0; rot < 24; rot++) {
                Room q; q.resize(4, 4, 4, -2, -2, -2); Block b; b.type = (uint8_t)t; b.rot = (uint8_t)rot; b.size = (uint8_t)sz; q.set(0, 0, 0, b);
                MeshData mm; q.buildMesh(mm); total++;
                V3 sum{ 0, 0, 0 }; double vol = 0;
                for (size_t i = 0; i < mm.i.size(); i += 3) {
                    auto& A = mm.v[mm.i[i]]; auto& Bv = mm.v[mm.i[i + 1]]; auto& C = mm.v[mm.i[i + 2]];
                    V3 pa{ A.p[0], A.p[1], A.p[2] }, pb{ Bv.p[0], Bv.p[1], Bv.p[2] }, pc{ C.p[0], C.p[1], C.p[2] };
                    V3 cr = cross(pb - pa, pc - pa); sum = sum + cr; vol += dot(pa, cross(pb, pc)) / 6.0;
                    V3 vn{ A.n[0] + Bv.n[0] + C.n[0], A.n[1] + Bv.n[1] + C.n[1], A.n[2] + Bv.n[2] + C.n[2] };
                    if (length(cr) > 1e-6f && dot(cr, vn) <= 0) badWind++;
                }
                if (length(sum) > 1e-3f) notClosed++;
                // expected volume from the collision boxes for box shapes; analytic for the rest
                double expect = -1; float f = si.sizeDiv ? (float)sz / si.sizeDiv : 0;
                switch (t) { case B_CUBE: expect = 1; break; case B_SLAB: expect = f; break; case B_SLOPE: expect = f * 0.5; break;
                    case B_CORNER: expect = f / 3.0; break; case B_INNER: expect = f * 2.0 / 3.0; break; case B_BEAM: expect = f * f; break;
                    case B_PANEL: expect = f; break; case B_STEP: expect = f * 0.5; break; case B_FRAME: expect = 1 - (1 - 2 * f) * (1 - 2 * f); break;
                    case B_STAIRS: expect = (sz + 1) / (2.0 * sz); break; case B_ROUND: expect = 3.14159265 / 4; break; case B_ARCH: expect = 1 - 3.14159265 / 4; break;
                    case B_PILLAR: expect = 3.14159265 * (f / 2) * (f / 2); break; }
                double err = std::fabs(vol - expect) / std::max(expect, 1e-3); worstVol = std::max(worstVol, err);
                if (err > 0.03) { badVol++; if (badVol < 4) printf("  volume %s size %d rot %d: %.4f expected %.4f\n", si.name, sz, rot, vol, expect); }
            }
        }
        CHECK(notClosed == 0, "all %d shape variants are closed solids (%d open)", total, notClosed);
        CHECK(badVol == 0, "volumes match the intended shapes (worst error %.1f%%)", worstVol * 100);
        CHECK(badWind == 0, "all shape triangles face outward (%d wrong)", badWind);
    }
    // ---- orientation: stairs placed on the floor face away from the player, slabs stick to walls
    {
        uint8_t r1 = chooseRot(B_STAIRS, 0, 1, 0, { 0, -0.3f, 1 }, 0, false);
        CHECK(rotUp(r1) == 2, "stairs placed on a floor stand upright");
        Room q; q.resize(4, 4, 4, -2, -2, -2); Block b{ B_STAIRS, r1, 2 }; q.set(0, 0, 0, b);
        CHECK(q.fullFace(0, 0, 0, 4) && !q.fullFace(0, 0, 0, 5), "stairs' tall side is away from the player (+Z)");
        uint8_t r2 = chooseRot(B_STAIRS, 0, -1, 0, { 0, 0, 1 }, 0, false);
        CHECK(rotUp(r2) == 3, "stairs placed on a ceiling hang upside down");
        uint8_t r3 = chooseRot(B_SLAB, -1, 0, 0, { 1, 0, 0 }, 0, false);
        Room w; w.resize(4, 4, 4, -2, -2, -2); w.set(0, 0, 0, Block{ B_SLAB, r3, 4 });
        CHECK(w.fullFace(0, 0, 0, 0) && !w.fullFace(0, 0, 0, 1), "a slab placed on a block's -X face hugs that block (its +X side)");
    }
    // ---- shapes in the world: ray casting and walking up stairs / slopes
    {
        Room q; q.newFlat(8);
        q.set(0, 0, 0, Block{ B_SLAB, makeRot(2, 0), 4 });
        RayHit h2; bool ok = q.raycast({ 0.5f, 3, 0.5f }, { 0, -1, 0 }, 10, h2);
        CHECK(ok && std::fabs(h2.t - 2.5f) < 1e-3f && h2.ny == 1, "ray hits a half slab's top at y=0.5 (t=%.3f)", h2.t);
        q.set(0, 0, -2, (uint8_t)B_CUBE);
        ok = q.raycast({ 0.5f, 0.75f, 3.5f }, { 0, 0, -1 }, 10, h2);
        CHECK(ok && h2.z == -2 && h2.y == 0, "ray just above the half slab passes over it to the cube behind (cell %d %d %d)", h2.x, h2.y, h2.z);
        q.set(0, 0, -2, (uint8_t)B_AIR);
        // stairs going up toward +Z: 4 steps of 0.25 at z=2..5, then a platform at height 1
        for (int x = -2; x <= 2; x++) { q.set(x, 0, 2, Block{ B_STAIRS, makeRot(2, 1), 4 }); for (int z = 3; z < 7; z++) q.set(x, 0, z, (uint8_t)B_CUBE); }
        uint8_t sr = chooseRot(B_STAIRS, 0, 1, 0, { 0, 0, 1 }, 0, false);
        for (int x = -2; x <= 2; x++) q.set(x, 0, 2, Block{ B_STAIRS, sr, 4 });
        V3 feet{ 0.5f, 0.f, -1.5f }, vel{ 0, 0, 0 }; bool g = true;
        for (int i = 0; i < 120; i++) { vel.z = 4; vel.y -= 24.f / 60; feet = q.moveBox(feet, vel, 0.35f, 1.9f, 1 / 60.f, g, 0.55f); }
        CHECK(feet.y > 0.99f && feet.z > 3.f, "walks up 4-step stairs onto a 1-block platform (y=%.2f z=%.2f)", feet.y, feet.z);
        Room q2; q2.newFlat(8); for (int x = -2; x <= 2; x++) q2.set(x, 0, 0, (uint8_t)B_CUBE);
        feet = { 0.5f, 0.f, -2.5f }; vel = { 0, 0, 0 }; g = true;
        for (int i = 0; i < 120; i++) { vel.z = 4; vel.y -= 24.f / 60; feet = q2.moveBox(feet, vel, 0.35f, 1.9f, 1 / 60.f, g, 0.55f); }
        CHECK(feet.y < 0.01f && feet.z < -0.3f, "but a full block still blocks walking (y=%.2f z=%.2f)", feet.y, feet.z);
        Room q3; q3.newFlat(8); uint8_t slr = chooseRot(B_SLOPE, 0, 1, 0, { 0, 0, 1 }, 0, false);
        for (int x = -2; x <= 2; x++) { q3.set(x, 0, 0, Block{ B_SLOPE, slr, 8 }); for (int z = 1; z < 5; z++) q3.set(x, 0, z, (uint8_t)B_CUBE); }
        feet = { 0.5f, 0.f, -2.5f }; vel = { 0, 0, 0 }; g = true;
        for (int i = 0; i < 80; i++) { vel.z = 4; vel.y -= 24.f / 60; feet = q3.moveBox(feet, vel, 0.35f, 1.9f, 1 / 60.f, g, 0.55f); }
        CHECK(feet.y > 0.99f && feet.z > 1.5f && feet.z < 5.f, "walks up a full slope onto the block behind it (y=%.2f z=%.2f)", feet.y, feet.z);
        q3.name = "shapes"; q3.save("/tmp/s.room"); Room q4; std::string e2; q4.load("/tmp/s.room", e2);
        CHECK(q4.cells == q3.cells && q4.rots == q3.rots && q4.sizes == q3.sizes, "shapes, rotations and sizes survive save/load");
    }
    // ---- big rooms: create, resize keeps blocks, chunked meshes equal the whole mesh
    {
        Room big; big.create(256, 64, 256, true, MAT_ROCK);
        CHECK(big.count() == 256u * 256u && big.ox == -128 && big.block(100, -1, -100).mat == MAT_ROCK, "256x64x256 room with a full rock floor");
        big.set(10, 5, 10, Block{ B_SLOPE, makeRot(2, 1), 8, MAT_SAND });
        big.resizeKeep(320, 80, 300);
        CHECK(big.sx == 320 && big.sy == 80 && big.sz == 300 && big.block(10, 5, 10) == (Block{ B_SLOPE, makeRot(2, 1), 8, MAT_SAND }) && big.count() == 256u * 256u + 1,
              "resizing keeps every block in place (%zu blocks)", big.count());
        big.resizeKeep(16, 16, 16);
        CHECK(big.sx == 16 && big.solid(0, -1, 0) && !big.inBounds(10, 5, 10), "shrinking crops to the new size");
        Room t; t.newFlat(16); for (int i = 0; i < 300; i++) t.set((i * 7) % 40 - 20, (i * 3) % 10, (i * 11) % 40 - 20, Block{ (uint8_t)(1 + i % 13), (uint8_t)(i % 24), 4, (uint8_t)(i % 10) });
        std::vector<SubMesh> whole; t.buildMeshes(whole); size_t wt = 0; for (auto& s2 : whole) wt += s2.mesh.i.size();
        size_t ct = 0; for (int cy = t.oy; cy < t.oy + t.sy; cy += 16) for (int cz = t.oz; cz < t.oz + t.sz; cz += 16) for (int cx = t.ox; cx < t.ox + t.sx; cx += 16) {
            std::vector<SubMesh> part; t.buildMeshesBox(cx, cy, cz, cx + 16, cy + 16, cz + 16, part); for (auto& s2 : part) ct += s2.mesh.i.size(); }
        CHECK(ct == wt && wt > 0, "16^3 chunk meshes add up to the whole room (%zu = %zu indices)", ct, wt);
    }
    // ---- baked lighting: lights, shadows, glowing blocks, sun, saving
    {
        auto topLight = [](const Room& r, float x, float z) {   // brightness of the floor top near (x, z)
            std::vector<SubMesh> subs; r.buildMeshes(subs); float best = -1, bd = 1e9f;
            for (auto& sm : subs) for (auto& v : sm.mesh.v) if (v.n[1] > 0.5f && std::fabs(v.p[1]) < 1e-3f) { float d = std::fabs(v.p[0] - x) + std::fabs(v.p[2] - z); if (d < bd) { bd = d; best = (v.c[0] + v.c[1] + v.c[2]) / 3; } }
            return best;
        };
        Room r; r.newFlat(12); r.mood.ambient = 0.05f; r.mood.glowLight = 0;
        float dark = topLight(r, 0, 0);
        r.lights.push_back({ { 0.5f, 2.5f, 0.5f }, { 1, 0.8f, 0.6f }, 1.5f, 8, true, true });
        float lit = topLight(r, 0, 0), far = topLight(r, 7, 0);
        CHECK(lit > dark + 0.5f && far < lit * 0.5f, "a light brightens the floor under it and fades with distance (%.2f -> %.2f, far %.2f)", dark, lit, far);
        for (int y = 0; y < 4; y++) for (int z = -3; z <= 3; z++) r.set(2, y, z, (uint8_t)B_CUBE);
        float shadowed = topLight(r, 4, 0), open_ = topLight(r, -3, 0);
        CHECK(shadowed < open_ * 0.5f, "a wall casts a shadow (behind the wall %.2f, open side %.2f)", shadowed, open_);
        r.lights[0].shadows = false;
        CHECK(topLight(r, 4, 0) > shadowed + 0.1f, "lights with shadows off shine through");
        Room g; g.newFlat(12); g.mood.ambient = 0.05f; float before = topLight(g, 2, 0);
        Block lava; lava.type = B_CUBE; lava.mat = MAT_LAVA; g.set(0, -1, 0, lava);
        float after = topLight(g, 1.5f, 0);
        CHECK(after > before + 0.1f, "lava lights the floor around it (%.2f -> %.2f)", before, after);
        Room sunny; sunny.newFlat(12); sunny.mood.ambient = 0.05f; sunny.mood.sun = true; sunny.mood.sunElev = 60;
        float sunLit = topLight(sunny, 0, 0); for (int z = -12; z < 12; z++) for (int x = -12; x < 12; x++) sunny.set(x, 6, z, (uint8_t)B_CUBE);
        CHECK(sunLit > 0.5f && topLight(sunny, 0, 0) < 0.2f, "sunlight lights the floor, and a roof blocks it");
        moodPreset(MP_MAGMOOR, r.mood); r.name = "lit"; r.save("/tmp/lit.room"); Room r2; std::string e; r2.load("/tmp/lit.room", e);
        CHECK(r2.lights.size() == 1 && std::fabs(r2.lights[0].intensity - 1.5f) < 1e-4f && !r2.lights[0].shadows && r2.mood.particles == PT_EMBERS && std::fabs(r2.mood.haze.x - 0.22f) < 1e-4f,
              "lights and room mood survive save/load");
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
