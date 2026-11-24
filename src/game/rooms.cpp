#include "rooms.h"
#include "../world/shapes.h"
#include "visors.h"   // escText
#include <algorithm>
#include <climits>
#include <fstream>
#include <sstream>

namespace mc {

// ------------------------------------------------------------------ a room's blocks
bool RoomShape::has(int x, int y, int z) const {
    auto it = bits.find(chunkOf(x, y, z)); if (it == bits.end()) return false;
    return it->second.has(RoomBits::at(floorMod(x, CS), floorMod(y, CS), floorMod(z, CS)));
}
bool RoomShape::add(int x, int y, int z) {
    if (!bits[chunkOf(x, y, z)].set(RoomBits::at(floorMod(x, CS), floorMod(y, CS), floorMod(z, CS)))) return false;
    if (!cells) { lo = hi = { x, y, z }; }
    else { lo = { std::min(lo.x, x), std::min(lo.y, y), std::min(lo.z, z) }; hi = { std::max(hi.x, x), std::max(hi.y, y), std::max(hi.z, z) }; }
    cells++; return true;
}
void RoomShape::forEach(const std::function<void(int, int, int)>& f) const {
    for (const auto& kv : bits)
        for (int w = 0; w < CS3 / 64; w++) {
            uint64_t m = kv.second.b[w];
            for (int bit = 0; m; bit++, m >>= 1) {
                if (!(m & 1)) continue; int i = w * 64 + bit;
                f(kv.first.x * CS + i % CS, kv.first.y * CS + i / (CS * CS), kv.first.z * CS + (i / CS) % CS);
            }
        }
}

static int64_t colKey(int x, int z) { return ((int64_t)x << 32) ^ (int64_t)(uint32_t)z; }
static const int N6[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };   // F_PX, F_NX, F_PY, F_NY, F_PZ, F_NZ

// ------------------------------------------------------------------ the search
uint8_t RoomFlood::kind(int x, int y, int z, uint8_t& mask, int& door) {
    mask = 0; door = -1;
    if (W.blocked && W.blocked(x, y, z)) return 2;
    I3 c = chunkOf(x, y, z);
    if (!doorsByChunk.empty()) {
        auto dit = doorsByChunk.find(c);
        if (dit != doorsByChunk.end())
            for (int i : dit->second) { const CookDoor& d = W.doors[i]; if (x >= d.lo.x && x <= d.hi.x && y >= d.lo.y && y <= d.hi.y && z >= d.lo.z && z <= d.hi.z) { door = i; return 3; } }
    }
    if (!(c == lastC) || !lastHave) { lastC = c; lastCh = W.chunkAt ? W.chunkAt(c) : nullptr; lastHave = true; }
    if (!lastCh) { shape.partial = true; return 2; }
    if (W.through && W.through(x, y, z)) return 1;
    BlockId v = lastCh->get(floorMod(x, CS), floorMod(y, CS), floorMod(z, CS));
    if (v == AIR) return 1;
    const BlockDef& d = W.reg->get(v);
    if (d.liquid || !d.solid) return 1;   // water, lava, plants: air for rooms (glass and other solid blocks are walls)
    int sh = resolveShape(v, d); if (sh == Shapes::FULL) return 2;
    const ShapeVariant& sv = Shapes::get().variant(sh, rotOf(v));   // a shape: a wall only on the sides it covers completely
    for (int f = 0; f < 6; f++) if (sv.full[f]) mask |= (uint8_t)(1u << f);
    return mask == 0x3F ? 2 : 4;
}
bool RoomFlood::passes(int x, int y, int z, int dir, int nx, int ny, int nz) {
    uint8_t ma, mb; int d;
    kind(x, y, z, ma, d); if (ma >> dir & 1) return false;
    kind(nx, ny, nz, mb, d); if (mb >> (dir ^ 1) & 1) return false;
    return true;
}
bool RoomFlood::mark(int x, int y, int z) {
    if (!seen[chunkOf(x, y, z)].set(RoomBits::at(floorMod(x, CS), floorMod(y, CS), floorMod(z, CS)))) return false;
    q.push_back({ x, y, z }); count++;
    if (count == 1) { lo = hi = { x, y, z }; }
    else { lo = { std::min(lo.x, x), std::min(lo.y, y), std::min(lo.z, z) }; hi = { std::max(hi.x, x), std::max(hi.y, y), std::max(hi.z, z) }; }
    return true;
}
bool RoomFlood::sky(int x, int y, int z) {   // nothing over this block for skyScan blocks (chunks not loaded up there: open)
    int64_t k = colKey(x, z);
    auto it = roofOf.find(k);
    if (it != roofOf.end()) { if (it->second == INT_MAX) return true; if (y < it->second) return false; }
    for (int yy = y + 1; yy <= y + R.skyScan;) {
        I3 c = chunkOf(x, yy, z); const Chunk* ch = W.chunkAt ? W.chunkAt(c) : nullptr;
        if (!ch || ch->nonAir == 0) { if (!(W.blocked && W.blocked(x, yy, z))) { yy = (c.y + 1) * CS; continue; } }
        uint8_t m; int d; uint8_t kk = kind(x, yy, z, m, d);
        if (kk != 1) { roofOf[k] = yy; return false; }
        yy++;
    }
    roofOf[k] = INT_MAX; return true;
}
bool RoomFlood::inFloorLayer(int x, int y, int z) {   // ground (anything not air) within `band` blocks below
    for (int i = 1; i <= R.band; i++) if (!open(x, y - i, z)) return true;
    return false;
}
int RoomFlood::topOfWall(int x, int y, int z) {   // the highest block of the wall going up from here
    int64_t k = colKey(x, z); auto it = wallTopOf.find(k);
    if (it != wallTopOf.end() && it->second >= y) return it->second;
    int t = y; for (int i = 1; i <= R.maxHeight && !open(x, y + i, z); i++) t = y + i;
    wallTopOf[k] = t; return t;
}
void RoomFlood::start(const RoomWorld& w, I3 s, const RoomRules& rules) {
    *this = RoomFlood{}; W = w; R = rules; seed0 = s;
    for (int i = 0; i < (int)W.doors.size(); i++) {
        const CookDoor& d = W.doors[i]; I3 a = chunkOf(d.lo.x, d.lo.y, d.lo.z), b = chunkOf(d.hi.x, d.hi.y, d.hi.z);
        for (int y = a.y; y <= b.y; y++) for (int z = a.z; z <= b.z; z++) for (int x = a.x; x <= b.x; x++) doorsByChunk[I3{ x, y, z }].push_back(i);
    }
    if (!W.chunkAt || !W.chunkAt(chunkOf(s.x, s.y, s.z))) { finish(Unknown); return; }
    if (!open(s.x, s.y, s.z)) {   // (in a block: the nearest air, a block or two away)
        bool found = false; int best = INT_MAX; I3 bs = s;
        for (int dy = -2; dy <= 2; dy++) for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
            int d2 = dx * dx + dy * dy + dz * dz; if (d2 >= best || !open(s.x + dx, s.y + dy, s.z + dz)) continue;
            uint8_t m; int dr; if (kind(s.x + dx, s.y + dy, s.z + dz, m, dr) == 3) continue;
            best = d2; bs = { s.x + dx, s.y + dy, s.z + dz }; found = true;
        }
        if (!found) { finish(Solid); return; }
        seed0 = s = bs;
    }
    shape.partial = false;
    phase = P3D; res = Running; mark(s.x, s.y, s.z); shape.add(s.x, s.y, s.z);
}
bool RoomFlood::beginFloor() {   // the air goes up into the sky: try the floor instead (a room without a roof)
    seen.clear(); q.clear(); qi = 0; count = 0; touched.clear(); shape.clear(); colTop.clear(); wallTop = INT_MIN; floorLo = INT_MAX;
    I3 s = seed0; int k = 0;
    while (k < 64 && openOrVoid(s.x, s.y - 1, s.z)) { s.y--; k++; }   // down to the ground
    if (k >= 64 || shape.partial || !inFloorLayer(s.x, s.y, s.z)) return false;
    phase = PFloor; mark(s.x, s.y, s.z); shape.add(s.x, s.y, s.z);
    auto& t = colTop[colKey(s.x, s.z)]; t = s.y; floorLo = s.y;
    return true;
}
void RoomFlood::finish(Result r) {
    res = r; phase = PDone;
    if (r == Room && !shape.roofless) { from.clear(); leak = 0; }   // (no roof: where its air got out is kept, to show)
    if (r == Room) { shape.doors.assign(touched.begin(), touched.end()); std::sort(shape.doors.begin(), shape.doors.end()); for (int i : shape.doors) shape.doorObjs.push_back(W.doors[i].obj); }
    else { if (r != Outside) seen.clear(); shape.clear(); }
}
std::vector<I3> RoomFlood::leakPath() const {
    std::vector<I3> p; if (!leak) return p;
    I3 c = leakAt;
    for (int n = 0; n < 200000; n++) {
        p.push_back(c);
        auto it = from.find(chunkOf(c.x, c.y, c.z)); if (it == from.end()) break;
        uint8_t d = it->second[RoomBits::at(floorMod(c.x, CS), floorMod(c.y, CS), floorMod(c.z, CS))]; if (!d) break;
        c = { c.x - N6[d - 1][0], c.y - N6[d - 1][1], c.z - N6[d - 1][2] };
    }
    std::reverse(p.begin(), p.end()); return p;
}
std::vector<I3> RoomFlood::leakGaps(const std::vector<I3>& p) {
    std::vector<I3> g;
    for (size_t i = 1; i + 1 < p.size() && g.size() < 8; i++) {
        I3 a = p[i - 1], c = p[i];
        bool vert = a.y != c.y || p[i + 1].y != c.y;
        bool hole;
        if (vert) hole = !open(c.x + 1, c.y, c.z) && !open(c.x - 1, c.y, c.z) && !open(c.x, c.y, c.z + 1) && !open(c.x, c.y, c.z - 1);   // through a floor / ceiling
        else if (a.x != c.x) hole = !open(c.x, c.y, c.z + 1) && !open(c.x, c.y, c.z - 1) && (!open(c.x, c.y + 1, c.z) || !open(c.x, c.y - 1, c.z));   // through a wall
        else hole = !open(c.x + 1, c.y, c.z) && !open(c.x - 1, c.y, c.z) && (!open(c.x, c.y + 1, c.z) || !open(c.x, c.y - 1, c.z));
        if (hole && (g.empty() || std::abs(g.back().x - c.x) + std::abs(g.back().y - c.y) + std::abs(g.back().z - c.z) > 1)) g.push_back(c);
    }
    return g;
}
std::vector<I3> RoomFlood::outsideCells() const {
    std::vector<I3> v; if (res != Outside) return v;
    for (const auto& kv : seen) for (int i = 0; i < CS3; i++) if (kv.second.has(i)) v.push_back({ kv.first.x * CS + i % CS, kv.first.y * CS + i / (CS * CS), kv.first.z * CS + (i / CS) % CS });
    return v;
}
RoomFlood::Result RoomFlood::step(size_t budget) {
    if (phase == PDone) return res;
    lastHave = false;   // (the chunks may have changed between steps)
    size_t n = 0;
    if (phase == P3D) {
        bool escaped = false;
        while (qi < q.size() && n < budget) {
            I3 c = q[qi++]; n++;
            if (sky(c.x, c.y, c.z)) { escaped = true; leak = 1; leakAt = c; break; }
            for (int dir = 0; dir < 6; dir++) {
                int nx = c.x + N6[dir][0], ny = c.y + N6[dir][1], nz = c.z + N6[dir][2];
                auto sit = seen.find(chunkOf(nx, ny, nz));
                if (sit != seen.end() && sit->second.has(RoomBits::at(floorMod(nx, CS), floorMod(ny, CS), floorMod(nz, CS)))) continue;
                uint8_t m; int door; uint8_t k = kind(nx, ny, nz, m, door);
                if (k == 3) { touched.insert(door); continue; }
                if (k == 2) continue;
                if (!passes(c.x, c.y, c.z, dir, nx, ny, nz)) continue;
                mark(nx, ny, nz); shape.add(nx, ny, nz);
                from[chunkOf(nx, ny, nz)][RoomBits::at(floorMod(nx, CS), floorMod(ny, CS), floorMod(nz, CS))] = (uint8_t)(1 + dir);
            }
            if (shape.partial) { escaped = true; leak = 4; leakAt = c; break; }   // (it reached chunks that are not loaded: no roof over it here, at least)
            if (count > R.maxCells || hi.x - lo.x > R.maxSpan || hi.y - lo.y > R.maxSpan || hi.z - lo.z > R.maxSpan) { escaped = true; leak = 2; leakAt = c; break; }
        }
        if (escaped) { if (!beginFloor()) { finish(Outside); seen.clear(); return res; } return Running; }
        if (qi >= q.size()) { shape.roofless = false; finish(Room); }
        return res;
    }
    if (phase == PFloor) {
        while (qi < q.size() && n < budget) {
            I3 c = q[qi++]; n++;
            for (int dir = 0; dir < 6; dir++) {
                int nx = c.x + N6[dir][0], ny = c.y + N6[dir][1], nz = c.z + N6[dir][2];
                auto sit = seen.find(chunkOf(nx, ny, nz));
                if (sit != seen.end() && sit->second.has(RoomBits::at(floorMod(nx, CS), floorMod(ny, CS), floorMod(nz, CS)))) continue;
                uint8_t m; int door; uint8_t k = kind(nx, ny, nz, m, door);
                if (k == 3) { touched.insert(door); continue; }
                if (k == 2) { if (dir != 2 && dir != 3) wallTop = std::max(wallTop, topOfWall(nx, ny, nz)); continue; }
                if (!passes(c.x, c.y, c.z, dir, nx, ny, nz)) continue;
                if (!inFloorLayer(nx, ny, nz)) {
                    if (dir == 2 || dir == 3) continue;   // (up out of the floor's layer: no)
                    int gy = ny - 1, k2 = 0; while (k2 < 64 && openOrVoid(nx, gy, nz)) { gy--; k2++; }   // a drop: a pit (it goes on at its bottom) or the edge of the world (the outside)
                    if (k2 >= 64) { finish(Outside); return res; }
                    ny = gy + 1;
                    auto s2 = seen.find(chunkOf(nx, ny, nz));
                    if (s2 != seen.end() && s2->second.has(RoomBits::at(floorMod(nx, CS), floorMod(ny, CS), floorMod(nz, CS)))) continue;
                }
                mark(nx, ny, nz); shape.add(nx, ny, nz);
                auto ct = colTop.find(colKey(nx, nz)); if (ct == colTop.end()) colTop[colKey(nx, nz)] = ny; else ct->second = std::max(ct->second, ny);
                floorLo = std::min(floorLo, ny);
            }
            if (shape.partial) { finish(Outside); return res; }   // (it goes on into chunks that are not loaded: as big as the world around you)
            if (count > R.maxFloorCells || hi.x - lo.x > R.maxSpan || hi.z - lo.z > R.maxSpan) { finish(Outside); return res; }
        }
        if (qi >= q.size()) {   // closed in: up to its walls' top
            phase = PUp; cols.clear(); for (const auto& kv : colTop) cols.push_back(kv.first); std::sort(cols.begin(), cols.end()); ci = 0;
            if (wallTop == INT_MIN) wallTop = floorLo + 4;
            wallTop = std::min(wallTop, floorLo + R.maxHeight);
        }
        return Running;
    }
    if (phase == PUp) {
        while (ci < cols.size() && n < budget) {
            int64_t k = cols[ci++]; int x = (int)(k >> 32), z = (int)(uint32_t)(k & 0xFFFFFFFF);
            for (int y = colTop[k] + 1; y <= wallTop; y++) { n++; uint8_t m; int door; uint8_t kk = kind(x, y, z, m, door); if (kk == 3) { touched.insert(door); break; } if (kk == 2 || (kk == 4 && (m >> 3 & 1))) break; shape.add(x, y, z); }
        }
        if (ci >= cols.size()) { shape.roofless = true; seen.clear(); finish(Room); }
        return res;
    }
    return res;
}

