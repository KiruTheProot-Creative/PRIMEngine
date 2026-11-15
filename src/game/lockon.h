#pragma once
// Lock-on (Metroid Prime's L button, here the right mouse button): the view stays on a target, strafing circles around it,
// shots go to it and missiles steer after it, jumping while strafing is a dash to the side. Plain C++, tested on Linux.
#include "game.h"
#include <cstdint>
#include <vector>

namespace mc {

struct LockOnSettings {
    bool on = true;
    int mode = 0;                 // 0: hold the button (like Prime); 1: a press locks, the next press lets go
    float range = 40;             // blocks: how far a target may be
    float cone = 24;              // degrees from the crosshair: what can be picked
    float turnSpeed = 12;         // how quickly the view swings to the target (higher: snappier)
    float keepTime = 0.6f;        // seconds a target may be out of sight before the lock lets go
    bool beamAim = true;          // shots go straight to the target (not just to the middle of the screen)
    float beamHoming = 1.0f;      // beam shots steer after it (radians per second, 0: straight)
    float missileHoming = 4.0f;   // missiles steer after it (radians per second)
    bool orbit = true;            // strafing (A / D alone) circles the target at the same distance
    bool dash = true;             // jump while strafing: a quick hop to the side
    float dashSpeed = 9;          // blocks per second sideways
    bool scanLock = true;         // the Scan Visor: holding to scan also keeps the view on what you scan
    bool holdLooksAhead = false;  // holding with nothing to lock onto: the view stays still (Prime) instead of following the mouse
    uint32_t color = 0xFFC040, lockedColor = 0xFF5A30;   // the marker on what can be locked; the reticle when locked
};

// something that could be locked onto: where it is, and how far and how many degrees from the crosshair
struct LockCand { uint32_t id = 0; V3 c{ 0, 0, 0 }; double dist = 0, angDeg = 0; };
// the best one to lock onto (nearest the crosshair, a little by distance), or -1
int pickLockTarget(const std::vector<LockCand>& cands, const LockOnSettings& s);
// turns yaw / pitch (radians, like the Player) from eye toward target; turn: LockOnSettings::turnSpeed
void lockLook(V3 eye, V3 target, double& yaw, double& pitch, double turn, double dt);
// the yaw / pitch that look straight at target
void lookAt(V3 eye, V3 target, double& yaw, double& pitch);
// a flying shot's direction, steered toward target by at most strength * dt radians (stays a unit vector)
V3 homeDir(V3 dir, V3 pos, V3 target, double strength, double dt);
// degrees between the view (yaw / pitch) and the way to p
double angleTo(V3 eye, double yaw, double pitch, V3 p);

} // namespace mc
