#include "objects.h"
#include "../world/shapes.h"
#include "visors.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
namespace mc {

// ------------------------------------------------------------------ block models
bool BlockModel::save(const std::string& path) const {
    std::ofstream f(path); if (!f) return false;
    f << "model 1\nname " << name << "\nsize " << sx << " " << sy << " " << sz << "\npivot " << pivot.x << " " << pivot.y << " " << pivot.z << "\ndata";
    for (size_t i = 0; i < cells.size();) {   // run-length: count*id
        size_t j = i; while (j < cells.size() && cells[j] == cells[i]) j++;
        f << " " << (j - i) << "*" << cells[i]; i = j;
    }
    f << "\n"; return (bool)f;
}
bool BlockModel::load(const std::string& path) {
    std::ifstream f(path); if (!f) return false;
    std::string k; int ver = 0;
    while (f >> k) {
        if (k == "model") f >> ver;
        else if (k == "name") { f >> std::ws; std::getline(f, name); }
        else if (k == "size") { int x, y, z; f >> x >> y >> z; V3 pv = pivot; resize(x, y, z); pivot = pv; }
        else if (k == "pivot") f >> pivot.x >> pivot.y >> pivot.z;
        else if (k == "data") {
            size_t i = 0; std::string run;
            while (i < cells.size() && f >> run) {
                size_t st = run.find('*'); if (st == std::string::npos) break;
                size_t n = std::stoul(run.substr(0, st)); BlockId v = (BlockId)std::stoul(run.substr(st + 1));
                for (size_t k2 = 0; k2 < n && i < cells.size(); k2++) cells[i++] = v;
            }
        } else { std::string rest; std::getline(f, rest); }
    }
    return ver >= 1 && sx > 0;
}

BlockModel modelFromWorld(const World& w, I3 lo, I3 hi, const std::string& name, bool trim, I3* corner) {
    I3 a{ std::min(lo.x, hi.x), std::min(lo.y, hi.y), std::min(lo.z, hi.z) }, b{ std::max(lo.x, hi.x), std::max(lo.y, hi.y), std::max(lo.z, hi.z) };
    if (trim) {   // shrink to the blocks that are there
        I3 mn{ b.x + 1, b.y + 1, b.z + 1 }, mx{ a.x - 1, a.y - 1, a.z - 1 };
        for (int y = a.y; y <= b.y; y++) for (int z = a.z; z <= b.z; z++) for (int x = a.x; x <= b.x; x++)
            if (w.get(x, y, z) != AIR) { mn = { std::min(mn.x, x), std::min(mn.y, y), std::min(mn.z, z) }; mx = { std::max(mx.x, x), std::max(mx.y, y), std::max(mx.z, z) }; }
        if (mx.x >= mn.x) { a = mn; b = mx; }
    }
    if (corner) *corner = a;
    BlockModel m; m.name = name; m.resize(b.x - a.x + 1, b.y - a.y + 1, b.z - a.z + 1);
    for (int y = 0; y < m.sy; y++) for (int z = 0; z < m.sz; z++) for (int x = 0; x < m.sx; x++) m.put(x, y, z, w.get(a.x + x, a.y + y, a.z + z));
    return m;
}

std::vector<Box3> modelBoxes(const BlockModel& m, const BlockRegistry& reg) {
    std::vector<Box3> out;
    auto full = [&](int x, int y, int z) {
        BlockId v = m.at(x, y, z); if (v == AIR || !reg.has(v)) return false;
        const BlockDef& d = reg.get(v); return d.solid && resolveShape(v, d) == Shapes::FULL;
    };
    // whole blocks: grow boxes along x, then z, then y (greedy), each block used once
    std::vector<uint8_t> used(m.cells.size(), 0);
    auto idx = [&](int x, int y, int z) { return ((size_t)y * m.sz + z) * m.sx + x; };
    for (int y = 0; y < m.sy; y++) for (int z = 0; z < m.sz; z++) for (int x = 0; x < m.sx; x++) {
        if (used[idx(x, y, z)] || !full(x, y, z)) continue;
        int x2 = x; while (x2 + 1 < m.sx && !used[idx(x2 + 1, y, z)] && full(x2 + 1, y, z)) x2++;
        auto rowOk = [&](int yy, int zz) { for (int k = x; k <= x2; k++) if (used[idx(k, yy, zz)] || !full(k, yy, zz)) return false; return true; };
        int z2 = z; while (z2 + 1 < m.sz && rowOk(y, z2 + 1)) z2++;
        auto layerOk = [&](int yy) { for (int zz = z; zz <= z2; zz++) if (!rowOk(yy, zz)) return false; return true; };
        int y2 = y; while (y2 + 1 < m.sy && layerOk(y2 + 1)) y2++;
        for (int yy = y; yy <= y2; yy++) for (int zz = z; zz <= z2; zz++) for (int k = x; k <= x2; k++) used[idx(k, yy, zz)] = 1;
        out.push_back({ { (double)x, (double)y, (double)z }, { x2 + 1.0, y2 + 1.0, z2 + 1.0 } });
    }
    // shaped blocks (slopes, slabs ...): their own small boxes
    for (int y = 0; y < m.sy; y++) for (int z = 0; z < m.sz; z++) for (int x = 0; x < m.sx; x++) {
        BlockId v = m.at(x, y, z); if (v == AIR || !reg.has(v) || full(x, y, z)) continue;
        const BlockDef& d = reg.get(v); if (!d.solid) continue;
        for (const SBox& b : Shapes::get().variant(resolveShape(v, d), rotOf(v)).boxes)
            out.push_back({ { x + (double)b.lo[0], y + (double)b.lo[1], z + (double)b.lo[2] }, { x + (double)b.hi[0], y + (double)b.hi[1], z + (double)b.hi[2] } });
    }
    return out;
}

const char* collideName(Collide c) { return c == Collide::Exact ? "Exact (from its blocks)" : c == Collide::OneBox ? "One box" : "None (walk through)"; }

// ------------------------------------------------------------------ placed objects
const char* kindName(ObjKind k) {
    switch (k) { case ObjKind::Door: return "Door"; case ObjKind::Platform: return "Platform"; case ObjKind::Pickup: return "Pickup"; case ObjKind::Checkpoint: return "Checkpoint";
    case ObjKind::Teleporter: return "Teleporter"; case ObjKind::Trigger: return "Trigger zone"; case ObjKind::Light: return "Light"; case ObjKind::Particles: return "Particles";
    case ObjKind::MapCamera: return "Map camera"; case ObjKind::MapStation: return "Map Station"; case ObjKind::Enemy: return "Enemy"; case ObjKind::SaveStation: return "Save Station"; case ObjKind::Ambient: return "Ambient light"; case ObjKind::GrapplePoint: return "Grapple point"; default: return "Prop"; }
}
const char* pickupName(PickupType t) { return t == PickupType::Key ? "Key" : t == PickupType::Collectible ? "Collectible (counted)" : "Health"; }
const char* triggerName(TriggerAction a) { return a == TriggerAction::RoomEdge ? "Room edge (closes a room)" : a == TriggerAction::Kill ? "Kill (traps, acid ...)" : a == TriggerAction::Heal ? "Heal" : a == TriggerAction::Checkpoint ? "Checkpoint" : "Show a message"; }
bool PlayInventory::collect(const WorldObject& o, float& health, float maxHealth, std::string& said) {
    if (o.kind != ObjKind::Pickup || has(o.id)) return false;
    const PickupSettings& p = o.pickup;
    switch (p.type) {
    case PickupType::Health: if (health >= maxHealth) return false; health = std::min(maxHealth, health + p.amount); said = "+" + std::to_string((int)p.amount) + " health"; break;
    case PickupType::Key: keys.insert(p.name); said = "Got the " + p.name; break;
    case PickupType::Collectible: { int n = ++counts[p.name]; said = p.name + " " + std::to_string(n); break; }
    }
    taken[o.id] = p.respawn > 0 ? p.respawn : -1;
    return true;
}
void PlayInventory::tick(float dt) {
    for (auto it = taken.begin(); it != taken.end();) { if (it->second > 0 && (it->second -= dt) <= 0) it = taken.erase(it); else ++it; }
}
V3 objectStretch(const WorldObject& o, const ModelFrame& m) {
    if (o.kind == ObjKind::Trigger && m.sx > 0 && m.sy > 0 && m.sz > 0) return { o.size.x / m.sx, o.size.y / m.sy, o.size.z / m.sz };   // a zone: exactly its box
    if (o.kind != ObjKind::Door || m.sx <= 0 || m.sy <= 0) return { o.scale, o.scale, o.scale };
    double x = o.door.w / m.sx, y = o.door.h / m.sy;
    return { x, y, std::min(x, y) };   // depth: as thick as the narrower stretch keeps it
}
V3 objectPoint(const WorldObject& o, const ModelFrame& m, V3 l) {
    double a = o.yaw * 3.14159265358979 / 180, c = std::cos(a), s = std::sin(a);
    V3 k = objectStretch(o, m), d = l - m.pivot;
    V3 p{ d.x * k.x, d.y * k.y, d.z * k.z };
    return { o.pos.x + p.x * c + p.z * s, o.pos.y + p.y, o.pos.z - p.x * s + p.z * c };
}
static Box3 turnedBox(const WorldObject& o, const ModelFrame& m, const Box3& b) {   // the box around a turned box
    Box3 r{ { 1e30, 1e30, 1e30 }, { -1e30, -1e30, -1e30 } };
    for (int i = 0; i < 8; i++) {
        V3 p = objectPoint(o, m, { (i & 1) ? b.hi.x : b.lo.x, (i & 2) ? b.hi.y : b.lo.y, (i & 4) ? b.hi.z : b.lo.z });
        r.lo = { std::min(r.lo.x, p.x), std::min(r.lo.y, p.y), std::min(r.lo.z, p.z) }; r.hi = { std::max(r.hi.x, p.x), std::max(r.hi.y, p.y), std::max(r.hi.z, p.z) };
    }
    return r;
}
Box3 objectBounds(const WorldObject& o, const ModelFrame& m) { return turnedBox(o, m, { { 0, 0, 0 }, { m.sx, m.sy, m.sz } }); }
std::vector<Box3> objectBoxes(const WorldObject& o, const ModelFrame& m, const std::vector<Box3>& mb) {
    std::vector<Box3> out;
    if (o.collide == Collide::None || mb.empty()) return out;
    if (o.collide == Collide::OneBox) {
        Box3 all = mb[0]; for (const Box3& b : mb) { all.lo = { std::min(all.lo.x, b.lo.x), std::min(all.lo.y, b.lo.y), std::min(all.lo.z, b.lo.z) }; all.hi = { std::max(all.hi.x, b.hi.x), std::max(all.hi.y, b.hi.y), std::max(all.hi.z, b.hi.z) }; }
        out.push_back(turnedBox(o, m, all)); return out;
    }
    for (const Box3& b : mb) out.push_back(turnedBox(o, m, b));
    return out;
}
bool rayBox(V3 o, V3 d, const Box3& b, double& t) {
    double t0 = 0, t1 = 1e30; const double lo[3] = { b.lo.x, b.lo.y, b.lo.z }, hi[3] = { b.hi.x, b.hi.y, b.hi.z }, oo[3] = { o.x, o.y, o.z }, dd[3] = { d.x, d.y, d.z };
    for (int a = 0; a < 3; a++) {
        if (std::fabs(dd[a]) < 1e-12) { if (oo[a] < lo[a] || oo[a] > hi[a]) return false; continue; }
        double ta = (lo[a] - oo[a]) / dd[a], tb = (hi[a] - oo[a]) / dd[a]; if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta); t1 = std::min(t1, tb); if (t0 > t1) return false;
    }
    t = t0; return true;
}