// ------------------------------------------------------------------ saved rooms
bool RoomDef::inBoxes(int x, int y, int z) const {
    for (const auto& b : boxes) if (x >= b.first.x && x <= b.second.x && y >= b.first.y && y <= b.second.y && z >= b.first.z && z <= b.second.z) return true;
    return false;
}
RoomDef* RoomList::find(uint32_t id) { for (RoomDef& r : rooms) if (r.id == id) return &r; return nullptr; }
const RoomDef* RoomList::find(uint32_t id) const { for (const RoomDef& r : rooms) if (r.id == id) return &r; return nullptr; }
uint32_t roomColor(uint32_t id) {
    static const uint32_t pal[] = { 0x5AC8FF, 0xFFB050, 0x7CE07C, 0xE07CE0, 0xFF6A6A, 0x6AE0D0, 0xD0D060, 0x9A8CFF, 0xFF8CC0, 0x8CFFB0 };
    return pal[id % (sizeof pal / sizeof pal[0])];
}
bool saveRooms(const std::string& path, const RoomList& l) {
    std::ofstream f(path); if (!f) return false;
    f << "rooms 1\nnext " << l.nextId << "\nband " << l.band << "\n";
    for (const RoomDef& r : l.rooms) {
        const AmbientSettings& A = r.amb;
        f << "room " << r.id << " " << escText(r.name) << " " << r.color << " " << r.anchor.x << " " << r.anchor.y << " " << r.anchor.z << " " << r.showName << " " << r.ambOn
          << " " << A.up << " " << A.down << " " << A.north << " " << A.south << " " << A.east << " " << A.west << " " << A.strength << " " << A.mix << " " << A.fog << " " << A.fogAmount
          << " " << A.reach << " " << A.blend << " " << escText(A.look) << " " << r.musicMode << " " << r.musicFade << " " << escText(soundUseText(r.music)) << " " << escText(A.preset) << "\n";
        for (const auto& b : r.boxes) f << "box " << b.first.x << " " << b.first.y << " " << b.first.z << " " << b.second.x << " " << b.second.y << " " << b.second.z << "\n";
    }
    return (bool)f;
}
bool loadRooms(const std::string& path, RoomList& l) {
    std::ifstream f(path); if (!f) return false;
    l = RoomList{}; std::string line;
    while (std::getline(f, line)) {
        std::istringstream s(line); std::string k; s >> k;
        if (k == "next") s >> l.nextId;
        else if (k == "band") { s >> l.band; l.band = std::clamp(l.band, 1, 8); }
        else if (k == "room") {
            RoomDef r; AmbientSettings& A = r.amb; std::string n, lk, mu;
            if (!(s >> r.id >> n >> r.color >> r.anchor.x >> r.anchor.y >> r.anchor.z >> r.showName >> r.ambOn >> A.up >> A.down >> A.north >> A.south >> A.east >> A.west
                    >> A.strength >> A.mix >> A.fog >> A.fogAmount >> A.reach >> A.blend >> lk >> r.musicMode >> r.musicFade >> mu)) continue;
            r.name = unescText(n); A.look = unescText(lk); soundUseFrom(unescText(mu), r.music); r.musicMode = std::clamp(r.musicMode, 0, 2);
            { std::string pr; if (s >> pr) A.preset = unescText(pr); }
            l.rooms.push_back(r); l.nextId = std::max(l.nextId, r.id + 1);
        }
        else if (k == "box" && !l.rooms.empty()) { I3 a, b; if (s >> a.x >> a.y >> a.z >> b.x >> b.y >> b.z) l.rooms.back().boxes.push_back({ a, b }); }
    }
    return true;
}

