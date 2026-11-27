    // ================================================================== the gunship: landing sites, boarding, the ship's menu, travel
    // (included inside struct App)
    // A landing site is a world object (the gunship) with "This is a landing site" ticked (ShipSettings). Interact with it: you step
    // onto its lift (a short shot from outside), the ship's menu opens: Save, Travel, Leave. Leave: you are where you stood before.
    // Travel: the sites of every world that are unlocked; the ship lifts you in, takes off, hops to the edge of space and lands there.
    // The take-off and landing are made here (ready-made, fitted to each pad), unless the site names cutscenes of its own.
    std::set<std::string> sitesOpen, sitesClosed, sitesVisited;   // (saved with the game)
    struct SiteRef { std::string world, object, name; AreaKind area = AreaKind::Plain; float mapX = 0.5f, mapY = 0.5f; bool open = true, here = false; };
    enum class ShipStep { None, Boarding, Menu, Leaving, Departing, Arriving };
    struct ShipRun { ShipStep step = ShipStep::None; uint32_t id = 0; bool map = false; std::vector<SiteRef> sites; SiteRef dest; std::string arriveAt; bool arriveShort = false; } shipRun;
    bool shipMenu = false;

    static std::string SiteName(const WorldObject& o, const std::string& world) { return o.ship.siteName.empty() ? world : o.ship.siteName; }
    bool SiteIsOpen(const std::string& name, bool byDefault) const { if (sitesClosed.count(name)) return false; return byDefault || sitesOpen.count(name); }
    std::vector<SiteRef> AllSites() {   // every world's landing sites (this world's as they are now; the others' from their files)
        std::vector<SiteRef> v; std::string cur = CurrentWorld();
        auto add = [&](const std::vector<WorldObject>& objs, const std::string& w) {
            for (const WorldObject& o : objs) if (o.ship.site) {
                SiteRef r; r.world = w; r.object = o.name; r.name = SiteName(o, w); r.area = o.ship.area; r.mapX = o.ship.mapX; r.mapY = o.ship.mapY;
                r.open = SiteIsOpen(r.name, o.ship.open); r.here = w == cur && o.id == shipRun.id; v.push_back(r);
            }
        };
        add(objects, cur);
        for (const std::string& w : worldList) if (w != cur) { std::vector<WorldObject> objs; loadObjects(WorldsDir() + "/" + w + "/objects.txt", objs); add(objs, w); }
        return v;
    }
    V3 ShipSpot(const WorldObject& o, V3 local) const { return effectSpot(o.pos, o.yaw, local * o.scale); }
    WorldObject* ShipInReach() {   // the gunship you stand by (its lift, or anywhere close round it)
        if (!playMode || player.ball || play.dead() || cutRun.on || shipRun.step != ShipStep::None) return nullptr;
        for (WorldObject& o : objects) {
            if (!o.ship.site || Gone(o)) continue;
            V3 l = ShipSpot(o, o.ship.lift); double dx = player.feet.x - l.x, dz = player.feet.z - l.z, dy = player.feet.y - l.y;
            if (dx * dx + dz * dz < 4.5 * 4.5 && dy > -1.5 && dy < 3) return &o;
        }
        return nullptr;
    }

    // ---- camera keys that look at a spot
    static CamKey LookKey(float t, V3 from, V3 at, float fov = 55, float roll = 0) {
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
    Cutscene MakeBoardCut(const WorldObject& o) {
        Cutscene c; c.name = "(boarding)"; c.view = CutView::Camera; c.length = 100000; c.skippable = false; c.letterbox = true; c.fromPlayer = true; c.blendOut = 0.9f;
        V3 lift = ShipSpot(o, o.ship.lift), mid = ShipSpot(o, { 0, 1.6, 0 });
        V3 side = effectSpot({ 0, 0, 0 }, o.yaw, { 1, 0, 0 }), front = effectSpot({ 0, 0, 0 }, o.yaw, { 0, 0, 1 });
        V3 from = player.feet; double dx = from.x - lift.x, dz = from.z - lift.z; bool right = dx * side.x + dz * side.z >= 0;   // the side you came from
        V3 cam = lift + side * ((right ? 1 : -1) * 6.5 * o.scale) + front * (3.0 * o.scale) + V3{ 0, 1.1, 0 };
        c.cam.push_back(LookKey(1.6f, cam, lift * 0.55 + mid * 0.45 + V3{ 0, 0.4, 0 }, 50));
        c.cam.back().ease = true;
        if (!o.ship.pilot.empty()) {
            CutActor p = PilotWalk(o.ship.pilot, from, lift, 0.2f, 1.6f);
            V3 out = cam - lift; p.keys.push_back(ShipKey(2.1f, lift, std::atan2(out.x, out.z) * 180 / 3.14159265358979)); p.keys.back().ease = true;   // turns round on the platform
            c.actors.push_back(p);
        }
        return c;
    }
    // ---- the take-off: the lift takes the pilot in, the gear comes up, the ship rises, noses up and climbs away
    Cutscene MakeTakeoffCut(const WorldObject& o, bool shortOne) {
        Cutscene c; c.name = "(take-off)"; c.view = CutView::Camera; c.skippable = true; c.letterbox = true; c.blendOut = 0;
        const ShipSettings& S = o.ship; double sc = o.scale; std::string ship = "Gunship";
        V3 P = o.pos, lift = ShipSpot(o, S.lift);
        auto L = [&](double x, double y, double z) { return ShipSpot(o, { x, y, z }); };
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
    Cutscene MakeLandingCut(const WorldObject& o, bool shortOne) {
        Cutscene c; c.name = "(landing)"; c.view = CutView::Camera; c.skippable = true; c.letterbox = true; c.handOff = false; c.blendOut = 1.0f;
        const ShipSettings& S = o.ship; double sc = o.scale; std::string ship = "Gunship";
        V3 P = o.pos, lift = ShipSpot(o, S.lift);
        V3 fwd = effectSpot({ 0, 0, 0 }, o.yaw, { 0, 0, 1 }), side = effectSpot({ 0, 0, 0 }, o.yaw, { 1, 0, 0 });
        auto L = [&](double x, double y, double z) { return ShipSpot(o, { x, y, z }); };
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
        a.keys.push_back(ShipKey(T + 0.0f, hi, o.yaw, -14, 10));
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

    // ---- the steps
    void BoardShip(WorldObject& o) {
        shipRun = ShipRun{}; shipRun.step = ShipStep::Boarding; shipRun.id = o.id;
        PlayCutsceneData(MakeBoardCut(o)); player.vel = { 0, 0, 0 };
    }
    void OpenShipMenu() { shipRun.step = ShipStep::Menu; shipRun.map = false; shipMenu = true; paused = true; menuSel = -2; Capture(false); }
    void CloseShipMenu() { shipMenu = false; paused = false; Capture(true); }
    void LeaveShip() {   // back where you stood (the view glides back)
        CloseShipMenu(); shipRun.step = ShipStep::Leaving;
        if (cutRun.on) { cutRun.endFrom = CutPoseRaw(); cutRun.ending = true; cutRun.endT = 0; }
    }
    void DepartTo(const SiteRef& d) {
        const WorldObject* o = ObjById(shipRun.id); if (!o) { LeaveShip(); return; }
        CloseShipMenu(); shipRun.dest = d; shipRun.step = ShipStep::Departing;
        if (cutRun.on) EndCutscene();
        bool own = !o->ship.takeoffCut.empty() && findCutscene(cutscenes, o->ship.takeoffCut) >= 0;
        if (own) PlayCutscene(o->ship.takeoffCut); else PlayCutsceneData(MakeTakeoffCut(*o, sitesVisited.count(SiteName(*o, CurrentWorld())) > 0));   // (the short one from where you landed before)
    }
    void ArriveNow() {   // (the world is there now): the landing at the site's gunship
        WorldObject* o = nullptr; for (WorldObject& x : objects) if (x.name == shipRun.arriveAt && x.ship.site) o = &x;
        shipRun.step = ShipStep::Arriving;
        if (!o) { shipRun.step = ShipStep::None; return; }
        shipRun.id = o->id;
        std::string name = SiteName(*o, CurrentWorld());
        bool own = !o->ship.landCut.empty() && findCutscene(cutscenes, o->ship.landCut) >= 0;
        if (own) PlayCutscene(o->ship.landCut); else PlayCutsceneData(MakeLandingCut(*o, shipRun.arriveShort));
        sitesVisited.insert(name);
        // where you stand after it: beside the ship, out from under its wing, looking away from it
        V3 side = effectSpot({ 0, 0, 0 }, o->yaw, { 1, 0, 0 }), fwd = effectSpot({ 0, 0, 0 }, o->yaw, { 0, 0, 1 });
        V3 out = ShipSpot(*o, o->ship.lift) + side * (4.6 * o->scale) + fwd * (0.4 * o->scale);
        player.feet = out; player.vel = { 0, 0, 0 }; player.yaw = std::atan2(side.x, side.z); player.pitch = 0; playSpawn = out; playYaw = player.yaw;
    }
    void UpdateShip() {
        if (!playMode) { if (shipRun.step != ShipStep::None) { shipRun = ShipRun{}; shipMenu = false; } return; }
        switch (shipRun.step) {
        case ShipStep::Boarding: if (!cutRun.on) { shipRun.step = ShipStep::None; break; } if (cutRun.t >= 2.2) OpenShipMenu(); break;
        case ShipStep::Leaving: if (!cutRun.on) shipRun.step = ShipStep::None; break;
        case ShipStep::Departing:
            if (cutRun.on) break;
            {   // the take-off is over (or skipped): fly there
                SiteRef d = shipRun.dest; shipRun.arriveAt = d.object; shipRun.arriveShort = sitesVisited.count(d.name) > 0;
                blackIn = 1;
                TravelTo(d.world == CurrentWorld() ? std::string() : d.world, d.object);
                bigMsg.clear(); bigMsgT = 0;
                if (!loadingOn) ArriveNow(); else shipRun.step = ShipStep::Arriving, shipRun.id = 0;
            }
            break;
        case ShipStep::Arriving:
            if (shipRun.id == 0) { if (!loadingOn) ArriveNow(); break; }
            if (!cutRun.on) { shipRun.step = ShipStep::None; Message(shipRun.dest.name); }
            break;
        default: break;
        }
    }

    // ---- the editor: a model that is the gunship at a landing site
    void ShipInspector(WorldObject& o) {
        ImGui::SeparatorText("Landing site");
        Prop("This is a landing site"); ImGui::Checkbox("##shs", &o.ship.site);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("This model is the gunship standing at a landing site. Play: walk up to it and press E - onto its lift, its menu (Save, Travel, Leave).");
        if (!o.ship.site) return;
        ShipSettings& S = o.ship;
        Hint("Travel lists the landing sites of every world that are unlocked. The ship takes off here, hops to the edge of space and lands there (ready-made cutscenes fitted to each pad, or your own).");
        Prop("Name on the map"); { char b[128]; snprintf(b, sizeof b, "%s", S.siteName.c_str()); ImGui::SetNextItemWidth(-1); if (ImGui::InputTextWithHint("##shn", CurrentWorld().c_str(), b, sizeof b)) S.siteName = b; }
        Prop("What kind of place"); { int a = (int)S.area; ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##sha", areaKindName(S.area))) { for (int i = 0; i < (int)AreaKind::COUNT; i++) if (ImGui::Selectable(areaKindName((AreaKind)i), i == a)) S.area = (AreaKind)i; ImGui::EndCombo(); } }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How the landing looks: dust, snow, ash and embers, sand, spray off the water...");
        Prop("Open from the start"); ImGui::Checkbox("##sho", &S.open);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off: it is on the map only once an event card says \"unlock the landing site\" with this name.");
        Prop("On the map"); { float xy[2] = { S.mapX, S.mapY }; ImGui::SetNextItemWidth(-1); if (ImGui::SliderFloat2("##shm", xy, 0, 1, "%.2f")) { S.mapX = xy[0]; S.mapY = xy[1]; } }
        Prop("Pilot (the player's model)"); { ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##shp", S.pilot.empty() ? "(none shown)" : S.pilot.c_str())) {
                if (ImGui::Selectable("(none shown)", S.pilot.empty())) S.pilot.clear();
                for (auto& mm : meshModels) if (ImGui::Selectable(mm.first.c_str(), mm.first == S.pilot)) S.pilot = mm.first;
                ImGui::EndCombo(); } }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shown in the ready-made cutscenes: stepping onto the lift, riding it up, coming out after landing.");
        Prop("Lift platform"); ImGui::SetNextItemWidth(-1); ImGui::DragScalarN("##shl", ImGuiDataType_Double, &S.lift.x, 3, 0.02f, nullptr, nullptr, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where you stand to board: the ship's own x (right), y (up), z (front). A green ring shows it.");
        auto cutCombo = [&](const char* label, const char* id, std::string& v) {
            Prop(label); ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo(id, v.empty() ? "(ready-made)" : v.c_str())) {
                if (ImGui::Selectable("(ready-made)", v.empty())) v.clear();
                for (const Cutscene& c : cutscenes) if (ImGui::Selectable(c.name.c_str(), c.name == v)) v = c.name;
                ImGui::EndCombo(); }
        };
        cutCombo("Landing cutscene", "##shlc", S.landCut); cutCombo("Take-off cutscene", "##shtc", S.takeoffCut);
        if (ImGui::TreeNode("Thrusters")) {
            Hint("Spots on the ship where the fire comes out (its own x right, y up, z front). Flying: all of these. Landing: the braking ones.");
            auto list = [&](const char* title, std::vector<V3>& v, const char* id) {
                ImGui::TextDisabled("%s", title); ImGui::PushID(id);
                for (int i = 0; i < (int)v.size(); i++) { ImGui::PushID(i); ImGui::SetNextItemWidth(-30); ImGui::DragScalarN("##t", ImGuiDataType_Double, &v[i].x, 3, 0.02f, nullptr, nullptr, "%.2f"); ImGui::SameLine(); if (ImGui::SmallButton("x")) { v.erase(v.begin() + i); ImGui::PopID(); break; } ImGui::PopID(); }
                if (ImGui::SmallButton("+ add")) v.push_back({ 0, 1, -2 });
                ImGui::PopID();
            };
            list("Flying", S.thrusters, "fly"); list("Landing (braking)", S.landThrusters, "land");
            ImGui::TreePop();
        }
    }
    // ---- the ship's menu (a pane like the Save Station's question)
    MenuPane ShipMenuPane() {
        MenuPane p; p.name = "Gunship"; p.pos = { 0, -0.02, 0.85 }; p.width = 0.9f; p.color = 0x6FD8FF; p.backing = 0.72f; p.bendX = 0.3f; p.bendY = 0.12f;
        auto text = [&](const std::string& t, float x, float y, float w, float h, float size, float al, int align = 0) { MenuItem i; i.kind = MenuKind::Text; i.text = t; i.x = x; i.y = y; i.w = w; i.h = h; i.textSize = size; i.alpha = al; i.color = 0xCFF2FF; i.align = align; p.items.push_back(i); };
        auto button = [&](const std::string& t, float x, float y, float w, float h, const std::string& tg, float size = 40) { MenuItem i; i.kind = MenuKind::Button; i.text = t; i.x = x; i.y = y; i.w = w; i.h = h; i.textSize = size; i.color = 0x6FD8FF; i.target = tg; p.items.push_back(i); };
        if (!shipRun.map) {
            p.height = 0.46f;
            text("GUNSHIP", 0.06f, 0.06f, 0.88f, 0.15f, 54, 1);
            const WorldObject* o = ObjById(shipRun.id);
            text(o ? "Landing site: " + SiteName(*o, CurrentWorld()) : std::string(), 0.06f, 0.24f, 0.88f, 0.1f, 28, 0.7f);
            button("Save", 0.06f, 0.5f, 0.27f, 0.3f, "ship:save"); button("Travel", 0.365f, 0.5f, 0.27f, 0.3f, "ship:travel"); button("Leave", 0.67f, 0.5f, 0.27f, 0.3f, "ship:leave");
            return p;
        }
        int n = 0; for (const SiteRef& s : shipRun.sites) n += s.open ? 1 : 0;
        int rows = std::max(1, std::min(n, 8)); p.height = 0.3f + 0.075f * rows;
        float H = p.height, rowH = 0.075f / H;
        text("TRAVEL  -  OSSYRA", 0.06f, 0.035f / H, 0.88f, 0.07f / H, 46, 1);
        int i = 0, k = 0;
        for (const SiteRef& s : shipRun.sites) {
            if (s.open && k < 8) {
                float y = (0.12f + 0.075f * k) / H;
                if (s.here) text(s.name + "   (you are here)", 0.08f, y, 0.84f, rowH * 0.9f, 32, 0.45f);
                else button(s.name + "   -   " + areaKindName(s.area), 0.06f, y, 0.88f, rowH * 0.9f, "ship:go:" + std::to_string(i), 32);
                k++;
            }
            i++;
        }
        if (n == 0) text("No landing sites yet.", 0.08f, 0.13f / H, 0.84f, rowH, 30, 0.6f);
        button("Back", 0.35f, (0.14f + 0.075f * rows) / H, 0.3f, 0.12f / H, "ship:back", 34);
        return p;
    }
    bool ShipMenuDo(const std::string& tg) {
        if (tg.rfind("ship:", 0) != 0) return false;
        if (tg == "ship:leave") LeaveShip();
        else if (tg == "ship:back") { shipRun.map = false; menuSel = -2; }
        else if (tg == "ship:travel") { shipRun.sites = AllSites(); shipRun.map = true; menuSel = -2; }
        else if (tg == "ship:save") { saveStationId = shipRun.id; bool ok = WriteSave(); if (ok) GameSnd(&PlayerSounds::saved); }
        else if (tg.rfind("ship:go:", 0) == 0) { int i = std::atoi(tg.c_str() + 8); if (i >= 0 && i < (int)shipRun.sites.size() && shipRun.sites[i].open) DepartTo(shipRun.sites[i]); }
        return true;
    }
