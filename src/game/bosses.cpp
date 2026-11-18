#include "bosses.h"
#include "visors.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace mc {

const char* bossMoveName(BossMoveKind k) {
    static const char* n[] = { "Wait", "Walk side to side", "Chase you", "Move to a spot", "Hover", "Jump at you", "Hop", "Dash", "Shoot",
                               "Slam (shockwave)", "Spawn enemies", "Shield (can't be hurt)", "Open weak point", "Close weak point", "Roar" };
    int i = (int)k; return i >= 0 && i < (int)BossMoveKind::COUNT ? n[i] : "?";
}
const char* shotPatternName(ShotPattern p) {
    static const char* n[] = { "Aimed at you", "Spread (fan)", "Straight ahead", "Ring (all around)", "Rain from above", "Homing", "Lobbed (arc)" };
    int i = (int)p; return i >= 0 && i < (int)ShotPattern::COUNT ? n[i] : "?";
}
const char* phaseStartName(PhaseStart s) {
    static const char* n[] = { "Health below %", "A part breaks", "Seconds into the fight", "A flag is on" };
    int i = (int)s; return i >= 0 && i < (int)PhaseStart::COUNT ? n[i] : "?";
}
const char* pickOrderName(PickOrder p) {
    static const char* n[] = { "In order", "Random", "By distance" };
    int i = (int)p; return i >= 0 && i < (int)PickOrder::COUNT ? n[i] : "?";
}
const char* partRoleName(PartRole r) {
    static const char* n[] = { "Weak point", "Armour", "Breakable armour" };
    int i = (int)r; return i >= 0 && i < (int)PartRole::COUNT ? n[i] : "?";
}

BossDef* BossSet::forType(int t) { for (BossDef& b : bosses) if (b.enemyType == t) return &b; return nullptr; }
const BossDef* BossSet::forType(int t) const { for (const BossDef& b : bosses) if (b.enemyType == t) return &b; return nullptr; }

BossDef defaultBoss(int enemyType) {
    BossDef d; d.enemyType = enemyType;
    BossPart w; d.parts.push_back(w);
    BossPhase p;
    BossMove walk; walk.kind = BossMoveKind::Walk; walk.time = 2.5f; walk.speed = 2; walk.anim = "walk"; p.moves.push_back(walk);
    BossMove shoot; shoot.kind = BossMoveKind::Shoot; shoot.time = 1.2f; shoot.pattern = ShotPattern::Spread; shoot.count = 3; shoot.spread = 30; shoot.anim = "attack"; p.moves.push_back(shoot);
    BossMove roar; roar.kind = BossMoveKind::Roar; roar.time = 1; roar.shake = 0.3f; p.moves.push_back(roar);
    d.phases.push_back(p);
    return d;
}
BossDef kraidExample(int enemyType) {
    BossDef d; d.enemyType = enemyType; d.title = "Kraid"; d.health = 800; d.barColor = 0x40C060;
    BossPart mouth; mouth.name = "Mouth"; mouth.box = { { 0.3, 0.7, 0.75 }, { 0.7, 0.9, 1.0 } }; mouth.damage = 2; d.parts.push_back(mouth);
    BossPhase p; p.name = "Walking";
    BossMove walk; walk.kind = BossMoveKind::Walk; walk.time = 2; walk.speed = 1.5f; walk.anim = "walk"; p.moves.push_back(walk);
    BossMove spikes; spikes.kind = BossMoveKind::Shoot; spikes.pattern = ShotPattern::Straight; spikes.count = 3; spikes.shotSpeed = 9; spikes.damage = 12;
    spikes.time = 1.5f; spikes.every = 0.75f; spikes.color = 0xE0E0A0; spikes.from = { 0, 0, 0.6 }; spikes.anim = "attack"; p.moves.push_back(spikes);
    BossMove claws; claws.kind = BossMoveKind::Shoot; claws.pattern = ShotPattern::Lob; claws.count = 2; claws.spread = 25; claws.shotSpeed = 8; claws.damage = 10;
    claws.time = 1.2f; claws.color = 0xC08040; claws.from = { 0, 1.0, 0.3 }; p.moves.push_back(claws);
    d.phases.push_back(p);
    BossPhase angry = p; angry.name = "Angry"; angry.start = PhaseStart::HealthBelow; angry.value = 50; angry.speed = 1.4f; angry.tintAmount = 0.35f; angry.order = PickOrder::Random;
    BossMove roar; roar.kind = BossMoveKind::Roar; roar.time = 1; roar.shake = 0.4f; angry.moves.insert(angry.moves.begin(), roar);
    d.phases.push_back(angry);
    return d;
}

