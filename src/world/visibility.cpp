#include "visibility.h"
#include "chunk.h"
#include <deque>

namespace mc {

int seePair(int a, int b) {
    if (a > b) std::swap(a, b);
    static const int start[6] = { 0, 5, 9, 12, 14, 15 };   // pairs (0,1..5), (1,2..5), (2,3..5), (3,4..5), (4,5)
    return start[a] + (b - a - 1);
}

uint16_t chunkSeeMask(const uint8_t* solid) {
    bool any = false; for (int i = 0; i < CS3; i++) if (solid[i]) { any = true; break; }
    if (!any) return SEE_ALL;
    std::vector<uint8_t> seen(CS3, 0); std::vector<int> stack; stack.reserve(CS3);
    uint16_t mask = 0;
    for (int s = 0; s < CS3; s++) {
        if (solid[s] || seen[s]) continue;
        int faces = 0; seen[s] = 1; stack.push_back(s);
        while (!stack.empty()) {   // one pocket of air: which sides does it touch?
            int c = stack.back(); stack.pop_back();
            int x = c % CS, z = (c / CS) % CS, y = c / (CS * CS);
            if (x == 0) faces |= 1; if (x == CS - 1) faces |= 2; if (y == 0) faces |= 4; if (y == CS - 1) faces |= 8; if (z == 0) faces |= 16; if (z == CS - 1) faces |= 32;
            const int nb[6][3] = { { x - 1, y, z }, { x + 1, y, z }, { x, y - 1, z }, { x, y + 1, z }, { x, y, z - 1 }, { x, y, z + 1 } };
            for (const auto& n : nb) {
                if (n[0] < 0 || n[0] >= CS || n[1] < 0 || n[1] >= CS || n[2] < 0 || n[2] >= CS) continue;
                int k = Chunk::idx(n[0], n[1], n[2]); if (solid[k] || seen[k]) continue;
                seen[k] = 1; stack.push_back(k);
            }
        }
        for (int a = 0; a < 6; a++) for (int b = a + 1; b < 6; b++) if ((faces >> a & 1) && (faces >> b & 1)) mask |= (uint16_t)(1u << seePair(a, b));
        if (mask == SEE_ALL) break;
    }
    return mask;
}

void visibleChunks(I3 cam, int r, int v, const std::function<uint16_t(I3)>& see, const std::function<bool(I3)>& inView, std::vector<I3>& out) {
    out.clear();
    int W = 2 * r + 1, H = 2 * v + 1;
    std::vector<uint8_t> visited((size_t)W * W * H, 0);
    auto slot = [&](int dx, int dy, int dz) { return ((size_t)(dy + v) * W + (dz + r)) * W + (dx + r); };
    struct Step { int dx, dy, dz; uint8_t dirs; int8_t entry; };
    static const int D[6][3] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
    // visited: bit 0..5 = reached through that side already (each side is walked on once), bit 6 = listed, bit 7 = out of view
    std::deque<Step> q; q.push_back({ 0, 0, 0, 0, -1 }); visited[slot(0, 0, 0)] = 64; out.push_back(cam);
    while (!q.empty()) {
        Step s = q.front(); q.pop_front();
        I3 c{ cam.x + s.dx, cam.y + s.dy, cam.z + s.dz };
        uint16_t m = s.entry >= 0 ? see(c) : SEE_ALL;
        for (int d = 0; d < 6; d++) {
            int back = d ^ 1;                                         // the opposite side
            if (s.dirs & (1 << back)) continue;                       // never back toward the camera
            if (s.entry >= 0 && !seeConnects(m, s.entry, d)) continue; // no opening from where we came in to this side
            int nx = s.dx + D[d][0], ny = s.dy + D[d][1], nz = s.dz + D[d][2];
            if (nx < -r || nx > r || ny < -v || ny > v || nz < -r || nz > r) continue;
            uint8_t& vis = visited[slot(nx, ny, nz)];
            if (vis & (128 | (1 << back))) continue;                  // out of view, or already walked into through this side
            I3 n{ cam.x + nx, cam.y + ny, cam.z + nz };
            if (!(vis & 64)) { if (!inView(n)) { vis |= 128; continue; } vis |= 64; out.push_back(n); }
            vis |= (uint8_t)(1 << back);
            q.push_back({ nx, ny, nz, (uint8_t)(s.dirs | (1 << d)), (int8_t)back });
        }
    }
}

} // namespace mc
