#include "map.h"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace mc {

static int localIndex(int x, int y, int z) { return (floorMod(y, CS) * CS + floorMod(z, CS)) * CS + floorMod(x, CS); }

bool SeenMap::seen(int x, int y, int z) const {
    auto it = chunks.find(chunkOf(x, y, z)); if (it == chunks.end()) return false;
    int i = localIndex(x, y, z); return (it->second.bits[i >> 6] >> (i & 63)) & 1;
}
bool SeenMap::mine(int x, int y, int z) const {
    auto it = chunks.find(chunkOf(x, y, z)); if (it == chunks.end()) return false;
    int i = localIndex(x, y, z); return (it->second.mine[i >> 6] >> (i & 63)) & 1;
}
bool SeenMap::mark(int x, int y, int z, double now, bool own) {
    I3 c = chunkOf(x, y, z); ChunkBits& b = chunks[c];
    int i = localIndex(x, y, z); uint64_t bit = 1ull << (i & 63); bool changed = false;
    if (!(b.bits[i >> 6] & bit)) { b.bits[i >> 6] |= bit; b.count++; total++; changed = true; }
    if (own && !(b.mine[i >> 6] & bit)) { b.mine[i >> 6] |= bit; b.mineCount++; mineTotal++; changed = true; }
    if (changed) { b.changed = now; dirty.insert(c); }   // (faces belong to the seen block's own chunk: only it needs new panels)
    return changed;
}

static bool mappable(const World& w, int x, int y, int z) {   // a block that shows on the map
    BlockId v = w.get(x, y, z); return v != AIR && w.reg.get(v).solid;
}
static bool openAt(const World& w, int x, int y, int z) {      // air (or water...) next to a block: its face shows
    if (!w.loaded(x, y, z)) return false;
    BlockId v = w.get(x, y, z); return v == AIR || !w.reg.get(v).solid;
}

// walks block by block along a ray (Amanatides & Woo) to the first block that shows on the map; false if none in range
static bool firstSolid(const World& w, V3 o, V3 d, double range, int& hx, int& hy, int& hz, int& nx, int& ny, int& nz) {
    int x = (int)std::floor(o.x), y = (int)std::floor(o.y), z = (int)std::floor(o.z);
    int sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1, sz = d.z > 0 ? 1 : -1;
    auto first = [](double p, double dd, int c) { if (std::fabs(dd) < 1e-12) return 1e30; double edge = dd > 0 ? c + 1 - p : p - c; return edge / std::fabs(dd); };
    double tx = first(o.x, d.x, x), ty = first(o.y, d.y, y), tz = first(o.z, d.z, z);
    double dx = std::fabs(d.x) < 1e-12 ? 1e30 : 1 / std::fabs(d.x), dy = std::fabs(d.y) < 1e-12 ? 1e30 : 1 / std::fabs(d.y), dz = std::fabs(d.z) < 1e-12 ? 1e30 : 1 / std::fabs(d.z);
    nx = ny = nz = 0;
    range = w.sightBlocked(o, d, range);   // a shut door: the look stops there
    for (int steps = 0; steps < 1024; steps++) {
        double t;
        if (tx < ty && tx < tz) { x += sx; t = tx; tx += dx; nx = -sx; ny = nz = 0; }
        else if (ty < tz) { y += sy; t = ty; ty += dy; ny = -sy; nx = nz = 0; }
        else { z += sz; t = tz; tz += dz; nz = -sz; nx = ny = 0; }
        if (t > range) return false;
        if (!w.loaded(x, y, z)) return false;
        if (mappable(w, x, y, z)) { hx = x; hy = y; hz = z; return true; }
    }
    return false;
}

bool canSee(const World& w, V3 eye, V3 target, double range) {
    V3 d = target - eye; double len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len > range) return false; if (len < 1e-6) return true;
    int hx, hy, hz, nx, ny, nz; V3 dir = d * (1.0 / len);
    if (w.sightBlocked(eye, dir, len - 0.3, &target) < len - 0.3) return false;   // another shut door in the way (not the one looked at)
    if (!firstSolid(w, eye, dir, len - 0.3, hx, hy, hz, nx, ny, nz)) return true;   // (stops just short of the target itself)
    return false;
}

