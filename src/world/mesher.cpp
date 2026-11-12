#include "mesher.h"
#include "shapes.h"

namespace mc {

bool gatherInput(const World& w, I3 pos, MeshInput& out) {
    const Chunk* self = w.chunk(pos); if (!self) return false;
    out.pos = pos; out.blocks.assign(PAD * PAD * PAD, UNKNOWN);
    for (int cy = -1; cy <= 1; cy++) for (int cz = -1; cz <= 1; cz++) for (int cx = -1; cx <= 1; cx++) {
        const Chunk* c = w.chunk({ pos.x + cx, pos.y + cy, pos.z + cz }); if (!c) continue;
        // the part of this neighbour that falls inside our padded box
        int x0 = cx < 0 ? CS - 1 : 0, x1 = cx > 0 ? 0 : CS - 1, y0 = cy < 0 ? CS - 1 : 0, y1 = cy > 0 ? 0 : CS - 1, z0 = cz < 0 ? CS - 1 : 0, z1 = cz > 0 ? 0 : CS - 1;
        for (int ly = y0; ly <= y1; ly++) for (int lz = z0; lz <= z1; lz++) for (int lx = x0; lx <= x1; lx++)
            out.blocks[padIdx(lx + cx * CS, ly + cy * CS, lz + cz * CS)] = c->get(lx, ly, lz);
    }
    out.lightSrc.clear(); out.lightColors.clear();
    if (w.emittersNear(pos)) {   // a lamp nearby: hand the mesh thread what it needs to spread the light
        out.lightSrc.assign((size_t)LPAD * LPAD * LPAD, 0);
        out.lightColors.push_back({ 0, 0, 0 });
        std::unordered_map<BlockId, uint8_t> slot;   // block type -> its colour entry
        auto colorOf = [&](BlockId t, const BlockDef& d) -> uint8_t {
            auto it = slot.find(t); if (it != slot.end()) return it->second;
            if (out.lightColors.size() >= 254) return 1;
            int lv[3]; d.lightLevels(lv); out.lightColors.push_back({ (uint8_t)lv[0], (uint8_t)lv[1], (uint8_t)lv[2] });
            return slot[t] = (uint8_t)(out.lightColors.size() - 1);
        };
        for (int cy = -1; cy <= 1; cy++) for (int cz = -1; cz <= 1; cz++) for (int cx = -1; cx <= 1; cx++) {
            const Chunk* c = w.chunk({ pos.x + cx, pos.y + cy, pos.z + cz });
            for (int ly = 0; ly < CS; ly++) for (int lz = 0; lz < CS; lz++) for (int lx = 0; lx < CS; lx++) {
                uint8_t v = 255;   // not loaded: blocks light
                if (c) {
                    BlockId b = c->get(lx, ly, lz);
                    if (b != AIR) { const BlockDef& d = w.reg.get(b); v = d.light ? colorOf(typeOf(b), d) : (d.render == Render::Opaque && resolveShape(b, d) == Shapes::FULL) ? 255 : 0; }
                    else v = 0;
                }
                out.lightSrc[lpadIdx(lx + cx * CS, ly + cy * CS, lz + cz * CS)] = v;
            }
        }
    }
    return true;
}

// what a neighbour covers on the side facing us (0 = nothing)
static const SideMask ALL = SideMask().set();
static const SideMask& coverOf(const BlockRegistry& reg, BlockId nb, int side) {
    static const SideMask NONE;
    const BlockDef& d = reg.get(nb); int s = resolveShape(nb, d);
    if (s == Shapes::FULL) return ALL;
    return Shapes::get().variant(s, rotOf(nb)).mask[side];
}
static bool isPlainCube(const BlockRegistry& reg, BlockId v) { return v != UNKNOWN && v != AIR && resolveShape(v, reg.get(v)) == Shapes::FULL; }

// spreads light from glowing blocks: each step away is one level darker, solid blocks stop it (Minecraft-style).
// Done once per colour channel, so a red and a blue lamp make purple where their light meets.
static void spreadLight(const std::vector<uint8_t>& src, const std::vector<std::array<uint8_t, 3>>& colors, int ch, std::vector<uint8_t>& L) {
    L.assign(src.size(), 0);
    std::vector<int> q; q.reserve(4096);
    for (size_t i = 0; i < src.size(); i++) if (src[i] > 0 && src[i] < 255) { uint8_t lv = colors[src[i] < colors.size() ? src[i] : 0][ch]; if (lv) { L[i] = lv; q.push_back((int)i); } }
    const int step[6] = { 1, -1, LPAD * LPAD, -LPAD * LPAD, LPAD, -LPAD };
    for (size_t h = 0; h < q.size(); h++) {
        int i = q[h], lv = L[i]; if (lv <= 1) continue;
        int x = i % LPAD, z = (i / LPAD) % LPAD, y = i / (LPAD * LPAD);
        for (int k = 0; k < 6; k++) {
            if ((k == 0 && x == LPAD - 1) || (k == 1 && x == 0) || (k == 2 && y == LPAD - 1) || (k == 3 && y == 0) || (k == 4 && z == LPAD - 1) || (k == 5 && z == 0)) continue;
            int j = i + step[k];
            if (src[j] == 255 || L[j] >= lv - 1) continue;
            L[j] = (uint8_t)(lv - 1); q.push_back(j);
        }
    }
}

void buildMesh(const BlockRegistry& reg, const MeshInput& in, ChunkMesh& out) {
    out.pos = in.pos; out.seq = in.seq; out.opaque = {}; out.cutout = {};
    std::vector<uint8_t> lightMap[3]; bool lit = !in.lightSrc.empty();
    if (lit) for (int c = 0; c < 3; c++) spreadLight(in.lightSrc, in.lightColors, c, lightMap[c]);
    auto lightAt = [&](int c, int x, int y, int z) { return lit ? (int)lightMap[c][lpadIdx(x, y, z)] : 0; };
    auto blocksLight = [&](int x, int y, int z) { return lit && in.lightSrc[lpadIdx(x, y, z)] == 255; };
    const auto& B = in.blocks;
    const Shapes& shapes = Shapes::get();
    auto at = [&](int x, int y, int z) { return B[padIdx(x, y, z)]; };
    auto occludes = [&](BlockId id) { return isPlainCube(reg, id) && reg.get(id).render == Render::Opaque; };   // corner shading
    // is the part `need` of our side `f` hidden by the neighbour there?
    auto hidden = [&](BlockId self, const BlockDef& d, BlockId nb, int f, const SideMask& need) {
        if (nb == UNKNOWN) return true;
        if (nb == AIR) return false;
        const BlockDef& nd = reg.get(nb);
        bool coverType = nd.render == Render::Opaque || (typeOf(nb) == typeOf(self) && d.cullSame);
        if (!coverType || nd.render == Render::Air) return false;
        return (need & ~coverOf(reg, nb, f ^ 1)).none();   // f^1 = the opposite side
    };
    for (int y = 0; y < CS; y++) for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
        BlockId id = at(x, y, z); if (id == AIR) continue;
        const BlockDef& d = reg.get(id); if (d.render == Render::Air) continue;
        bool cut = d.render == Render::Cutout;
        MeshData& md = cut ? out.cutout : out.opaque;
        int shape = resolveShape(id, d);
        if (shape != Shapes::FULL) {
            // any other shape: prepared triangles, sides lying on the block border hidden like cube faces
            const ShapeVariant& sv = shapes.variant(shape, rotOf(id)); bool two = shapes.info(shape).twoSided;
            int own[3]; if (d.light) d.lightLevels(own); else for (int c = 0; c < 3; c++) own[c] = lightAt(c, x, y, z);
            uint32_t litv = packLit(1.f, own, d.light > 0, d.mirror ? 1.f : d.shine, d.mirror);
            float wave = d.waves ? (shape == Shapes::CROSS || shape == Shapes::PLANE ? 2.f : 1.f) : 0.f;
            for (int g = 0; g < 7; g++) {
                if (sv.tris[g].empty()) continue;
                if (g < 6) { int n[3] = { 0, 0, 0 }; n[g / 2] = (g % 2 == 0) ? 1 : -1; if (hidden(id, d, at(x + n[0], y + n[1], z + n[2]), g, sv.mask[g])) continue; }
                for (const STri& t : sv.tris[g]) {
                    int f = faceOfNormal(t.fn); float layer = (float)d.tex[f] + 16384.f * wave;
                    uint32_t base = (uint32_t)md.v.size();
                    for (int k = 0; k < 3; k++) {
                        float u, v; faceUV(t.p[k][0], t.p[k][1], t.p[k][2], f, u, v);
                        md.v.push_back({ x + t.p[k][0], y + t.p[k][1], z + t.p[k][2], u, v, layer, litv, packNormal(t.n[k][0], t.n[k][1], t.n[k][2], f) });
                    }
                    md.i.insert(md.i.end(), { base, base + 1, base + 2 });
                    if (two) md.i.insert(md.i.end(), { base, base + 2, base + 1 });
                }
            }
            continue;
        }
        int c[3] = { x, y, z };
        for (int f = 0; f < 6; f++) {
            int a = f / 2, sgn = (f % 2 == 0) ? 1 : -1, n[3] = { 0, 0, 0 }; n[a] = sgn;
            if (hidden(id, d, at(x + n[0], y + n[1], z + n[2]), f, ALL)) continue;
            int ua = (a + 1) % 3, va = (a + 2) % 3, L[3] = { x + n[0], y + n[1], z + n[2] };
            const int du[4] = { 0, 1, 1, 0 }, dv[4] = { 0, 0, 1, 1 };
            Vtx q[4]; int ao[4];
            for (int k = 0; k < 4; k++) {
                float p[3] = { (float)c[0], (float)c[1], (float)c[2] };
                p[a] += sgn > 0 ? 1.f : 0.f; p[ua] += (float)du[k]; p[va] += (float)dv[k];
                int s1[3] = { L[0], L[1], L[2] }, s2[3] = { L[0], L[1], L[2] }, cc[3] = { L[0], L[1], L[2] };
                int su = du[k] ? 1 : -1, sv = dv[k] ? 1 : -1;
                s1[ua] += su; s2[va] += sv; cc[ua] += su; cc[va] += sv;
                int o1 = occludes(at(s1[0], s1[1], s1[2])), o2 = occludes(at(s2[0], s2[1], s2[2])), oc = occludes(at(cc[0], cc[1], cc[2]));
                ao[k] = (o1 && o2) ? 0 : 3 - (o1 + o2 + oc);
                // texture coordinates inside the block: side faces keep "up" pointing up
                float lx = p[0] - c[0], ly = p[1] - c[1], lz = p[2] - c[2], u, v;
                if (a == 1) { u = lx; v = lz; } else if (a == 0) { u = sgn > 0 ? 1 - lz : lz; v = 1 - ly; } else { u = sgn > 0 ? lx : 1 - lx; v = 1 - ly; }
                float shade = 0.4f + 0.6f * ao[k] / 3.f;   // corner darkening; the light itself comes from the sun in the shader
                // smooth lamp light: average of the open cells touching this corner in front of the face
                int bl[3] = { 0, 0, 0 };
                if (lit) {
                    int sum[3] = { 0, 0, 0 }, cnt = 0;
                    auto take = [&](const int c3[3]) { if (!blocksLight(c3[0], c3[1], c3[2])) { for (int c = 0; c < 3; c++) sum[c] += lightAt(c, c3[0], c3[1], c3[2]); cnt++; } };
                    take(L); take(s1); take(s2); if (!(occludes(at(s1[0], s1[1], s1[2])) && occludes(at(s2[0], s2[1], s2[2])))) take(cc);
                    if (cnt) for (int c = 0; c < 3; c++) bl[c] = (sum[c] + cnt / 2) / cnt;
                }
                if (d.light) d.lightLevels(bl);
                q[k] = { p[0], p[1], p[2], u, v, (float)d.tex[f] + (d.waves ? 16384.f : 0.f), packLit(shade, bl, d.light > 0, d.mirror ? 1.f : d.shine, d.mirror), packNormal((float)n[0], (float)n[1], (float)n[2], f) };
            }
            // split along the brighter diagonal, and wind each triangle clockwise when seen from outside
            uint32_t base = (uint32_t)md.v.size();
            for (auto& vv : q) md.v.push_back(vv);
            uint32_t t[6];
            if (ao[0] + ao[2] > ao[1] + ao[3]) { uint32_t s[6] = { 0, 1, 2, 0, 2, 3 }; std::copy(s, s + 6, t); }
            else { uint32_t s[6] = { 0, 1, 3, 1, 2, 3 }; std::copy(s, s + 6, t); }
            for (int tri = 0; tri < 2; tri++) {
                uint32_t i0 = t[tri * 3], i1 = t[tri * 3 + 1], i2 = t[tri * 3 + 2];
                const Vtx &A = q[i0], &Bv = q[i1], &C = q[i2];
                float e1[3] = { Bv.x - A.x, Bv.y - A.y, Bv.z - A.z }, e2[3] = { C.x - A.x, C.y - A.y, C.z - A.z };
                float cr[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
                if (cr[0] * n[0] + cr[1] * n[1] + cr[2] * n[2] < 0) std::swap(i1, i2);
                md.i.insert(md.i.end(), { base + i0, base + i1, base + i2 });
            }
        }
    }
}

// ------------------------------------------------------------------ worker threads
void MeshWorkers::start(int n) {
    stop(); quit = false;
    for (int i = 0; i < std::max(1, n); i++) threads.emplace_back([this] { run(); });
}
void MeshWorkers::stop() {
    { std::lock_guard<std::mutex> l(m); quit = true; }
    cv.notify_all();
    for (auto& t : threads) t.join();
    threads.clear();
    std::lock_guard<std::mutex> l(m); jobs.clear(); busy = 0;
}
void MeshWorkers::submit(MeshInput&& in) {
    { std::lock_guard<std::mutex> l(m); jobs.push_back(std::move(in)); }
    cv.notify_one();
}
void MeshWorkers::collect(std::vector<ChunkMesh>& out) {
    std::lock_guard<std::mutex> l(m);
    for (auto& d : done) out.push_back(std::move(d));
    done.clear();
}
void MeshWorkers::run() {
    for (;;) {
        MeshInput job;
        {
            std::unique_lock<std::mutex> l(m);
            cv.wait(l, [&] { return quit || !jobs.empty(); });
            if (quit) return;
            job = std::move(jobs.front()); jobs.pop_front(); busy++;
        }
        ChunkMesh cm; buildMesh(reg, job, cm);
        std::lock_guard<std::mutex> l(m); done.push_back(std::move(cm)); busy--;
    }
}

} // namespace mc
