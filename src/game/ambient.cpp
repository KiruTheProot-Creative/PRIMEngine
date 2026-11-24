#include "ambient.h"
#include "../world/shapes.h"
#include <cmath>
#include <deque>

namespace mc {

// (fast: everything in flat arrays over the box it can reach - a fill of a big room takes a few milliseconds)
void fillAmbient(const BlockRegistry& reg, const AmbChunkAt& chunkAt, V3 at, float reach, int blend, const std::vector<CookDoor>& doors, AmbientFill& out, size_t maxCells, const std::function<bool(int, int, int)>& through) {
    out = AmbientFill{};
    const int R = (int)std::ceil(std::max(1.f, reach)) + std::max(0, blend) + 2;
    const I3 c0{ (int)std::floor(at.x) - R, (int)std::floor(at.y) - R, (int)std::floor(at.z) - R };
    const int N = 2 * R + 1; const size_t NN = (size_t)N * N * N;
    if (NN > (size_t)40 << 20) { out.stuck = true; return; }
    // the chunks of the box, once
    const I3 k0 = chunkOf(c0.x, c0.y, c0.z), k1 = chunkOf(c0.x + N - 1, c0.y + N - 1, c0.z + N - 1);
    const int KX = k1.x - k0.x + 1, KY = k1.y - k0.y + 1, KZ = k1.z - k0.z + 1;
    std::vector<const Chunk*> chunks((size_t)KX * KY * KZ);
    for (int y = 0; y < KY; y++) for (int z = 0; z < KZ; z++) for (int x = 0; x < KX; x++) chunks[((size_t)y * KZ + z) * KX + x] = chunkAt({ k0.x + x, k0.y + y, k0.z + z });
    // what each block of the box is: 0 not yet looked at, 1 open, 2 closed, 3 a door, 4 a shape (slope, slab, panel, pipe...: closed only on the
    // sides it covers completely - side bits in `full`); and how much it got (0 none)
    std::vector<uint8_t> kind(NN, 0), got(NN, 0), full(NN, 0);
    auto idx = [&](int x, int y, int z) { return ((size_t)(y - c0.y) * N + (z - c0.z)) * N + (x - c0.x); };
    auto inBox = [&](int x, int y, int z) { return x >= c0.x && y >= c0.y && z >= c0.z && x < c0.x + N && y < c0.y + N && z < c0.z + N; };
    auto kindAt = [&](int x, int y, int z) -> uint8_t {
        uint8_t& k = kind[idx(x, y, z)]; if (k) return k;
        for (const CookDoor& d : doors) if (x >= d.lo.x && x <= d.hi.x && y >= d.lo.y && y <= d.hi.y && z >= d.lo.z && z <= d.hi.z) return k = 3;
        I3 c = chunkOf(x, y, z); const Chunk* ch = chunks[((size_t)(c.y - k0.y) * KZ + (c.z - k0.z)) * KX + (c.x - k0.x)];
        if (!ch) return k = 2;   // (not loaded: stops it)
        if (through && through(x, y, z)) return k = 1;
        BlockId v = ch->get(floorMod(x, CS), floorMod(y, CS), floorMod(z, CS));
        if (v == AIR) return k = 1;
        const BlockDef& d = reg.get(v); if (d.liquid || d.render != Render::Opaque) return k = 1;   // (see-through blocks, liquids)
        int sh = resolveShape(v, d); if (sh == Shapes::FULL) return k = 2;
        const ShapeVariant& sv = Shapes::get().variant(sh, rotOf(v)); uint8_t m = 0;   // a shape: walls only where it covers a side completely
        for (int f = 0; f < 6; f++) if (sv.full[f]) m |= (uint8_t)(1u << f);
        full[idx(x, y, z)] = m; return k = 4;
    };
    auto passes = [&](int x, int y, int z, int dir, int nx, int ny, int nz) -> bool {   // from one block into the next (dir: F_PX .. F_NZ)
        size_t a = idx(x, y, z), b = idx(nx, ny, nz);
        if (kind[a] == 4 && (full[a] >> dir & 1)) return false;          // its own side that way is covered
        if (kind[b] == 4 && (full[b] >> (dir ^ 1) & 1)) return false;    // the next one's side facing it is covered
        return true;
    };
    I3 s{ (int)std::floor(at.x), (int)std::floor(at.y), (int)std::floor(at.z) };
    { uint8_t k0s = kindAt(s.x, s.y, s.z);
      if (k0s != 1 && k0s != 4) {   // (standing on the floor or half in a block: the nearest air around it, up to 3 blocks away)
        bool found = false; double best = 1e9; I3 bs = s;
        for (int dy = -3; dy <= 3; dy++) for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) {
            int x = s.x + dx, y = s.y + dy, z = s.z + dz; if (!inBox(x, y, z)) continue;
            uint8_t k = kindAt(x, y, z); if (k != 1 && k != 4) continue;
            double d2 = dx * dx + dz * dz + (dy < 0 ? dy * dy * 1.5 : dy * dy * 0.8); if (d2 < best) { best = d2; bs = { x, y, z }; found = true; }
        }
        if (!found) { out.stuck = true; return; }
        s = bs;
      } }
    const double R2 = (double)reach * reach;
    std::vector<I3> q; q.reserve(1 << 16); q.push_back(s); got[idx(s.x, s.y, s.z)] = 255;
    std::vector<I3> edge, cells{ s };
    const int N6[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (size_t qi = 0; qi < q.size() && cells.size() < maxCells; qi++) {
        I3 c = q[qi]; bool isEdge = false;
        for (auto& d : N6) {
            int nx = c.x + d[0], ny = c.y + d[1], nz = c.z + d[2];
            if (!inBox(nx, ny, nz)) continue;
            size_t ni = idx(nx, ny, nz); if (got[ni]) continue;
            uint8_t k = kindAt(nx, ny, nz); if (k == 2) continue;
            if (k != 3 && !passes(c.x, c.y, c.z, (int)(&d - N6), nx, ny, nz)) continue;
            double dx = nx + 0.5 - at.x, dy = ny + 0.5 - at.y, dz = nz + 0.5 - at.z;
            if (k == 3 || dx * dx + dy * dy + dz * dz > R2) { isEdge = true; continue; }
            got[ni] = 255; q.push_back({ nx, ny, nz }); cells.push_back({ nx, ny, nz });
        }
        if (isEdge) edge.push_back(c);
    }
    if (blend > 0) {   // spilling: a few steps further (doors and the reach let it), fading
        std::vector<std::pair<I3, int>> sq; for (const I3& e : edge) sq.push_back({ e, 0 });
        for (size_t qi = 0; qi < sq.size(); qi++) {
            auto [c, step] = sq[qi]; if (step >= blend) continue;
            for (auto& d : N6) {
                int nx = c.x + d[0], ny = c.y + d[1], nz = c.z + d[2];
                if (!inBox(nx, ny, nz)) continue;
                size_t ni = idx(nx, ny, nz); if (got[ni]) continue;
                uint8_t k = kindAt(nx, ny, nz); if (k != 1 && k != 4) continue;   // (doors are a hard stop: it only spills through open air)
                if (!passes(c.x, c.y, c.z, (int)(&d - N6), nx, ny, nz)) continue;
                got[ni] = (uint8_t)std::lround(255.0 * (1.0 - (step + 1.0) / (blend + 1.0)));
                sq.push_back({ { nx, ny, nz }, step + 1 }); cells.push_back({ nx, ny, nz });
            }
        }
    }
    out.cells.reserve(cells.size());
    out.lo = s; out.hi = s;
    for (const I3& c : cells) {
        out.cells.push_back({ c, got[idx(c.x, c.y, c.z)] });
        out.lo = { std::min(out.lo.x, c.x), std::min(out.lo.y, c.y), std::min(out.lo.z, c.z) };
        out.hi = { std::max(out.hi.x, c.x), std::max(out.hi.y, c.y), std::max(out.hi.z, c.z) };
    }
}

} // namespace mc