bool SeenMap::look(const World& w, V3 eye, V3 dir, double range, int patch, double now, bool own) {
    struct { int x, y, z, nx, ny, nz; } h;
    if (!firstSolid(w, eye, dir, range, h.x, h.y, h.z, h.nx, h.ny, h.nz)) return false;
    mark(h.x, h.y, h.z, now, own);
    if (patch > 0) {   // the same surface around it: blocks in the plane of the face that also open the same way
        for (int a = -patch; a <= patch; a++) for (int b = -patch; b <= patch; b++) {
            if (a * a + b * b > patch * patch + 1) continue;
            int x = h.x, y = h.y, z = h.z;
            if (h.nx) { y += a; z += b; } else if (h.ny) { x += a; z += b; } else { x += a; y += b; }
            if (mappable(w, x, y, z) && openAt(w, x + h.nx, y + h.ny, z + h.nz)) mark(x, y, z, now, own);
        }
    }
    return true;
}

int SeenMap::lookAround(const World& w, V3 eye, double yaw, double pitch, double fovY, double aspect, int rays, double range, uint32_t frame, double now) {
    int hits = 0;
    double ty = std::tan(fovY * 0.5), tx = ty * aspect;
    V3 f{ std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch) };
    V3 r{ std::cos(yaw), 0, -std::sin(yaw) }, u = cross(f, r);
    uint32_t s = frame * 2654435761u + 12345u;
    for (int i = 0; i < rays; i++) {   // shuffled points over the screen: a different set every frame
        s = s * 1664525u + 1013904223u; double sx = ((s >> 8) & 0xFFFF) / 65535.0 * 2 - 1;
        s = s * 1664525u + 1013904223u; double sy = ((s >> 8) & 0xFFFF) / 65535.0 * 2 - 1;
        V3 d = normalize(f + r * (sx * tx) + u * (sy * ty));
        hits += look(w, eye, d, range, 1, now) ? 1 : 0;
    }
    return hits;
}

void SeenMap::floorUnder(const World& w, V3 feet, int radius, double now) {
    int fx = (int)std::floor(feet.x), fz = (int)std::floor(feet.z), fy = (int)std::floor(feet.y - 0.05);
    for (int dx = -radius; dx <= radius; dx++) for (int dz = -radius; dz <= radius; dz++)
        for (int dy = 0; dy >= -2; dy--) {   // the first ground below each spot
            int x = fx + dx, y = fy + dy, z = fz + dz;
            if (mappable(w, x, y, z)) { if (openAt(w, x, y + 1, z)) mark(x, y, z, now); break; }
        }
}

int SeenMap::revealFrom(const World& w, V3 at, double range, int rays, double now) {
    int hits = 0; const double ga = 3.14159265358979 * (3 - std::sqrt(5.0));
    for (int i = 0; i < rays; i++) {   // evenly all around (a Fibonacci sphere)
        double y = 1 - (i + 0.5) * 2.0 / rays, rr = std::sqrt(std::max(0.0, 1 - y * y)), t = ga * i;
        hits += look(w, at, { std::cos(t) * rr, y, std::sin(t) * rr }, range, 2, now, false) ? 1 : 0;
    }
    return hits;
}