static std::string sndText(const SoundUse& s) { return soundUseText(s); }
static void sndRead(std::istringstream& l, SoundUse& u) { std::string rest; std::getline(l, rest); SoundUse r; if (soundUseFrom(rest, r)) u = r; }

bool saveBosses(const std::string& path, const BossSet& s) {
    std::ofstream f(path); if (!f) return false;
    f << "bosses 1\n";
    for (const BossDef& b : s.bosses) {
        f << "boss " << b.enemyType << " " << escText(b.title) << " " << b.health << " " << b.barColor << " " << (b.bodyHurts ? 1 : 0) << " " << escText(b.arena) << " " << (b.sealDoors ? 1 : 0) << " "
          << b.introTime << " " << escText(b.introAnim) << " " << b.deathTime << " " << escText(b.flag) << "\n";
        if (!b.roar.empty()) f << "roar " << sndText(b.roar) << "\n";
        if (!b.music.empty()) f << "music " << sndText(b.music) << "\n";
        for (const BossPart& p : b.parts)
            f << "part " << escText(p.name) << " " << escText(p.modelPart) << " " << p.box.lo.x << " " << p.box.lo.y << " " << p.box.lo.z << " " << p.box.hi.x << " " << p.box.hi.y << " " << p.box.hi.z << " "
              << (int)p.role << " " << p.damage << " " << p.health << " " << (p.startsOpen ? 1 : 0) << "\n";
        for (const BossPhase& p : b.phases) {
            f << "phase " << escText(p.name) << " " << (int)p.start << " " << p.value << " " << escText(p.what) << " " << (int)p.order << " " << p.speed << " " << p.tint << " " << p.tintAmount << " " << p.musicMode << "\n";
            if (!p.music.empty()) f << "pmusic " << sndText(p.music) << "\n";
            for (const BossMove& m : p.moves) {
                f << "move " << (int)m.kind << " " << m.time << " " << m.speed << " " << m.height << " " << m.offset.x << " " << m.offset.y << " " << m.offset.z << " "
                  << (int)m.pattern << " " << m.count << " " << m.spread << " " << m.shotSpeed << " " << m.damage << " " << m.size << " " << m.gravity << " " << m.homing << " " << m.color << " " << m.every << " "
                  << m.from.x << " " << m.from.y << " " << m.from.z << " " << m.spawnType << " " << m.spawnCount << " " << escText(m.part) << " " << m.slamRadius << " " << m.slamDamage << " " << m.shake << " "
                  << escText(m.anim) << " " << m.weight << " " << m.range << "\n";
                if (!m.sound.empty()) f << "msound " << sndText(m.sound) << "\n";
            }
        }
    }
    f << "end\n";
    return (bool)f;
}
bool loadBosses(const std::string& path, BossSet& s) {
    std::ifstream f(path); if (!f) return false;
    s = BossSet{}; std::string line;
    BossDef* b = nullptr; BossPhase* ph = nullptr; BossMove* mv = nullptr;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k;
        if (k == "boss") {
            BossDef d; std::string t, a, ia, fl; int bh = 1, sd = 1;
            if (!(l >> d.enemyType >> t >> d.health >> d.barColor >> bh >> a >> sd >> d.introTime >> ia >> d.deathTime >> fl)) { b = nullptr; continue; }
            d.title = unescText(t); d.arena = unescText(a); d.introAnim = unescText(ia); d.flag = unescText(fl); d.bodyHurts = bh != 0; d.sealDoors = sd != 0;
            d.health = std::max(1.f, d.health);
            if (s.forType(d.enemyType)) { b = nullptr; continue; }
            s.bosses.push_back(d); b = &s.bosses.back(); ph = nullptr; mv = nullptr;
        }
        else if (!b) continue;
        else if (k == "roar") sndRead(l, b->roar);
        else if (k == "music") sndRead(l, b->music);
        else if (k == "part") {
            BossPart p; std::string n, mp; int r = 0, so = 1;
            if (l >> n >> mp >> p.box.lo.x >> p.box.lo.y >> p.box.lo.z >> p.box.hi.x >> p.box.hi.y >> p.box.hi.z >> r >> p.damage >> p.health >> so) {
                p.name = unescText(n); p.modelPart = unescText(mp); p.role = (PartRole)std::clamp(r, 0, (int)PartRole::COUNT - 1); p.startsOpen = so != 0; b->parts.push_back(p);
            }
        }
        else if (k == "phase") {
            BossPhase p; std::string n, w; int st = 0, o = 0;
            if (l >> n >> st >> p.value >> w >> o >> p.speed >> p.tint >> p.tintAmount >> p.musicMode) {
                p.name = unescText(n); p.what = unescText(w); p.start = (PhaseStart)std::clamp(st, 0, (int)PhaseStart::COUNT - 1); p.order = (PickOrder)std::clamp(o, 0, (int)PickOrder::COUNT - 1);
                p.speed = std::clamp(p.speed, 0.1f, 10.f); b->phases.push_back(p); ph = &b->phases.back(); mv = nullptr;
            }
        }
        else if (k == "pmusic" && ph) sndRead(l, ph->music);
        else if (k == "move" && ph) {
            BossMove m; int kd = 0, pt = 0; std::string part, anim;
            if (l >> kd >> m.time >> m.speed >> m.height >> m.offset.x >> m.offset.y >> m.offset.z >> pt >> m.count >> m.spread >> m.shotSpeed >> m.damage >> m.size >> m.gravity >> m.homing >> m.color >> m.every
                  >> m.from.x >> m.from.y >> m.from.z >> m.spawnType >> m.spawnCount >> part >> m.slamRadius >> m.slamDamage >> m.shake >> anim >> m.weight >> m.range) {
                m.kind = (BossMoveKind)std::clamp(kd, 0, (int)BossMoveKind::COUNT - 1); m.pattern = (ShotPattern)std::clamp(pt, 0, (int)ShotPattern::COUNT - 1);
                m.part = unescText(part); m.anim = unescText(anim); m.time = std::max(0.05f, m.time); m.count = std::clamp(m.count, 1, 64);
                ph->moves.push_back(m); mv = &ph->moves.back();
            }
        }
        else if (k == "msound" && mv) sndRead(l, mv->sound);
    }
    return true;
}

