#include "cook.h"
#include "../world/shapes.h"
#include "visors.h"   // escText
#include <algorithm>
#include <fstream>
#include <map>

namespace mc {

bool cookOpen(const World& w, BlockId v) {
    if (v == AIR) return true;
    const BlockDef& d = w.reg.get(v);
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
        int outsideId = 0;
        for (int z = 0; z < Z; z++) for (int x = 0; x < X; x++) {
            bool sky = true;   // (the area's top is not always the sky: a ceiling above it makes this column a room's)
            for (int y = a.y1; y < a.y1 + 96 && sky; y++) sky = cookOpen(w, w.get(x + bx, y, z + bz));
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
