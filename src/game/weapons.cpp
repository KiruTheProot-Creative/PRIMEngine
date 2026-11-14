#include "weapons.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace mc {

V3 pointDir(const WeaponPoint& p) {
    double y = p.yaw * 3.14159265358979 / 180, x = p.pitch * 3.14159265358979 / 180;
    return { std::sin(y) * std::cos(x), std::sin(x), std::cos(y) * std::cos(x) };
}

const char* timelineName(int k) { static const char* n[] = { "Fire", "Charging", "Charged shot", "Missile" }; return n[std::clamp(k, 0, 3)]; }
const std::string& beamClip(const Beam& b, int k) {
    if (k == Beam::MissileShot) return b.missileClip;
    if (k == Beam::Charging) return b.chargingClip;
    if (k == Beam::Charged && !b.chargedShotClip.empty()) return b.chargedShotClip;
    return b.fireClip;
}
int bestOwned(const std::vector<char>& owned, int fallback) { for (int i = (int)owned.size() - 1; i >= 0; i--) if (owned[i]) return i; return fallback; }

void FreeAim::update(const WeaponSet& w, float dx, float dy, float W, float H, double dt, float& ty, float& tp) {
    float s = std::max(0.05f, w.cursorSpeed);
    x = std::clamp(x + dx / std::max(1.f, W * 0.5f) * s, -1.f, 1.f);
    y = std::clamp(y - dy / std::max(1.f, H * 0.5f) * s, -1.f, 1.f);
    auto turn = [&](float v) {   // nothing inside the dead zone, then faster toward the edge
        float dz = std::clamp(w.deadZone, 0.f, 0.95f), a = std::fabs(v); if (a <= dz) return 0.f;
        float k = (a - dz) / (1 - dz); return (v > 0 ? 1 : -1) * k * k * w.turnSpeed * (float)dt;
    };
    ty = turn(x); tp = turn(y);
}
V3 FreeAim::dir(float fovDeg, float aspect) const {
    double ty = std::tan(fovDeg * 3.14159265358979 / 360), tx = ty * aspect;
    V3 d{ x * tx, y * ty, 1 }; double l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z); return d * (1 / l);
}

// ------------------------------------------------------------------ the starter set (the built-in arm cannon: its barrel points to +Z)
WeaponSet defaultWeapons() {
    WeaponSet w;
    Beam p; p.name = "Power Beam"; p.model = "@arm-cannon";
    p.pivot.pos = { 0.15, 0.15, 0.05 }; p.muzzle.pos = { 0.15, 0.15, 0.9 }; p.emitter.pos = { 0.15, 0.15, 0.95 };
    w.beams.push_back(p);
    Beam v = p; v.name = "Wave Beam"; v.color = 0xC070FF; v.shotLook = "Magic"; v.fireRate = 3; v.speed = 35; v.size = 0.12f;
    w.beams.push_back(v);
    MissileType m; w.missileTypes.push_back(m);
    MissileType sm; sm.name = "Super Missile"; sm.startsOwned = false; sm.cost = 5; sm.damage = 150; sm.speed = 24; sm.rate = 0.7f; sm.size = 0.7f; sm.color = 0x60FF70; sm.sphere = 4.0f;
    w.missileTypes.push_back(sm);
    return w;
}

