#include "world.h"
#include <algorithm>

namespace mc {

void Generator::fill(Chunk& c) const {
    c.b.fill(AIR); c.nonAir = 0; c.modified = false;
    if (kind != Flat || layers.empty()) return;
    int total = 0; for (auto& l : layers) total += l.second;
    int y0 = c.pos.y * CS, bottom = -total;
    if (y0 + CS <= bottom || y0 >= 0) return;
    for (int ly = 0; ly < CS; ly++) {
        int y = y0 + ly; if (y < bottom || y >= 0) continue;
        int acc = bottom; BlockId id = AIR;
        for (auto& l : layers) { if (y < acc + l.second) { id = l.first; break; } acc += l.second; }
        for (int lz = 0; lz < CS; lz++) for (int lx = 0; lx < CS; lx++) c.b[Chunk::idx(lx, ly, lz)] = id;
    }
    c.recount();
}

void World::markAround(I3 c) {
    for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        I3 n{ c.x + dx, c.y + dy, c.z + dz }; if (chunk(n)) dirty.insert(n);
    }
}

Chunk& World::loadChunk(I3 c) {
    if (Chunk* e = chunk(c)) return *e;
    auto ch = std::make_unique<Chunk>(); ch->pos = c;
    if (!(store && store->load(c, *ch))) gen.fill(*ch);
    else ch->modified = false;   // matches what is on disk
    Chunk& ref = *ch; chunks.emplace(c, std::move(ch));
    markAround(c);               // its neighbours can now hide the faces they share with it
    return ref;
}

void World::unloadChunk(I3 c) {
    auto it = chunks.find(c); if (it == chunks.end()) return;
    if (it->second->modified && store) store->save(*it->second);
    chunks.erase(it); dirty.erase(c);
}

void World::set(int x, int y, int z, BlockId id) {
    I3 cp = chunkOf(x, y, z); Chunk& c = loadChunk(cp);
    int lx = floorMod(x, CS), ly = floorMod(y, CS), lz = floorMod(z, CS);
    if (c.get(lx, ly, lz) == id) return;
    c.set(lx, ly, lz, id); c.modified = true;
    // this chunk, plus every neighbour whose border (and corner shading) touches the block
    int ax[2] = { 0, lx == 0 ? -1 : lx == CS - 1 ? 1 : 0 }, ay[2] = { 0, ly == 0 ? -1 : ly == CS - 1 ? 1 : 0 }, az[2] = { 0, lz == 0 ? -1 : lz == CS - 1 ? 1 : 0 };
    for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) for (int k = 0; k < 2; k++) {
        I3 n{ cp.x + ax[i], cp.y + ay[j], cp.z + az[k] }; if (chunk(n)) dirty.insert(n);
    }
}

void World::stream(I3 center, int rh, int rv, int maxLoads) {
    if (rh != offH || rv != offV) {
        offsets.clear();
        for (int dy = -rv; dy <= rv; dy++) for (int dz = -rh; dz <= rh; dz++) for (int dx = -rh; dx <= rh; dx++)
            if (dx * dx + dz * dz <= rh * rh + rh) offsets.push_back({ dx, dy, dz });
        std::sort(offsets.begin(), offsets.end(), [](const I3& a, const I3& b) {
            return a.x * a.x + a.y * a.y * 2 + a.z * a.z < b.x * b.x + b.y * b.y * 2 + b.z * b.z; });
        offH = rh; offV = rv;
    }
    int loadedNow = 0;
    for (const I3& o : offsets) {
        if (loadedNow >= maxLoads) break;
        I3 c{ center.x + o.x, center.y + o.y, center.z + o.z };
        if (!chunk(c)) { loadChunk(c); loadedNow++; }
    }
    // unload what is clearly out of range (a margin avoids load/unload flicker at the edge)
    std::vector<I3> drop;
    for (auto& kv : chunks) {
        I3 d{ kv.first.x - center.x, kv.first.y - center.y, kv.first.z - center.z };
        if (d.x * d.x + d.z * d.z > (rh + 2) * (rh + 2) || std::abs(d.y) > rv + 2) drop.push_back(kv.first);
    }
    for (auto& c : drop) unloadChunk(c);
}

