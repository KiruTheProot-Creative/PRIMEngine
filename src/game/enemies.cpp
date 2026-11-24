#include "enemies.h"
#include "visors.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace mc {

const char* enemyMoveName(EnemyMove m) {
    static const char* n[] = { "Stands still", "Walks", "Crawls over blocks", "Flies", "Hops", "Drops from the ceiling" };
    int i = (int)m; return i >= 0 && i < (int)EnemyMove::COUNT ? n[i] : "?";
}
const char* enemyRespawnName(EnemyRespawn r) {
    static const char* n[] = { "Never", "When you come back", "After some seconds" };
    int i = (int)r; return i >= 0 && i < (int)EnemyRespawn::COUNT ? n[i] : "?";
}
const char* enemyDeathName(EnemyDeath d) {
    static const char* n[] = { "Explode (little cubes)", "Fracture (big chunks)", "Its die animation", "Burst (sparks only)" };
    int i = (int)d; return i >= 0 && i < (int)EnemyDeath::COUNT ? n[i] : "?";
}

EnemyType* EnemySet::find(int id) { for (EnemyType& t : types) if (t.id == id) return &t; return nullptr; }
const EnemyType* EnemySet::find(int id) const { for (const EnemyType& t : types) if (t.id == id) return &t; return nullptr; }
int EnemySet::add(EnemyType t) {
    if (t.id <= 0 || find(t.id)) { int m = 0; for (const EnemyType& x : types) m = std::max(m, x.id); t.id = m + 1; }
    types.push_back(t); return t.id;
}

EnemySet defaultEnemies() {
    EnemySet s;
    EnemyType z; z.name = "Zoomer"; z.model = "@enemy-crawler"; z.move = EnemyMove::Crawl; z.size = 0.8f; z.health = 15; z.touchDamage = 8; z.speed = 1.6f; z.color = 0xFFB040; z.death = EnemyDeath::Explode;
    z.drops = { { "Energy", 10, 0.4f } }; s.add(z);
    EnemyType h; h.name = "Hornoad"; h.model = "@enemy-hopper"; h.move = EnemyMove::Hop; h.size = 1.0f; h.health = 25; h.touchDamage = 10; h.speed = 3; h.hopHeight = 1.4f; h.hopEvery = 1.2f; h.color = 0x80E060;
    h.death = EnemyDeath::Fracture; h.drops = { { "Energy", 10, 0.5f } }; s.add(h);
    EnemyType r; r.name = "Reo"; r.model = "@enemy-flyer"; r.move = EnemyMove::Fly; r.size = 0.9f; r.health = 20; r.touchDamage = 12; r.speed = 2.5f; r.chaseSpeed = 5; r.hover = 2.5f; r.color = 0x60C0FF;
    r.death = EnemyDeath::Burst; r.weak = { { "Wave Beam", 2 } }; r.drops = { { "Missile", 3, 0.4f } }; s.add(r);
    EnemyType p; p.name = "Trooper"; p.model = "@enemy-walker"; p.move = EnemyMove::Walk; p.size = 1.6f; p.health = 60; p.touchDamage = 15; p.speed = 1.8f; p.chaseSpeed = 2.6f; p.shoots = true; p.shootEvery = 1.6f;
    p.color = 0xE04040; p.death = EnemyDeath::Fracture; p.weak = { { "Missile", 2 }, { "Charged", 1.5f } }; p.drops = { { "Energy", 20, 0.5f }, { "Missile", 5, 0.3f } }; s.add(p);
    EnemyType k; k.name = "Skree"; k.model = "@enemy-skree"; k.move = EnemyMove::Drop; k.size = 0.9f; k.health = 10; k.touchDamage = 14; k.speed = 3; k.dropReach = 2.5f; k.shrapnel = 6; k.color = 0xC080FF;
    k.death = EnemyDeath::Explode; k.respawn = EnemyRespawn::OnReturn; s.add(k);
    EnemyType t; t.name = "Turret"; t.model = "@enemy-turret"; t.move = EnemyMove::Still; t.size = 1.0f; t.health = 40; t.touchDamage = 5; t.shoots = true; t.shootEvery = 1.2f; t.sight = 18; t.shotColor = 0xFF3040;
    t.color = 0xFF6040; t.death = EnemyDeath::Explode; t.onlyWeak = true; t.weak = { { "Missile", 1 }, { "Charged", 1 }, { "Bomb", 1 }, { "Power Bomb", 1 } }; s.add(t);
    return s;
}