// ---- the fight
static const float GRAV = 22;
static V3 flat(V3 v) { v.y = 0; double l = std::sqrt(v.x * v.x + v.z * v.z); return l > 1e-6 ? V3{ v.x / l, 0, v.z / l } : V3{ 0, 0, 1 }; }
static V3 nrm(V3 v) { double l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); return l > 1e-9 ? v * (1 / l) : V3{ 0, 0, 1 }; }
static double hdist(V3 a, V3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }
static V3 rotY(V3 v, double deg) { double a = deg * 3.14159265358979 / 180, c = std::cos(a), s = std::sin(a); return { v.x * c + v.z * s, v.y, -v.x * s + v.z * c }; }
static float rnd(uint32_t& r) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return (r & 0xFFFFFF) / float(0x1000000); }

static bool boxSolid(const SolidFn& solid, V3 lo, V3 hi) {
    for (int x = (int)std::floor(lo.x); x <= (int)std::floor(hi.x - 1e-6); x++)
        for (int y = (int)std::floor(lo.y); y <= (int)std::floor(hi.y - 1e-6); y++)
            for (int z = (int)std::floor(lo.z); z <= (int)std::floor(hi.z - 1e-6); z++) if (solid(x, y, z)) return true;
    return false;
}
struct Bump { bool side = false, ground = false, ceiling = false; };
static Bump moveBody(V3& p, V3 d, float hw, float h, const SolidFn& solid) {
    Bump m;
    auto blocked = [&](V3 q) { return boxSolid(solid, { q.x - hw, q.y, q.z - hw }, { q.x + hw, q.y + h, q.z + hw }); };
    // a few small steps, so fast dashes don't pass through walls
    int n = std::max(1, (int)std::ceil(std::max({ std::fabs(d.x), std::fabs(d.y), std::fabs(d.z) }) / 0.4));
    V3 s = d * (1.0 / n);
    for (int i = 0; i < n; i++) {
        V3 q = p; q.x += s.x; if (blocked(q)) { m.side = true; } else p = q;
        q = p; q.z += s.z; if (blocked(q)) { m.side = true; } else p = q;
        q = p; q.y += s.y; if (blocked(q)) { if (s.y < 0) m.ground = true; else m.ceiling = true; } else p = q;
    }
    return m;
}

