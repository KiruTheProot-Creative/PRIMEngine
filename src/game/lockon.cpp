#include "lockon.h"
#include <algorithm>
#include <cmath>

namespace mc {

static const double PI = 3.14159265358979;

int pickLockTarget(const std::vector<LockCand>& cands, const LockOnSettings& s) {
    int best = -1; double bestScore = 1e30;
    for (int i = 0; i < (int)cands.size(); i++) {
        const LockCand& c = cands[i];
        if (c.dist > s.range || c.angDeg > s.cone) continue;
        double score = c.angDeg / std::max(1.f, s.cone) + 0.35 * c.dist / std::max(1.f, s.range);   // mostly: nearest the crosshair
        if (score < bestScore) { bestScore = score; best = i; }
    }
    return best;
}

void lookAt(V3 eye, V3 target, double& yaw, double& pitch) {
    V3 d = target - eye; double hz = std::sqrt(d.x * d.x + d.z * d.z);
    if (hz > 1e-6) yaw = std::atan2(d.x, d.z);
    pitch = std::clamp(std::atan2(d.y, std::max(1e-6, hz)), -1.55, 1.55);
}

void lockLook(V3 eye, V3 target, double& yaw, double& pitch, double turn, double dt) {
    double ty = yaw, tp = pitch; lookAt(eye, target, ty, tp);
    double dy = std::remainder(ty - yaw, 2 * PI);
    double k = 1 - std::exp(-std::max(0.0, turn) * dt);
    yaw += dy * k; pitch += (tp - pitch) * k;
    pitch = std::clamp(pitch, -1.55, 1.55);
}

V3 homeDir(V3 dir, V3 pos, V3 target, double strength, double dt) {
    V3 want = target - pos; double wl = length(want), dl = length(dir);
    if (wl < 1e-6 || dl < 1e-9) return dl > 1e-9 ? dir * (1 / dl) : dir;
    want = want * (1 / wl); dir = dir * (1 / dl);
    double c = std::clamp(dir.x * want.x + dir.y * want.y + dir.z * want.z, -1.0, 1.0), ang = std::acos(c), step = std::max(0.0, strength) * dt;
    if (ang <= step || ang < 1e-6) return want;
    // turn dir toward want by step, in the plane of the two
    V3 perp = want - dir * c; double pl = length(perp);
    if (pl < 1e-9) return dir;   // exactly behind: no way to pick a side, keep going
    perp = perp * (1 / pl);
    return dir * std::cos(step) + perp * std::sin(step);
}

double angleTo(V3 eye, double yaw, double pitch, V3 p) {
    V3 f{ std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch) }, d = p - eye; double l = length(d);
    if (l < 1e-9) return 0;
    return std::acos(std::clamp((f.x * d.x + f.y * d.y + f.z * d.z) / l, -1.0, 1.0)) * 180 / PI;
}

} // namespace mc