const DropLook* EnemySet::drop(const std::string& what) const { for (const DropLook& d : drops) if (d.what == what) return &d; return nullptr; }
bool saveEnemies(const std::string& path, const EnemySet& s) {
    std::ofstream f(path); if (!f) return false;
    f << "enemies 1\n";
    for (const EnemyType& t : s.types) {
        f << "enemy " << t.id << " " << escText(t.name) << " " << escText(t.model) << " " << t.size << " " << (int)t.move << " " << t.health << " " << t.touchDamage << " " << t.knockback << " "
          << t.speed << " " << t.chaseSpeed << " " << t.sight << " " << (t.chases ? 1 : 0) << " " << (t.shoots ? 1 : 0) << " " << t.shootEvery << " " << t.shotSpeed << " " << t.shotDamage << " " << t.shotRange << " " << t.shotColor << " "
          << t.hopHeight << " " << t.hopEvery << " " << t.hover << " " << t.roam << " " << t.dropReach << " " << t.shrapnel << " " << (t.onlyWeak ? 1 : 0) << " " << (int)t.respawn << " " << t.respawnTime << " " << (int)t.death << " "
          << escText(t.animIdle) << " " << escText(t.animMove) << " " << escText(t.animAttack) << " " << escText(t.animHurt) << " " << escText(t.animDie) << " "
          << escText(t.sndNotice) << " " << escText(t.sndAttack) << " " << escText(t.sndHurt) << " " << escText(t.sndDie) << " " << t.scanId << " " << t.color << " " << (t.lockable ? 1 : 0) << " " << (t.onRadar ? 1 : 0) << " " << t.grapple << "\n";
        for (const EnemyWeak& w : t.weak) f << "weak " << escText(w.by) << " " << w.times << "\n";
        for (const EnemyDrop& d : t.drops) f << "drop " << escText(d.what) << " " << d.amount << " " << d.chance << "\n";
    }
    for (const DropLook& d : s.drops)
        f << "droplook " << escText(d.what) << " " << escText(d.model) << " " << d.scale << " " << d.bigFrom << " " << escText(d.bigModel) << " " << d.bigScale << " " << d.spin << " " << d.bob << " "
          << d.lightColor << " " << d.light << " " << d.lightPower << " " << escText(soundUseText(d.sound)) << " " << d.life << " " << d.blinkAt << "\n";
    return (bool)f;
}
bool loadEnemies(const std::string& path, EnemySet& s) {
    std::ifstream f(path); if (!f) return false;
    s = EnemySet{}; std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k;
        if (k == "droplook") {
            DropLook d; std::string w, m, bm, snd;
            if (l >> w >> m >> d.scale >> d.bigFrom >> bm >> d.bigScale >> d.spin >> d.bob >> d.lightColor >> d.light >> d.lightPower >> snd >> d.life >> d.blinkAt) {
                d.what = unescText(w); d.model = unescText(m); d.bigModel = unescText(bm); soundUseFrom(unescText(snd), d.sound);
                if (!d.what.empty() && !s.drop(d.what)) s.drops.push_back(d);
            }
            continue;
        }
        if (k == "enemy") {
            EnemyType t; std::string n, m, a[5], snd[4]; int mv = 1, ch = 1, sh = 0, ow = 0, rs = 1, de = 0;
            l >> t.id >> n >> m >> t.size >> mv >> t.health >> t.touchDamage >> t.knockback >> t.speed >> t.chaseSpeed >> t.sight >> ch >> sh >> t.shootEvery >> t.shotSpeed >> t.shotDamage >> t.shotRange >> t.shotColor
              >> t.hopHeight >> t.hopEvery >> t.hover >> t.roam >> t.dropReach >> t.shrapnel >> ow >> rs >> t.respawnTime >> de >> a[0] >> a[1] >> a[2] >> a[3] >> a[4] >> snd[0] >> snd[1] >> snd[2] >> snd[3] >> t.scanId >> t.color;
            if (!l || t.id <= 0 || s.find(t.id)) continue;
            { int lk = 1; if (l >> lk) t.lockable = lk != 0; int rd = 1; if (l >> rd) t.onRadar = rd != 0; int gp = 0; if (l >> gp) t.grapple = std::clamp(gp, 0, 2); }   // (older files: every enemy can be locked onto and shows on the radar)
            t.name = unescText(n); t.model = unescText(m); t.move = (EnemyMove)std::clamp(mv, 0, (int)EnemyMove::COUNT - 1); t.chases = ch != 0; t.shoots = sh != 0; t.onlyWeak = ow != 0;
            t.respawn = (EnemyRespawn)std::clamp(rs, 0, (int)EnemyRespawn::COUNT - 1); t.death = (EnemyDeath)std::clamp(de, 0, (int)EnemyDeath::COUNT - 1);
            t.animIdle = unescText(a[0]); t.animMove = unescText(a[1]); t.animAttack = unescText(a[2]); t.animHurt = unescText(a[3]); t.animDie = unescText(a[4]);
            t.sndNotice = unescText(snd[0]); t.sndAttack = unescText(snd[1]); t.sndHurt = unescText(snd[2]); t.sndDie = unescText(snd[3]);
            t.size = std::clamp(t.size, 0.1f, 50.f); t.health = std::max(0.1f, t.health);
            s.types.push_back(t);
        } else if (k == "weak" && !s.types.empty()) { EnemyWeak w; std::string b; if (l >> b >> w.times) { w.by = unescText(b); s.types.back().weak.push_back(w); } }
        else if (k == "drop" && !s.types.empty()) { EnemyDrop d; std::string w; if (l >> w >> d.amount >> d.chance) { d.what = unescText(w); s.types.back().drops.push_back(d); } }
    }
    return true;
}

