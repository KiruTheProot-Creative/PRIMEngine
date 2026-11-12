// Mesher tests: hidden faces, glass, corner shading, winding, edges of the loaded area, worker threads.
#include "../src/world/mesher.h"
#include <chrono>
#include <cstdio>
using namespace mc;
int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)

static ChunkMesh meshOf(const World& w, const BlockRegistry& r, I3 c) { MeshInput in; gatherInput(w, c, in); ChunkMesh m; buildMesh(r, in, m); return m; }
static size_t faces(const MeshData& m) { return m.i.size() / 6; }

int main() {
    BlockRegistry reg; registerBuiltinBlocks(reg);
    BlockId stone = reg.find("stone"), glass = reg.find("glass"), leaves = reg.find("leaves");
    World w(reg); w.gen.kind = Generator::Void;
    for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++) for (int x = -1; x <= 1; x++) w.loadChunk({ x, y, z });
    w.set(5, 5, 5, stone);
    CHECK(faces(meshOf(w, reg, { 0, 0, 0 }).opaque) == 6, "one block in the air: 6 faces");
    w.set(6, 5, 5, stone);
    CHECK(faces(meshOf(w, reg, { 0, 0, 0 }).opaque) == 10, "two touching blocks hide the faces between them: 10");
    w.set(10, 5, 5, glass); w.set(11, 5, 5, glass);
    ChunkMesh m = meshOf(w, reg, { 0, 0, 0 });
    CHECK(faces(m.cutout) == 10 && faces(m.opaque) == 10, "glass goes to its own pass and hides faces between glass blocks");
    w.set(10, 7, 5, leaves); w.set(11, 7, 5, leaves);
    CHECK(faces(meshOf(w, reg, { 0, 0, 0 }).cutout) == 22, "leaves keep the faces between them (bushy look)");
    w.set(7, 5, 5, glass);
    CHECK(faces(meshOf(w, reg, { 0, 0, 0 }).opaque) == 10, "stone next to glass keeps its face behind the glass");
    // a block across the border: the neighbour chunk hides the shared face
    w.set(15, 2, 2, stone); w.set(16, 2, 2, stone);
    size_t a = faces(meshOf(w, reg, { 0, 0, 0 }).opaque), b = faces(meshOf(w, reg, { 1, 0, 0 }).opaque);
    CHECK(a == 10 + 5 && b == 5, "a pair of blocks across a chunk border: 5 faces in each chunk");
    // the edge of the loaded area: no faces toward unloaded chunks
    World f(reg); f.gen.kind = Generator::Flat; f.gen.layers = { { stone, 16 } }; f.loadChunk({ 0, -1, 0 });
    ChunkMesh e = meshOf(f, reg, { 0, -1, 0 });
    CHECK(faces(e.opaque) == 0, "a full chunk with nothing loaded around it shows nothing (%zu faces)", faces(e.opaque));
    f.loadChunk({ 0, 0, 0 });
    CHECK(faces(meshOf(f, reg, { 0, -1, 0 }).opaque) == 256, "once the air above loads, its top shows: 256 faces");
    // corner shading: the floor next to a wall is darker
    World s(reg); s.gen.kind = Generator::Flat; s.gen.layers = { { stone, 1 } };
    for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++) for (int x = -1; x <= 1; x++) s.loadChunk({ x, y, z });
    s.set(5, 0, 5, stone);
    ChunkMesh sm = meshOf(s, reg, { 0, -1, 0 });
    float nearWall = 1, open = 0;
    for (auto& v : sm.opaque.v) if (faceOf(v) == 2) { if (v.x == 5 && v.z == 5) nearWall = std::min(nearWall, aoOf(v)); if (v.x == 1 && v.z == 1) open = std::max(open, aoOf(v)); }
    CHECK(nearWall < open * 0.9f, "floor at the foot of a block is shaded (%.2f vs %.2f)", nearWall, open);
    // winding: every triangle is clockwise seen from outside (cross product along the face normal)
    size_t bad = 0;
    for (auto* md : { &sm.opaque, &m.opaque, &m.cutout }) for (size_t i = 0; i < md->i.size(); i += 3) {
        const Vtx &A = md->v[md->i[i]], &B = md->v[md->i[i + 1]], &C = md->v[md->i[i + 2]];
        float e1[3] = { B.x - A.x, B.y - A.y, B.z - A.z }, e2[3] = { C.x - A.x, C.y - A.y, C.z - A.z };
        float cr[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
        static const float N[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
        int nf = faceOf(A); if (cr[0] * N[nf][0] + cr[1] * N[nf][1] + cr[2] * N[nf][2] <= 0) bad++;
    }
    CHECK(bad == 0, "every triangle faces outward (%zu wrong)", bad);
    // worker threads give the same meshes as building directly
    {
        World t(reg); t.gen.kind = Generator::Flat; t.gen.layers = { { stone, 3 }, { reg.find("dirt"), 2 }, { reg.find("grass"), 1 } };
        t.stream({ 0, 0, 0 }, 6, 2, 100000);
        for (int i = 0; i < 400; i++) t.set((i * 37) % 160 - 80, (i * 7) % 12, (i * 53) % 160 - 80, (BlockId)(1 + i % (reg.count() - 1)));
        MeshWorkers mw(reg); mw.start(4);
        size_t direct = 0; int jobs = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (auto& kv : t.chunks) { MeshInput in; gatherInput(t, kv.first, in); MeshInput copy = in; ChunkMesh d; buildMesh(reg, copy, d); direct += d.opaque.i.size() + d.cutout.i.size(); mw.submit(std::move(in)); jobs++; }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::vector<ChunkMesh> got; while (got.size() < (size_t)jobs) { mw.collect(got); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        size_t threaded = 0; for (auto& g : got) threaded += g.opaque.i.size() + g.cutout.i.size();
        CHECK(threaded == direct && direct > 0, "%d chunks meshed on 4 threads match direct meshing (%zu indices)", jobs, threaded);
        printf("      direct meshing: %.3f ms per chunk\n", ms / jobs);
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
