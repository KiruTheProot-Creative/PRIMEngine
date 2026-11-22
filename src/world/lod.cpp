#include "lod.h"
#include "shapes.h"
#include <unordered_map>

namespace mc {

namespace {
struct Kinds {   // what a block type is for far copies: 0 air, 1 solid, 2 liquid (3: unknown)
    const BlockRegistry& reg; std::vector<uint8_t> k;
    explicit Kinds(const BlockRegistry& r) : reg(r), k(65536, 255) {}
    uint8_t operator()(uint16_t t) {
        if (t == LOD_UNKNOWN) return 3;
        uint8_t& v = k[t]; if (v != 255) return v;
        if (t == 0) return v = 0;
        const BlockDef& d = reg.get((BlockId)t);
        v = d.liquid ? 2 : (d.render == Render::Opaque || d.render == Render::Cutout) ? 1 : 0;
        return v;
    }
};
// eight cells into one (index dy * 4 + dz * 2 + dx): solid when at least half are; its look from the top half
uint16_t merge8(const uint16_t c[8], Kinds& kind) {
    int solid = 0, liquid = 0, unk = 0;
    for (int i = 0; i < 8; i++) { uint8_t k = kind(c[i]); solid += k == 1; liquid += k == 2; unk += k == 3; }
    auto common = [&](int want, int from, int to) -> uint16_t {
        uint16_t best = 0; int bn = 0;
        for (int i = from; i < to; i++) {
            if (kind(c[i]) != want) continue; int n = 0;
            for (int j = from; j < to; j++) n += c[j] == c[i];
            if (n > bn) { bn = n; best = c[i]; }
        }
        return best;
    };
    if (solid >= 4) { uint16_t t = common(1, 4, 8); return t ? t : common(1, 0, 4); }
    if (unk > 0) return LOD_UNKNOWN;
    if (liquid >= 4) { uint16_t t = common(2, 4, 8); return t ? t : common(2, 0, 4); }
    return 0;
}
} // namespace

LodSummary lodSummarize(const BlockRegistry& reg, const Chunk& ch) {
    Kinds kind(reg);
    LodSummary s; s.cells.resize(512);
    for (int y = 0; y < 8; y++) for (int z = 0; z < 8; z++) for (int x = 0; x < 8; x++) {
        uint16_t c[8];
        for (int dy = 0; dy < 2; dy++) for (int dz = 0; dz < 2; dz++) for (int dx = 0; dx < 2; dx++)
            c[dy * 4 + dz * 2 + dx] = (uint16_t)typeOf(ch.get(x * 2 + dx, y * 2 + dy, z * 2 + dz));
        s.cells[((size_t)y * 8 + z) * 8 + x] = merge8(c, kind);
    }
    bool same = true; for (uint16_t v : s.cells) if (v != s.cells[0]) { same = false; break; }
    if (same) { s.kind = s.cells[0] ? 1 : 0; s.uni = s.cells[0]; s.cells.clear(); s.cells.shrink_to_fit(); }
    else s.kind = 2;
    return s;
}

void lodGroupCells(const BlockRegistry& reg, int k, I3 group, const std::function<const LodSummary*(I3)>& get, std::vector<uint16_t>& out) {
    Kinds kind(reg);
    const int m = 1 << (k - 1);            // level-1 cells per cell, a side
    const int S = 18 * m;                   // the level-1 cells of the group and its border, a side
    const I3 o{ group.x * 8 * (1 << k) - m, group.y * 8 * (1 << k) - m, group.z * 8 * (1 << k) - m };   // (in level-1 cells: a chunk is 8)
    std::vector<uint16_t> a((size_t)S * S * S);
    std::unordered_map<I3, const LodSummary*, I3Hash> seen;
    auto sum = [&](I3 c) -> const LodSummary* { auto it = seen.find(c); if (it != seen.end()) return it->second; const LodSummary* s = get(c); seen[c] = s; return s; };
    for (int y = 0; y < S; y++) for (int z = 0; z < S; z++) for (int x = 0; x < S; x++) {
        int gx = o.x + x, gy = o.y + y, gz = o.z + z;
        const LodSummary* s = sum({ floorDiv(gx, 8), floorDiv(gy, 8), floorDiv(gz, 8) });
        a[((size_t)y * S + z) * S + x] = s ? s->at(floorMod(gx, 8), floorMod(gy, 8), floorMod(gz, 8)) : LOD_UNKNOWN;
    }
    for (int n = S; n > 18; n /= 2) {   // halve, level by level
        int h = n / 2; std::vector<uint16_t> b((size_t)h * h * h);
        for (int y = 0; y < h; y++) for (int z = 0; z < h; z++) for (int x = 0; x < h; x++) {
            uint16_t c[8];
            for (int dy = 0; dy < 2; dy++) for (int dz = 0; dz < 2; dz++) for (int dx = 0; dx < 2; dx++)
                c[dy * 4 + dz * 2 + dx] = a[((size_t)(y * 2 + dy) * n + (z * 2 + dz)) * n + (x * 2 + dx)];
            b[((size_t)y * h + z) * h + x] = merge8(c, kind);
        }
        a.swap(b);
    }
    out.swap(a);
}

bool lodMesh(const BlockRegistry& reg, int k, const std::vector<uint16_t>& cells, MeshData& out) {
    Kinds kind(reg);
    const float f = (float)(1 << k);
    auto at = [&](int x, int y, int z) { return cells[lodCellIdx(x, y, z)]; };
    auto quad = [&](const float p[4][3], int face, int layer, uint32_t lit) {
        int n[3] = { 0, 0, 0 }; n[face / 2] = (face % 2 == 0) ? 1 : -1;
        uint32_t base = (uint32_t)out.v.size();
        for (int q = 0; q < 4; q++) { float u, v; faceUV(p[q][0], p[q][1], p[q][2], face, u, v);
            out.v.push_back({ p[q][0], p[q][1], p[q][2], u, v, (float)layer, lit, packNormal((float)n[0], (float)n[1], (float)n[2], face) }); }
        uint32_t t[6] = { 0, 1, 2, 0, 2, 3 };
        for (int tri = 0; tri < 2; tri++) {
            uint32_t i0 = t[tri * 3], i1 = t[tri * 3 + 1], i2 = t[tri * 3 + 2];
            float e1[3], e2[3]; for (int c = 0; c < 3; c++) { e1[c] = p[i1][c] - p[i0][c]; e2[c] = p[i2][c] - p[i0][c]; }
            float cr[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
            if (cr[0] * n[0] + cr[1] * n[1] + cr[2] * n[2] < 0) std::swap(i1, i2);
            out.i.insert(out.i.end(), { base + i0, base + i1, base + i2 });
        }
    };
    auto face = [&](int x, int y, int z, int fc, float top, int layer, uint32_t lit) {   // one side of a cell (top: how high its top is, 0..1)
        float x0 = x * f, y0 = y * f, z0 = z * f, x1 = x0 + f, y1 = y0 + f * top, z1 = z0 + f;
        float p[4][3];
        switch (fc) {
        case F_PX: { float q[4][3] = { { x1, y0, z0 }, { x1, y0, z1 }, { x1, y1, z1 }, { x1, y1, z0 } }; std::copy(&q[0][0], &q[0][0] + 12, &p[0][0]); break; }
        case F_NX: { float q[4][3] = { { x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 } }; std::copy(&q[0][0], &q[0][0] + 12, &p[0][0]); break; }
        case F_PY: { float q[4][3] = { { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 } }; std::copy(&q[0][0], &q[0][0] + 12, &p[0][0]); break; }
        case F_NY: { float q[4][3] = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 } }; std::copy(&q[0][0], &q[0][0] + 12, &p[0][0]); break; }
        case F_PZ: { float q[4][3] = { { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } }; std::copy(&q[0][0], &q[0][0] + 12, &p[0][0]); break; }
        default:   { float q[4][3] = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 } }; std::copy(&q[0][0], &q[0][0] + 12, &p[0][0]); break; }
        }
        quad(p, fc, layer, lit);
    };
    const int D[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    const int none[3] = { 0, 0, 0 };
    for (int y = 0; y < 16; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
        uint16_t t = at(x, y, z); uint8_t kd = kind(t); if (kd == 0 || kd == 3) continue;
        const BlockDef& d = reg.get((BlockId)t);
        if (kd == 2) {   // a liquid: its flat top, where nothing is above it
            uint8_t up = kind(at(x, y + 1, z)); if (up == 1 || up == 2) continue;
            bool lava = d.liquid == 2;
            face(x, y, z, F_PY, 0.9f, (int)(d.tex[F_PY] + (lava ? 16384 * 4 : 0)), packLit(1.f, none, lava, lava ? 0.f : 0.6f));
            continue;
        }
        uint32_t lit = packLit(1.f, none, d.light > 0, 0.f);
        uint8_t above = kind(at(x, y + 1, z));
        for (int fc = 0; fc < 6; fc++) {
            int nx = x + D[fc][0], ny = y + D[fc][1], nz = z + D[fc][2];
            uint8_t nk = kind(at(nx, ny, nz));
            bool border = nx < 0 || ny < 0 || nz < 0 || nx > 15 || ny > 15 || nz > 15;
            bool show = nk != 1 || (border && fc != F_PY && fc != F_NY && above != 1);   // (at the group's edge, its outer sides hang down: no gaps next to a finer or coarser group)
            if (show) face(x, y, z, fc, 1.f, d.tex[fc], lit);
        }
    }
    return !out.i.empty();
}

} // namespace mc