// ------------------------------------------------------------------ the rooms found
uint32_t RoomSet::roomAt(int x, int y, int z) const {
    for (const RoomDef& r : list.rooms) if (r.marked() && r.inBoxes(x, y, z)) { auto it = live.find(r.id); if (it != live.end() && it->second.shape.has(x, y, z)) return r.id; }
    auto it = byChunk.find(chunkOf(x, y, z)); if (it == byChunk.end()) return 0;
    for (uint32_t id : it->second) { auto l = live.find(id); if (l != live.end() && l->second.shape.has(x, y, z)) return id; }
    return 0;
}
std::string RoomSet::busyText() const {
    if (!jobOn) return "";
    if (jobFor) { const RoomDef* d = list.find(jobFor); return "Checking " + (d ? d->label() : std::string("a room")) + " ..."; }
    return "Looking for a new room ...";
}
std::vector<I3> RoomSet::doorSeeds(const std::vector<CookDoor>& doors) {
    std::vector<I3> v;
    for (const CookDoor& d : doors) {
        int sx = d.hi.x - d.lo.x + 1, sz = d.hi.z - d.lo.z + 1, cx = (d.lo.x + d.hi.x) / 2, cz = (d.lo.z + d.hi.z) / 2, y = d.lo.y;
        if (sx <= sz) { v.push_back({ d.lo.x - 1, y, cz }); v.push_back({ d.hi.x + 1, y, cz }); }   // thin along x: the rooms are to its east and west
        if (sz <= sx) { v.push_back({ cx, y, d.lo.z - 1 }); v.push_back({ cx, y, d.hi.z + 1 }); }
    }
    return v;
}
bool RoomSet::isOutside(I3 c) const {
    auto it = outside.find(chunkOf(c.x, c.y, c.z)); return it != outside.end() && it->second.has(RoomBits::at(floorMod(c.x, CS), floorMod(c.y, CS), floorMod(c.z, CS)));
}
uint64_t RoomSet::doorsSig(const RoomWorld& w) const {
    uint64_t h = 1469598103934665603ull;
    for (const CookDoor& d : w.doors) for (int v : { d.lo.x, d.lo.y, d.lo.z, d.hi.x, d.hi.y, d.hi.z }) h = (h ^ (uint64_t)(uint32_t)v) * 1099511628211ull;
    for (const RoomDef& r : list.rooms) for (const auto& b : r.boxes) for (int v : { b.first.x, b.first.y, b.first.z, b.second.x, b.second.y, b.second.z }) h = (h ^ (uint64_t)(uint32_t)v) * 1099511628211ull;
    return h;
}
uint64_t RoomSet::sigOf(const RoomShape& s, const std::function<uint32_t(I3)>& chunkVer, uint64_t dsig) const {
    uint64_t h = dsig; if (s.empty()) return h;
    I3 a = chunkOf(s.lo.x - 1, s.lo.y - 1, s.lo.z - 1), b = chunkOf(s.hi.x + 1, s.hi.y + 1, s.hi.z + 1);
    for (int y = a.y; y <= b.y; y++) for (int z = a.z; z <= b.z; z++) for (int x = a.x; x <= b.x; x++) h = (h ^ chunkVer({ x, y, z })) * 1099511628211ull ^ (uint64_t)(x * 73856093 ^ y * 19349663 ^ z * 83492791);
    return h;
}
void RoomSet::index() {
    byChunk.clear();
    for (const auto& kv : live) if (!kv.second.shape.empty()) for (const auto& b : kv.second.shape.bits) byChunk[b.first].push_back(kv.first);
    for (auto& kv : byChunk) std::sort(kv.second.begin(), kv.second.end(), [&](uint32_t a, uint32_t b) {   // (marked rooms first)
        const RoomDef* A = list.find(a); const RoomDef* B = list.find(b); bool ma = A && A->marked(), mb = B && B->marked(); return ma != mb ? ma : a < b; });
}
uint64_t RoomSet::sigFor(const RoomDef& d, const RoomShape& s, const std::function<uint32_t(I3)>& chunkVer, uint64_t dsig) const {
    if (!s.empty()) return sigOf(s, chunkVer, dsig);
    RoomShape probe; probe.add(d.anchor.x, d.anchor.y, d.anchor.z);   // (no blocks: the chunks around its spot)
    probe.lo = { d.anchor.x - CS, d.anchor.y - CS, d.anchor.z - CS }; probe.hi = { d.anchor.x + CS, d.anchor.y + CS, d.anchor.z + CS };
    return sigOf(probe, chunkVer, dsig);
}
void RoomSet::apply(uint32_t id, RoomFlood::Result r, RoomShape&& s, const std::function<uint32_t(I3)>& chunkVer, uint64_t dsig) {
    Live& L = live[id]; L.searched = true; L.retry = 0;
    size_t before = L.shape.cells; I3 blo = L.shape.lo, bhi = L.shape.hi; bool wasOpen = L.open; uint32_t wasJoined = L.joined;
    L.open = false; L.joined = 0;
    if (r == RoomFlood::Room) {
        L.shape = std::move(s);
        for (RoomDef& e : list.rooms) {   // another room's spot in it: they are one room now (the older one keeps it)
            if (e.id == id || e.marked() || !L.shape.has(e.anchor.x, e.anchor.y, e.anchor.z)) continue;
            if (e.id < id) { L.joined = e.id; L.shape.clear(); Live& E = live[e.id]; if (E.shape.empty()) E.searched = false; break; }
            Live& E = live[e.id]; E.shape.clear(); E.joined = id; E.open = false; E.searched = true;
        }
        for (auto& kv : live) if (kv.second.joined == id && kv.first != id) { const RoomDef* e = list.find(kv.first); if (!e || L.shape.empty() || !L.shape.has(e->anchor.x, e->anchor.y, e->anchor.z)) { kv.second.joined = 0; kv.second.searched = false; } }
    } else {
        L.shape.clear(); L.open = r == RoomFlood::Outside || r == RoomFlood::Solid;
        if (r == RoomFlood::Unknown) L.retry = 8;
    }
    if (const RoomDef* d = list.find(id)) L.sig = sigFor(*d, L.shape, chunkVer, dsig);
    if (L.shape.cells != before || !(L.shape.lo == blo) || !(L.shape.hi == bhi) || L.open != wasOpen || L.joined != wasJoined) generation++;
    index();
}