void bossStart(BossState& b, const BossDef& d, EnemyState& e) {
    uint32_t seed = b.rng;
    b = BossState{}; b.rng = seed ? seed : 1; b.rng ^= e.obj * 2654435761u; if (!b.rng) b.rng = 1;
    b.fighting = true; b.introT = std::max(0.f, d.introTime); b.maxHealth = std::max(1.f, d.health); e.health = b.maxHealth;
    for (const BossPart& p : d.parts) {
        if (p.role == PartRole::Breakable) b.partHealth[p.name] = std::max(0.1f, p.health);
        if (p.role == PartRole::Weak && p.startsOpen) b.open.insert(p.name);
    }
    b.home = e.pos; b.anim = d.introAnim; e.mode = EnemyMode::Chase; e.seen = true;
}

Box3 bossPartBox(const BossPart& p, const EnemyState& e, const EnemyType& t) {
    float hw = enemyHalfWidth(t), h = enemyHeight(t);
    V3 f = flat(e.fwd), r{ f.z, 0, -f.x };              // its right (looking along f, y up)
    Box3 o{ { 1e30, 1e30, 1e30 }, { -1e30, -1e30, -1e30 } };
    for (int i = 0; i < 8; i++) {
        double fx = (i & 1) ? p.box.hi.x : p.box.lo.x, fy = (i & 2) ? p.box.hi.y : p.box.lo.y, fz = (i & 4) ? p.box.hi.z : p.box.lo.z;
        double lx = (fx * 2 - 1) * hw, ly = fy * h, lz = (fz * 2 - 1) * hw;
        V3 c = e.pos + r * lx + f * lz; c.y = e.pos.y + ly;
        o.lo = { std::min(o.lo.x, c.x), std::min(o.lo.y, c.y), std::min(o.lo.z, c.z) };
        o.hi = { std::max(o.hi.x, c.x), std::max(o.hi.y, c.y), std::max(o.hi.z, c.z) };
    }
    return o;
}

float bossHealthFrac(const BossState& b, const EnemyState& e) { return std::clamp(e.health / std::max(1.f, b.maxHealth), 0.f, 1.f); }

BossHitResult bossHit(BossState& b, const BossDef& d, EnemyState& e, const std::string& part, float damage) {
    BossHitResult r; r.part = part;
    if (!b.fighting || b.dying || b.introT > 0 || b.shieldT > 0 || damage <= 0) { r.deflected = true; return r; }
    const BossPart* p = nullptr;
    for (const BossPart& q : d.parts) if (q.name == part) { p = &q; break; }
    float dmg = 0;
    if (!p || (p->role == PartRole::Breakable && b.broken.count(p->name))) { if (d.bodyHurts) dmg = damage; else r.deflected = true; }
    else if (p->role == PartRole::Armour) r.deflected = true;
    else if (p->role == PartRole::Weak) { if (b.open.count(p->name)) dmg = damage * p->damage; else r.deflected = true; }
    else {   // breakable armour: takes the hit itself
        float& hp = b.partHealth[p->name]; hp -= damage;
        if (hp <= 0) { hp = 0; b.broken.insert(p->name); r.broke = true; }
        return r;
    }
    if (dmg > 0) {
        r.damage = dmg; e.health -= dmg; e.hurtT = 0.15f;
        if (e.health <= 0) { e.health = 0; b.dying = true; b.dieT = 0; b.anim = ""; }
    }
    return r;
}