// ------------------------------------------------------------------ doors
bool DoorRuntime::shoot(float u, float v) {
    switch (state) {
    case Closed: state = Dissolving; t = 0; hitU = u; hitV = v; return true;
    case Restoring: state = Dissolving; t = (1 - field) * dissolveTime; hitU = u; hitV = v; return true;   // carry on from how far it had come back
    case Closing: state = Opening; t = 0; return true;
    default: return false;
    }
}
void DoorRuntime::tick(float dt, bool near, float openLen, float closeLen, float stayOpen) {
    t += dt;
    switch (state) {
    case Closed: field = 1; clip = -1; break;
    case Dissolving: field = std::max(0.f, 1 - t / dissolveTime); clip = -1; if (t >= dissolveTime) { state = Opening; t = 0; } break;
    case Opening: field = 0; clip = 0; clipTime = std::min(t, openLen); if (t >= openLen) { state = Open; t = 0; } break;
    case Open: field = 0; clip = openLen > 0 ? 0 : -1; clipTime = openLen; if (near) t = 0; else if (t >= stayOpen) { state = Closing; t = 0; } break;
    case Closing: field = 0; clip = closeLen > 0 ? 1 : -1; clipTime = std::min(t, closeLen); if (t >= closeLen) { state = Restoring; t = 0; clip = -1; } break;
    case Restoring: field = std::min(1.f, t / restoreTime); clip = -1; if (t >= restoreTime) { state = Closed; t = 0; field = 1; } break;
    }
}
static double fieldZ(const WorldObject& o, const ModelFrame& m, const Box3& op, int side) {   // just outside the door, on its front or back
    double k = std::max(objectStretch(o, m).z, 1e-6), gap = std::max(0.01f, o.door.fieldGap) / k;   // never right on the door: that would flicker (two surfaces at the same depth)
    return side == 0 ? op.lo.z - gap : op.hi.z + gap;
}
Box3 fieldBox(const WorldObject& o, const ModelFrame& m, const Box3& op) {   // from the front field to the back one (at least 0.16 blocks thick)
    double k = std::max(objectStretch(o, m).z, 1e-6), z0 = fieldZ(o, m, op, 0), z1 = fieldZ(o, m, op, 1), zm = (z0 + z1) * 0.5, half = std::max((z1 - z0) * 0.5, 0.08 / k);
    return turnedBox(o, m, { { op.lo.x, op.lo.y, zm - half }, { op.hi.x, op.hi.y, zm + half } });
}
Box3 turnedBoxPublic(const WorldObject& o, const ModelFrame& m, const Box3& b) { return turnedBox(o, m, b); }
bool doorOpensFor(const DoorSettings& d, const std::string& weapon, bool missile) {
    if (d.openedBy.empty()) return true;                      // any shot
    if (d.openedBy == "Missile") return missile;              // missile doors
    return d.openedBy == weapon;                              // that beam only, or that kind of missile (shots from missiles carry the missile's name)
}
bool fieldPartOn(const DoorSettings& d, const std::string& n) {
    if (d.fieldParts.empty()) return true;
    std::string s = "," + d.fieldParts + ","; return s.find("," + n + ",") != std::string::npos;
}
void fieldQuad(const WorldObject& o, const ModelFrame& m, const Box3& op, V3& origin, V3& across, V3& up, int side) {
    double zm = fieldZ(o, m, op, side);
    origin = objectPoint(o, m, { op.lo.x, op.lo.y, zm });
    across = objectPoint(o, m, { op.hi.x, op.lo.y, zm }) - origin;
    up = objectPoint(o, m, { op.lo.x, op.hi.y, zm }) - origin;
}

