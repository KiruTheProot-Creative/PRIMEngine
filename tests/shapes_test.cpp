// Shape tests: rotations, every shape closed and facing outward, hidden sides between shapes,
// walking up stairs and ramps, aiming past thin shapes, automatic placement rotation, loading old saves.
#include "../src/world/mesher.h"
#include "../src/world/shapes.h"
#include "../src/game/game.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>
using namespace mc;
int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)

static size_t tris(const MeshData& m) { return m.i.size() / 3; }
static World* flatWorld(const BlockRegistry& r) {
    World* w = new World(r); w->gen.kind = Generator::Flat; w->gen.layers = { { r.find("stone"), 4 } };
    w->stream({ 0, 0, 0 }, 3, 2, 100000); return w;
}

int main() {
    auto t0 = std::chrono::steady_clock::now();
    const Shapes& S = Shapes::get();
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(S.count() > 90, "%d shapes in %zu categories, prepared in all 24 rotations in %.0f ms", S.count() - 1, S.categories().size(), ms);

    // rotations
    { std::set<std::vector<int>> seen; bool idFirst = true;
      for (int r = 0; r < 24; r++) { std::vector<int> k; for (int f = 0; f < 6; f++) k.push_back(Shapes::sideAfter(r, f)); seen.insert(k); if (r == 0) for (int f = 0; f < 6; f++) idFirst &= k[f] == f; }
      CHECK(seen.size() == 24 && idFirst, "24 different rotations, number 0 changes nothing");
      bool closed = true; for (int r = 0; r < 24; r++) for (int a = 0; a < 3; a++) { int q = r; for (int i = 0; i < 4; i++) q = Shapes::turn(q, a); closed &= q == r; }
      CHECK(closed, "four quarter turns around any axis come back to the start");
      int r = Shapes::rotFor(F_PZ, F_PX, F_PY, F_NY); CHECK(r >= 0 && Shapes::sideAfter(r, F_PZ) == F_PX && Shapes::sideAfter(r, F_PY) == F_NY, "rotation found by two directions"); }

    // every shape in every rotation: inside the block, closed surface, all triangles facing outwards
    { int bad = 0, badW = 0, badBox = 0; std::string worst;
      for (int s = 1; s < S.count(); s++) for (int r = 0; r < 24; r++) {
        const ShapeVariant& v = S.variant(s, r); double sum[3] = {}; size_t n = 0;
        for (int g = 0; g < 7; g++) for (const STri& t : v.tris[g]) {
            n++;
            float e1[3], e2[3], c[3]; for (int k = 0; k < 3; k++) { e1[k] = t.p[1][k] - t.p[0][k]; e2[k] = t.p[2][k] - t.p[0][k]; }
            c[0] = e1[1] * e2[2] - e1[2] * e2[1]; c[1] = e1[2] * e2[0] - e1[0] * e2[2]; c[2] = e1[0] * e2[1] - e1[1] * e2[0];
            if (c[0] * t.fn[0] + c[1] * t.fn[1] + c[2] * t.fn[2] <= 0) badW++;
            for (int k = 0; k < 3; k++) { sum[k] += c[k] * 0.5; for (int q = 0; q < 3; q++) if (t.p[k][q] < -1e-4f || t.p[k][q] > 1 + 1e-4f) badBox++; }
        }
        if (!S.info(s).twoSided && std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]) > 1e-3) { bad++; if (r == 0) { worst += S.info(s).key + " "; } }
        if (n == 0 || v.boxes.empty()) { bad++; worst = S.info(s).key + " (empty)"; }
      }
      CHECK(bad == 0, "every solid shape is closed (%d bad%s%s)", bad, bad ? ", e.g. " : "", worst.c_str());
      CHECK(badW == 0 && badBox == 0, "all triangles face outward and stay inside the block (%d / %d bad)", badW, badBox); }

    int HALF = S.find("half"), RAMP = S.find("ramp"), STAIRS = S.find("stairs"), POLE = S.find("pole4");
    { const ShapeVariant& f = S.variant(Shapes::FULL, 0); bool all = true; for (bool b : f.full) all &= b; CHECK(all, "full block covers all 6 sides");
      const ShapeVariant& h = S.variant(HALF, 0); CHECK(h.full[F_NY] && !h.full[F_PY] && !h.full[F_PX] && h.mask[F_PX].count() == 128, "half block: bottom full, side half covered (%zu/256)", h.mask[F_PX].count());
      const ShapeVariant& r = S.variant(RAMP, 0); CHECK(r.full[F_NY] && r.full[F_PZ] && r.mask[F_NZ].none() && r.mask[F_PX].count() > 100 && r.mask[F_PX].count() < 156, "ramp: bottom and back full, front open, sides triangles"); }

    // hidden faces between shapes
    {
        BlockRegistry reg; registerBuiltinBlocks(reg); BlockId stone = reg.find("stone");
        World w(reg); w.gen.kind = Generator::Void; for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++) w.loadChunk({ x, y, z });
        auto meshTris = [&] { MeshInput in; gatherInput(w, { 0, 0, 0 }, in); ChunkMesh m; buildMesh(reg, in, m); return tris(m.opaque); };
        w.set(5, 5, 5, makeBlock(stone, HALF)); size_t one = meshTris();
        w.set(6, 5, 5, makeBlock(stone, HALF)); size_t two = meshTris();
        CHECK(one == 12 && two == 20, "two half blocks side by side hide the sides between them (%zu, %zu triangles)", one, two);
        w.set(5, 4, 5, stone); size_t three = meshTris();
        CHECK(three == 20 - 2 + 10, "stone under a half block: its top and the half block's bottom are hidden (%zu)", three);
        w.set(7, 5, 5, stone); size_t four = meshTris();
        CHECK(four == three - 2 + 12, "stone beside a half block keeps its side (only half covered) (%zu)", four);
        w.set(5, 5, 6, makeBlock(stone, POLE)); size_t five = meshTris(); w.set(5, 5, 6, AIR);
        CHECK(five > four && five < four + 80, "a pole renders (%zu triangles)", five - four);
    }

    // walking: up a half block, up stairs, up a ramp; blocked by a pole only where it is
    {
        BlockRegistry reg; registerBuiltinBlocks(reg); BlockId stone = reg.find("stone");
        World* w = flatWorld(reg);
        auto walk = [&](V3 start, double secs) { Player p; p.feet = start; p.flying = false; p.yaw = 0; p.onGround = true; Controls c; c.forward = true;
            V3 r = p.feet; for (int i = 0; i < (int)(secs * 60); i++) { p.update(*w, c, 1.0 / 60); r.z = p.feet.z; r.x = p.feet.x; r.y = std::max(r.y, p.feet.y); } return r; };   // y: highest point reached
        w->set(0, 0, 3, makeBlock(stone, HALF));
        V3 a = walk({ 0.5, 0, 0.5 }, 0.8); CHECK(a.y > 0.49 && a.z > 3.2, "walks up onto a half block (y = %.2f, z = %.2f)", a.y, a.z);
        int rs = Shapes::placeRot(S.info(STAIRS).place, F_PY, F_PZ, false);
        for (int z = 10; z < 13; z++) { w->set(2, z - 10, z, makeBlock(stone, STAIRS, rs)); for (int y = 0; y < z - 10; y++) w->set(2, y, z, stone); }
        V3 b = walk({ 2.5, 0, 8.5 }, 2.5); CHECK(b.y > 2.9, "walks up three stairs (y = %.2f)", b.y);
        int rr = Shapes::placeRot(S.info(RAMP).place, F_PY, F_PZ, false);
        for (int z = 20; z < 23; z++) { w->set(-3, z - 20, z, makeBlock(stone, RAMP, rr)); for (int y = 0; y < z - 20; y++) w->set(-3, y, z, stone); }
        V3 c = walk({ -2.5, 0, 18.5 }, 2.5); CHECK(c.y > 2.9, "walks up a ramp (y = %.2f)", c.y);
        w->set(5, 0, 3, makeBlock(stone, POLE)); w->set(5, 1, 3, makeBlock(stone, POLE));
        V3 d = walk({ 5.5, 0, 0.5 }, 1.0); V3 e = walk({ 5.0, 0, 0.5 }, 1.0);
        CHECK(d.z < 3.4 && e.z > 4, "a 4 px pole blocks the middle (z %.2f) but not its edge (z %.2f)", d.z, e.z);
        Hit h; bool hit = w->raycast({ 5.1, 1.5, 0.5 }, { 0, 0, 1 }, 8, h);
        w->set(5, 1, 6, stone); hit = w->raycast({ 5.1, 1.5, 0.5 }, { 0, 0, 1 }, 8, h);
        CHECK(hit && h.z == 6, "aiming past the pole hits the block behind (z = %d)", h.z);
        hit = w->raycast({ 5.5, 1.5, 0.5 }, { 0, 0, 1 }, 8, h);
        CHECK(hit && h.z == 3 && std::fabs(h.pz - 3.375) < 1e-6 && h.nz == -1, "aiming at the pole hits its surface (z = %.3f)", h.pz);
        delete w;
    }

    // automatic rotation when placing
    {
        int r = Shapes::placeRot(Place::Facing, F_PY, F_PX, false);
        CHECK(Shapes::sideAfter(r, F_PZ) == F_PX && Shapes::sideAfter(r, F_PY) == F_PY, "stairs face the way you look");
        r = Shapes::placeRot(Place::Facing, F_NY, F_PX, false); CHECK(Shapes::sideAfter(r, F_PY) == F_NY, "against a ceiling they are upside down");
        r = Shapes::placeRot(Place::Facing, F_NZ, F_PZ, true); CHECK(Shapes::sideAfter(r, F_PY) == F_NY, "on the top half of a wall too");
        r = Shapes::placeRot(Place::Axis, F_PX, F_NX, false); CHECK(Shapes::sideAfter(r, F_PY) == F_PX, "a pillar sticks out of the wall you click");
        r = Shapes::placeRot(Place::Surface, F_NZ, F_PZ, false); CHECK(Shapes::sideAfter(r, F_PX) == F_NZ, "a plane lies flat on the wall you click");
    }

    // old saves (milestone 2): 16-bit ids with the plane's facing in the top 4 bits
    {
        std::vector<uint8_t> blob = { 7, (uint8_t)(F_PZ << 4), 0x00, 0x10 };   // value 7 | F_PZ << 12, 4096 times
        Chunk c; bool ok = RegionStore::decode(blob, c, 1);
        BlockId v = c.b[0]; int r = rotOf(v);
        CHECK(ok && typeOf(v) == 7 && shapeOf(v) == 0 && Shapes::sideAfter(r, F_PX) == F_PZ, "an old plane keeps facing the same way");
        std::vector<uint8_t> blob2 = { 3, 0, 0x00, 0x10 }; Chunk c2; RegionStore::decode(blob2, c2, 1);
        CHECK(c2.b[0] == 3, "old plain blocks stay exactly the same");
        Chunk c3; for (int i = 0; i < CS3; i++) c3.b[i] = makeBlock(5, i % 97, i % 24);
        Chunk c4; CHECK(RegionStore::decode(RegionStore::encode(c3), c4) && c3.b == c4.b, "new format keeps shape and rotation");
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
