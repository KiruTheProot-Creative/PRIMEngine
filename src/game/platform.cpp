#include "platform.h"
#include <algorithm>
#include <cmath>

namespace mc {

const char* pathModeName(PathMode m) { return m == PathMode::Loop ? "Round and round (loop)" : m == PathMode::Once ? "Once to the end" : "Back and forth"; }
const char* pathTriggerName(PathTrigger t) { return t == PathTrigger::StandOn ? "When you stand on it (elevator)" : t == PathTrigger::Shot ? "When it is shot" : "Always moving"; }

static double len(V3 d) { return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z); }

PathLine::PathLine(const std::vector<V3>& points, bool lp) : pts(points), loop(lp) {
    if (pts.empty()) pts.push_back({ 0, 0, 0 });
    at.push_back(0);
    for (size_t i = 1; i < pts.size(); i++) at.push_back(at.back() + len(pts[i] - pts[i - 1]));
    length = at.back();
    if (loop && pts.size() > 2) length += len(pts.front() - pts.back()); else loop = loop && pts.size() > 2;
}
V3 PathLine::pos(double s) const {
    if (pts.size() < 2 || length <= 0) return pts[0];
    if (loop) { s = std::fmod(s, length); if (s < 0) s += length; } else s = std::clamp(s, 0.0, length);
    for (size_t i = 1; i < pts.size(); i++) if (s <= at[i]) { double seg = at[i] - at[i - 1]; double f = seg > 0 ? (s - at[i - 1]) / seg : 0; return pts[i - 1] + (pts[i] - pts[i - 1]) * f; }
    double seg = length - at.back(), f = seg > 0 ? (s - at.back()) / seg : 0;   // the closing piece of a loop
    return pts.back() + (pts.front() - pts.back()) * f;
}
int PathLine::pointAt(double s) const {
    for (size_t i = 0; i < at.size(); i++) if (std::fabs(s - at[i]) < 1e-6) return (int)i;
    if (loop && std::fabs(s - length) < 1e-6) return 0;
    return -1;
}

static PathLine lineOf(const PlatformSettings& p) {
    std::vector<V3> pts = p.points; if (pts.empty() || pts[0].x != 0 || pts[0].y != 0 || pts[0].z != 0) pts.insert(pts.begin(), V3{ 0, 0, 0 });
    return PathLine(pts, p.mode == PathMode::Loop);
}

void PlatformRun::reset(const PlatformSettings& p) {
    movers.clear(); spawnT = 0;
    PathLine L = lineOf(p);
    if (p.spawner) { movers.push_back(Mover{}); movers.back().moving = true; return; }
    int n = p.trigger == PathTrigger::Always ? std::clamp(p.copies, 1, 64) : 1;
    for (int i = 0; i < n; i++) {
        Mover m; m.moving = p.trigger == PathTrigger::Always;
        if (p.mode == PathMode::Loop) m.s = L.length * i / n;
        else {   // back and forth: spread over the whole trip there and back
            double c = 2 * L.length * i / n;
            if (c <= L.length) { m.s = c; m.dir = 1; } else { m.s = 2 * L.length - c; m.dir = -1; }
        }
        movers.push_back(m);
    }
}

// the next stop from s going dir (on demand, or always-moving with stops)
static double nextStop(const PathLine& L, const PlatformSettings& p, double s, int dir) {
    if (p.stopEvery) {
        if (dir > 0) { for (double a : L.at) if (a > s + 1e-6) return a; return L.loop ? L.length : L.length; }
        for (int i = (int)L.at.size() - 1; i >= 0; i--) if (L.at[i] < s - 1e-6) return L.at[i];
        return 0;
    }
    return dir > 0 ? L.length : 0;
}

void PlatformRun::tick(const PlatformSettings& p, double dt, const std::vector<bool>& stoodOn, const std::vector<bool>& shot) {
    PathLine L = lineOf(p);
    if (L.length <= 0) return;
    double speed = std::max(0.05f, p.speed);
    if (p.spawner) {   // a new one every few seconds; each goes once to the end and is gone
        spawnT += (float)dt;
        if (spawnT >= std::max(0.2f, p.every)) { spawnT = 0; Mover m; m.moving = true; movers.push_back(m); }
        for (Mover& m : movers) { m.s += speed * dt; if (m.s >= L.length) m.alive = false; }
        movers.erase(std::remove_if(movers.begin(), movers.end(), [](const Mover& m) { return !m.alive; }), movers.end());
        return;
    }
    for (size_t i = 0; i < movers.size(); i++) {
        Mover& m = movers[i];
        bool on = i < stoodOn.size() && stoodOn[i], hit = i < shot.size() && shot[i];
        if (m.pause > 0) { m.pause -= (float)dt; if (m.pause > 0) continue; }
        if (p.trigger != PathTrigger::Always && !m.moving) {   // on demand: waiting at a stop
            m.idle = on ? 0 : m.idle + (float)dt;
            if (!on) m.needOff = false;
            bool go = (p.trigger == PathTrigger::StandOn && on && !m.needOff) || (p.trigger == PathTrigger::Shot && hit);
            bool home = p.comeBack > 0 && m.idle >= p.comeBack && m.s > 1e-6;
            if (go || home) {
                int dir = m.dir;
                if (home) dir = L.loop ? 1 : -1;
                else if (!L.loop) { if (m.s >= L.length - 1e-6) dir = -1; else if (m.s <= 1e-6) dir = 1; }
                if (p.mode == PathMode::Once && m.s >= L.length - 1e-6 && !home) continue;   // at the end for good
                m.dir = dir; m.target = home ? (L.loop ? L.length : 0) : nextStop(L, p, m.s, dir); m.moving = true; m.idle = 0;
            } else continue;
        }
        if (!m.moving) continue;
        double step = speed * dt * m.dir, ns = m.s + step;
        if (p.trigger != PathTrigger::Always) {   // on demand: stop at the target
            if ((m.dir > 0 && ns >= m.target) || (m.dir < 0 && ns <= m.target)) {
                ns = m.target; m.moving = false; m.pause = p.wait; m.needOff = on;
                if (L.loop && ns >= L.length - 1e-6) ns = 0;
            }
            m.s = ns; continue;
        }
        // always moving: stops at the ends (or at every point), turning round or wrapping
        double stop = nextStop(L, p, m.s, m.dir);
        bool reached = (m.dir > 0 && ns >= stop) || (m.dir < 0 && ns <= stop);
        if (reached) {
            ns = stop; m.pause = p.wait;
            if (L.loop) { if (ns >= L.length - 1e-6) ns = 0; }
            else if (p.mode == PathMode::Once) { if (ns >= L.length - 1e-6) { m.moving = false; m.pause = 0; } }
            else if (ns >= L.length - 1e-6) m.dir = -1; else if (ns <= 1e-6) m.dir = 1;
        }
        m.s = ns;
    }
}

std::vector<V3> PlatformRun::offsets(const PlatformSettings& p) const {
    PathLine L = lineOf(p); std::vector<V3> o;
    for (const Mover& m : movers) o.push_back(L.pos(m.s));
    return o;
}

bool standsOn(const Box3& b, V3 f, double h) {
    return f.y >= b.hi.y - 0.08 && f.y <= b.hi.y + 0.12 && f.x + h > b.lo.x && f.x - h < b.hi.x && f.z + h > b.lo.z && f.z - h < b.hi.z;
}

} // namespace mc
