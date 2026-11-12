#include "mesher.h"

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
    return true;
}

// Flat shapes: always drawn from both sides, no ambient occlusion.
static void quad2(MeshData& md, const float p[4][3], const float uv[4][2], float layer, float shade, float normal) {
    uint32_t base = (uint32_t)md.v.size();
    for (int k = 0; k < 4; k++) md.v.push_back({ p[k][0], p[k][1], p[k][2], uv[k][0], uv[k][1], layer, shade, normal });
    md.i.insert(md.i.end(), { base, base + 1, base + 2, base, base + 2, base + 3, base, base + 2, base + 1, base, base + 3, base + 2 });
}
static void planes(MeshData& md, const BlockDef& d, int state, int x, int y, int z) {
    const float uv[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };   // corners: bottom-left, bottom-right, top-right, top-left
    float X = (float)x, Y = (float)y, Z = (float)z, layer = (float)d.tex[0];
    if (d.shape == Shape::Cross) {
        const float e = 0.0f;
        float a[4][3] = { { X + e, Y, Z + e }, { X + 1 - e, Y, Z + 1 - e }, { X + 1 - e, Y + 1, Z + 1 - e }, { X + e, Y + 1, Z + e } };
        float b[4][3] = { { X + e, Y, Z + 1 - e }, { X + 1 - e, Y, Z + e }, { X + 1 - e, Y + 1, Z + e }, { X + e, Y + 1, Z + 1 - e } };
        quad2(md, a, uv, layer, 0.9f, 6); quad2(md, b, uv, layer, 0.9f, 6);
        return;
    }
    // Plane: lies against the wall it was placed on. state = the direction it faces (away from that wall).
    const float o = 1.f / 32.f;   // half a pixel away from the wall so it does not flicker
    static const float shade[6] = { 0.75f, 0.75f, 1.0f, 0.6f, 0.85f, 0.85f };
    float q[4][3];
    switch (state < 6 ? state : F_PY) {
    case F_PX: { float px = X + o;     float t[4][3] = { { px, Y, Z + 1 }, { px, Y, Z }, { px, Y + 1, Z }, { px, Y + 1, Z + 1 } }; std::copy(&t[0][0], &t[0][0] + 12, &q[0][0]); break; }
    case F_NX: { float px = X + 1 - o; float t[4][3] = { { px, Y, Z }, { px, Y, Z + 1 }, { px, Y + 1, Z + 1 }, { px, Y + 1, Z } }; std::copy(&t[0][0], &t[0][0] + 12, &q[0][0]); break; }
    case F_PZ: { float pz = Z + o;     float t[4][3] = { { X, Y, pz }, { X + 1, Y, pz }, { X + 1, Y + 1, pz }, { X, Y + 1, pz } }; std::copy(&t[0][0], &t[0][0] + 12, &q[0][0]); break; }
    case F_NZ: { float pz = Z + 1 - o; float t[4][3] = { { X + 1, Y, pz }, { X, Y, pz }, { X, Y + 1, pz }, { X + 1, Y + 1, pz } }; std::copy(&t[0][0], &t[0][0] + 12, &q[0][0]); break; }
    case F_NY: { float py = Y + 1 - o; float t[4][3] = { { X, py, Z + 1 }, { X + 1, py, Z + 1 }, { X + 1, py, Z }, { X, py, Z } }; std::copy(&t[0][0], &t[0][0] + 12, &q[0][0]); break; }
    default:   { float py = Y + o;     float t[4][3] = { { X, py, Z + 1 }, { X + 1, py, Z + 1 }, { X + 1, py, Z }, { X, py, Z } }; std::copy(&t[0][0], &t[0][0] + 12, &q[0][0]); break; }
    }
    quad2(md, q, uv, layer, shade[state < 6 ? state : F_PY], (float)(state < 6 ? state : F_PY));
}

void buildMesh(const BlockRegistry& reg, const MeshInput& in, ChunkMesh& out) {
    out.pos = in.pos; out.seq = in.seq; out.opaque = {}; out.cutout = {};
    const auto& B = in.blocks;
    auto at = [&](int x, int y, int z) { return B[padIdx(x, y, z)]; };
    auto occludes = [&](BlockId id) { return id != UNKNOWN && reg.opaque(id); };
    static const float faceShade[6] = { 0.6f, 0.6f, 1.0f, 0.5f, 0.8f, 0.8f };   // +X -X top bottom +Z -Z (Minecraft-style)
    for (int y = 0; y < CS; y++) for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
        BlockId id = at(x, y, z); if (id == AIR) continue;
        const BlockDef& d = reg.get(id); if (d.render == Render::Air) continue;
        bool cut = d.render == Render::Cutout;
        MeshData& md = cut ? out.cutout : out.opaque;
        if (d.shape != Shape::Cube) { planes(md, d, stateOf(id), x, y, z); continue; }
        int c[3] = { x, y, z };
        for (int f = 0; f < 6; f++) {
            int a = f / 2, sgn = (f % 2 == 0) ? 1 : -1, n[3] = { 0, 0, 0 }; n[a] = sgn;
            BlockId nb = at(x + n[0], y + n[1], z + n[2]);
            if (nb == UNKNOWN) continue;
            if (nb != AIR) {
                const BlockDef& nd = reg.get(nb);
                if (reg.opaque(nb)) continue;
                if (nd.shape == Shape::Cube && typeOf(nb) == typeOf(id) && d.cullSame) continue;
            }
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
                float shade = faceShade[f] * (0.45f + 0.55f * ao[k] / 3.f);
                q[k] = { p[0], p[1], p[2], u, v, (float)d.tex[f], shade, (float)f };
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
