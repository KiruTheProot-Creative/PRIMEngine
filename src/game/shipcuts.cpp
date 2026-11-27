#include "shipcuts.h"
#include <cmath>

namespace mc {

V3 shipSpot(const WorldObject& o, V3 local) { return effectSpot(o.pos, o.yaw, local * o.scale); }
V3 shipExitSpot(const WorldObject& o) {
    V3 side = effectSpot({ 0, 0, 0 }, o.yaw, { 1, 0, 0 }), fwd = effectSpot({ 0, 0, 0 }, o.yaw, { 0, 0, 1 });
    return shipSpot(o, o.ship.lift) + side * (4.6 * o.scale) + fwd * (0.4 * o.scale);
}
double shipExitYaw(const WorldObject& o) { V3 side = effectSpot({ 0, 0, 0 }, o.yaw, { 1, 0, 0 }); return std::atan2(side.x, side.z); }

// ---- camera keys that look at a spot
static thread_local const SolidAt* gSolid = nullptr;   // (while one is made)
static V3 Clear(V3 p) {   // lifted out of the ground (and walls) until a little ball round it is all air
    if (!gSolid || !*gSolid) return p;
    for (int up = 0; up < 48; up++) {
        bool hit = false; for (int dx = -1; dx <= 1 && !hit; dx++) for (int dy = -1; dy <= 1 && !hit; dy++) for (int dz = -1; dz <= 1 && !hit; dz++) hit = (*gSolid)(p + V3{ dx * 0.35, dy * 0.35, dz * 0.35 });
        if (!hit) return p; p.y += 0.5;
    }
    return p;
}
static CamKey LookKey(float t, V3 from, V3 at, float fov = 55, float roll = 0) {
    from = Clear(from);
    CamKey k; k.t = t; k.pos = from; V3 d = at - from; double h = std::sqrt(d.x * d.x + d.z * d.z);
    k.yaw = (float)(std::atan2(d.x, d.z) * 180 / 3.14159265358979); k.pitch = (float)(std::atan2(d.y, std::max(1e-6, h)) * 180 / 3.14159265358979); k.fov = fov; k.roll = roll; return k;
}
static ActorKey ShipKey(float t, V3 p, double yaw, float pitch = 0, float roll = 0, bool ease = false) { ActorKey k; k.t = t; k.pos = p; k.yaw = (float)yaw; k.pitch = pitch; k.roll = roll; k.ease = ease; return k; }
static const char* LandingDustFor(AreaKind a) {
    switch (a) { case AreaKind::Ice: return "Landing snow"; case AreaKind::Lava: return "Landing ash"; case AreaKind::Desert: return "Landing sand"; case AreaKind::Ocean: return "Landing spray"; default: return "Landing dust"; }
}
static void AddJets(Cutscene& c, const std::string& actor, const std::vector<V3>& at, const char* preset, float t, float len, float size, double scale) {
    for (const V3& p : at) { CutEffect e; e.t = t; e.length = len; e.actor = actor; e.preset = preset; e.offset = p * scale; e.size = size; c.effects.push_back(e); }
}
static void AddClip(CutActor& a, const char* clip, float t, float len, bool once, float speed = 1) { ActorClip k; k.t = t; k.length = len; k.clip = clip; k.once = once; k.speed = speed; a.clips.push_back(k); }
// the pilot (the player's model): on foot from one spot to another, then standing
static CutActor PilotWalk(const std::string& model, V3 from, V3 to, float t0, float t1, float showFrom = 0) {
    CutActor a; a.name = "Pilot"; a.model = model; a.showFrom = showFrom;
    V3 d = to - from; double yaw = std::atan2(d.x, d.z) * 180 / 3.14159265358979;
    ActorKey k0; k0.t = t0; k0.pos = from; k0.yaw = (float)yaw; ActorKey k1; k1.t = t1; k1.pos = to; k1.yaw = (float)yaw; a.keys = { k0, k1 };
    if (t1 - t0 > 0.05f) AddClip(a, "walk", t0, t1 - t0, false);
    AddClip(a, "idle", t1, 1000, false);
    return a;
}

// ---- stepping onto the lift: the camera swings out, the pilot walks onto the platform; it holds there while the menu is open
Cutscene shipBoardCut(const WorldObject& o, V3 from, const SolidAt& solid) {
    gSolid = &solid; struct Done { ~Done() { gSolid = nullptr; } } done;
    Cutscene c; c.name = "(boarding)"; c.view = CutView::Camera; c.length = 100000; c.skippable = false; c.letterbox = true; c.fromPlayer = true; c.blendOut = 0.9f;
    V3 lift = shipSpot(o, o.ship.lift), mid = shipSpot(o, { 0, 1.6, 0 });
    V3 side = effectSpot({ 0, 0, 0 }, o.yaw, { 1, 0, 0 }), front = effectSpot({ 0, 0, 0 }, o.yaw, { 0, 0, 1 });
    double dx = from.x - lift.x, dz = from.z - lift.z; bool right = dx * side.x + dz * side.z >= 0;   // the side you came from
    V3 cam = lift + side * ((right ? 1 : -1) * 6.5 * o.scale) + front * (3.0 * o.scale) + V3{ 0, 1.1, 0 };
    c.cam.push_back(LookKey(1.6f, cam, lift * 0.55 + mid * 0.45 + V3{ 0, 0.4, 0 }, 50));
    c.cam.back().ease = true;
    if (!o.ship.pilot.empty()) {
        CutActor p = PilotWalk(o.ship.pilot, from, lift, 0.2f, 1.6f); p.keys[1].ease = true;   // (stops on the platform; then only turns)
        V3 out = cam - lift; p.keys.push_back(ShipKey(2.1f, lift, std::atan2(out.x, out.z) * 180 / 3.14159265358979)); p.keys.back().ease = true;   // turns round on the platform
        c.actors.push_back(p);
    }
    return c;
}
// ---- the take-off: the lift takes the pilot in, the gear comes up, the ship rises, noses up and climbs away
Cutscene shipTakeoffCut(const WorldObject& o, bool shortOne, const SolidAt& solid) {
    gSolid = &solid; struct Done { ~Done() { gSolid = nullptr; } } done;
    Cutscene c; c.name = "(take-off)"; c.view = CutView::Camera; c.skippable = true; c.letterbox = true; c.blendOut = 0;
    const ShipSettings& S = o.ship; double sc = o.scale; std::string ship = "Gunship";
    V3 P = o.pos, lift = shipSpot(o, S.lift);
    auto L = [&](double x, double y, double z) { return shipSpot(o, { x, y, z }); };
    float T0 = shortOne ? 0.f : 1.8f;   // (the long one: the pilot rides the lift up first)
    CutActor a; a.name = ship; a.object = o.name;
    a.keys.push_back(ShipKey(0, P, o.yaw)); a.keys.push_back(ShipKey(T0 + 0.6f, P, o.yaw, 0, 0, true));
    a.keys.push_back(ShipKey(T0 + 2.4f, P + V3{ 0, 5 * sc, 0 }, o.yaw, 4, -3, false));
    a.keys.push_back(ShipKey(T0 + 3.6f, P + V3{ 0, 8 * sc, 0 }, o.yaw, 16, 4, false));
    V3 fwd = effectSpot({ 0, 0, 0 }, o.yaw, { 0, 0, 1 });
    a.keys.push_back(ShipKey(T0 + 6.0f, P + V3{ 0, 40 * sc, 0 } + fwd * (70 * sc), o.yaw, 28, 18, false));
    AddClip(a, "Taking off", T0 + 1.0f, 3, true);
    c.actors.push_back(a);
    if (!shortOne && !S.pilot.empty()) {   // on the platform, rising into the hatch
        CutActor p; p.name = "Pilot"; p.model = S.pilot; p.showTo = T0;
        V3 face = effectSpot({ 0, 0, 0 }, o.yaw, { 1, 0, 0 }); double yaw = std::atan2(face.x, face.z) * 180 / 3.14159265358979;
        p.keys = { ShipKey(0, lift, yaw), ShipKey(0.3f, lift, yaw, 0, 0, true), ShipKey(T0, lift + V3{ 0, 1.9 * sc, 0 }, yaw) };
        AddClip(p, "idle", 0, 1000, false); c.actors.push_back(p);
        CamKey k0 = LookKey(0, lift + effectSpot({ 0, 0, 0 }, o.yaw, { 4.5 * sc, 0.4, 1.5 * sc }), lift + V3{ 0, 1.4, 0 }, 45); k0.cut = true;
        CamKey k1 = LookKey(T0, lift + effectSpot({ 0, 0, 0 }, o.yaw, { 4.2 * sc, 0.5, 1.3 * sc }), lift + V3{ 0, 2.4 * sc, 0 }, 45);
        c.cam.push_back(k0); c.cam.push_back(k1);
    }
    // low, beside it, looking up as it lifts; then it climbs past
    V3 cam1 = L(9, 0.6, 5); CamKey a0 = LookKey(T0, cam1, P + V3{ 0, 1.8 * sc, 0 }, 52); a0.cut = true; c.cam.push_back(a0);
    c.cam.push_back(LookKey(T0 + 3.2f, cam1 + V3{ 0, 0.3, 0 }, P + V3{ 0, 8.5 * sc, 0 }, 50));
    CamKey b0 = LookKey(T0 + 3.4f, L(-6, 9, -16), P + V3{ 0, 10 * sc, 0 } + fwd * (10 * sc), 60); b0.cut = true; c.cam.push_back(b0);
    c.cam.push_back(LookKey(T0 + 6.0f, L(-4, 14, -10), P + V3{ 0, 40 * sc, 0 } + fwd * (70 * sc), 46));
    AddJets(c, ship, S.landThrusters, "Thrusters", T0 + 0.5f, 3.0f, 1.2f, sc);   // lifting: the wings' jets push down
    AddJets(c, ship, S.thrusters, "Thruster jet", T0 + 2.8f, 4, 1.3f, sc);        // then the engines at the back light up
    { CutEffect d; d.t = T0 + 0.6f; d.length = 3.2f; d.actor = ship; d.preset = LandingDustFor(S.area); d.offset = { 0, 0.1, 0 }; d.size = 1.2f; c.effects.push_back(d); }
    CutFade f; f.t = T0 + 5.2f; f.length = 0.8f; f.kind = CutFadeKind::ToBlack; c.fades.push_back(f);
    c.length = T0 + 6.0f;
    return c;
}
// ---- the landing: out at the edge of space over the planet (it sways and rolls), burning back into the air, down over the pad,
// a shot from right under it as it comes down, the gear, touch-down, and the pilot out of the lift. Short: only the last part
Cutscene shipLandingCut(const WorldObject& o, bool shortOne, const SolidAt& solid) {
    gSolid = &solid; struct Done { ~Done() { gSolid = nullptr; } } done;
    Cutscene c; c.name = "(landing)"; c.view = CutView::Camera; c.skippable = true; c.letterbox = true; c.handOff = false; c.blendOut = 1.0f;
    const ShipSettings& S = o.ship; double sc = o.scale; std::string ship = "Gunship";
    V3 P = o.pos, lift = shipSpot(o, S.lift);
    V3 fwd = effectSpot({ 0, 0, 0 }, o.yaw, { 0, 0, 1 }), side = effectSpot({ 0, 0, 0 }, o.yaw, { 1, 0, 0 });
    auto L = [&](double x, double y, double z) { return shipSpot(o, { x, y, z }); };
    CutActor a; a.name = ship; a.object = o.name;
    float T = 0;
    if (!shortOne) {
        // 1) space: the ship flies down at the planet, swaying, with a little barrel roll (the camera rides beside it)
        CutSpace sp; sp.t = 0; sp.length = 7.5f; sp.yaw = (float)o.yaw; sp.pitch = -38; sp.size = 62; sp.spin = 0.8f; sp.stars = 0.7f; sp.nebula = 0.3f; sp.sunYaw = (float)o.yaw + 70; sp.sunPitch = 20;
        c.space.push_back(sp);
        V3 s0 = P + V3{ 0, 900, 0 };   // (out there it does not matter where: the world is hidden)
        auto at = [&](double back, double up) { return s0 - fwd * back + V3{ 0, up, 0 }; };
        a.keys.push_back(ShipKey(0.0f, at(60, 18), o.yaw, -8, -14));
        a.keys.push_back(ShipKey(1.2f, at(46, 13), o.yaw + 6, -12, 22));
        a.keys.push_back(ShipKey(2.4f, at(33, 9), o.yaw - 5, -15, -18));
        a.keys.push_back(ShipKey(3.4f, at(22, 5), o.yaw + 3, -18, -18 + 360));   // a barrel roll
        a.keys.push_back(ShipKey(4.6f, at(10, 1), o.yaw - 4, -22, 360 + 16));
        a.keys.push_back(ShipKey(5.6f, at(0, -4), o.yaw + 2, -26, 360 - 12));
        AddClip(a, "flight position", 0, 7.5f, true);
        CamKey k0 = LookKey(0.0f, at(60, 18) + side * 7.0 + fwd * 9.0 + V3{ 0, 2.0, 0 }, at(60, 18), 50); k0.cut = true; c.cam.push_back(k0);
        c.cam.push_back(LookKey(3.0f, at(28, 8) + side * 6.0 + fwd * 10.0 + V3{ 0, 1.0, 0 }, at(25, 6), 46));
        c.cam.push_back(LookKey(5.4f, at(4, -3) + side * 4.0 + fwd * 14.0 + V3{ 0, -1.0, 0 }, at(0, -4), 40, -4));
        AddJets(c, ship, S.thrusters, "Thruster jet", 0, 5.6f, 1.3f, sc);
        // 2) burning in: the ship dives, fire all round it, the camera shakes along (still out there: the planet huge below)
        a.keys.push_back(ShipKey(5.8f, at(-2, -6), o.yaw, -30, 360));
        a.keys.push_back(ShipKey(7.5f, at(-30, -24), o.yaw + 1, -34, 360 + 6));
        CamKey r0 = LookKey(5.8f, at(-2, -6) - fwd * 9.0 + side * 2.5 + V3{ 0, -2.5, 0 }, at(-2, -6), 58, 3); r0.cut = true; c.cam.push_back(r0);
        for (int i = 1; i <= 6; i++) { double f = i / 6.0; V3 sh = at(-2 - 28 * f, -6 - 18 * f); c.cam.push_back(LookKey(5.8f + 1.7f * (float)f, sh - fwd * 9.0 + side * (2.5 + (i % 2 ? 0.35 : -0.35)) + V3{ 0, -2.5 + (i % 2 ? 0.25 : -0.2), 0 }, sh, 58, (i % 2 ? 4.f : 1.f))); }
        { CutEffect e; e.t = 5.9f; e.length = 1.7f; e.actor = ship; e.preset = "Re-entry fire"; e.offset = { 0, 1.8 * sc, 2.5 * sc }; e.size = 1.5f; c.effects.push_back(e); }
        { CutFade f; f.t = 7.2f; f.length = 0.45f; f.kind = CutFadeKind::Flash; c.fades.push_back(f); }
        T = 7.5f;
    }
    // 3) down over the pad: coming in from behind and above, slowing; the wings' jets brake
    V3 hi = P + V3{ 0, 26 * sc, 0 } - fwd * (34 * sc), low = P + V3{ 0, 7 * sc, 0 } - fwd * (2 * sc), hover = P + V3{ 0, 2.2 * sc, 0 };
    a.keys.push_back(ShipKey(T + 0.0f, hi, o.yaw, -14, 10)); a.keys.back().cut = !shortOne;   // (a jump from out there: no glide through the planet)
    a.keys.push_back(ShipKey(T + 2.6f, low, o.yaw, 5, -3));
    a.keys.push_back(ShipKey(T + 4.4f, hover, o.yaw, 1, 1, false));
    a.keys.push_back(ShipKey(T + 5.6f, P, o.yaw, 0, 0, true));
    if (shortOne) AddClip(a, "flight position", T, 3.4f, true);
    AddClip(a, "landing", T + 3.4f, 3, true);
    { CamKey k = LookKey(T, L(14, 3.5, 12), hi, 50); k.cut = true; c.cam.push_back(k); }
    c.cam.push_back(LookKey(T + 2.4f, L(11, 2.5, 9), low, 48));
    // 4) from right under it: the camera on the ground looking up as it comes down over you (and drifts out from under it)
    { V3 under = L(1.5, 0.35, 4.8); CamKey k = LookKey(T + 2.5f, under, low + V3{ 0, 1.0, 0 }, 68, 6); k.cut = true; c.cam.push_back(k);
      c.cam.push_back(LookKey(T + 4.5f, under + side * (0.8 * sc) + fwd * (1.2 * sc), hover + V3{ 0, 1.2, 0 }, 66, 2)); }
    // 5) the touch-down, seen from the side; then the pilot comes down on the lift and walks out
    V3 out = lift + side * (4.6 * sc) + fwd * (0.4 * sc);
    { CamKey k = LookKey(T + 4.6f, L(10, 1.6, -7), P + V3{ 0, 1.6, 0 }, 44); k.cut = true; c.cam.push_back(k); }
    c.cam.push_back(LookKey(T + 6.2f, L(9.4, 1.5, -6.4), P + V3{ 0, 1.3, 0 }, 44));
    AddJets(c, ship, S.landThrusters, "Thrusters", T, 5.8f, 1.2f, sc);
    { CutEffect d; d.t = T + 3.6f; d.length = 3.2f; d.actor = ship; d.preset = LandingDustFor(S.area); d.offset = { 0, 0.1, 0 }; d.size = 1.3f; c.effects.push_back(d); }
    if (S.area == AreaKind::Lava) { CutEffect e; e.t = T + 3.6f; e.length = 4; e.actor = ship; e.preset = "Embers"; e.offset = { 0, 0.2, 0 }; e.size = 2.0f; c.effects.push_back(e); }
    { EventDo d; d.kind = Do::Shake; d.number = 0.5f; CutEvent ev; ev.t = T + 5.6f; ev.action = d; c.events.push_back(ev); }
    c.actors.push_back(a);
    float T2 = T + 6.4f;
    if (!S.pilot.empty()) {
        CutActor p; p.name = "Pilot"; p.model = S.pilot; p.showFrom = T2;
        double yaw = std::atan2(side.x, side.z) * 180 / 3.14159265358979;
        p.keys = { ShipKey(T2, lift + V3{ 0, 1.9 * sc, 0 }, yaw), ShipKey(T2 + 1.4f, lift, yaw, 0, 0, true), ShipKey(T2 + 1.6f, lift, yaw), ShipKey(T2 + 3.0f, out, yaw, 0, 0, true) };
        AddClip(p, "idle", T2, 1.6f, false); AddClip(p, "walk", T2 + 1.6f, 1.4f, false); AddClip(p, "idle", T2 + 3.0f, 1000, false);
        c.actors.push_back(p);
        CamKey k = LookKey(T2, lift + side * (5.6 * sc) + fwd * (3.2 * sc) + V3{ 0, 0.9, 0 }, lift + V3{ 0, 1.7 * sc, 0 }, 48); k.cut = true; c.cam.push_back(k);
        c.cam.push_back(LookKey(T2 + 1.5f, lift + side * (5.8 * sc) + fwd * (3.0 * sc) + V3{ 0, 0.8, 0 }, lift + V3{ 0, 1.0, 0 }, 48));
        c.cam.push_back(LookKey(T2 + 3.2f, out + side * 3.2 + fwd * 2.6 + V3{ 0, 1.3, 0 }, out + V3{ 0, 1.2, 0 }, 50));
        T2 += 3.4f;
    }
    c.length = T2;
    return c;
}

} // namespace mc
