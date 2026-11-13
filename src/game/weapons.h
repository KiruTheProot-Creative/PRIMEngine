#pragma once
// Weapons (Samus controls, part 1): beams, each with its own arm cannon model, three points on it (pivot, muzzle, emitter),
// what it shoots, missiles, and how the cannon moves (idle / walk / land / recoil, made by the engine). Plain C++, tested on Linux.
#include "game.h"
#include <string>
#include <vector>

namespace mc {

// a point on the cannon model (model space: blocks from the model's corner) and where it points (yaw / pitch, degrees; 0 0 = +Z)
struct WeaponPoint { V3 pos{ 0, 0, 0 }; float yaw = 0, pitch = 0; };
V3 pointDir(const WeaponPoint& p);

struct Beam {
    std::string name = "Power Beam";
    std::string model;                                  // the arm cannon with this beam (its "fire" / "missile" animations come from the model)
    WeaponPoint pivot, muzzle, emitter;                 // pivot: the cannon turns around it; muzzle: flash; emitter: where shots come out
    // where the cannon sits in front of the camera (camera space, like a HUD part: x right, y up, z forward)
    V3 viewPos{ 0.32, -0.32, 0.55 }; float viewYaw = 0, viewPitch = 0, viewRoll = 0; double scale = 0.5;
    float sway = 0.8f;                                  // how much of the HUD sway it gets
    // the shot
    uint32_t color = 0xFFD860;
    std::string shotLook = "Beam trail", hitLook = "Impact", flashLook = "Impact";   // particle presets: trail, impact, muzzle flash
    float speed = 45, damage = 10, fireRate = 5, range = 80, size = 0.07f;      // blocks/s, -, shots per second, blocks, blocks
    bool auto_ = false;                                 // keeps firing while the button is held
    bool waitForAnim = false;                           // a shot only when the firing animation is done
    std::string fireClip = "fire";
    // its missiles
    bool missiles = true; std::string missileClip = "missile"; bool missileWait = true;   // default: wait until the missile animation is done
    float missileSpeed = 30, missileDamage = 30, missileRate = 1.5f; uint32_t missileColor = 0xFF8A3A;
    bool startsOwned = true;                            // else: picked up later (a key pickup with the beam's name gives it)
};

// what the engine adds on top of the cannon's own animations
struct WeaponMotion {
    float idle = 1, walk = 1, land = 1, recoil = 1;     // strengths: breathing when still, bob when walking, dip when landing, kick when firing
};

struct WeaponSet {
    std::vector<Beam> beams;
    WeaponMotion motion;
    std::string missileCounter = "Missile";            // the counter that holds missiles (the HUD can show it)
    int missileStart = 25, missileMax = 25;
    bool empty() const { return beams.empty(); }
};
WeaponSet defaultWeapons();                             // a Power Beam and a Wave Beam on the built-in arm cannon
bool saveWeapons(const std::string& path, const WeaponSet& w);
bool loadWeapons(const std::string& path, WeaponSet& w);

// the cannon's place, turned and moved by the motion layers (camera space offsets)
struct WeaponPose {
    V3 offset{ 0, 0, 0 }; float yaw = 0, pitch = 0, roll = 0;
    double t = 0, walk = 0;
    float dropVel = 0, drop = 0, kick = 0, kickVel = 0;
    // speed: walking speed (blocks/s); landed this step at fallSpeed; fired: a shot (strength 1 beam, ~2.5 missile)
    void update(const WeaponMotion& m, double dt, float speed, bool landed, float fallSpeed, float fired);
};

// may it fire now? (fire rate, waiting for the animation, missiles left)
struct WeaponState {
    int current = 0;
    float cooldown = 0;                                 // until the next shot
    float animLeft = 0;                                 // the firing animation still playing (seconds)
    bool animIsMissile = false;
    void tick(float dt) { cooldown = std::max(0.f, cooldown - dt); animLeft = std::max(0.f, animLeft - dt); }
    // tries to fire (missile or beam); animLength: the animation that plays (0 if none); missilesLeft is used up
    bool fire(const Beam& b, bool missile, float animLength, int& missilesLeft);
};

} // namespace mc