// ------------------------------------------------------------------ motion layers
void WeaponPose::update(const WeaponMotion& m, double dt, float speed, bool landed, float fallSpeed, float fired) {
    dt = std::clamp(dt, 0.0, 0.1); t += dt;
    float walking = std::clamp(speed / 4.3f, 0.f, 1.f) * m.walk;
    walk += dt * speed * 1.8;
    if (landed) dropVel -= std::min(std::max(0.f, fallSpeed), 40.f) * 0.008f * m.land;
    if (fired > 0) kickVel += fired * 8 * m.recoil;
    for (double left = dt; left > 1e-9; left -= 1.0 / 240) {
        float h = (float)std::min(left, 1.0 / 240);
        dropVel += (-120 * drop - 2 * std::sqrt(120.f) * 0.65f * dropVel) * h; drop += dropVel * h;
        kickVel += (-260 * kick - 2 * std::sqrt(260.f) * 0.7f * kickVel) * h; kick += kickVel * h;
    }
    float breathe = (float)std::sin(t * 1.25) * m.idle * (1 - std::min(1.f, walking));
    offset = { std::sin(walk) * 0.012 * walking, breathe * 0.006 + std::sin(walk * 2) * 0.008 * walking + drop, -kick * 0.08 };
    yaw = (float)std::sin(walk) * 0.8f * walking;
    pitch = -kick * 12 + breathe * 0.3f;
    roll = (float)std::sin(t * 0.9) * 0.4f * m.idle + (float)std::sin(walk) * 1.2f * walking;
}

bool WeaponState::fire(const Beam& b, const MissileType* m, float animLength, int& missilesLeft) {
    if (cooldown > 0) return false;
    bool missile = m != nullptr;
    bool wait = missile ? b.missileWait : b.waitForAnim;
    if (wait && animLeft > 0 && animIsMissile == missile) return false;   // waits for its own animation (a beam shot never holds up a missile, nor the other way)
    int cost = missile ? std::max(1, m->cost) : 0;
    if (missile && missilesLeft < cost) return false;
    missilesLeft -= cost;
    cooldown = 1.f / std::max(0.1f, missile ? m->rate : b.fireRate);
    animLeft = std::max(0.f, animLength); animIsMissile = missile;
    return true;
}

WeaponState::Shot WeaponState::beamButton(const Beam& b, bool down, bool wasDown, float dt, float animLength) {
    int none = 1;
    if (!b.charge) {   // plain: a press (or holding, for "keeps firing")
        charging = false; charge = 0; held = 0;
        return down && (b.auto_ || !wasDown) && fire(b, nullptr, animLength, none) ? Shot::Normal : Shot::None;
    }
    Shot out = Shot::None;
    if (down && !wasDown) { if (fire(b, nullptr, animLength, none)) out = Shot::Normal; charging = true; held = 0; charge = 0; }
    else if (down && charging) { held += dt; charge = std::clamp((held - tapTime) / std::max(0.05f, b.chargeTime), 0.f, 1.f); }
    else if (!down && charging) {
        charging = false;
        if (charge >= 1) { cooldown = 1.f / std::max(0.1f, b.fireRate); animLeft = std::max(0.f, animLength); animIsMissile = false; out = Shot::Charged; }
        charge = 0; held = 0;
    }
    return out;
}

