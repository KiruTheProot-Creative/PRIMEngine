#include "world.h"
#include "shapes.h"
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
    Chunk& ref = *ch; ref.emitters = countEmitters(ref); chunks.emplace(c, std::move(ch));
    markAround(c);               // its neighbours can now hide the faces they share with it
    if (ref.emitters) markAll27(c);   // and its lamps light them up
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
    BlockId old = c.get(lx, ly, lz);
    c.set(lx, ly, lz, id); c.modified = true;
    int lo = reg.get(old).light, ln = reg.get(id).light;
    if (old != AIR && lo) c.emitters--;
    if (id != AIR && ln) c.emitters++;
    // light reaches up to 15 blocks: near a lamp, every chunk around has to be redrawn
    if ((old != AIR && lo) || (id != AIR && ln) || emittersNear(cp)) markAll27(cp);
    // this chunk, plus every neighbour whose border (and corner shading) touches the block
    int ax[2] = { 0, lx == 0 ? -1 : lx == CS - 1 ? 1 : 0 }, ay[2] = { 0, ly == 0 ? -1 : ly == CS - 1 ? 1 : 0 }, az[2] = { 0, lz == 0 ? -1 : lz == CS - 1 ? 1 : 0 };
    for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) for (int k = 0; k < 2; k++) {
        I3 n{ cp.x + ax[i], cp.y + ay[j], cp.z + az[k] }; if (chunk(n)) dirty.insert(n);
    }
}

int World::countEmitters(const Chunk& c) const {
    if (!c.nonAir) return 0;
    int n = 0; for (BlockId v : c.b) if (v != AIR && reg.get(v).light) n++;
    return n;
}
void World::recountEmitters() { for (auto& kv : chunks) kv.second->emitters = countEmitters(*kv.second); }
bool World::emittersNear(I3 c) const {
    for (int dy = -1; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) { const Chunk* n = chunk({ c.x + dx, c.y + dy, c.z + dz }); if (n && n->emitters) return true; }
    return false;
}
void World::markAll27(I3 c) {
    for (int dy = -1; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) { I3 n{ c.x + dx, c.y + dy, c.z + dz }; if (chunk(n)) dirty.insert(n); }
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
    BlockId v = c->get(floorMod(x, CS), floorMod(y, CS), floorMod(z, CS));
    return v != AIR && reg.get(v).solid;
}

// the ray against one shape's boxes (block-local); t along the ray, n = side entered
static bool rayBoxes(const ShapeVariant& sv, const double o[3], const double d[3], double& bestT, int n[3]) {
    bool any = false;
    for (const SBox& b : sv.boxes) {
        double t0 = -1e30, t1 = 1e30; int ax = -1;
        bool miss = false;
        for (int a = 0; a < 3 && !miss; a++) {
            if (std::fabs(d[a]) < 1e-12) { if (o[a] < b.lo[a] || o[a] > b.hi[a]) miss = true; continue; }
            double ta = (b.lo[a] - o[a]) / d[a], tb = (b.hi[a] - o[a]) / d[a]; if (ta > tb) std::swap(ta, tb);
            if (ta > t0) { t0 = ta; ax = a; }
            t1 = std::min(t1, tb);
        }
        if (miss || t0 > t1 || t0 < 0 || ax < 0 || t0 >= bestT) continue;
        bestT = t0; n[0] = n[1] = n[2] = 0; n[ax] = d[ax] > 0 ? -1 : 1; any = true;
    }
    return any;
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
        BlockId v = get(x, y, z);
        if (v != AIR) {
            int s = resolveShape(v, reg.get(v));
            if (s == Shapes::FULL) { hit = { x, y, z, nx, ny, nz, t, o.x + d.x * t, o.y + d.y * t, o.z + d.z * t }; return true; }
            // other shapes: only their solid parts count, so you can aim past a pole or through a window frame
            double lo[3] = { o.x - x, o.y - y, o.z - z }, dd[3] = { d.x, d.y, d.z }, bt = maxT; int n[3];
            if (rayBoxes(Shapes::get().variant(s, rotOf(v)), lo, dd, bt, n)) { hit = { x, y, z, n[0], n[1], n[2], bt, o.x + d.x * bt, o.y + d.y * bt, o.z + d.z * bt }; return true; }
        }
        if (tmx < tmy && tmx < tmz) { x += sx; t = tmx; tmx += tdx; nx = -sx; ny = 0; nz = 0; }
        else if (tmy < tmz) { y += sy; t = tmy; tmy += tdy; nx = 0; ny = -sy; nz = 0; }
        else { z += sz; t = tmz; tmz += tdz; nx = 0; ny = 0; nz = -sz; }
    }
    return false;
}

bool World::boxFree(V3 f, double h, double H) const {
    const double e = 1e-5;
    double lo[3] = { f.x - h + e, f.y + e, f.z - h + e }, hi[3] = { f.x + h - e, f.y + H - e, f.z + h - e };
    for (int y = ifloor(lo[1]); y <= ifloor(hi[1]); y++)
        for (int z = ifloor(lo[2]); z <= ifloor(hi[2]); z++)
            for (int x = ifloor(lo[0]); x <= ifloor(hi[0]); x++) {
                const Chunk* c = chunk(chunkOf(x, y, z));
                if (!c) return false;   // not loaded yet: solid, so you never fall through
                BlockId v = c->get(floorMod(x, CS), floorMod(y, CS), floorMod(z, CS));
                if (v == AIR) continue;
                const BlockDef& d = reg.get(v); if (!d.solid) continue;
                int s = resolveShape(v, d);
                if (s == Shapes::FULL) return false;
                for (const SBox& b : Shapes::get().variant(s, rotOf(v)).boxes)
                    if (lo[0] < x + b.hi[0] && hi[0] > x + b.lo[0] && lo[1] < y + b.hi[1] && hi[1] > y + b.lo[1] && lo[2] < z + b.hi[2] && hi[2] > z + b.lo[2]) return false;
            }
    return true;
}

V3 World::moveBox(V3 feet, V3& vel, double h, double H, double dt, bool& onGround, double stepUp) const {
    bool wasOnGround = onGround; onGround = false;
    V3 total = vel * dt;
    int steps = std::max(1, (int)std::ceil(std::max(std::fabs(total.x), std::max(std::fabs(total.y), std::fabs(total.z))) / 0.25));
    V3 step = total * (1.0 / steps);
    for (int s = 0; s < steps; s++) {
        for (int axis : { 1, 0, 2 }) {   // y first (landing), then x, then z
            double d = axis == 0 ? step.x : axis == 1 ? step.y : step.z;
            if (d == 0) continue;
            V3 np = feet; (axis == 0 ? np.x : axis == 1 ? np.y : np.z) += d;
            if (boxFree(np, h, H)) { feet = np; continue; }
            if (axis != 1 && stepUp > 0 && (wasOnGround || onGround)) {
                // walking into something low (a half block, a stair step, a ramp): climb onto it, as low as it fits
                V3 up = np; up.y += stepUp; V3 raised = feet; raised.y += stepUp;
                if (boxFree(raised, h, H) && boxFree(up, h, H)) {
                    double lo = 0, hi = stepUp;
                    for (int k = 0; k < 12; k++) { double m = (lo + hi) * 0.5; V3 t = np; t.y += m; if (boxFree(t, h, H)) hi = m; else lo = m; }
                    feet = np; feet.y += hi; onGround = true; continue;
                }
            }
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
