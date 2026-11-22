#include "ambient.h"
#include <cmath>
#include <deque>
#include <unordered_map>

namespace mc {

void fillAmbient(World& w, V3 at, float reach, int blend, const std::vector<CookDoor>& doors, AmbientFill& out, size_t maxCells, const std::function<bool(int, int, int)>& through) {
    out = AmbientFill{};
    auto inDoor = [&](int x, int y, int z) {
        for (const CookDoor& d : doors) if (x >= d.lo.x && x <= d.hi.x && y >= d.lo.y && y <= d.hi.y && z >= d.lo.z && z <= d.hi.z) return true;
        return false;
    };
    std::unordered_map<I3, const Chunk*, I3Hash> cache;
    auto open = [&](int x, int y, int z) {
        I3 c = chunkOf(x, y, z); auto it = cache.find(c); const Chunk* ch;
        if (it == cache.end()) { ch = w.chunk(c); cache[c] = ch; } else ch = it->second;
        if (!ch) return false;   // (not loaded: stops it)
        return cookOpen(w, ch->get(floorMod(x, CS), floorMod(y, CS), floorMod(z, CS))) || (through && through(x, y, z));
    };
    I3 s{ (int)std::floor(at.x), (int)std::floor(at.y), (int)std::floor(at.z) };
    if (!open(s.x, s.y, s.z) || inDoor(s.x, s.y, s.z)) {   // (standing on the floor or half in a block: the air just above or around)
        bool found = false;
        const int D[7][3] = { { 0, 1, 0 }, { 0, 2, 0 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, -1, 0 } };
        for (auto& d : D) if (open(s.x + d[0], s.y + d[1], s.z + d[2]) && !inDoor(s.x + d[0], s.y + d[1], s.z + d[2])) { s = { s.x + d[0], s.y + d[1], s.z + d[2] }; found = true; break; }
        if (!found) { out.stuck = true; return; }
    }
    const double R2 = (double)reach * reach;
    std::unordered_map<I3, uint8_t, I3Hash> got;   // cell -> 255 own room, less: spilled
    std::deque<I3> q; got[s] = 255; q.push_back(s);
    std::vector<I3> edge;   // own cells next to something it couldn't fill (a doorway, an opening past its reach)
    const int N6[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    while (!q.empty() && got.size() < maxCells) {
        I3 c = q.front(); q.pop_front(); bool isEdge = false;
        for (auto& d : N6) {
            I3 n{ c.x + d[0], c.y + d[1], c.z + d[2] };
            if (got.count(n)) continue;
            if (!open(n.x, n.y, n.z)) continue;
            double dx = n.x + 0.5 - at.x, dy = n.y + 0.5 - at.y, dz = n.z + 0.5 - at.z;
            if (inDoor(n.x, n.y, n.z) || dx * dx + dy * dy + dz * dz > R2) { isEdge = true; continue; }
            got[n] = 255; q.push_back(n);
        }
        if (isEdge) edge.push_back(c);
    }
    if (blend > 0) {   // spilling: a few steps further (doors and the reach let it), fading
        std::deque<std::pair<I3, int>> sq; for (const I3& e : edge) sq.push_back({ e, 0 });
        while (!sq.empty()) {
            auto [c, step] = sq.front(); sq.pop_front(); if (step >= blend) continue;
            for (auto& d : N6) {
                I3 n{ c.x + d[0], c.y + d[1], c.z + d[2] };
                if (got.count(n) || !open(n.x, n.y, n.z)) continue;
                got[n] = (uint8_t)std::lround(255.0 * (1.0 - (step + 1.0) / (blend + 1.0)));
                sq.push_back({ n, step + 1 });
            }
        }
    }
    out.cells.reserve(got.size());
    out.lo = s; out.hi = s;
    for (auto& kv : got) {
        out.cells.push_back(kv);
        const I3& c = kv.first;
        out.lo = { std::min(out.lo.x, c.x), std::min(out.lo.y, c.y), std::min(out.lo.z, c.z) };
        out.hi = { std::max(out.hi.x, c.x), std::max(out.hi.y, c.y), std::max(out.hi.z, c.z) };
    }
}

} // namespace mc
