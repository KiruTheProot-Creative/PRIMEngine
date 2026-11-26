// Mesher tests: hidden faces, glass, corner shading, winding, edges of the loaded area, worker threads.
#include "../src/world/mesher.h"
#include "../src/world/visibility.h"
#include "../src/world/lod.h"
#include <set>
#include <map>
#include <unordered_map>
#include "../src/world/shapes.h"
#include <chrono>
#include <cmath>
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
    {   // a face that varies from block to block: turned, mirrored, other textures - by the block's place, always the same
        BlockRegistry vr; registerBuiltinBlocks(vr);
        BlockId vs = vr.find("stone"); BlockDef d = vr.get(vs); int own = d.tex[F_PY], cob = vr.textureIndex("cobblestone"), dirtT = vr.textureIndex("dirt");
        d.vary[F_PY].turns = 2; d.vary[F_PY].others = { "cobblestone", "dirt", "no such texture" }; d.vary[F_PY].weights = { 1, 2, 5 }; d.vary[F_PY].ownWeight = 1;
        vr.put(vs, d);
        World vw(vr); vw.gen.kind = Generator::Void;
        for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++) for (int x = -1; x <= 1; x++) vw.loadChunk({ x, y, z });
        for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) vw.set(x, 3, z, vs);
        auto tops = [&](const ChunkMesh& cm) {   // each top face: its layer, and which way its first corner's texture coordinates point
            std::map<std::pair<int, int>, std::pair<int, int>> out;
            for (size_t i = 0; i + 3 < cm.opaque.v.size(); i += 4) {
                const Vtx& a = cm.opaque.v[i]; if (faceOf(a) != F_PY) continue;
                int turn = (a.u < 0.5f ? 0 : 1) + (a.v < 0.5f ? 0 : 2);
                out[{ (int)std::floor(a.x), (int)std::floor(a.z) }] = { (int)std::lround(a.layer), turn };
            }
            return out;
        };
        auto t1 = tops(meshOf(vw, vr, { 0, 0, 0 })), t2 = tops(meshOf(vw, vr, { 0, 0, 0 }));
        std::map<int, int> layers; std::set<int> turns; int sides = 0;
        for (auto& kv : t1) { layers[kv.second.first]++; turns.insert(kv.second.second); }
        for (const Vtx& vx : meshOf(vw, vr, { 0, 0, 0 }).opaque.v) if (faceOf(vx) == F_PX && std::lround(vx.layer) != d.tex[F_PX]) sides++;
        CHECK(t1.size() == (size_t)CS * CS && t1 == t2, "varying tops: the same look each time the chunk is built");
        CHECK(turns.size() == 4, "quarter turns: all four ways come up (%zu)", turns.size());
        float n = (float)t1.size();
        CHECK(layers.size() == 3 && layers[own] > 0 && std::fabs(layers[dirtT] / n - 0.5f) < 0.12f && std::fabs(layers[cob] / n - 0.25f) < 0.12f,
              "other textures as often as their weights say (own %d, cobblestone %d, dirt %d of %d; a missing texture left out)", layers[own], layers[cob], layers[dirtT], (int)n);
        CHECK(sides == 0, "the faces that don't vary stay as they were");
        BlockDef d2 = vr.get(vs); d2.vary[F_PY].seed = 7; vr.put(vs, d2);
        auto t3 = tops(meshOf(vw, vr, { 0, 0, 0 })); int diff = 0; for (auto& kv : t1) diff += t3[kv.first] != kv.second;
        CHECK(diff > (int)t1.size() / 3, "shuffle (another seed): another pick (%d of %zu changed)", diff, t1.size());
        BlockDef h = vr.get(vs); h.vary[F_PY] = FaceVary{}; h.vary[F_PY].turns = 1; vr.put(vs, h);
        std::set<int> half; for (auto& kv : tops(meshOf(vw, vr, { 0, 0, 0 }))) { half.insert(kv.second.second); if (kv.second.first != own) diff = -1000; }
        CHECK(half.size() == 2 && diff > -1000, "half turns: only two ways (0 and 180 degrees), its own texture only");
        float u = 0.1f, v = 0.2f; BlockDef m = vr.get(vs); m.vary[F_PY] = FaceVary{}; m.vary[F_PY].mirror = true; int mirrored = 0;
        for (int x = 0; x < 64; x++) { float uu = u, vv = v; varyFace(m, F_PY, x, 0, 0, own, uu, vv); mirrored += uu > 0.5f; if (vv != v) mirrored = -1000; }
        CHECK(mirrored > 16 && mirrored < 48, "mirrored: about half of them (%d of 64), not turned", mirrored);
    }
    {   // a block lit by its texture's emission map lights what is around it, like a lamp (but does not glow all over)
        BlockRegistry r2; registerBuiltinBlocks(r2);
        BlockDef gd = r2.get(r2.find("stone")); gd.name = "glow panel"; gd.light = 0; gd.autoLight = 12; gd.autoLightColor = 0x2080FF; BlockId gp = r2.add(gd);
        World lw(r2); lw.gen.kind = Generator::Void;
        for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++) for (int x = -1; x <= 1; x++) lw.loadChunk({ x, y, z });
        lw.set(5, 5, 5, gp); BlockId st = r2.find("stone"); lw.set(8, 5, 5, st); lw.recountEmitters();
        ChunkMesh lm = meshOf(lw, r2, { 0, 0, 0 });
        int bestB = 0, bestR = 0; bool panelGlows = false;
        for (const Vtx& v : lm.opaque.v) {
            int rr = (v.lit >> 8) & 15, bb = (v.lit >> 16) & 15;
            if (v.x > 7.9f && v.x < 8.1f) { bestB = std::max(bestB, bb); bestR = std::max(bestR, rr); }   // the stone's face toward it
            if (v.x > 4.9f && v.x < 6.1f && ((v.lit >> 20) & 1)) panelGlows = true;
        }
        int lv[3]; r2.get(gp).lightLevels(lv);
        CHECK(lv[2] == 12 && lv[0] < lv[2] && r2.get(gp).lightOf() == 12, "light from an emission map: its strength and colour (%d %d %d)", lv[0], lv[1], lv[2]);
        CHECK(bestB >= 8 && bestB > bestR, "the stone next to it is lit blue by it (blue %d, red %d)", bestB, bestR);
        CHECK(!panelGlows, "the block itself is not flooded with light (its emission map shows what glows)");
    }
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
    {   // turned blocks turn their textures: a log tipped over shows its rings (top texture) at the ends
        BlockId log = reg.find("log"); const BlockDef& ld = reg.get(log);
        int tip = Shapes::rotFor(F_PY, F_PX, F_PZ, F_PZ);   // its top now looks along +X
        CHECK(tip > 0 && Shapes::sideBefore(tip, F_PX) == F_PY && Shapes::sideAfter(tip, Shapes::sideBefore(tip, F_NZ)) == F_NZ, "the turn and its way back");
        bool back = true; for (int r = 0; r < 24; r++) for (float t : { 0.1f, 0.5f, 0.9f }) { float p[3] = { t, 0.3f, 0.8f }; float q[3]; const Rot& m = Shapes::rot(r);
            for (int i = 0; i < 3; i++) q[i] = m.m[i][0] * (p[0] - 0.5f) + m.m[i][1] * (p[1] - 0.5f) + m.m[i][2] * (p[2] - 0.5f) + 0.5f; Shapes::unturn(r, q);
            for (int i = 0; i < 3; i++) back &= std::fabs(q[i] - p[i]) < 1e-5f; }
        CHECK(back, "every one of the 24 turns can be undone for a point in the block");
        World t(reg); t.gen.kind = Generator::Void; for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++) for (int x = -1; x <= 1; x++) t.loadChunk({ x, y, z });
        t.set(4, 4, 4, makeBlock(log, 0, tip));
        ChunkMesh lm = meshOf(t, reg, { 0, 0, 0 }); int ends = 0, sides = 0, wrong = 0;
        for (const Vtx& v : lm.opaque.v) { int f = faceOf(v), layer = (int)v.layer % 16384; bool end = f == F_PX || f == F_NX;
            if (end) { ends += layer == ld.tex[F_PY] || layer == ld.tex[F_NY]; wrong += layer != ld.tex[F_PY] && layer != ld.tex[F_NY]; } else { sides += layer == ld.tex[F_PX]; wrong += layer != ld.tex[F_PX]; } }
        CHECK(ld.tex[F_PY] != ld.tex[F_PX] && ends == 8 && sides == 16 && wrong == 0, "a tipped-over log: rings on its two ends, bark on the four sides (%d, %d, %d wrong)", ends, sides, wrong);
        BlockDef nd = ld; nd.texTurns = false; reg.put(log, nd);
        ChunkMesh wm = meshOf(t, reg, { 0, 0, 0 }); int topRings = 0; for (const Vtx& v : wm.opaque.v) if (faceOf(v) == F_PY && (int)v.layer % 16384 == ld.tex[F_PY]) topRings++;
        CHECK(topRings == 4, "with 'textures turn' off they stay lined up with the world (rings on top)");
        nd.texTurns = true; reg.put(log, nd);
        t.set(4, 4, 4, makeBlock(log, 0, 0)); ChunkMesh um = meshOf(t, reg, { 0, 0, 0 }); int upRings = 0; for (const Vtx& v : um.opaque.v) if (faceOf(v) == F_PY && (int)v.layer % 16384 == ld.tex[F_PY]) upRings++;
        CHECK(upRings == 4, "an unturned log: rings on top as before");
        BlockId stairsLog = makeBlock(log, Shapes::get().find("stairs"), tip);
        t.set(4, 4, 4, stairsLog); ChunkMesh sm = meshOf(t, reg, { 0, 0, 0 }); int px = 0; for (const Vtx& v : sm.opaque.v) if (faceOf(v) == F_PX && (int)v.layer % 16384 == ld.tex[F_PY]) px++;
        CHECK(Shapes::get().find("stairs") > 0 && px > 0, "shapes turn their textures too (a tipped log stair shows rings toward +X)");
    }
    {   // cave culling: which sides of a chunk are joined, and the walk from the camera
        printf("\ncave culling\n");
        std::vector<uint8_t> sol(CS3, 0);
        CHECK(chunkSeeMask(sol.data()) == SEE_ALL, "an empty chunk: every side joined");
        std::fill(sol.begin(), sol.end(), 1); CHECK(chunkSeeMask(sol.data()) == 0, "a solid chunk: nothing joined");
        for (int y = 0; y < CS; y++) for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) sol[Chunk::idx(x, y, z)] = y < 8 ? 1 : 0;   // solid floor, air above
        uint16_t f = chunkSeeMask(sol.data());
        CHECK(seeConnects(f, 0, 1) && seeConnects(f, 0, 3) && seeConnects(f, 4, 5) && !seeConnects(f, 2, 3) && !seeConnects(f, 2, 0), "half solid (the bottom): the sides and the top are joined, the bottom with nothing");
        std::fill(sol.begin(), sol.end(), 1); for (int x = 0; x < CS; x++) sol[Chunk::idx(x, 8, 8)] = 0;   // a tunnel along X
        f = chunkSeeMask(sol.data()); CHECK(f == (1u << seePair(0, 1)), "a tunnel along X: only -X with +X");
        int pairs = 0; std::set<int> bits; for (int a = 0; a < 6; a++) for (int b = a + 1; b < 6; b++) { bits.insert(seePair(a, b)); pairs++; }
        CHECK(pairs == 15 && bits.size() == 15 && *bits.rbegin() == 14, "15 side pairs, 15 bits");

        // a world of chunks: open air above y >= 0, solid below; a sealed cave room deep down; a shaft down to a second cave
        std::map<std::tuple<int, int, int>, uint16_t> masks;
        auto see = [&](I3 c) -> uint16_t { auto it = masks.find({ c.x, c.y, c.z }); if (it != masks.end()) return it->second; return c.y >= 0 ? SEE_ALL : 0; };
        auto all = [](I3) { return true; };
        std::vector<I3> vis; auto has = [&](I3 c) { for (const I3& v : vis) if (v.x == c.x && v.y == c.y && v.z == c.z) return true; return false; };
        masks[{ 3, -3, 0 }] = 0;                                    // (a room sealed in rock: solid around it)
        visibleChunks({ 0, 1, 0 }, 6, 4, see, all, vis);
        CHECK(has({ 5, 1, 2 }) && has({ 0, 0, 0 }) && !has({ 0, -2, 0 }) && !has({ 3, -3, 0 }), "standing in the open: the surface chunks show, the rock and the cave under it do not (%d of %d)", (int)vis.size(), 13 * 13 * 9);
        // a shaft: chunk (0,-1,0) joins top and bottom, (0,-2,0) is a cave joined on all sides with (1,-2,0) open too
        masks[{ 0, -1, 0 }] = (uint16_t)(1u << seePair(2, 3)); masks[{ 0, -2, 0 }] = SEE_ALL; masks[{ 1, -2, 0 }] = SEE_ALL;
        visibleChunks({ 0, 1, 0 }, 6, 4, see, all, vis);
        CHECK(has({ 0, -1, 0 }) && has({ 0, -2, 0 }) && has({ 1, -2, 0 }) && has({ 0, -3, 0 }) && !has({ 3, -3, 0 }) && !has({ 4, -2, 0 }), "looking down a shaft: the shaft, the cave at its bottom and what touches it show; the sealed room still not");
        visibleChunks({ 3, -3, 0 }, 6, 4, see, all, vis);
        CHECK(vis.size() == 7 && has({ 3, -3, 0 }) && has({ 4, -3, 0 }) && !has({ 0, 1, 0 }), "standing inside the sealed room: only it and the rock right around it");
        auto front = [](I3 c) { return c.z >= 0; };
        visibleChunks({ 0, 1, 0 }, 6, 4, see, front, vis);
        CHECK(has({ 0, 1, 4 }) && !has({ 0, 1, -3 }) && has({ 0, 1, 0 }), "the view: chunks behind you are not walked to");
    }
    {   // far copies (levels of detail)
        World lw(reg); lw.gen.kind = Generator::Void;
        BlockId stone = reg.find("stone"), grass = reg.find("grass"), water = reg.liquidBlock(1);
        for (int z = 0; z < 128; z++) for (int x = 0; x < 128; x++) { lw.set(x, 0, z, grass); lw.set(x, -1, z, stone); lw.set(x, -2, z, stone); }   // ground: grass over stone
        for (int z = 0; z < 128; z++) lw.set(5, 1, z, stone);                                      // a wall one block thick, one high (thin: gone far away)
        for (int z = 10; z < 20; z++) for (int x = 10; x < 20; x++) for (int y = 2; y <= 3; y++) lw.set(x, y, z, makeLiquid(water, 16));   // a pond (two deep)
        std::unordered_map<I3, LodSummary, I3Hash> sums;
        for (int cy = -1; cy <= 0; cy++) for (int cz = -1; cz <= 8; cz++) for (int cx = -1; cx <= 8; cx++) { Chunk& c = lw.loadChunk({ cx, cy, cz }); sums[{ cx, cy, cz }] = lodSummarize(reg, c); }
        auto get = [&](I3 c) -> const LodSummary* { auto it = sums.find(c); return it == sums.end() ? nullptr : &it->second; };
        const LodSummary& s0 = sums[{ 0, 0, 0 }];
        CHECK(s0.kind == 2 && s0.at(1, 0, 1) == typeOf(grass) && s0.at(1, 1, 1) == 0, "a chunk boiled down: the ground's cells look like grass (the top), above them air");
        const LodSummary& sb = sums[I3{ 0, -1, 0 }];
        CHECK(sb.kind == 1 || sb.kind == 2, "below: stone cells");
        for (int k = 1; k <= 3; k++) {
            std::vector<uint16_t> cells; lodGroupCells(reg, k, I3{ 0, 0, 0 }, get, cells);
            int solidTop = 0, water = 0;
            for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) for (int y = 0; y < 16; y++) {
                uint16_t t = cells[lodCellIdx(x, y, z)];
                if (t == typeOf(grass)) solidTop++;
                if (reg.get((BlockId)t).liquid) water++;
            }
            MeshData md; bool any = lodMesh(reg, k, cells, md);
            CHECK(solidTop > 0 && any && md.i.size() >= 6 * 16 * 16 / 2, "level %d: the ground stays, looks like grass, and has triangles (%d grass cells, %zu triangles)", k, solidTop, md.i.size() / 3);
            if (k == 1) CHECK(water > 0, "level 1: the pond shows as water");
        }
        std::vector<uint16_t> unk; lodGroupCells(reg, 1, I3{ 20, 0, 20 }, get, unk);
        CHECK(unk[lodCellIdx(3, 3, 3)] == LOD_UNKNOWN, "chunks not boiled down yet: unknown");
    }
    {   // connected textures: a connecting block creeps over its neighbours' faces on the same surface; grass blades: where they grow
        printf("\nconnected textures, grass blades\n");
        BlockRegistry r2; registerBuiltinBlocks(r2);
        BlockId gr = r2.find("grass"), sa = r2.find("sand"), co = r2.find("cobblestone");
        { BlockDef d = r2.get(gr); d.connects = true; d.connectWidth = 4; d.grass = true; d.grassHeight = 0.6f; r2.put(gr, d); }
        { BlockDef d = r2.get(sa); d.connects = true; d.connectWidth = 2; r2.put(sa, d); }
        World t(r2); t.gen.kind = Generator::Void;
        for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++) for (int x = -1; x <= 1; x++) t.loadChunk({ x, y, z });
        t.set(5, 5, 5, co); t.set(6, 5, 5, gr); t.set(4, 5, 5, co); t.set(5, 5, 6, sa); t.set(7, 5, 5, gr); t.set(6, 6, 6, co);
        ChunkMesh m = meshOf(t, r2, { 0, 0, 0 });
        auto topOf = [&](const ChunkMesh& mm, int x, int y, int z) -> const Vtx* {   // the top face of the block at x, y, z (cube faces: 4 vertices each)
            for (size_t i = 0; i + 3 < mm.opaque.v.size(); i += 4) {
                const Vtx* q = &mm.opaque.v[i]; float cx = 0, cy = 0, cz = 0; bool top = true;
                for (int k = 0; k < 4; k++) { cx += q[k].x / 4; cy += q[k].y / 4; cz += q[k].z / 4; top &= faceOf(q[k]) == F_PY; }
                if (top && std::fabs(cx - (x + 0.5f)) < 1e-3f && std::fabs(cy - (y + 1)) < 1e-3f && std::fabs(cz - (z + 0.5f)) < 1e-3f) return q;
            }
            return nullptr; };
        const Vtx* c5 = topOf(m, 5, 5, 5);
        int grassTop = r2.get(gr).tex[F_PY], sandTop = r2.get(sa).tex[F_PY];
        CHECK(c5 && connMode(*c5, 3) == 1 && connLayer(*c5, 3) == grassTop && connWidth(*c5, 3) == 4 && connMode(*c5, 1) == 1 && connLayer(*c5, 1) == sandTop && connWidth(*c5, 1) == 2 && connMode(*c5, 2) == 0,
              "a non-connecting block: the grass and the sand beside it creep over it, the cobblestone doesn't");
        const Vtx* g6 = topOf(m, 6, 5, 5);
        CHECK(g6 && connMode(*g6, 2) == 0 && connMode(*g6, 3) == 0, "the grass's own top: not crept over by the cobblestone (it doesn't connect), nor by grass (the same texture)");
        // two connecting blocks side by side: both creep, the border wanders either way
        t.set(5, 5, 5, AIR); t.set(4, 5, 5, AIR); t.set(5, 5, 6, AIR); t.set(9, 5, 5, sa); t.set(10, 5, 5, gr); m = meshOf(t, r2, { 0, 0, 0 });
        const Vtx* s9 = topOf(m, 9, 5, 5);
        CHECK(s9 && connMode(*s9, 3) == 2 && connWidth(*s9, 3) == 4 && connLayer(*s9, 3) == grassTop, "both connect: the border wanders either way, as wide as the wider one");
        // grass blades: the grass tops listed, with the sides that have no grass beside them (the blades lean out there)
        const GrassTop* g = nullptr; for (const GrassTop& q : m.grass) if (q.x == 6 && q.y == 5 && q.z == 5) g = &q;
        CHECK(m.grass.size() == 3 && g && g->layer == grassTop && (g->edges & 1) && !(g->edges & 2) && (g->edges & 4) && (g->edges & 8) && std::abs(g->height - (int)std::lround(0.6 / 1.5 * 255)) <= 1,
              "grass tops: listed with their texture, height and open sides (grass beside: no lean)");
    }
    {   // an overlay: a texture laid over a varying face - the vertices carry its layer and how the face below was turned (to turn it back)
        BlockRegistry r3; registerBuiltinBlocks(r3);
        BlockId dt = r3.find("dirt"); int over = r3.textureIndex("grass_side");
        { BlockDef d = r3.get(dt); for (int f : { F_PX, F_NX, F_PZ, F_NZ }) { d.overlay[f] = "grass_side"; d.vary[f].turns = 2; d.vary[f].mirror = true; } r3.put(dt, d); r3.resolve(); }
        World t(r3); t.gen.kind = Generator::Void;
        for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++) for (int x = -1; x <= 1; x++) t.loadChunk({ x, y, z });
        for (int k = 0; k < 6; k++) t.set(2 + k * 2, 5, 5, dt);
        ChunkMesh m = meshOf(t, r3, { 0, 0, 0 });
        int sides = 0, ok = 0, turned = 0, tops = 0;
        for (const Vtx& v : m.opaque.v) {
            int f = faceOf(v);
            if (f == F_PY || f == F_NY) { tops += v.over == 0; continue; }
            sides++;
            int bx = 2 * (int)std::floor(v.x / 2), k2 = 0; bool mi = false; float u = 0.25f, vv = 0.75f;   // (the blocks stand two apart, at even x)
            varyFace(r3.get(dt), f, bx, 5, 5, r3.get(dt).tex[f], u, vv, &k2, &mi);
            if ((int)(v.over & 0xFFFF) == over && (int)((v.over >> 16) & 3) == k2 && (((v.over >> 18) & 1) != 0) == mi) ok++;
            if ((v.over >> 16) & 7) turned++;
        }
        CHECK(over > 0 && sides == 6 * 4 * 4 && ok == sides && turned > 0 && tops == 6 * 2 * 4, "overlay: on the sides (with how each was turned, some turned), not on top or bottom");
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