void World::saveAll() {
    if (!store) return;
    for (auto& kv : chunks) if (kv.second->modified) { store->save(*kv.second); kv.second->modified = false; }
    store->flush();
}

// ------------------------------------------------------------------ queries
bool World::solidAt(int x, int y, int z) const {
    const Chunk* c = chunk(chunkOf(x, y, z));
    if (!c) return true;
    return reg.get(c->get(floorMod(x, CS), floorMod(y, CS), floorMod(z, CS))).solid;
}

bool World::raycast(V3 o, V3 d, double maxT, Hit& hit) const {
    d = normalize(d);
    int x = ifloor(o.x), y = ifloor(o.y), z = ifloor(o.z);
    int sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1, sz = d.z > 0 ? 1 : -1;
    auto delta = [](double v) { return std::fabs(v) > 1e-12 ? std::fabs(1.0 / v) : 1e30; };
    auto first = [](double oo, double dd, int cell) { if (std::fabs(dd) < 1e-12) return 1e30; double b = dd > 0 ? cell + 1.0 : (double)cell; return (b - oo) / dd; };
    double tdx = delta(d.x), tdy = delta(d.y), tdz = delta(d.z), tmx = first(o.x, d.x, x), tmy = first(o.y, d.y, y), tmz = first(o.z, d.z, z), t = 0;
    int nx = 0, ny = 0, nz = 0;
    for (int i = 0; i < 1024 && t <= maxT; i++) {
        if (!loaded(x, y, z)) return false;
        if (get(x, y, z) != AIR) { hit = { x, y, z, nx, ny, nz, t }; return true; }
        if (tmx < tmy && tmx < tmz) { x += sx; t = tmx; tmx += tdx; nx = -sx; ny = 0; nz = 0; }
        else if (tmy < tmz) { y += sy; t = tmy; tmy += tdy; nx = 0; ny = -sy; nz = 0; }
        else { z += sz; t = tmz; tmz += tdz; nx = 0; ny = 0; nz = -sz; }
    }
    return false;
}

bool World::boxFree(V3 f, double h, double H) const {
    const double e = 1e-5;
    for (int y = ifloor(f.y + e); y <= ifloor(f.y + H - e); y++)
        for (int z = ifloor(f.z - h + e); z <= ifloor(f.z + h - e); z++)
            for (int x = ifloor(f.x - h + e); x <= ifloor(f.x + h - e); x++)
                if (solidAt(x, y, z)) return false;
    return true;
}

V3 World::moveBox(V3 feet, V3& vel, double h, double H, double dt, bool& onGround) const {
    onGround = false;
    V3 total = vel * dt;
    int steps = std::max(1, (int)std::ceil(std::max(std::fabs(total.x), std::max(std::fabs(total.y), std::fabs(total.z))) / 0.25));
    V3 step = total * (1.0 / steps);
    for (int s = 0; s < steps; s++) {
        for (int axis : { 1, 0, 2 }) {   // y first (landing), then x, then z
            double d = axis == 0 ? step.x : axis == 1 ? step.y : step.z;
            if (d == 0) continue;
            V3 np = feet; (axis == 0 ? np.x : axis == 1 ? np.y : np.z) += d;
            if (boxFree(np, h, H)) { feet = np; continue; }
            // blocked: move as far as possible (binary search), then stop on that axis
            double lo = 0, hi = std::fabs(d); V3 best = feet;
            for (int k = 0; k < 12; k++) { double m = (lo + hi) * 0.5; V3 t = feet; (axis == 0 ? t.x : axis == 1 ? t.y : t.z) += d > 0 ? m : -m; if (boxFree(t, h, H)) { lo = m; best = t; } else hi = m; }
            feet = best;
            if (axis == 1) { if (d < 0) onGround = true; vel.y = 0; step.y = 0; }
            else if (axis == 0) { vel.x = 0; step.x = 0; } else { vel.z = 0; step.z = 0; }
        }
    }
    if (!onGround) { V3 below = feet; below.y -= 0.02; onGround = !boxFree(below, h, H); }
    return feet;
}

} // namespace mc