static std::string esc(const std::string& s) { std::string o; for (char c : s) o += c == ' ' ? std::string("%20") : std::string(1, c); return o.empty() ? "-" : o; }
static std::string unesc(const std::string& s) { if (s == "-") return ""; std::string o; for (size_t i = 0; i < s.size(); i++) { if (s.compare(i, 3, "%20") == 0) { o += ' '; i += 2; } else o += s[i]; } return o; }

const char* ambientPresetName(int i) {
    static const char* n[] = { "Cave", "Crypt", "Lava cavern", "Ice cave", "Lab", "Ruins", "Forest night" };
    return i >= 0 && i < 7 ? n[i] : nullptr;
}
AmbientSettings ambientPreset(int i) {
    AmbientSettings a;
    auto set = [&](uint32_t up, uint32_t down, uint32_t side, float strength, uint32_t fog, float fogAmount) {
        a.up = up; a.down = down; a.north = a.south = a.east = a.west = side; a.strength = strength; a.fog = fog; a.fogAmount = fogAmount; };
    switch (i) {
    case 0: set(0x5E6E86, 0x2A2622, 0x3C4452, 0.8f, 0x4A5A70, 0.25f); break;   // cave: cool from above, earthy below
    case 1: set(0x4C5460, 0x1C1C22, 0x2E323C, 0.55f, 0x3A4048, 0.4f); break;   // crypt: dim, grey, misty
    case 2: set(0x6A3020, 0xFF7A2A, 0x8A3A1C, 1.0f, 0xC0502A, 0.35f); break;   // lava cavern: hot from below
    case 3: set(0xA8D8FF, 0x6A90B8, 0x80B0E0, 0.9f, 0x9AC8F0, 0.3f); break;    // ice cave: bright cold blue
    case 4: set(0xD8E8FF, 0x5A6070, 0x9AA8C0, 1.1f, 0x8090A8, 0.05f); break;   // lab: clean, bright from above
    case 5: set(0xC8A878, 0x4A3A28, 0x8A7050, 0.8f, 0xA08860, 0.15f); break;   // ruins: warm, dusty
    case 6: set(0x3A4A78, 0x1A2218, 0x2A3448, 0.5f, 0x2A3A58, 0.2f); break;    // forest night: deep blue
    }
    return a;
}