static bool phaseReady(const BossPhase& p, const BossState& b, const EnemyState& e, const FlagFn& flagOn) {
    switch (p.start) {
    case PhaseStart::HealthBelow: return e.health / std::max(1.f, b.maxHealth) * 100 < p.value;
    case PhaseStart::PartBroken: return b.broken.count(p.what) > 0;
    case PhaseStart::AfterTime: return b.fightT >= p.value;
    case PhaseStart::Flag: return flagOn && flagOn(p.what);
    default: return false;
    }
}

static int pickMove(BossState& b, const BossPhase& p, double dist) {
    int n = (int)p.moves.size(); if (!n) return -1;
    if (p.order == PickOrder::InOrder) return (b.move + 1) % n;
    std::vector<int> cand;
    if (p.order == PickOrder::ByDistance) {   // the moves for this distance first; moves without a range when none fits
        for (int i = 0; i < n; i++) if (p.moves[i].range > 0 && dist <= p.moves[i].range) cand.push_back(i);
        if (cand.empty()) for (int i = 0; i < n; i++) if (p.moves[i].range <= 0) cand.push_back(i);
    }
    if (cand.empty()) for (int i = 0; i < n; i++) cand.push_back(i);
    if (cand.size() > 1 && b.move >= 0) {     // not the same move twice in a row, when there is a choice
        std::vector<int> c2; for (int i : cand) if (i != b.move) c2.push_back(i);
        if (!c2.empty()) cand = c2;
    }
    float total = 0; for (int i : cand) total += std::max(0.f, p.moves[i].weight);
    if (total <= 0) return cand[(size_t)(rnd(b.rng) * cand.size()) % cand.size()];
    float x = rnd(b.rng) * total;
    for (int i : cand) { x -= std::max(0.f, p.moves[i].weight); if (x <= 0) return i; }
    return cand.back();
}

static void fire(BossTick& k, BossState& b, const BossMove& m, const EnemyState& e, const EnemyType& t, V3 you) {
    V3 f = flat(e.fwd), r{ f.z, 0, -f.x };
    V3 mid = e.pos; mid.y += enemyHeight(t) * 0.5;
    V3 from = mid + r * m.from.x + f * m.from.z; from.y += m.from.y;
    int n = std::max(1, m.count);
    auto shot = [&](V3 at, V3 vel, float grav, float hom) { k.shots.push_back({ at, vel, m.damage, m.size, grav, hom, 8, m.color }); };
    auto fan = [&](int i) { return n > 1 ? -m.spread * 0.5 + m.spread * i / (n - 1) : 0.0; };
    V3 aim = nrm(you - from);
    switch (m.pattern) {
    case ShotPattern::Aimed:
        for (int i = 0; i < n; i++) { V3 d = rotY(aim, (rnd(b.rng) - 0.5) * m.spread * 0.2); d.y += (rnd(b.rng) - 0.5) * m.spread * 0.002; shot(from, nrm(d) * m.shotSpeed, 0, 0); }
        break;
    case ShotPattern::Spread: for (int i = 0; i < n; i++) shot(from, nrm(rotY(aim, fan(i))) * m.shotSpeed, 0, 0); break;
    case ShotPattern::Straight:
        for (int i = 0; i < n; i++) { V3 at = from; at.y += (i - (n - 1) * 0.5) * 0.6; shot(at, f * m.shotSpeed, 0, 0); }
        break;
    case ShotPattern::Ring: for (int i = 0; i < n; i++) shot(from, rotY(f, 360.0 * i / n) * m.shotSpeed, 0, 0); break;
    case ShotPattern::Rain:
        for (int i = 0; i < n; i++) {
            double a = rnd(b.rng) * 6.2831853, rr = std::sqrt(rnd(b.rng)) * std::max(1.f, m.spread / 10);
            V3 at{ you.x + std::cos(a) * rr, you.y + 12 + rnd(b.rng) * 4, you.z + std::sin(a) * rr };
            shot(at, { 0, -m.shotSpeed, 0 }, 0, 0);
        }
        break;
    case ShotPattern::Homing: for (int i = 0; i < n; i++) shot(from, nrm(rotY(aim, fan(i))) * m.shotSpeed, 0, m.homing); break;
    case ShotPattern::Lob:
        for (int i = 0; i < n; i++) {
            V3 target = from + rotY(you - from, fan(i)); target.y = you.y;
            double hd = hdist(target, from), T = std::max(0.5, hd / std::max(0.5f, m.shotSpeed));
            V3 v{ (target.x - from.x) / T, (target.y - from.y + 0.5 * m.gravity * T * T) / T, (target.z - from.z) / T };
            shot(from, v, m.gravity, 0);
        }
        break;
    default: break;
    }
}

