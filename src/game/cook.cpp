#include "cook.h"
#include "../world/shapes.h"
#include "visors.h"   // escText
#include <algorithm>
#include <fstream>
#include <map>
#include <cmath>
#include <thread>
#include <unordered_set>

namespace mc {

bool cookOpen(const World& w, BlockId v) { return cookOpenReg(w.reg, v); }
bool cookOpenReg(const BlockRegistry& reg, BlockId v) {
    if (v == AIR) return true;
    const BlockDef& d = reg.get(v);
    if (d.liquid || d.render != Render::Opaque) return true;
    return resolveShape(v, d) != Shapes::FULL;
}

int CookRooms::roomAt(int x, int y, int z) const {
    auto it = cells.find(chunkOf(x, y, z)); if (it == cells.end()) return 0;
    return it->second[(size_t)(floorMod(y, CS) * CS + floorMod(z, CS)) * CS + floorMod(x, CS)];
}
int CookRooms::count(bool outsideToo) const { int n = 0; for (const CookRoom& r : rooms) if ((outsideToo || !r.outside) && !r.pocket()) n++; return n; }

CookRooms findRooms(World& w, const CookArea& a, const std::vector<CookDoor>& doors, const std::vector<EnvZone>& zones) {
    CookRooms out;
    const int X = (a.cx1 - a.cx0 + 1) * CS, Z = (a.cz1 - a.cz0 + 1) * CS, Y = a.y1 - a.y0;
    if (X <= 0 || Z <= 0 || Y <= 0 || (long long)X * Y * Z > 48ll * 1024 * 1024) return out;   // (too big at once: cook it in parts)
    const int bx = a.cx0 * CS, bz = a.cz0 * CS, by = a.y0;
    auto idx = [&](int x, int y, int z) { return ((size_t)y * Z + z) * X + x; };   // area-local
    // the blocks: 0 solid, 1 air, 2 a door's opening (the air stops there)
    std::vector<uint8_t> st((size_t)X * Y * Z, 0);
    for (int cy = floorDiv(a.y0, CS); cy <= floorDiv(a.y1 - 1, CS); cy++)
        for (int cz = a.cz0; cz <= a.cz1; cz++) for (int cx = a.cx0; cx <= a.cx1; cx++) {
            I3 c{ cx, cy, cz }; const Chunk* ch = w.chunk(c); if (!ch) ch = &w.loadChunk(c);
            for (int ly = 0; ly < CS; ly++) {
                int y = cy * CS + ly - by; if (y < 0 || y >= Y) continue;
                for (int lz = 0; lz < CS; lz++) for (int lx = 0; lx < CS; lx++)
                    st[idx((cx - a.cx0) * CS + lx, y, (cz - a.cz0) * CS + lz)] = cookOpen(w, ch->get(lx, ly, lz)) ? 1 : 0;
            }
        }
    for (const CookDoor& d : doors)
        for (int y = std::max(d.lo.y - by, 0); y <= std::min(d.hi.y - by, Y - 1); y++)
            for (int z = std::max(d.lo.z - bz, 0); z <= std::min(d.hi.z - bz, Z - 1); z++)
                for (int x = std::max(d.lo.x - bx, 0); x <= std::min(d.hi.x - bx, X - 1); x++) if (st[idx(x, y, z)] == 1) st[idx(x, y, z)] = 2;
    std::vector<uint16_t> lab((size_t)X * Y * Z, 0);
    auto newRoom = [&](bool outside, const std::string& zone) -> int {
        if (out.rooms.size() >= 65000) return 0;
        CookRoom r; r.id = (int)out.rooms.size() + 1; r.outside = outside; r.zone = zone; r.lo = { 1 << 30, 1 << 30, 1 << 30 }; r.hi = { -(1 << 30), -(1 << 30), -(1 << 30) };
        out.rooms.push_back(r); return r.id;
    };
    auto mark = [&](size_t i, int x, int y, int z, int id) {
        lab[i] = (uint16_t)id; CookRoom& r = out.rooms[id - 1]; r.cells++;
        int wx = x + bx, wy = y + by, wz = z + bz;
        r.lo = { std::min(r.lo.x, wx), std::min(r.lo.y, wy), std::min(r.lo.z, wz) }; r.hi = { std::max(r.hi.x, wx), std::max(r.hi.y, wy), std::max(r.hi.z, wz) };
    };
    // 1. zones: their air is their own room
    std::vector<uint8_t> inZone; if (!zones.empty()) inZone.assign((size_t)X * Y * Z, 0);
    for (const EnvZone& zn : zones) {
        int id = 0;
        for (int y = std::max(zn.lo.y - by, 0); y <= std::min(zn.hi.y - by, Y - 1); y++)
            for (int z = std::max(zn.lo.z - bz, 0); z <= std::min(zn.hi.z - bz, Z - 1); z++)
                for (int x = std::max(zn.lo.x - bx, 0); x <= std::min(zn.hi.x - bx, X - 1); x++) {
                    size_t i = idx(x, y, z); if (st[i] != 1 || lab[i]) continue;
                    if (!id) id = newRoom(false, zn.name); if (!id) break;
                    mark(i, x, y, z, id); inZone[i] = 1;
                }
    }
    // 2. the outside: every column from the top down, as long as it is air - then everything that air touches
    std::vector<size_t> stack;
    auto flood = [&](int id) {
        while (!stack.empty()) {
            size_t i = stack.back(); stack.pop_back();
            int x = (int)(i % X), z = (int)((i / X) % Z), y = (int)(i / ((size_t)X * Z));
            const int D[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
            for (auto& d : D) {
                int nx = x + d[0], ny = y + d[1], nz = z + d[2]; if (nx < 0 || ny < 0 || nz < 0 || nx >= X || ny >= Y || nz >= Z) continue;
                size_t j = idx(nx, ny, nz); if (st[j] != 1 || lab[j]) continue;
                mark(j, nx, ny, nz, id); stack.push_back(j);
            }
        }
    };
    {
        auto getL = [&](int x, int y, int z) { I3 c = chunkOf(x, y, z); const Chunk* ch = w.chunk(c); if (!ch) ch = &w.loadChunk(c); return ch->get(floorMod(x, CS), floorMod(y, CS), floorMod(z, CS)); };
        int outsideId = 0;
        for (int z = 0; z < Z; z++) for (int x = 0; x < X; x++) {   // the void under the world (nothing below the range): outside too
            size_t i = idx(x, 0, z); if (st[i] != 1 || lab[i]) continue;
            bool voidBelow = true;
            for (int y = a.y0 - 1; y >= a.y0 - 96 && voidBelow; y--) voidBelow = cookOpen(w, getL(x + bx, y, z + bz));
            if (!voidBelow) continue;
            if (!outsideId) outsideId = newRoom(true, "");
            mark(i, x, 0, z, outsideId); stack.push_back(i);
        }
        for (int z = 0; z < Z; z++) for (int x = 0; x < X; x++) {
            bool sky = true;   // (the area's top is not always the sky: a ceiling above it makes this column a room's)
            for (int y = a.y1; y < a.y1 + 96 && sky; y++) sky = cookOpen(w, getL(x + bx, y, z + bz));
            if (!sky) continue;
            for (int y = Y - 1; y >= 0; y--) {
                size_t i = idx(x, y, z); if (st[i] != 1) break; if (lab[i]) { if (!inZone.empty() && inZone[i]) break; continue; }
                if (!outsideId) outsideId = newRoom(true, "");
                mark(i, x, y, z, outsideId); stack.push_back(i);
            }
        }
        if (outsideId) flood(outsideId);
    }
    // 3. what is left: rooms
    for (int y = 0; y < Y; y++) for (int z = 0; z < Z; z++) for (int x = 0; x < X; x++) {
        size_t i = idx(x, y, z); if (st[i] != 1 || lab[i]) continue;
        int id = newRoom(false, ""); if (!id) continue;
        mark(i, x, y, z, id); stack.push_back(i); flood(id);
    }
    // 4. portals: doors (the spaces touching each door's opening), and open edges of zones
    std::map<std::pair<int, int>, size_t> seen;
    auto portal = [&](int p, int q, I3 lo, I3 hi, const std::string& via) {
        if (p == q || !p || !q) return; if (p > q) std::swap(p, q);
        auto it = seen.find({ p, q });
        if (it == seen.end()) { seen[{ p, q }] = out.portals.size(); out.portals.push_back({ p, q, lo, hi, via }); return; }
        CookPortal& c = out.portals[it->second];
        c.lo = { std::min(c.lo.x, lo.x), std::min(c.lo.y, lo.y), std::min(c.lo.z, lo.z) }; c.hi = { std::max(c.hi.x, hi.x), std::max(c.hi.y, hi.y), std::max(c.hi.z, hi.z) };
    };
    for (const CookDoor& d : doors) {
        std::vector<int> touch;
        for (int y = d.lo.y - 1; y <= d.hi.y + 1; y++) for (int z = d.lo.z - 1; z <= d.hi.z + 1; z++) for (int x = d.lo.x - 1; x <= d.hi.x + 1; x++) {
            bool inside = x >= d.lo.x && x <= d.hi.x && y >= d.lo.y && y <= d.hi.y && z >= d.lo.z && z <= d.hi.z; if (inside) continue;
            int lx = x - bx, ly = y - by, lz = z - bz; if (lx < 0 || ly < 0 || lz < 0 || lx >= X || ly >= Y || lz >= Z) continue;
            int id = lab[idx(lx, ly, lz)]; if (id && std::find(touch.begin(), touch.end(), id) == touch.end()) touch.push_back(id);
        }
        for (size_t p = 0; p < touch.size(); p++) for (size_t q = p + 1; q < touch.size(); q++) portal(touch[p], touch[q], d.lo, d.hi, d.name.empty() ? "door" : d.name);
    }
    if (!inZone.empty())
        for (int y = 0; y < Y; y++) for (int z = 0; z < Z; z++) for (int x = 0; x < X; x++) {
            size_t i = idx(x, y, z); int p = lab[i]; if (!p) continue;
            const int D[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
            for (auto& d : D) {
                int nx = x + d[0], ny = y + d[1], nz = z + d[2]; if (nx >= X || ny >= Y || nz >= Z) continue;
                size_t j = idx(nx, ny, nz); int q = lab[j];
                if (q && q != p && (inZone[i] || inZone[j])) { I3 c{ x + bx, y + by, z + bz }; portal(p, q, c, { nx + bx, ny + by, nz + bz }, "zone"); }
            }
        }
    // 5. per chunk, for looking it up later
    for (int cy = floorDiv(a.y0, CS); cy <= floorDiv(a.y1 - 1, CS); cy++)
        for (int cz = a.cz0; cz <= a.cz1; cz++) for (int cx = a.cx0; cx <= a.cx1; cx++) {
            std::vector<uint16_t> v((size_t)CS * CS * CS, 0); bool any = false;
            for (int ly = 0; ly < CS; ly++) { int y = cy * CS + ly - by; if (y < 0 || y >= Y) continue;
                for (int lz = 0; lz < CS; lz++) for (int lx = 0; lx < CS; lx++) { uint16_t id = lab[idx((cx - a.cx0) * CS + lx, y, (cz - a.cz0) * CS + lz)]; v[((size_t)ly * CS + lz) * CS + lx] = id; any |= id != 0; } }
            if (any) out.cells[{ cx, cy, cz }] = std::move(v);
        }
    return out;
}

CookLit bakeLight(World& w, const CookArea& a, const std::vector<CookLamp>& lamps, int threads, size_t maxCells) {
    CookLit L;
    const int X = (a.cx1 - a.cx0 + 1) * CS, Z = (a.cz1 - a.cz0 + 1) * CS, Y = a.y1 - a.y0;
    if (X <= 0 || Y <= 0 || Z <= 0 || (size_t)X * Y * Z > maxCells) return L;
    L.lo = { a.cx0 * CS, a.y0, a.cz0 * CS }; L.X = X; L.Y = Y; L.Z = Z;
    L.rgb.assign((size_t)X * Y * Z * 3, 0.f); L.dir.assign((size_t)X * Y * Z * 3, 0.f);
    // which blocks are open, with a margin round the area (lamps just outside it shine in)
    const int M = 20, GX = X + 2 * M, GY = Y + 2 * M, GZ = Z + 2 * M; const I3 g0{ L.lo.x - M, L.lo.y - M, L.lo.z - M };
    std::vector<uint8_t> open((size_t)GX * GY * GZ, 1);
    for (int cy = floorDiv(g0.y, CS); cy <= floorDiv(g0.y + GY - 1, CS); cy++)
        for (int cz = floorDiv(g0.z, CS); cz <= floorDiv(g0.z + GZ - 1, CS); cz++)
            for (int cx = floorDiv(g0.x, CS); cx <= floorDiv(g0.x + GX - 1, CS); cx++) {
                const Chunk* ch = w.chunk({ cx, cy, cz }); if (!ch) ch = &w.loadChunk({ cx, cy, cz });
                for (int ly = 0; ly < CS; ly++) { int gy = cy * CS + ly - g0.y; if (gy < 0 || gy >= GY) continue;
                    for (int lz = 0; lz < CS; lz++) { int gz = cz * CS + lz - g0.z; if (gz < 0 || gz >= GZ) continue;
                        for (int lx = 0; lx < CS; lx++) { int gx = cx * CS + lx - g0.x; if (gx < 0 || gx >= GX) continue;
                            open[((size_t)gy * GZ + gz) * GX + gx] = cookOpen(w, ch->get(lx, ly, lz)) ? 1 : 0; } } }
            }
    auto openW = [&](int x, int y, int z) -> bool {   // (outside the grid: open)
        int gx = x - g0.x, gy = y - g0.y, gz = z - g0.z;
        if (gx < 0 || gy < 0 || gz < 0 || gx >= GX || gy >= GY || gz >= GZ) return true;
        return open[((size_t)gy * GZ + gz) * GX + gx] != 0;
    };
    auto blocked = [&](V3 p, V3 q) -> bool {   // a solid block on the way from p to q (not p's own, not q's own)
        int x = (int)std::floor(p.x), y = (int)std::floor(p.y), z = (int)std::floor(p.z);
        const int ex = (int)std::floor(q.x), ey = (int)std::floor(q.y), ez = (int)std::floor(q.z);
        V3 d = q - p; int sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1, sz = d.z > 0 ? 1 : -1;
        auto tStart = [](double p0, double dd, int c, int s) { if (std::fabs(dd) < 1e-12) return 1e30; double edge = s > 0 ? c + 1 : c; return (edge - p0) / dd; };
        double tx = tStart(p.x, d.x, x, sx), ty = tStart(p.y, d.y, y, sy), tz = tStart(p.z, d.z, z, sz);
        double dx = std::fabs(d.x) < 1e-12 ? 1e30 : 1 / std::fabs(d.x), dy = std::fabs(d.y) < 1e-12 ? 1e30 : 1 / std::fabs(d.y), dz = std::fabs(d.z) < 1e-12 ? 1e30 : 1 / std::fabs(d.z);
        for (int k = 0; k < 256; k++) {
            if (tx <= ty && tx <= tz) { x += sx; tx += dx; } else if (ty <= tz) { y += sy; ty += dy; } else { z += sz; tz += dz; }
            if (x == ex && y == ey && z == ez) return false;
            if (!openW(x, y, z)) return true;
        }
        return false;
    };
    auto work = [&](int ya, int yb) {
        for (const CookLamp& lp : lamps) {
            const double R = lp.range; if (R <= 0) continue;
            int x0 = std::max(0, (int)std::floor(lp.c.x - lp.half.x - R) - L.lo.x), x1 = std::min(X - 1, (int)std::floor(lp.c.x + lp.half.x + R) - L.lo.x);
            int y0 = std::max(ya, (int)std::floor(lp.c.y - lp.half.y - R) - L.lo.y), y1 = std::min(yb - 1, (int)std::floor(lp.c.y + lp.half.y + R) - L.lo.y);
            int z0 = std::max(0, (int)std::floor(lp.c.z - lp.half.z - R) - L.lo.z), z1 = std::min(Z - 1, (int)std::floor(lp.c.z + lp.half.z + R) - L.lo.z);
            double lum = lp.col.x * 0.3 + lp.col.y * 0.59 + lp.col.z * 0.11;
            for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
                int wx = x + L.lo.x, wy = y + L.lo.y, wz = z + L.lo.z; if (!openW(wx, wy, wz)) continue;
                V3 p{ wx + 0.5, wy + 0.5, wz + 0.5 };
                V3 q{ std::clamp(p.x, lp.c.x - lp.half.x, lp.c.x + lp.half.x), std::clamp(p.y, lp.c.y - lp.half.y, lp.c.y + lp.half.y), std::clamp(p.z, lp.c.z - lp.half.z, lp.c.z + lp.half.z) };
                V3 Lv = q - p; double dd = std::sqrt(Lv.x * Lv.x + Lv.y * Lv.y + Lv.z * Lv.z); if (dd >= R) continue;
                double att = 1 - dd / R; att *= att; if (att <= 0.002) continue;
                if (dd > 0.5 && blocked(p, q)) continue;
                size_t i = L.at(x, y, z) * 3; double k = dd > 1e-6 ? 1 / dd : 0;
                L.rgb[i] += (float)(lp.col.x * att); L.rgb[i + 1] += (float)(lp.col.y * att); L.rgb[i + 2] += (float)(lp.col.z * att);
                L.dir[i] += (float)(Lv.x * k * att * lum); L.dir[i + 1] += (float)(Lv.y * k * att * lum); L.dir[i + 2] += (float)(Lv.z * k * att * lum);
            }
        }
    };
    threads = std::clamp(threads, 1, 32);
    if (threads == 1) work(0, Y);
    else { std::vector<std::thread> th; for (int t = 0; t < threads; t++) th.emplace_back(work, Y * t / threads, Y * (t + 1) / threads); for (auto& t : th) t.join(); }
    for (size_t i = 0; i < L.dir.size(); i += 3) {   // the direction: unit length (0: no light)
        float l = std::sqrt(L.dir[i] * L.dir[i] + L.dir[i + 1] * L.dir[i + 1] + L.dir[i + 2] * L.dir[i + 2]);
        if (l > 1e-6f) { L.dir[i] /= l; L.dir[i + 1] /= l; L.dir[i + 2] /= l; }
    }
    {   // solid blocks take the light of the air around them (so a lookup between a block of air and a wall isn't half dark)
        std::vector<float> rgb2 = L.rgb, dir2 = L.dir;
        for (int y = 0; y < Y; y++) for (int z = 0; z < Z; z++) for (int x = 0; x < X; x++) {
            if (openW(x + L.lo.x, y + L.lo.y, z + L.lo.z)) continue;
            const int D[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
            float s[6] = { 0, 0, 0, 0, 0, 0 }; int n = 0;
            for (auto& d : D) {
                int nx = x + d[0], ny = y + d[1], nz = z + d[2]; if (nx < 0 || ny < 0 || nz < 0 || nx >= X || ny >= Y || nz >= Z) continue;
                if (!openW(nx + L.lo.x, ny + L.lo.y, nz + L.lo.z)) continue;
                size_t j = L.at(nx, ny, nz) * 3; for (int c = 0; c < 3; c++) { s[c] += L.rgb[j + c]; s[3 + c] += L.dir[j + c]; } n++;
            }
            if (!n) continue;
            size_t i = L.at(x, y, z) * 3; for (int c = 0; c < 3; c++) { rgb2[i + c] = s[c] / n; dir2[i + c] = s[3 + c] / n; }
        }
        L.rgb.swap(rgb2); L.dir.swap(dir2);
    }
    return L;
}

std::vector<std::pair<int, V3>> probeSpots(const CookRooms& r, int maxProbes) {
    std::vector<const CookRoom*> order;
    for (const CookRoom& c : r.rooms) if (!c.outside && !c.pocket() && c.cells >= 27) order.push_back(&c);
    std::sort(order.begin(), order.end(), [](const CookRoom* a, const CookRoom* b) { return a->cells > b->cells; });
    if ((int)order.size() > maxProbes) order.resize(maxProbes);
    std::vector<std::pair<int, V3>> out;
    for (const CookRoom* c : order) {
        double mx = (c->lo.x + c->hi.x + 1) * 0.5, my = (c->lo.y + c->hi.y + 1) * 0.5, mz = (c->lo.z + c->hi.z + 1) * 0.5;
        int bx = 0, by = 0, bz = 0; double best = 1e30;
        for (int y = c->lo.y; y <= c->hi.y; y++) for (int z = c->lo.z; z <= c->hi.z; z++) for (int x = c->lo.x; x <= c->hi.x; x++) {
            double d = (x + 0.5 - mx) * (x + 0.5 - mx) + (y + 0.5 - my) * (y + 0.5 - my) * 2 + (z + 0.5 - mz) * (z + 0.5 - mz);   // (heights count double: not stuck to the floor or ceiling)
            if (d >= best || r.roomAt(x, y, z) != c->id) continue;
            best = d; bx = x; by = y; bz = z;
        }
        if (best < 1e30) out.push_back({ c->id, V3{ bx + 0.5, by + 0.5, bz + 0.5 } });
    }
    return out;
}

std::unordered_map<I3, std::vector<uint16_t>, I3Hash> chunkRooms(const CookRooms& r) {
    std::unordered_map<I3, std::unordered_set<uint16_t>, I3Hash> sets;
    for (const auto& kv : r.cells) {
        const I3 c = kv.first; const std::vector<uint16_t>& v = kv.second;
        auto& own = sets[c];
        for (int y = 0; y < CS; y++) for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
            uint16_t id = v[((size_t)y * CS + z) * CS + x]; if (!id) continue;
            own.insert(id);
            if (x == 0) sets[{ c.x - 1, c.y, c.z }].insert(id); if (x == CS - 1) sets[{ c.x + 1, c.y, c.z }].insert(id);   // (the air at a chunk's edge sees the next chunk's blocks)
            if (y == 0) sets[{ c.x, c.y - 1, c.z }].insert(id); if (y == CS - 1) sets[{ c.x, c.y + 1, c.z }].insert(id);
            if (z == 0) sets[{ c.x, c.y, c.z - 1 }].insert(id); if (z == CS - 1) sets[{ c.x, c.y, c.z + 1 }].insert(id);
        }
    }
    std::unordered_map<I3, std::vector<uint16_t>, I3Hash> out;
    for (auto& kv : sets) out[kv.first] = std::vector<uint16_t>(kv.second.begin(), kv.second.end());
    return out;
}

void visibleRooms(const CookRooms& r, const float vp[16], V3 cam, std::vector<uint8_t>& vis) {
    const int N = (int)r.rooms.size();
    vis.assign(N + 1, 0);
    int start = r.roomAt((int)std::floor(cam.x), (int)std::floor(cam.y), (int)std::floor(cam.z));
    if (!start) { std::fill(vis.begin(), vis.end(), 1); return; }   // (in a door, a wall, or outside the cooked area: everything)
    struct Rect { float x0 = 2, y0 = 2, x1 = -2, y1 = -2; bool empty() const { return x0 >= x1 || y0 >= y1; } };
    std::vector<Rect> seen(N + 1);
    std::vector<std::vector<int>> adj(N + 1);
    for (int i = 0; i < (int)r.portals.size(); i++) { const CookPortal& p = r.portals[i]; if (p.a > 0 && p.a <= N && p.b > 0 && p.b <= N) { adj[p.a].push_back(i); adj[p.b].push_back(i); } }
    auto onScreen = [&](const CookPortal& p, Rect& out) -> bool {
        double lo[3] = { p.lo.x - 0.05, p.lo.y - 0.05, p.lo.z - 0.05 }, hi[3] = { p.hi.x + 1.05, p.hi.y + 1.05, p.hi.z + 1.05 };
        double c[3] = { cam.x, cam.y, cam.z };
        if (c[0] > lo[0] - 1 && c[0] < hi[0] + 1 && c[1] > lo[1] - 1 && c[1] < hi[1] + 1 && c[2] > lo[2] - 1 && c[2] < hi[2] + 1) { out = { -1, -1, 1, 1 }; return true; }   // (right at it)
        int behind = 0; Rect rr;
        for (int k = 0; k < 8; k++) {
            float x = (float)((k & 1 ? hi[0] : lo[0]) - c[0]), y = (float)((k & 2 ? hi[1] : lo[1]) - c[1]), z = (float)((k & 4 ? hi[2] : lo[2]) - c[2]);
            float cx = x * vp[0] + y * vp[4] + z * vp[8] + vp[12], cy = x * vp[1] + y * vp[5] + z * vp[9] + vp[13], cw = x * vp[3] + y * vp[7] + z * vp[11] + vp[15];
            if (cw <= 0.05f) { behind++; continue; }
            float nx = cx / cw, ny = cy / cw;
            rr.x0 = std::min(rr.x0, nx); rr.y0 = std::min(rr.y0, ny); rr.x1 = std::max(rr.x1, nx); rr.y1 = std::max(rr.y1, ny);
        }
        if (behind == 8) return false;
        if (behind > 0) rr = { -1, -1, 1, 1 };   // (partly behind the camera: it can be anywhere on screen)
        rr.x0 = std::max(rr.x0, -1.f); rr.y0 = std::max(rr.y0, -1.f); rr.x1 = std::min(rr.x1, 1.f); rr.y1 = std::min(rr.y1, 1.f);
        out = rr; return !rr.empty();
    };
    seen[start] = { -1, -1, 1, 1 };
    std::vector<int> queue{ start }; int budget = 16 * (N + (int)r.portals.size()) + 64;
    while (!queue.empty() && budget-- > 0) {
        int a = queue.back(); queue.pop_back(); Rect ra = seen[a];
        for (int pi : adj[a]) {
            const CookPortal& p = r.portals[pi]; int b = p.a == a ? p.b : p.a;
            Rect pr; if (!onScreen(p, pr)) continue;
            Rect in{ std::max(ra.x0, pr.x0), std::max(ra.y0, pr.y0), std::min(ra.x1, pr.x1), std::min(ra.y1, pr.y1) }; if (in.empty()) continue;
            Rect& sb = seen[b]; Rect u = sb.empty() ? in : Rect{ std::min(sb.x0, in.x0), std::min(sb.y0, in.y0), std::max(sb.x1, in.x1), std::max(sb.y1, in.y1) };
            if (!sb.empty() && u.x0 >= sb.x0 - 1e-4f && u.y0 >= sb.y0 - 1e-4f && u.x1 <= sb.x1 + 1e-4f && u.y1 <= sb.y1 + 1e-4f) continue;   // (nothing new)
            sb = u; queue.push_back(b);
        }
    }
    for (int i = 1; i <= N; i++) vis[i] = seen[i].empty() ? 0 : 1;
}

bool saveCookRooms(const std::string& path, const CookArea& a, const CookRooms& r) {
    std::ofstream f(path); if (!f) return false;
    f << "cookrooms 1\narea " << a.cx0 << " " << a.cz0 << " " << a.cx1 << " " << a.cz1 << " " << a.y0 << " " << a.y1 << "\n";
    for (const CookRoom& c : r.rooms)
        f << "room " << c.id << " " << (c.outside ? 1 : 0) << " " << escText(c.zone) << " " << c.lo.x << " " << c.lo.y << " " << c.lo.z << " " << c.hi.x << " " << c.hi.y << " " << c.hi.z << " " << c.cells << "\n";
    for (const CookPortal& p : r.portals)
        f << "portal " << p.a << " " << p.b << " " << escText(p.via) << " " << p.lo.x << " " << p.lo.y << " " << p.lo.z << " " << p.hi.x << " " << p.hi.y << " " << p.hi.z << "\n";
    return (bool)f;
}

} // namespace mc