static bool roomOpenAt(const RoomWorld& w, int x, int y, int z) {
    for (const CookDoor& d : w.doors) if (x >= d.lo.x && x <= d.hi.x && y >= d.lo.y && y <= d.hi.y && z >= d.lo.z && z <= d.hi.z) return false;
    const Chunk* ch = w.chunkAt ? w.chunkAt(chunkOf(x, y, z)) : nullptr; if (!ch) return false;
    if (w.through && w.through(x, y, z)) return true;
    BlockId v = ch->get(floorMod(x, CS), floorMod(y, CS), floorMod(z, CS)); if (v == AIR) return true;
    const BlockDef& d = w.reg->get(v); if (d.liquid || !d.solid) return true;
    int sh = resolveShape(v, d); if (sh == Shapes::FULL) return false;
    const ShapeVariant& sv = Shapes::get().variant(sh, rotOf(v)); for (int f = 0; f < 6; f++) if (!sv.full[f]) return true;
    return false;
}

void RoomSet::tick(const RoomWorld& w, const std::vector<I3>& seeds, const std::function<uint32_t(I3)>& chunkVer, bool checkNow, size_t budget) {
    uint64_t dsig = doorsSig(w);
    if (jobOn) {
        RoomFlood::Result r = job.step(budget);
        if (r == RoomFlood::Running) return;
        jobOn = false;
        if (jobFor) {
            if (list.find(jobFor)) {
                std::vector<I3> lp = job.leak ? job.leakPath() : std::vector<I3>{}; std::vector<I3> gp = lp.empty() ? std::vector<I3>{} : job.leakGaps(lp);
                bool roofless = r == RoomFlood::Room && job.shape.roofless;
                apply(jobFor, r, std::move(job.shape), chunkVer, dsig);
                Live& L = live[jobFor]; bool keep = r == RoomFlood::Outside || roofless;
                L.leak = keep ? job.leak : 0; L.leakAt = job.leakAt; L.leakPath = keep ? lp : std::vector<I3>{}; L.gaps = keep ? gp : std::vector<I3>{};
            }
        }
        else if (r == RoomFlood::Room) {
            uint32_t owner = 0;
            for (const RoomDef& e : list.rooms) if (!e.marked() && job.shape.has(e.anchor.x, e.anchor.y, e.anchor.z)) { owner = e.id; break; }
            if (!owner) {
                RoomDef d; d.id = list.nextId++; d.anchor = job.seed(); d.color = roomColor(d.id); d.amb = ambientPreset(0);
                list.rooms.push_back(d); listChanged = true; owner = d.id;
            }
            if (!job.shape.has(jobSeed.x, jobSeed.y, jobSeed.z)) failed.insert(jobSeed);   // (it started next to it: no need to ask again)
            bool roofless = job.shape.roofless; std::vector<I3> lp = roofless && job.leak ? job.leakPath() : std::vector<I3>{}; std::vector<I3> gp = lp.empty() ? lp : job.leakGaps(lp);
            apply(owner, r, std::move(job.shape), chunkVer, dsig);
            if (roofless) { Live& L = live[owner]; L.leak = job.leak; L.leakAt = job.leakAt; L.leakPath = lp; L.gaps = gp; }
        }
        else if (r == RoomFlood::Outside) {
            if (job.leak) { Leak lk; lk.seed = jobSeed; lk.leak = job.leak; lk.at = job.leakAt; lk.path = job.leakPath(); lk.gaps = job.leakGaps(lk.path);
                leaks.erase(std::remove_if(leaks.begin(), leaks.end(), [&](const Leak& q) { return q.seed == jobSeed; }), leaks.end()); leaks.push_back(std::move(lk)); }
            for (const I3& c : job.outsideCells()) outside[chunkOf(c.x, c.y, c.z)].set(RoomBits::at(floorMod(c.x, CS), floorMod(c.y, CS), floorMod(c.z, CS))); failed.insert(jobSeed); }
        else failed.insert(jobSeed);
        return;
    }
    // what changed: rooms to search again
    for (auto it = live.begin(); it != live.end();) { if (!list.find(it->first)) { it = live.erase(it); generation++; index(); } else ++it; }
    if (checkNow) {
        for (RoomDef& d : list.rooms) {
            if (d.marked()) continue;
            auto it = live.find(d.id); if (it == live.end() || !it->second.searched) continue;
            Live& L = it->second;
            if (L.joined) { const Live* o = liveOf(L.joined); if (!o || o->shape.empty()) L.searched = false; continue; }
            if (L.retry > 0) { if (--L.retry == 0) L.searched = false; continue; }
            if (sigFor(d, L.shape, chunkVer, dsig) != L.sig) L.searched = false;
        }
    }
    // marked rooms: their boxes' air (at once)
    for (RoomDef& d : list.rooms) {
        if (!d.marked()) continue;
        Live& L = live[d.id];
        uint64_t s = dsig; for (const auto& b : d.boxes) { RoomShape bb; bb.add(b.first.x, b.first.y, b.first.z); bb.lo = b.first; bb.hi = b.second; s ^= sigOf(bb, chunkVer, 0x51ED); }
        if (L.searched && L.sig == s) continue;
        RoomShape sh; size_t n = 0;
        for (const auto& b : d.boxes) for (int y = b.first.y; y <= b.second.y; y++) for (int z = b.first.z; z <= b.second.z; z++) for (int x = b.first.x; x <= b.second.x; x++) {
            if (++n > 4000000) break;
            if (roomOpenAt(w, x, y, z)) sh.add(x, y, z);
        }
        for (int i = 0; i < (int)w.doors.size(); i++) {   // the doors at its edges
            const CookDoor& dr = w.doors[i]; bool touch = false;
            for (const auto& b : d.boxes) if (dr.hi.x >= b.first.x - 1 && dr.lo.x <= b.second.x + 1 && dr.hi.y >= b.first.y - 1 && dr.lo.y <= b.second.y + 1 && dr.hi.z >= b.first.z - 1 && dr.lo.z <= b.second.z + 1) touch = true;
            if (touch) { sh.doors.push_back(i); sh.doorObjs.push_back(dr.obj); }
        }
        size_t before = L.shape.cells; L.shape = std::move(sh); L.searched = true; L.sig = s; L.open = false; L.joined = 0;
        if (!d.boxes.empty()) { I3 c{ (d.boxes[0].first.x + d.boxes[0].second.x) / 2, (d.boxes[0].first.y + d.boxes[0].second.y) / 2, (d.boxes[0].first.z + d.boxes[0].second.z) / 2 }; if (!(d.anchor == c)) { d.anchor = c; listChanged = true; } }
        if (L.shape.cells != before) generation++;
        index();
    }
    // the next room to search again
    for (RoomDef& d : list.rooms) {
        if (d.marked()) continue;
        Live& L = live[d.id]; if (L.searched) continue;
        if (!roomOpenAt(w, d.anchor.x, d.anchor.y, d.anchor.z)) {   // its spot was built over: another block of its air
            bool moved = false;
            if (!L.shape.empty()) L.shape.forEach([&](int x, int y, int z) { if (!moved && roomOpenAt(w, x, y, z)) { d.anchor = { x, y, z }; moved = true; } });
            if (moved) listChanged = true;
        }
        jobFor = d.id; job.start(w, d.anchor, RoomRules{ 1500000, 400, 96, list.band }); jobOn = true;
        if (job.result() != RoomFlood::Running) { jobOn = false; apply(d.id, job.result(), std::move(job.shape), chunkVer, dsig); }
        return;
    }
    // new rooms: beside the doors, where you are
    for (const I3& s : seeds) {
        if (roomAt(s.x, s.y, s.z) || isOutside(s) || failed.count(s)) continue;
        bool known = false;   // (a room whose spot is this, being searched)
        for (const RoomDef& d : list.rooms) if (d.anchor == s) { known = true; break; }
        if (known) continue;
        jobFor = 0; jobSeed = s; job.start(w, s, RoomRules{ 1500000, 400, 96, list.band }); jobOn = true;
        if (job.result() != RoomFlood::Running) { jobOn = false; failed.insert(s); continue; }
        return;
    }
}

} // namespace mc
