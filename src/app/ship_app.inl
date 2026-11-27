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
    V3 ShipSpot(const WorldObject& o, V3 local) const { return shipSpot(o, local); }
    const MeshModel* ShipModel(const WorldObject& o) const { auto it = meshModels.find(o.model); return it == meshModels.end() ? nullptr : &it->second; }
    SolidAt CamSolid() const {   // (the ready-made cutscenes keep their cameras out of the ground)
        return [this](V3 p) { if (!world) return false; int x = ifloor(p.x), y = ifloor(p.y), z = ifloor(p.z); if (!world->loaded(x, y, z)) return false;
            BlockId b = world->get(x, y, z); return b != AIR && reg.get(b).render == Render::Opaque; };
    }
    WorldObject* ShipInReach() {   // the gunship you stand by (its lift, or anywhere close round it)
        if (!playMode || player.ball || play.dead() || cutRun.on || shipRun.step != ShipStep::None) return nullptr;
        for (WorldObject& o : objects) {
            if (!o.ship.site || Gone(o)) continue;
            V3 l = ShipSpot(o, o.ship.lift); double dx = player.feet.x - l.x, dz = player.feet.z - l.z, dy = player.feet.y - l.y;
            if (dx * dx + dz * dz < 4.5 * 4.5 && dy > -1.5 && dy < 3) return &o;
        }
        return nullptr;
    }

    // ---- the steps
    void BoardShip(WorldObject& o) {
        shipRun = ShipRun{}; shipRun.step = ShipStep::Boarding; shipRun.id = o.id;
        PlayCutsceneData(shipBoardCut(o, player.feet, CamSolid())); player.vel = { 0, 0, 0 };
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
        if (own) PlayCutscene(o->ship.takeoffCut); else PlayCutsceneData(shipTakeoffCut(*o, sitesVisited.count(SiteName(*o, CurrentWorld())) > 0, CamSolid(), ShipModel(*o)));   // (the short one from where you landed before)
    }
    void ArriveNow() {   // (the world is there now): the landing at the site's gunship
        WorldObject* o = nullptr; for (WorldObject& x : objects) if (x.name == shipRun.arriveAt && x.ship.site) o = &x;
        shipRun.step = ShipStep::Arriving;
        if (!o) { shipRun.step = ShipStep::None; return; }
        shipRun.id = o->id;
        std::string name = SiteName(*o, CurrentWorld());
        bool own = !o->ship.landCut.empty() && findCutscene(cutscenes, o->ship.landCut) >= 0;
        if (own) PlayCutscene(o->ship.landCut); else PlayCutsceneData(shipLandingCut(*o, shipRun.arriveShort, CamSolid(), ShipModel(*o)));
        sitesVisited.insert(name);
        // where you stand after it: beside the ship, out from under its wing, looking away from it
        V3 out = shipExitSpot(*o);
        player.feet = out; player.vel = { 0, 0, 0 }; player.yaw = shipExitYaw(*o); player.pitch = 0; playSpawn = out; playYaw = player.yaw;
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