std::vector<MapQuad> mapPanels(const World& w, const SeenMap& m, I3 cp) {
    std::vector<MapQuad> out;
    auto it = m.chunks.find(cp); if (it == m.chunks.end() || it->second.count == 0) return out;
    const SeenMap::ChunkBits& cb = it->second; float born = (float)cb.changed;
    int bx = cp.x * CS, by = cp.y * CS, bz = cp.z * CS;
    auto seenL = [&](int x, int y, int z) { int i = (y * CS + z) * CS + x; return !((cb.bits[i >> 6] >> (i & 63)) & 1) ? 0 : ((cb.mine[i >> 6] >> (i & 63)) & 1) ? 1 : 2; };   // 0 not, 1 yours, 2 downloaded
    static const int D[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (int f = 0; f < 6; f++) {
        int ax = f / 2;                                   // the face's axis; u, v: the two others
        int ua = ax == 0 ? 2 : 0, va = ax == 1 ? 2 : 1;
        for (int layer = 0; layer < CS; layer++) {
            unsigned char mask[CS][CS] = {};
            for (int a = 0; a < CS; a++) for (int b = 0; b < CS; b++) {
                int l[3]; l[ax] = layer; l[ua] = a; l[va] = b;
                int sv = seenL(l[0], l[1], l[2]); if (!sv) continue;
                int x = bx + l[0], y = by + l[1], z = bz + l[2];
                mask[a][b] = openAt(w, x + D[f][0], y + D[f][1], z + D[f][2]) ? (unsigned char)sv : 0;
            }
            for (int a = 0; a < CS; a++) for (int b = 0; b < CS;) {   // greedy: grow along b, then along a (only over the same kind: yours / downloaded)
                unsigned char kind = mask[a][b];
                if (!kind) { b++; continue; }
                int bl = 1; while (b + bl < CS && mask[a][b + bl] == kind) bl++;
                int al = 1; bool ok = true;
                while (a + al < CS && ok) { for (int k = 0; k < bl; k++) if (mask[a + al][b + k] != kind) { ok = false; break; } if (ok) al++; }
                for (int i = 0; i < al; i++) for (int k = 0; k < bl; k++) mask[a + i][b + k] = 0;
                MapQuad q; double off = D[f][ax] > 0 ? 1 : 0;
                double c0[3], du[3] = { 0, 0, 0 }, dv[3] = { 0, 0, 0 };
                c0[ax] = (ax == 0 ? bx : ax == 1 ? by : bz) + layer + off; c0[ua] = (ua == 0 ? bx : ua == 1 ? by : bz) + a; c0[va] = (va == 0 ? bx : va == 1 ? by : bz) + b;
                du[ua] = al; dv[va] = bl;
                for (int k = 0; k < 3; k++) { q.p[0][k] = (float)c0[k]; q.p[1][k] = (float)(c0[k] + du[k]); q.p[2][k] = (float)(c0[k] + du[k] + dv[k]); q.p[3][k] = (float)(c0[k] + dv[k]); }   // (drawn from both sides)
                q.n[0] = (float)D[f][0]; q.n[1] = (float)D[f][1]; q.n[2] = (float)D[f][2]; q.w = (float)al; q.h = (float)bl; q.born = born; q.mine = kind == 1;
                out.push_back(q);
                b += bl;
            }
        }
    }
    return out;
}

bool saveSeenMap(const std::string& path, const SeenMap& m) {
    std::ofstream f(path, std::ios::binary); if (!f) return false;
    uint32_t magic = 0x32414D54, n = (uint32_t)m.chunks.size(); f.write((const char*)&magic, 4); f.write((const char*)&n, 4);   // version 2: yours and downloaded
    for (auto& kv : m.chunks) { int32_t c[3] = { kv.first.x, kv.first.y, kv.first.z }; f.write((const char*)c, 12); f.write((const char*)kv.second.bits, sizeof kv.second.bits); f.write((const char*)kv.second.mine, sizeof kv.second.mine); }
    uint32_t nd = (uint32_t)m.doors.size(); f.write((const char*)&nd, 4); for (uint32_t d : m.doors) f.write((const char*)&d, 4);
    return (bool)f;
}
bool loadSeenMap(const std::string& path, SeenMap& m) {
    std::ifstream f(path, std::ios::binary); if (!f) return false;
    uint32_t magic = 0, n = 0; f.read((char*)&magic, 4); f.read((char*)&n, 4); if (magic != 0x50414D54 && magic != 0x32414D54) return false;
    bool v2 = magic == 0x32414D54;
    m.clear();
    for (uint32_t i = 0; i < n && f; i++) {
        int32_t c[3]; f.read((char*)c, 12); SeenMap::ChunkBits b; f.read((char*)b.bits, sizeof b.bits);
        if (v2) f.read((char*)b.mine, sizeof b.mine); else std::copy(std::begin(b.bits), std::end(b.bits), std::begin(b.mine));   // (old maps: all yours)
        if (!f) break;
        for (uint64_t v : b.bits) { while (v) { b.count++; v &= v - 1; } }
        for (uint64_t v : b.mine) { while (v) { b.mineCount++; v &= v - 1; } }
        m.total += b.count; m.mineTotal += b.mineCount; I3 k{ c[0], c[1], c[2] }; m.chunks[k] = b; m.dirty.insert(k);
    }
    uint32_t nd = 0; if (f.read((char*)&nd, 4)) for (uint32_t i = 0; i < nd; i++) { uint32_t d; if (f.read((char*)&d, 4)) m.doors.insert(d); }
    return true;
}

} // namespace mc