float enemyDamage(const EnemyType& t, const std::string& weapon, bool missile, bool charged, bool bomb, float base) {
    float best = 0; bool any = false;
    for (const EnemyWeak& w : t.weak) {
        bool match = w.by == weapon || (w.by == "Missile" && missile) || (w.by == "Charged" && charged) || (w.by == "Any beam" && !missile && !bomb) || (w.by == "Bomb" && weapon == "Bomb");
        if (match) { best = any ? std::max(best, w.times) : w.times; any = true; }
    }
    if (any) return base * best;
    return t.onlyWeak ? 0.f : base;
}

// ---- moving
static const float GRAV = 22;
float enemyHalfWidth(const EnemyType& t) { return std::max(0.1f, t.size * 0.42f); }
float enemyHeight(const EnemyType& t) { return std::max(0.2f, t.size * (t.move == EnemyMove::Crawl ? 0.6f : 0.9f)); }
static V3 nrm(V3 v) { double l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); return l > 1e-9 ? v * (1 / l) : V3{ 0, 0, 1 }; }
static V3 iv(const int a[3]) { return { (double)a[0], (double)a[1], (double)a[2] }; }

Box3 enemyBox(const EnemyState& s, const EnemyType& t) {
    float hw = enemyHalfWidth(t), h = enemyHeight(t);
    if (t.move == EnemyMove::Crawl) { double r = std::max(hw, h * 0.5f); V3 c = s.pos + s.up * (h * 0.5); return { { c.x - r, c.y - r, c.z - r }, { c.x + r, c.y + r, c.z + r } }; }
    return { { s.pos.x - hw, s.pos.y, s.pos.z - hw }, { s.pos.x + hw, s.pos.y + h, s.pos.z + hw } };
}
static bool boxSolid(const SolidFn& solid, V3 lo, V3 hi) {
    for (int x = (int)std::floor(lo.x); x <= (int)std::floor(hi.x - 1e-6); x++)
        for (int y = (int)std::floor(lo.y); y <= (int)std::floor(hi.y - 1e-6); y++)
            for (int z = (int)std::floor(lo.z); z <= (int)std::floor(hi.z - 1e-6); z++) if (solid(x, y, z)) return true;
    return false;
}
struct Moved { bool x = false, y = false, z = false, ground = false; };
static Moved moveBody(V3& p, V3 delta, float hw, float h, const SolidFn& solid) {   // one axis at a time, stopping at blocks
    Moved m;
    auto lo = [&](V3 q) { return V3{ q.x - hw, q.y, q.z - hw }; }; auto hi = [&](V3 q) { return V3{ q.x + hw, q.y + h, q.z + hw }; };
    V3 q = p; q.y += delta.y;
    if (boxSolid(solid, lo(q), hi(q))) { m.y = true; if (delta.y < 0) { q.y = std::floor(q.y) + 1; m.ground = true; } else q.y = std::floor(q.y + h) - h - 1e-4; if (boxSolid(solid, lo(q), hi(q))) q.y = p.y; }
    p.y = q.y;
    q = p; q.x += delta.x;
    if (boxSolid(solid, lo(q), hi(q))) { m.x = true; q.x = p.x; }
    p.x = q.x;
    q = p; q.z += delta.z;
    if (boxSolid(solid, lo(q), hi(q))) { m.z = true; q.z = p.z; }
    p.z = q.z;
    return m;
}
static bool groundAhead(const EnemyState& s, const EnemyType& t, V3 dir, const SolidFn& solid) {
    V3 probe = s.pos + dir * (enemyHalfWidth(t) + 0.3);
    return solid((int)std::floor(probe.x), (int)std::floor(s.pos.y - 0.5), (int)std::floor(probe.z));
}