bool saveObjects(const std::string& path, const std::vector<WorldObject>& objs) {
    std::ofstream f(path); if (!f) return false;
    f << "objects 1\n";
    for (const WorldObject& o : objs) {
        f << "object " << o.id << " " << esc(o.name) << " " << esc(o.folder) << " " << esc(o.model) << " " << o.pos.x << " " << o.pos.y << " " << o.pos.z
          << " " << o.yaw << " " << o.scale << " " << (int)o.collide
          << " " << (int)o.kind << " " << o.door.w << " " << o.door.h << " " << o.door.color << " " << o.door.stayOpen << " " << (o.door.field ? 1 : 0);
        const PlatformSettings& p = o.platform;
        f << " " << (int)p.mode << " " << (int)p.trigger << " " << p.speed << " " << p.wait << " " << (p.stopEvery ? 1 : 0) << " " << p.comeBack << " " << p.copies
          << " " << (p.spawner ? 1 : 0) << " " << p.every << " " << p.points.size();
        for (const V3& v : p.points) f << " " << v.x << " " << v.y << " " << v.z;
        f << " " << o.size.x << " " << o.size.y << " " << o.size.z << " " << (int)o.pickup.type << " " << o.pickup.amount << " " << esc(o.pickup.name) << " " << o.pickup.respawn
          << " " << esc(o.teleport.world) << " " << esc(o.teleport.target) << " " << (int)o.trigger.action << " " << esc(o.trigger.text) << " " << o.trigger.amount << " " << (o.trigger.once ? 1 : 0)
          << " " << esc(o.door.key);
        const LightSettings& L = o.light;
        f << " " << (int)L.type << " " << L.color << " " << L.intensity << " " << L.range << " " << L.cone << " " << L.tilt << " " << (int)(L.anim.kind == GlowAnim::Candle ? Flicker::Candle : L.anim.kind == GlowAnim::Flicker ? Flicker::Broken : L.anim.kind == GlowAnim::SlowPulse ? Flicker::Pulse : Flicker::None);
        const ParticleSettings& P = o.particles;
        f << " " << P.rate << " " << P.burst << " " << P.burstEvery << " " << P.life << " " << P.lifeVar << " " << P.dir.x << " " << P.dir.y << " " << P.dir.z << " " << P.spread
          << " " << P.speed << " " << P.speedVar << " " << P.gravity << " " << P.drag << " " << P.size0 << " " << P.size1 << " " << P.color0 << " " << P.color1 << " " << P.alpha0 << " " << P.alpha1
          << " " << P.glow << " " << (P.additive ? 1 : 0) << " " << (int)P.shape << " " << (int)P.area << " " << P.areaSize.x << " " << P.areaSize.y << " " << P.areaSize.z << " " << P.spin
          << " " << (P.collide ? 1 : 0) << " " << (P.bounce ? 1 : 0) << " " << (P.light ? 1 : 0) << " " << P.lightRange << " " << P.lightIntensity << " " << P.maxParticles;
        f << " " << o.door.fieldSide << " " << o.door.fieldGap << " " << (o.door.fieldOnParts ? 1 : 0) << " " << esc(o.door.fieldParts) << " " << esc(o.door.openedBy) << " " << P.pull << " " << o.map.id << " " << o.map.range;
        f << " " << (o.scan.on ? 1 : 0) << " " << o.scan.id << " " << o.enemyType << " " << (o.save.energy ? 1 : 0) << " " << (o.save.ammo ? 1 : 0) << " " << (o.save.anim == "-" ? std::string("%2D") : escText(o.save.anim)) << " " << (L.shadows ? 1 : 0) << " " << L.length
          << " " << escText(soundUseText(o.door.openSound)) << " " << escText(soundUseText(o.door.closeSound))
          << " " << escText(o.save.title) << " " << escText(o.save.text) << " " << (o.save.ask ? 1 : 0);
        if (o.kind == ObjKind::Ambient) {
            const AmbientSettings& A = o.ambient;
            f << " amb " << A.up << " " << A.down << " " << A.north << " " << A.south << " " << A.east << " " << A.west << " " << A.strength << " " << A.mix
              << " " << A.fog << " " << A.fogAmount << " " << A.reach << " " << A.blend << " " << escText(A.look);
            if (!A.preset.empty()) f << " ambp " << escText(A.preset);
        }
        if (o.kind == ObjKind::GrapplePoint) f << " grp " << o.grapple.mode << " " << o.grapple.length << " " << o.grapple.reach;
        { const ParticleSettings& P2 = o.particles;   // particles: swaying, fogging the visor, only in water, an electric arc (newer files)
          if (P2.wobble > 0 || P2.fogsVisor > 0 || P2.stayIn || P2.arc)
              f << " pfx2 " << P2.wobble << " " << P2.fogsVisor << " " << P2.stayIn << " " << (P2.arc ? 1 : 0) << " " << P2.arcTo.x << " " << P2.arcTo.y << " " << P2.arcTo.z << " " << P2.arcBolts << " " << P2.arcJag; }
        if (L.anim.on() || L.matchGlow) f << " lanim " << glowAnimText(L.anim) << " " << (L.matchGlow ? 1 : 0);   // a light's animation (newer files)
        if (o.ownGlow) f << " glow " << glowAnimText(o.glowAnim);   // its own glow animation (else its model's)
        if (o.kind == ObjKind::MapStation && !o.map.rooms.empty()) { f << " mrooms " << o.map.rooms.size(); for (uint32_t r : o.map.rooms) f << " " << r; }
        f << "\n";
    }
    return (bool)f;
}
bool loadObjects(const std::string& path, std::vector<WorldObject>& objs) {
    objs.clear(); std::ifstream f(path); if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k; if (k != "object") continue;
        WorldObject o; std::string n, fo, mo; int c = 0;
        l >> o.id >> n >> fo >> mo >> o.pos.x >> o.pos.y >> o.pos.z >> o.yaw >> o.scale >> c;
        if (!l) continue;
        o.name = unesc(n); o.folder = unesc(fo); o.model = unesc(mo); o.collide = (Collide)std::clamp(c, 0, 2);
        int kind = 0, fieldOn = 1;   // doors (older files end before this)
        if (l >> kind >> o.door.w >> o.door.h >> o.door.color >> o.door.stayOpen >> fieldOn) { o.kind = (ObjKind)std::clamp(kind, 0, (int)ObjKind::COUNT - 1); o.door.field = fieldOn != 0; }
        PlatformSettings& p = o.platform; int mode, trig, se, sp; size_t np;
        if (l >> mode >> trig >> p.speed >> p.wait >> se >> p.comeBack >> p.copies >> sp >> p.every >> np) {
            p.mode = (PathMode)std::clamp(mode, 0, 2); p.trigger = (PathTrigger)std::clamp(trig, 0, 3); p.stopEvery = se != 0; p.spawner = sp != 0;
            for (size_t k = 0; k < np && k < 256; k++) { V3 v; if (l >> v.x >> v.y >> v.z) p.points.push_back(v); }
        }
        int pt, ta, once; std::string pn, tw, tt, tx, dk;   // pickups, teleporters, trigger zones (newer files)
        if (l >> o.size.x >> o.size.y >> o.size.z >> pt >> o.pickup.amount >> pn >> o.pickup.respawn >> tw >> tt >> ta >> tx >> o.trigger.amount >> once >> dk) {
            o.pickup.type = (PickupType)std::clamp(pt, 0, 2); o.pickup.name = unesc(pn); o.teleport.world = unesc(tw); o.teleport.target = unesc(tt);
            o.trigger.action = (TriggerAction)std::clamp(ta, 0, 4); o.trigger.text = unesc(tx); o.trigger.once = once != 0; o.door.key = unesc(dk);
        }
        int lt, fl; LightSettings L;   // lights and particles (newer files)
        if (l >> lt >> L.color >> L.intensity >> L.range >> L.cone >> L.tilt >> fl) { L.type = (LightType)std::clamp(lt, 0, 2); L.flicker = (Flicker)std::clamp(fl, 0, 3);
            L.anim.kind = L.flicker == Flicker::Candle ? GlowAnim::Candle : L.flicker == Flicker::Broken ? GlowAnim::Flicker : L.flicker == Flicker::Pulse ? GlowAnim::SlowPulse : GlowAnim::Steady;   // (older files: the flicker)
            if (L.anim.kind == GlowAnim::SlowPulse) L.anim.low = 0.1f; else if (L.anim.kind == GlowAnim::Flicker) L.anim.low = 0.05f;
            o.light = L; }
        ParticleSettings P; int add, sh, ar, co, bo, li;
        if (l >> P.rate >> P.burst >> P.burstEvery >> P.life >> P.lifeVar >> P.dir.x >> P.dir.y >> P.dir.z >> P.spread >> P.speed >> P.speedVar >> P.gravity >> P.drag >> P.size0 >> P.size1
              >> P.color0 >> P.color1 >> P.alpha0 >> P.alpha1 >> P.glow >> add >> sh >> ar >> P.areaSize.x >> P.areaSize.y >> P.areaSize.z >> P.spin >> co >> bo >> li >> P.lightRange >> P.lightIntensity >> P.maxParticles) {
            P.additive = add != 0; P.shape = (PShape)std::clamp(sh, 0, (int)PShape::COUNT - 1); P.area = (PArea)std::clamp(ar, 0, (int)PArea::COUNT - 1); P.collide = co != 0; P.bounce = bo != 0; P.light = li != 0;
            o.particles = P;
        }
        int fs = 0, fp = 0; float fg = 0; std::string fpn;   // forcefield placement (newer files)
        if (l >> fs >> fg >> fp >> fpn) { o.door.fieldSide = std::clamp(fs, 0, 2); o.door.fieldGap = std::clamp(fg, 0.f, 2.f); o.door.fieldOnParts = fp != 0; o.door.fieldParts = unesc(fpn); std::string ob; if (l >> ob) o.door.openedBy = unesc(ob); float pl; if (l >> pl) o.particles.pull = std::max(0.f, pl); int mid; float mr; if (l >> mid >> mr) { o.map.id = mid; o.map.range = std::max(1.f, mr); } }
        { int on = 0; std::string tok;   // can be scanned: on, the entry's ID (newer files); older ones kept the entry's text here
          if (l >> on >> tok) {
              o.scan.on = on != 0;
              if (!tok.empty() && tok.find_first_not_of("0123456789") == std::string::npos) { o.scan.id = std::stoi(tok); int et; if (l >> et) { o.enemyType = et; int se, sa; if (l >> se >> sa) { o.save.energy = se != 0; o.save.ammo = sa != 0; std::string an; if (l >> an) { o.save.anim = unescText(an); int ls; float ll; if (l >> ls >> ll) { o.light.shadows = ls != 0; o.light.length = std::clamp(ll, 0.25f, 64.f); std::string so, sc; if (l >> so >> sc) { soundUseFrom(unescText(so), o.door.openSound); soundUseFrom(unescText(sc), o.door.closeSound); std::string stt, stx; int ask; if (l >> stt >> stx >> ask) { o.save.title = unescText(stt); o.save.text = unescText(stx); o.save.ask = ask != 0; } } } } } } }
              else { std::string sc, st; int imp = 0; float tm = 0; o.scan.legacyName = unescText(tok); if (l >> sc >> st >> imp >> tm) { o.scan.legacyCategory = unescText(sc); o.scan.legacyText = unescText(st); o.scan.legacyImportant = imp != 0; o.scan.legacyTime = std::max(0.f, tm); } }
          } }
        for (std::string t; l >> t;) { if (t == "lanim") { std::string a[4]; int mg = 0; if (l >> a[0] >> a[1] >> a[2] >> a[3] >> mg) { glowAnimFrom(a[0] + " " + a[1] + " " + a[2] + " " + a[3], o.light.anim); o.light.matchGlow = mg != 0; } }
          else if (t == "glow") { std::string a[4]; if (l >> a[0] >> a[1] >> a[2] >> a[3] && glowAnimFrom(a[0] + " " + a[1] + " " + a[2] + " " + a[3], o.glowAnim)) o.ownGlow = true; }
          else if (t == "mrooms") { size_t n = 0; if (l >> n) for (size_t i = 0; i < n && i < 4096; i++) { uint32_t r; if (l >> r) o.map.rooms.push_back(r); } }
          else if (t == "pfx2") { ParticleSettings& P2 = o.particles; int si = 0, ar = 0;
              if (l >> P2.wobble >> P2.fogsVisor >> si >> ar >> P2.arcTo.x >> P2.arcTo.y >> P2.arcTo.z >> P2.arcBolts >> P2.arcJag) {
                  P2.wobble = std::clamp(P2.wobble, 0.f, 5.f); P2.fogsVisor = std::clamp(P2.fogsVisor, 0.f, 1.f); P2.stayIn = std::clamp(si, 0, 1); P2.arc = ar != 0;
                  P2.arcBolts = std::clamp(P2.arcBolts, 1, 6); P2.arcJag = std::clamp(P2.arcJag, 0.f, 1.f); } }
          else if (t == "grp") { GrappleSettings& G = o.grapple; if (l >> G.mode >> G.length >> G.reach) { G.mode = std::clamp(G.mode, 0, 1); G.reach = std::clamp(G.reach, 2.f, 80.f); G.length = std::clamp(G.length, 0.f, 60.f); } }
          else if (t == "ambp") { std::string n; if (l >> n) o.ambient.preset = unescText(n); }
          else if (t == "amb") {   // an ambient light's settings (newer files)
            AmbientSettings& A = o.ambient; std::string lk;
            if (l >> A.up >> A.down >> A.north >> A.south >> A.east >> A.west >> A.strength >> A.mix >> A.fog >> A.fogAmount >> A.reach >> A.blend >> lk) {
                A.look = unescText(lk); A.reach = std::clamp(A.reach, 2.f, 256.f); A.blend = std::clamp(A.blend, 0, 16); A.mix = std::clamp(A.mix, 0.f, 1.f);
            }
        } }
        objs.push_back(o);
    }
    return true;
}

