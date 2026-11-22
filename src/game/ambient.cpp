#include "ambient.h"
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
    // what each block of the box is: 0 not yet looked at, 1 open, 2 closed, 3 a door; and how much it got (0 none)
    std::vector<uint8_t> kind(NN, 0), got(NN, 0);
    auto idx = [&](int x, int y, int z) { return ((size_t)(y - c0.y) * N + (z - c0.z)) * N + (x - c0.x); };
    auto inBox = [&](int x, int y, int z) { return x >= c0.x && y >= c0.y && z >= c0.z && x < c0.x + N && y < c0.y + N && z < c0.z + N; };
    auto kindAt = [&](int x, int y, int z) -> uint8_t {
        uint8_t& k = kind[idx(x, y, z)]; if (k) return k;
        for (const CookDoor& d : doors) if (x >= d.lo.x && x <= d.hi.x && y >= d.lo.y && y <= d.hi.y && z >= d.lo.z && z <= d.hi.z) return k = 3;
        I3 c = chunkOf(x, y, z); const Chunk* ch = chunks[((size_t)(c.y - k0.y) * KZ + (c.z - k0.z)) * KX + (c.x - k0.x)];
        bool op = ch && (cookOpenReg(reg, ch->get(floorMod(x, CS), floorMod(y, CS), floorMod(z, CS))) || (through && through(x, y, z)));
        return k = op ? 1 : 2;
    };
    I3 s{ (int)std::floor(at.x), (int)std::floor(at.y), (int)std::floor(at.z) };
    if (kindAt(s.x, s.y, s.z) != 1) {   // (standing on the floor or half in a block: the air just above or around)
        bool found = false;
        const int D[7][3] = { { 0, 1, 0 }, { 0, 2, 0 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, -1, 0 } };
        for (auto& d : D) if (kindAt(s.x + d[0], s.y + d[1], s.z + d[2]) == 1) { s = { s.x + d[0], s.y + d[1], s.z + d[2] }; found = true; break; }
        if (!found) { out.stuck = true; return; }
    }
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
                size_t ni = idx(nx, ny, nz); if (got[ni] || kindAt(nx, ny, nz) == 2) continue;
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