// ------------------------------------------------------------------ saving (one line per beam; texts escaped)
static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '%') o += "%25"; else if (c == ' ') o += "%20"; else if (c == '\n') o += "%0A"; else o += c; } return o.empty() ? "-" : o; }
static std::string unesc(const std::string& s) {
    if (s == "-") return ""; std::string o;
    for (size_t i = 0; i < s.size(); i++) { if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16); i += 2; } else o += s[i]; }
    return o;
}
static void putPoint(std::ostream& f, const WeaponPoint& p) { f << " " << p.pos.x << " " << p.pos.y << " " << p.pos.z << " " << p.yaw << " " << p.pitch; }
static void getPoint(std::istream& f, WeaponPoint& p) { f >> p.pos.x >> p.pos.y >> p.pos.z >> p.yaw >> p.pitch; }
bool saveWeapons(const std::string& path, const WeaponSet& w) {
    std::ofstream f(path); if (!f) return false;
    f << "weapons 1\n";
    f << "motion " << w.motion.idle << " " << w.motion.walk << " " << w.motion.land << " " << w.motion.recoil << "\n";
    f << "missiles " << esc(w.missileCounter) << " " << w.missileStart << " " << w.missileMax << "\n";
    f << "layer " << w.cannonInFront << "\n";
    f << "switch " << w.switchMode << "\n";
    for (const MissileType& m : w.missileTypes) {
        f << "missile " << esc(m.name) << " " << esc(m.model) << " " << m.startsOwned << " " << m.cost << " " << m.speed << " " << m.damage << " " << m.rate << " " << m.size
          << " " << m.color << " " << esc(m.trailLook) << " " << esc(m.hitLook) << " " << m.sphere << "\n";
        for (const SoundUse& u : m.launch.sounds) f << "msound launch " << soundUseText(u) << "\n";
        for (const SoundUse& u : m.hit.sounds) f << "msound hit " << soundUseText(u) << "\n";
    }
    f << "aim " << w.aimMode << " " << w.deadZone << " " << w.turnSpeed << " " << w.cursorSpeed << " " << w.cannonFollow << "\n";
    for (const Beam& b : w.beams) {
        f << "beam " << esc(b.name) << " " << esc(b.model);
        putPoint(f, b.pivot); putPoint(f, b.muzzle); putPoint(f, b.emitter);
        f << " " << b.viewPos.x << " " << b.viewPos.y << " " << b.viewPos.z << " " << b.viewYaw << " " << b.viewPitch << " " << b.viewRoll << " " << b.scale << " " << b.sway
          << " " << b.color << " " << esc(b.shotLook) << " " << esc(b.hitLook) << " " << esc(b.flashLook) << " " << b.speed << " " << b.damage << " " << b.fireRate << " " << b.range << " " << b.size
          << " " << b.auto_ << " " << b.waitForAnim << " " << esc(b.fireClip) << " 1 " << esc(b.missileClip) << " " << b.missileWait
          << " 30 30 1.5 16747066 " << b.startsOwned   // (the old per-beam missile settings: kept so older versions still read it)
          << " " << b.charge << " " << b.chargeTime << " " << b.chargeDamage << " " << b.chargeSize << " " << b.chargeSpeed << " " << esc(b.chargeLook) << " " << b.hitSphere << " " << b.chargeSphere << " 2\n";
        f << "beam2 " << b.shotStyle << " " << esc(b.chargingClip) << " " << esc(b.chargedClip) << " " << esc(b.chargedShotClip) << "\n";
        auto bs = [&](const char* k, const SoundSet& set) { for (const SoundUse& u : set.sounds) f << "bsound " << k << " " << soundUseText(u) << "\n"; };
        bs("done", b.chargeDone); bs("loop", b.chargedLoop); bs("hit", b.hitSound); bs("chit", b.chargedHitSound);
        bs("fire", b.fireSound); bs("up", b.chargeUp); bs("cshot", b.chargedShotSound);
        for (int k = 0; k < Beam::TIMELINES; k++) {   // its timelines (after the beam they belong to)
            const Timeline& t = b.timelines[k];
            f << "timeline " << k << " " << t.animSpeed << " " << t.shotAt << "\n";
            for (const SoundClip& s : t.sounds) f << "clip " << k << " " << s.at << " " << soundUseText(s.sound) << "\n";
        }
    }
    return (bool)f;
}
bool loadWeapons(const std::string& path, WeaponSet& w) {
    std::ifstream f(path); if (!f) return false;
    w = WeaponSet{}; std::string line; bool anyMissile = false; MissileType old;   // old: missiles from older files (they were per beam)
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k;
        if (k == "motion") l >> w.motion.idle >> w.motion.walk >> w.motion.land >> w.motion.recoil;
        else if (k == "layer") l >> w.cannonInFront;
        else if (k == "aim") l >> w.aimMode >> w.deadZone >> w.turnSpeed >> w.cursorSpeed >> w.cannonFollow;
        else if (k == "timeline" && !w.beams.empty()) { int t; float sp, at; if (l >> t >> sp >> at && t >= 0 && t < Beam::TIMELINES) { w.beams.back().timelines[t].animSpeed = sp; w.beams.back().timelines[t].shotAt = at; } }
        else if (k == "clip" && !w.beams.empty()) { int t; SoundClip c; std::string rest; if (l >> t >> c.at && t >= 0 && t < Beam::TIMELINES) { std::getline(l, rest); if (soundUseFrom(rest, c.sound)) w.beams.back().timelines[t].sounds.push_back(c); } }
        else if (k == "switch") l >> w.switchMode;
        else if (k == "missile") {
            MissileType m; std::string n, md, tl, hl;
            if (l >> n >> md >> m.startsOwned >> m.cost >> m.speed >> m.damage >> m.rate >> m.size >> m.color >> tl >> hl >> m.sphere) {
                m.name = unesc(n); m.model = unesc(md); m.trailLook = unesc(tl); m.hitLook = unesc(hl); w.missileTypes.push_back(m); anyMissile = true;
            }
        }
        else if (k == "msound" && !w.missileTypes.empty()) {
            std::string slot, rest; l >> slot; std::getline(l, rest); SoundUse u;
            if (soundUseFrom(rest, u) && !u.empty()) (slot == "hit" ? w.missileTypes.back().hit : w.missileTypes.back().launch).sounds.push_back(u);
        }
        else if (k == "beam2" && !w.beams.empty()) {
            Beam& b = w.beams.back(); std::string a, c, d;
            if (l >> b.shotStyle >> a >> c >> d) { b.chargingClip = unesc(a); b.chargedClip = unesc(c); b.chargedShotClip = unesc(d); }
        }
        else if (k == "bsound" && !w.beams.empty()) {
            Beam& b = w.beams.back(); std::string slot, rest; l >> slot; std::getline(l, rest); SoundUse u;
            SoundSet* set = slot == "done" ? &b.chargeDone : slot == "loop" ? &b.chargedLoop : slot == "hit" ? &b.hitSound : slot == "chit" ? &b.chargedHitSound
                : slot == "fire" ? &b.fireSound : slot == "up" ? &b.chargeUp : slot == "cshot" ? &b.chargedShotSound : nullptr;
            if (set && soundUseFrom(rest, u) && !u.empty()) set->sounds.push_back(u);
        }
        else if (k == "missiles") { std::string c; l >> c >> w.missileStart >> w.missileMax; w.missileCounter = unesc(c); }
        else if (k == "beam") {
            Beam b; std::string n, m, sl, hl, fl, fc, mc; bool hasMissiles = true; MissileType om;
            l >> n >> m; getPoint(l, b.pivot); getPoint(l, b.muzzle); getPoint(l, b.emitter);
            l >> b.viewPos.x >> b.viewPos.y >> b.viewPos.z >> b.viewYaw >> b.viewPitch >> b.viewRoll >> b.scale >> b.sway
              >> b.color >> sl >> hl >> fl >> b.speed >> b.damage >> b.fireRate >> b.range >> b.size
              >> b.auto_ >> b.waitForAnim >> fc >> hasMissiles >> mc >> b.missileWait >> om.speed >> om.damage >> om.rate >> om.color >> b.startsOwned;
            if (!l) continue;
            { Beam cb; std::string cl; if (l >> cb.charge >> cb.chargeTime >> cb.chargeDamage >> cb.chargeSize >> cb.chargeSpeed >> cl) { b.charge = cb.charge; b.chargeTime = cb.chargeTime; b.chargeDamage = cb.chargeDamage; b.chargeSize = cb.chargeSize; b.chargeSpeed = cb.chargeSpeed; b.chargeLook = unesc(cl); float s1, s2, s3; if (l >> s1 >> s2 >> s3) { b.hitSphere = s1; b.chargeSphere = s2; om.sphere = s3; } } }
            if (w.beams.empty()) old = om;   // older files: the first beam's missiles become the missile
            b.name = unesc(n); b.model = unesc(m); b.shotLook = unesc(sl); b.hitLook = unesc(hl); b.flashLook = unesc(fl); b.fireClip = unesc(fc); b.missileClip = unesc(mc);
            w.beams.push_back(b);
        }
    }
    if (!anyMissile) w.missileTypes.push_back(old);
    return true;
}

} // namespace mc