// crawling: the surface point of air cell c on the block behind normal n
static V3 surf(const int c[3], const int n[3]) { return { c[0] + 0.5 - n[0] * 0.5, c[1] + 0.5 - n[1] * 0.5, c[2] + 0.5 - n[2] * 0.5 }; }
static bool attachCrawler(EnemyState& s, const SolidFn& solid, V3 wantFwd) {
    static const int N[6][3] = { { 0, 1, 0 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, -1, 0 } };
    int c[3] = { (int)std::floor(s.pos.x), (int)std::floor(s.pos.y + 0.01), (int)std::floor(s.pos.z) };
    if (solid(c[0], c[1], c[2])) c[1]++;
    for (auto& n : N) {
        if (!solid(c[0] - n[0], c[1] - n[1], c[2] - n[2])) continue;
        std::copy(c, c + 3, s.cell); std::copy(n, n + 3, s.n);
        // the way along the surface closest to where it faces
        static const int A[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
        double best = -2; for (auto& a : A) { if (a[0] * n[0] + a[1] * n[1] + a[2] * n[2] != 0) continue; double d = a[0] * wantFwd.x + a[1] * wantFwd.y + a[2] * wantFwd.z; if (d > best) { best = d; std::copy(a, a + 3, s.d); } }
        s.attached = true; s.pos = surf(s.cell, s.n); s.from = s.to = s.pos; s.t = 1; s.up = iv(s.n); s.fwd = iv(s.d);
        s.fromUp = s.toUp = s.up; s.fromFwd = s.toFwd = s.fwd; return true;
    }
    return false;
}
static void crawl(EnemyState& s, const EnemyType& t, const SolidFn& solid, double dt) {
    if (!s.attached) {   // falling until it lands, then it clings on
        s.vel.y = std::max(-25.0, s.vel.y - GRAV * dt); Moved m = moveBody(s.pos, s.vel * dt, enemyHalfWidth(t), enemyHeight(t), solid);
        if (m.ground) { s.vel = { 0, 0, 0 }; attachCrawler(s, solid, s.fwd); }
        return;
    }
    s.t += (float)(t.speed * dt / std::max(0.05f, s.stepLen));
    while (s.t >= 1) {   // the next step: along, up a wall (inside corner), or around an edge (outside corner)
        int* c = s.cell; int *n = s.n, *d = s.d;
        if (!solid(c[0] - n[0], c[1] - n[1], c[2] - n[2])) { s.attached = false; s.vel = { 0, 0, 0 }; return; }   // the block it held is gone
        int ahead[3] = { c[0] + d[0], c[1] + d[1], c[2] + d[2] };
        int nc[3], nn[3], nd[3]; V3 mid;
        if (solid(ahead[0], ahead[1], ahead[2])) {   // a wall in front: climb it
            std::copy(c, c + 3, nc); for (int i = 0; i < 3; i++) { nn[i] = -d[i]; nd[i] = n[i]; }
            mid = V3{ c[0] + 0.5, c[1] + 0.5, c[2] + 0.5 } + iv(d) * 0.5 - iv(n) * 0.5;
        } else if (solid(ahead[0] - n[0], ahead[1] - n[1], ahead[2] - n[2])) {   // flat on
            std::copy(ahead, ahead + 3, nc); std::copy(n, n + 3, nn); std::copy(d, d + 3, nd);
            mid = (surf(c, n) + surf(nc, nn)) * 0.5;
        } else {   // an edge: around it
            for (int i = 0; i < 3; i++) { nc[i] = ahead[i] - n[i]; nn[i] = d[i]; nd[i] = -n[i]; }
            mid = V3{ c[0] + 0.5, c[1] + 0.5, c[2] + 0.5 } + iv(d) * 0.5 - iv(n) * 0.5;
        }
        s.from = surf(c, n); s.to = surf(nc, nn); s.fromUp = iv(n); s.toUp = iv(nn); s.fromFwd = iv(d); s.toFwd = iv(nd);
        s.mid = mid; std::copy(nc, nc + 3, s.cell); std::copy(nn, nn + 3, s.n); std::copy(nd, nd + 3, s.d);
        s.t -= 1;
    }
    float k = s.t;
    s.pos = k < 0.5f ? s.from + (s.mid - s.from) * (k * 2) : s.mid + (s.to - s.mid) * ((k - 0.5f) * 2);
    s.up = nrm(s.fromUp + (s.toUp - s.fromUp) * k); s.fwd = nrm(s.fromFwd + (s.toFwd - s.fromFwd) * k);
}

void enemyStart(EnemyState& s, const EnemyType& t, V3 pos, double yawDeg, const SolidFn& solid) {
    double a = yawDeg * 3.14159265358979 / 180;
    s.type = t.id; s.home = pos; s.pos = pos; s.vel = { 0, 0, 0 }; s.fwd = { std::sin(a), 0, std::cos(a) }; s.up = { 0, 1, 0 };
    s.health = t.health; s.mode = EnemyMode::Idle; s.hurtT = 0; s.shootT = t.shootEvery * 0.5f; s.hopT = t.hopEvery * 0.5f; s.timer = 0; s.lostT = 0; s.deadT = 0; s.age = 0; s.seen = false; s.onGround = false;
    if (t.move == EnemyMove::Crawl) { s.attached = false; attachCrawler(s, solid, s.fwd); }
    if (t.move == EnemyMove::Fly) { s.pos.y += t.hover; }
    if (t.move == EnemyMove::Drop) {   // up to the ceiling
        int x = (int)std::floor(pos.x), z = (int)std::floor(pos.z), y = (int)std::floor(pos.y + 0.01);
        for (int k = 0; k < 24; k++, y++) if (solid(x, y, z)) { s.pos.y = y - enemyHeight(t); break; }
        s.home = s.pos;
    }
}

void enemyHit(EnemyState& s, const EnemyType& t, float damage, V3 from) {
    if (!s.alive() || damage <= 0) return;
    s.health -= damage; s.hurtT = 0.25f;
    if (t.move != EnemyMove::Crawl && t.move != EnemyMove::Still && !(t.move == EnemyMove::Drop && s.mode == EnemyMode::Idle)) {
        V3 k = s.pos - from; k.y = 0; k = nrm(k); s.vel = s.vel + k * t.knockback; if (t.move != EnemyMove::Fly) s.vel.y = std::max(s.vel.y, (double)t.knockback * 0.4);
    }
    if (s.mode == EnemyMode::Idle && t.move != EnemyMove::Drop && t.move != EnemyMove::Crawl) s.mode = EnemyMode::Chase;   // it knows where you are now
    if (s.health <= 0) { s.health = 0; s.mode = EnemyMode::Dead; s.deadT = 0; }
}

EnemyTick enemyUpdate(EnemyState& s, const EnemyType& t, const SolidFn& solid, V3 target, bool canSee, double dt) {
    EnemyTick out;
    if (!s.alive()) { s.deadT += (float)dt; return out; }
    dt = std::min(dt, 0.05);
    s.age += (float)dt; s.hurtT = std::max(0.f, s.hurtT - (float)dt);
    float hw = enemyHalfWidth(t), h = enemyHeight(t);
    V3 eye = s.pos + s.up * (h * 0.65), to = target - eye; double dist = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
    bool sees = canSee && dist <= t.sight;
    s.seen = sees;
    // noticing you, and losing you
    if (t.move != EnemyMove::Crawl && t.move != EnemyMove::Drop) {
        if (sees && s.mode == EnemyMode::Idle && (t.chases || t.shoots || t.move == EnemyMove::Still)) { s.mode = EnemyMode::Chase; out.noticed = true; s.lostT = 0; }
        if (s.mode == EnemyMode::Chase) { if (sees) s.lostT = 0; else if ((s.lostT += (float)dt) > 3) s.mode = EnemyMode::Idle; }
    }
    V3 flat{ to.x, 0, to.z }; V3 toward = nrm(flat);
    switch (t.move) {
    case EnemyMove::Still:
        if (s.mode == EnemyMode::Chase && sees) s.fwd = toward;
        break;
    case EnemyMove::Walk: {
        bool chase = s.mode == EnemyMode::Chase && t.chases;
        if (chase && (flat.x * flat.x + flat.z * flat.z) > 0.04) s.fwd = toward;
        double sp = chase ? t.chaseSpeed : t.speed;
        if (chase && dist < 1.2) sp = 0;   // close enough
        V3 want = s.fwd * sp;
        if (s.onGround && !groundAhead(s, t, s.fwd, solid)) { if (chase) want = { 0, 0, 0 }; else { s.fwd = s.fwd * -1; want = s.fwd * sp; } }   // a ledge: stop (or turn back)
        double k = std::min(1.0, dt * 8); s.vel.x += (want.x - s.vel.x) * k; s.vel.z += (want.z - s.vel.z) * k;
        s.vel.y = std::max(-25.0, s.vel.y - GRAV * dt);
        Moved m = moveBody(s.pos, s.vel * dt, hw, h, solid);
        s.onGround = m.ground; if (m.y) s.vel.y = 0;
        if ((m.x || m.z) && !chase) s.fwd = s.fwd * -1;   // a wall: turn back
        break; }
    case EnemyMove::Hop: {
        bool chase = s.mode == EnemyMode::Chase && t.chases;
        s.vel.y = std::max(-25.0, s.vel.y - GRAV * dt);
        if (s.onGround) {
            s.vel.x = s.vel.z = 0; s.hopT -= (float)dt;
            if (s.hopT <= 0) {
                if (chase) s.fwd = toward; else { V3 f = s.fwd; s.fwd = { f.z, 0, -f.x }; }   // idle: a quarter turn each hop
                V3 v = s.fwd * t.speed; v.y = std::sqrt(2 * GRAV * std::max(0.1f, t.hopHeight)); s.vel = v; s.hopT = t.hopEvery; s.onGround = false;
            }
        }
        Moved m = moveBody(s.pos, s.vel * dt, hw, h, solid);
        if (m.y) { if (s.vel.y < 0) s.onGround = true; s.vel.y = 0; }
        if (m.x) s.vel.x = 0; if (m.z) s.vel.z = 0;
        break; }
    case EnemyMove::Fly: {
        bool chase = s.mode == EnemyMode::Chase && t.chases;
        V3 home = s.home + V3{ 0, t.hover, 0 };
        V3 goal = chase ? target + V3{ 0, 0.6, 0 } : home + V3{ std::sin(s.age * 0.6) * t.roam, std::sin(s.age * 1.1) * 0.4, std::cos(s.age * 0.45) * t.roam };
        V3 d = goal - s.pos; double l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        double sp = chase ? t.chaseSpeed : t.speed; V3 want = l > 0.3 ? d * (sp / l) : V3{ 0, 0, 0 };
        double k = std::min(1.0, dt * 2.5); s.vel = s.vel + (want - s.vel) * k;
        Moved m = moveBody(s.pos, s.vel * dt, hw, h, solid); if (m.x) s.vel.x = 0; if (m.y) s.vel.y = 0; if (m.z) s.vel.z = 0;
        V3 hv{ s.vel.x, 0, s.vel.z }; if (hv.x * hv.x + hv.z * hv.z > 0.05) s.fwd = nrm(hv);
        break; }
    case EnemyMove::Crawl:
        crawl(s, t, solid, dt);
        break;
    case EnemyMove::Drop:
        if (s.mode == EnemyMode::Idle) {   // hanging: you below it (and close to the side) makes it fall
            double side = std::sqrt((target.x - s.pos.x) * (target.x - s.pos.x) + (target.z - s.pos.z) * (target.z - s.pos.z));
            if (side < t.dropReach && target.y < s.pos.y && canSee) { s.mode = EnemyMode::Fall; s.vel = { 0, 0, 0 }; out.noticed = true; }
        } else if (s.mode == EnemyMode::Fall) {
            s.vel.y = std::max(-28.0, s.vel.y - GRAV * 1.4 * dt);
            V3 side{ target.x - s.pos.x, 0, target.z - s.pos.z }; s.vel.x += (side.x * 2 - s.vel.x) * std::min(1.0, dt * 3); s.vel.z += (side.z * 2 - s.vel.z) * std::min(1.0, dt * 3);   // steers toward you a little
            double a = dt * 14; V3 f = s.fwd; s.fwd = { f.x * std::cos(a) - f.z * std::sin(a), 0, f.x * std::sin(a) + f.z * std::cos(a) };   // spinning
            Moved m = moveBody(s.pos, s.vel * dt, hw, h, solid);
            if (m.ground) { s.mode = EnemyMode::Burrow; s.timer = 0.55f; s.vel = { 0, 0, 0 }; }
        } else if (s.mode == EnemyMode::Burrow) {   // digs in, then bursts
            s.pos.y -= dt * 0.7; s.timer -= (float)dt;
            if (s.timer <= 0) { out.burst = true; s.mode = EnemyMode::Dead; s.deadT = 0; s.health = 0; }
        }
        break;
    default: break;
    }
    // shooting
    if (t.shoots && s.mode == EnemyMode::Chase && sees && dist <= t.shotRange) {
        s.shootT -= (float)dt;
        if (s.shootT <= 0) { out.fire = true; out.shotFrom = eye + s.fwd * (hw + 0.1); out.shotDir = nrm(target - out.shotFrom); s.shootT = t.shootEvery; }
    }
    return out;
}

} // namespace mc