BossTick bossUpdate(BossState& b, const BossDef& d, EnemyState& e, const EnemyType& t, const SolidFn& solid, V3 you, const FlagFn& flagOn, double dt) {
    BossTick k;
    if (!b.fighting) return k;
    e.age += (float)dt; e.hurtT = std::max(0.f, e.hurtT - (float)dt);
    if (b.dying) { b.dieT += (float)dt; return k; }
    float hw = enemyHalfWidth(t), h = enemyHeight(t);
    bool flies = t.move == EnemyMove::Fly;
    V3 toYou = you - e.pos; double dist = std::sqrt(toYou.x * toYou.x + toYou.z * toYou.z);
    V3 face = flat(toYou);
    auto fall = [&](double ddt) {
        if (flies) return;
        b.vy -= GRAV * ddt;
        Bump m = moveBody(e.pos, { 0, b.vy * ddt, 0 }, hw, h, solid);
        if (m.ground) { b.vy = 0; b.onGround = true; } else { b.onGround = false; if (m.ceiling) b.vy = std::min(0.f, b.vy); }
    };
    if (b.introT > 0) {   // the intro: it turns to you and roars, no attacks
        b.introT -= (float)dt; e.fwd = face; fall(dt);
        if (b.introT <= 0) { k.introOver = true; k.phaseStarted = 0; b.phase = 0; b.move = -1; }
        return k;
    }
    b.fightT += (float)dt; b.shieldT = std::max(0.f, b.shieldT - (float)dt);
    if (d.phases.empty()) { e.fwd = face; fall(dt); return k; }
    if (b.phase < 0 || b.phase >= (int)d.phases.size()) b.phase = 0;
    for (int i = (int)d.phases.size() - 1; i > b.phase; i--)   // a later phase whose start has come (the latest wins)
        if (phaseReady(d.phases[i], b, e, flagOn)) { b.phase = i; b.move = -1; b.moveT = 0; k.phaseStarted = i; break; }
    const BossPhase& p = d.phases[b.phase];
    float sp = std::max(0.1f, p.speed); double sdt = dt * sp;

    bool newMove = b.move < 0 || b.move >= (int)p.moves.size() || b.moveT >= p.moves[b.move].time;
    bool wasDash = false;
    if (newMove) {
        if (b.move >= 0 && b.move < (int)p.moves.size() && p.moves[b.move].kind == BossMoveKind::Shield) b.shieldT = 0;
        b.move = pickMove(b, p, dist); b.moveT = 0; b.fireT = 0; b.fired = false;
        if (b.move < 0) { e.fwd = face; fall(dt); return k; }
        const BossMove& m = p.moves[b.move];
        if (!m.sound.empty()) k.sound = &m.sound;
        b.anim = m.anim;
        switch (m.kind) {
        case BossMoveKind::JumpAt: case BossMoveKind::Slam: case BossMoveKind::Hop:
            if (b.onGround || flies || boxSolid(solid, { e.pos.x - hw, e.pos.y - 0.05, e.pos.z - hw }, { e.pos.x + hw, e.pos.y, e.pos.z + hw })) b.vy = std::sqrt(2 * GRAV * std::max(0.2f, m.kind == BossMoveKind::Hop ? m.height * 0.5f : m.height));
            b.target = m.kind == BossMoveKind::Slam ? e.pos : you; break;
        case BossMoveKind::MoveTo: b.target = b.home + m.offset; break;
        case BossMoveKind::Dash: b.target = face; break;
        case BossMoveKind::Shield: b.shieldT = m.time / sp; break;
        case BossMoveKind::OpenWeak: case BossMoveKind::CloseWeak:
            for (const BossPart& q : d.parts) if (q.role == PartRole::Weak && (m.part.empty() || m.part == q.name)) {
                if (m.kind == BossMoveKind::OpenWeak) b.open.insert(q.name); else b.open.erase(q.name);
            }
            break;
        case BossMoveKind::Spawn: { k.spawnType = m.spawnType; k.spawnCount = std::max(1, m.spawnCount); V3 a = e.pos + face * (hw + 1.0); k.spawnAt = a; break; }
        case BossMoveKind::Roar: k.shake = m.shake; break;
        case BossMoveKind::Walk: if (rnd(b.rng) < 0.5f) b.walkDir = -b.walkDir; break;
        default: break;
        }
    }
    const BossMove& m = p.moves[b.move];
    b.moveT += (float)sdt;

    // moving
    V3 step{ 0, 0, 0 }; bool facing = true, gravity = !flies;
    switch (m.kind) {
    case BossMoveKind::Walk: {
        V3 side{ face.z, 0, -face.x }; step = side * (b.walkDir * m.speed * sdt);
        V3 next = e.pos + step; if (hdist(next, b.home) > std::max(2.f, m.speed * m.time * 1.5f) && hdist(next, b.home) > hdist(e.pos, b.home)) { b.walkDir = -b.walkDir; step = step * -1; }
        break;
    }
    case BossMoveKind::Chase: if (dist > hw + 1.2) step = face * (m.speed * sdt); break;
    case BossMoveKind::MoveTo: {
        V3 to = b.target - e.pos; if (!flies) to.y = 0; double l = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
        if (l > 0.05) step = to * (std::min(l, (double)m.speed * sdt) / l);
        break;
    }
    case BossMoveKind::Hover: {
        gravity = false; double wantY = b.home.y + m.height; b.vy = 0;
        step.y = std::clamp(wantY - e.pos.y, -m.speed * sdt, m.speed * sdt);
        if (dist > hw + 2) { V3 hz = face * (m.speed * 0.5 * sdt); step.x = hz.x; step.z = hz.z; }
        break;
    }
    case BossMoveKind::JumpAt: case BossMoveKind::Hop:
        if (!b.onGround || b.moveT < 0.1f) { V3 to = flat(b.target - e.pos); double l = hdist(b.target, e.pos); step = to * std::min(l, (double)m.speed * 1.5 * sdt); }
        break;
    case BossMoveKind::Dash: step = b.target * (m.speed * 3 * sdt); facing = false; wasDash = true; break;
    default: break;
    }
    if (facing) e.fwd = face; else e.fwd = flat(b.target);
    if (step.x != 0 || step.z != 0 || (!gravity && step.y != 0)) {
        Bump bm = moveBody(e.pos, { step.x, gravity ? 0.0 : step.y, step.z }, hw, h, solid);
        if (bm.side && m.kind == BossMoveKind::Walk) b.walkDir = -b.walkDir;
        if (bm.side && wasDash) { b.moveT = m.time; k.shake = std::max(k.shake, 0.2f); }   // hit a wall: the dash ends with a thud
    }
    bool wasAir = !b.onGround;
    if (gravity) fall(dt); else if (!flies && m.kind == BossMoveKind::Hover) b.onGround = false;
    if (m.kind == BossMoveKind::Slam && wasAir && b.onGround && b.moveT > 0.15f && !b.fired) {   // landed: the shockwave
        b.fired = true; k.slam = true; k.slamRadius = m.slamRadius; k.slamDamage = m.slamDamage; k.shake = std::max(k.shake, m.shake > 0 ? m.shake : 0.5f);
    }

    // shooting
    if (m.kind == BossMoveKind::Shoot) {
        b.fireT -= (float)sdt;
        if (!b.fired || (m.every > 0 && b.fireT <= 0)) { fire(k, b, m, e, t, you); b.fired = true; b.fireT = m.every; }
    }
    e.vel = { 0, b.vy, 0 }; e.onGround = b.onGround;
    return k;
}

} // namespace mc