// ------------------------------------------------------------------ model library
static std::string fileName(const std::string& n) { std::string f; for (char c : n) f += (std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_') ? c : '_'; return f.empty() ? "model" : f; }
void ModelLibrary::loadAll(const std::string& folder) {
    dir = folder; models.clear(); std::error_code ec; fs::create_directories(dir, ec);
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (e.path().extension() != ".txt") continue;
        BlockModel m; if (m.load(e.path().string())) { if (m.name.empty()) m.name = e.path().stem().string(); models[m.name] = m; }
    }
}
bool ModelLibrary::store(const BlockModel& m) { models[m.name] = m; std::error_code ec; fs::create_directories(dir, ec); return m.save(dir + "/" + fileName(m.name) + ".txt"); }
bool ModelLibrary::remove(const std::string& n) { std::error_code ec; fs::remove(dir + "/" + fileName(n) + ".txt", ec); return models.erase(n) > 0; }
std::string ModelLibrary::uniqueName(const std::string& base) const {
    std::string n = base.empty() ? "Model" : base; int k = 2;
    while (models.count(n)) n = base + " " + std::to_string(k++);
    return n;
}

// ------------------------------------------------------------------ selection box
std::vector<Edit> fillEdits(const World& w, I3 lo, I3 hi, BlockId v) {
    std::vector<Edit> out;
    for (int y = std::min(lo.y, hi.y); y <= std::max(lo.y, hi.y); y++)
        for (int z = std::min(lo.z, hi.z); z <= std::max(lo.z, hi.z); z++)
            for (int x = std::min(lo.x, hi.x); x <= std::max(lo.x, hi.x); x++) {
                BlockId b = w.get(x, y, z); if (b != v) out.push_back({ x, y, z, b, v });
            }
    return out;
}
std::vector<Edit> pasteEdits(const World& w, const BlockModel& m, I3 at, bool skipAir) {
    std::vector<Edit> out;
    for (int y = 0; y < m.sy; y++) for (int z = 0; z < m.sz; z++) for (int x = 0; x < m.sx; x++) {
        BlockId v = m.at(x, y, z); if (skipAir && v == AIR) continue;
        BlockId b = w.get(at.x + x, at.y + y, at.z + z); if (b != v) out.push_back({ at.x + x, at.y + y, at.z + z, b, v });
    }
    return out;
}

} // namespace mc

namespace mc {
bool saveAmbientPresets(const std::string& path, const std::vector<AmbientPreset>& v) {
    std::ofstream f(path); if (!f) return false;
    f << "ambient_presets 1\n";
    for (const AmbientPreset& p : v) { const AmbientSettings& A = p.s;
        f << "preset " << escText(p.name) << " " << A.up << " " << A.down << " " << A.north << " " << A.south << " " << A.east << " " << A.west << " " << A.strength << " " << A.mix
          << " " << A.fog << " " << A.fogAmount << " " << escText(A.look) << "\n"; }
    return (bool)f;
}
bool loadAmbientPresets(const std::string& path, std::vector<AmbientPreset>& v) {
    std::ifstream f(path); if (!f) return false;
    v.clear(); std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k, n, lk; l >> k; if (k != "preset") continue;
        AmbientPreset p; AmbientSettings& A = p.s;
        if (l >> n >> A.up >> A.down >> A.north >> A.south >> A.east >> A.west >> A.strength >> A.mix >> A.fog >> A.fogAmount >> lk) { p.name = unescText(n); A.look = unescText(lk); v.push_back(p); }
    }
    return true;
}
} // namespace mc
