#pragma once
// Weapons (Samus controls, part 1): beams, each with its own arm cannon model, three points on it (pivot, muzzle, emitter),
// what it shoots, missiles, and how the cannon moves (idle / walk / land / recoil, made by the engine). Plain C++, tested on Linux.
#include "game.h"
#include "sound.h"
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
    std::string shotLook = "Beam trail", hitLook = "Impact", flashLook = "Muzzle flash";   // particle presets: trail, impact, muzzle flash
    float speed = 45, damage = 10, fireRate = 5, range = 80, size = 0.07f;      // blocks/s, -, shots per second, blocks, blocks
    bool auto_ = false;                                 // keeps firing while the button is held
    bool waitForAnim = false;                           // a shot only when the firing animation is done
    std::string fireClip = "fire";
    int shotStyle = 1;                                  // 0: particles only; 1: a glowing 3D orb (with the trail behind it)
    // missiles (the same missiles for every beam; this is only how this cannon fires them)
    std::string missileClip = "missile"; bool missileWait = true;   // default: wait until the missile animation is done
    bool startsOwned = true;                            // else: picked up later (a key pickup with the beam's name gives it)
    // charge shot: hold the button, let go when it is full
    bool charge = true; float chargeTime = 1.0f;        // seconds to fill (after a short tap time)
    float chargeDamage = 4, chargeSize = 3.5f, chargeSpeed = 0.8f;   // times the normal shot's
    std::string chargeLook = "Charge";                  // particles gathering at the muzzle
    std::string chargingClip = "charging", chargedClip = "charged", chargedShotClip = "charged shot";   // the cannon: while filling (once), while full (loops), firing it
    SoundSet chargeDone, chargedLoop;                  // when it is full: a sound once, and a loop until you let go (the charging sounds stop)
    SoundSet hitSound, chargedHitSound;                 // where its shots hit
    float hitSphere = 0.35f, chargeSphere = 1.3f;       // impact spheres where shots hit (blocks across, 0: none)
    // timelines: the animation's speed, sounds, and when the shot leaves, for each thing the beam does
    enum TimelineKind { Fire, Charging, Charged, MissileShot, TIMELINES };
    Timeline timelines[TIMELINES];
};
const char* timelineName(int kind);
const std::string& beamClip(const Beam& b, int timelineKind);   // the cannon animation for it (the charged shot falls back to "fire")

// missiles: one supply for all beams; more kinds can be found later (Ice Missile, Super Missile...)
struct MissileType {
    std::string name = "Missile";
    std::string model = "@missile";                     // flies as this 3D model (its front: +Z)
    bool startsOwned = true;                            // else: picked up later (a key pickup with its name)
    int cost = 1;                                       // missiles used per shot
    float speed = 30, damage = 30, rate = 1.5f, size = 0.45f;   // blocks/s, -, per second (at most), how long the model is (blocks)
    uint32_t color = 0xFF8A3A;
    std::string trailLook = "Smoke", hitLook = "Impact";
    float sphere = 2.0f;                                // impact sphere (blocks across)
    SoundSet launch, hit;
};                     // "Fire", "Charging", "Charged shot", "Missile"

// what the engine adds on top of the cannon's own animations
struct WeaponMotion {
    float idle = 1, walk = 1, land = 1, recoil = 1;     // strengths: breathing when still, bob when walking, dip when landing, kick when firing
};

struct WeaponSet {
    std::vector<Beam> beams;
    std::vector<MissileType> missileTypes;              // the first one is the plain missile
    // switching: 0 freely (1-9 / mouse wheel for beams, Q for missiles); 1 not at all, always the best one you have
    // (the last one in the list, like Prime 3); 2 in the pause menu (like Super Metroid)
    int switchMode = 0;
    WeaponMotion motion;
    std::string missileCounter = "Missile";            // the counter that holds missiles (the HUD can show it)
    int missileStart = 25, missileMax = 25;
    bool cannonInFront = false;                         // the cannon's own layer: behind the HUD (like a visor) or in front of it
    // aiming: classic (the camera turns with the mouse, shots go to the middle) or free aim (Wii style: a cursor moves on the
    // screen, the cannon and the shots follow it; past the dead zone the camera turns, faster toward the edge)
    int aimMode = 0;                                    // 0 classic, 1 free aim
    float deadZone = 0.45f;                             // free aim: the middle part of the screen where the camera stays still (0..1)
    float turnSpeed = 140;                              // free aim: degrees per second at the very edge
    float cursorSpeed = 1;                              // free aim: how fast the mouse moves the cursor
    float cannonFollow = 1;                             // free aim: how much the cannon turns toward the cursor
    bool empty() const { return beams.empty(); }
};
int bestOwned(const std::vector<char>& owned, int fallback);   // the last one you have (the list goes from weakest to best)
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

// free aim: a cursor on the screen (x, y: -1 .. 1 from the middle to the edges, y up)
struct FreeAim {
    float x = 0, y = 0;
    // mouse moved by dx, dy pixels on a screen w x h; returns how far the camera turns (degrees, right / up) this step
    void update(const WeaponSet& w, float dx, float dy, float screenW, float screenH, double dt, float& turnYaw, float& turnPitch);
    // the direction through the cursor, in camera space (x right, y up, z forward), for a vertical view angle fovDeg
    V3 dir(float fovDeg, float aspect) const;
};

// may it fire now? (fire rate, waiting for the animation, missiles left)
struct WeaponState {
    int current = 0, missile = 0;                       // the beam, the missile kind
    float cooldown = 0;                                 // until the next shot
    float animLeft = 0;                                 // the firing animation still playing (seconds)
    bool animIsMissile = false;
    float charge = 0, held = 0; bool charging = false;  // charge: 0 .. 1 (full)
    static constexpr float tapTime = 0.25f;            // held shorter than this: just a tap, no charge shown
    enum class Shot { None, Normal, Charged };
    // the beam button this step: a press fires a normal shot; with charging, holding fills it and letting go when full fires the charged shot
    Shot beamButton(const Beam& b, bool down, bool wasDown, float dt, float animLength);
    void tick(float dt) { cooldown = std::max(0.f, cooldown - dt); animLeft = std::max(0.f, animLeft - dt); }
    bool full() const { return charging && charge >= 1; }
    // tries to fire the beam (m null) or a missile of kind m; animLength: the animation that plays (0 if none); missilesLeft is used up
    bool fire(const Beam& b, const MissileType* m, float animLength, int& missilesLeft);
};

} // namespace mc
