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

// ------------------------------------------------------------------ the starter set (the built-in arm cannon: its barrel points to +Z)
WeaponSet defaultWeapons() {
    WeaponSet w;
    Beam p; p.name = "Power Beam"; p.model = "@arm-cannon";
    p.pivot.pos = { 0.15, 0.15, 0.05 }; p.muzzle.pos = { 0.15, 0.15, 0.9 }; p.emitter.pos = { 0.15, 0.15, 0.95 };
    w.beams.push_back(p);
    Beam v = p; v.name = "Wave Beam"; v.color = 0xC070FF; v.shotLook = "Magic"; v.fireRate = 3; v.speed = 35; v.size = 0.12f; v.auto_ = true;
    w.beams.push_back(v);
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

bool WeaponState::fire(const Beam& b, bool missile, float animLength, int& missilesLeft) {
    if (cooldown > 0) return false;
    if (missile && !b.missiles) return false;
    bool wait = missile ? b.missileWait : b.waitForAnim;
    if (wait && animLeft > 0) return false;
    if (missile && missilesLeft <= 0) return false;
    if (missile) missilesLeft--;
    cooldown = 1.f / std::max(0.1f, missile ? b.missileRate : b.fireRate);
    animLeft = std::max(0.f, animLength); animIsMissile = missile;
    return true;
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
    for (const Beam& b : w.beams) {
        f << "beam " << esc(b.name) << " " << esc(b.model);
        putPoint(f, b.pivot); putPoint(f, b.muzzle); putPoint(f, b.emitter);
        f << " " << b.viewPos.x << " " << b.viewPos.y << " " << b.viewPos.z << " " << b.viewYaw << " " << b.viewPitch << " " << b.viewRoll << " " << b.scale << " " << b.sway
          << " " << b.color << " " << esc(b.shotLook) << " " << esc(b.hitLook) << " " << esc(b.flashLook) << " " << b.speed << " " << b.damage << " " << b.fireRate << " " << b.range << " " << b.size
          << " " << b.auto_ << " " << b.waitForAnim << " " << esc(b.fireClip) << " " << b.missiles << " " << esc(b.missileClip) << " " << b.missileWait
          << " " << b.missileSpeed << " " << b.missileDamage << " " << b.missileRate << " " << b.missileColor << " " << b.startsOwned << "\n";
    }
    return (bool)f;
}
bool loadWeapons(const std::string& path, WeaponSet& w) {
    std::ifstream f(path); if (!f) return false;
    w = WeaponSet{}; std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k;
        if (k == "motion") l >> w.motion.idle >> w.motion.walk >> w.motion.land >> w.motion.recoil;
        else if (k == "missiles") { std::string c; l >> c >> w.missileStart >> w.missileMax; w.missileCounter = unesc(c); }
        else if (k == "beam") {
            Beam b; std::string n, m, sl, hl, fl, fc, mc;
            l >> n >> m; getPoint(l, b.pivot); getPoint(l, b.muzzle); getPoint(l, b.emitter);
            l >> b.viewPos.x >> b.viewPos.y >> b.viewPos.z >> b.viewYaw >> b.viewPitch >> b.viewRoll >> b.scale >> b.sway
              >> b.color >> sl >> hl >> fl >> b.speed >> b.damage >> b.fireRate >> b.range >> b.size
              >> b.auto_ >> b.waitForAnim >> fc >> b.missiles >> mc >> b.missileWait >> b.missileSpeed >> b.missileDamage >> b.missileRate >> b.missileColor >> b.startsOwned;
            if (!l) continue;
            b.name = unesc(n); b.model = unesc(m); b.shotLook = unesc(sl); b.hitLook = unesc(hl); b.flashLook = unesc(fl); b.fireClip = unesc(fc); b.missileClip = unesc(mc);
            w.beams.push_back(b);
        }
    }
    return true;
}

} // namespace mc
