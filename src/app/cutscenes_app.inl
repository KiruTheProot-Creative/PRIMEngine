    // ================================================================== cutscenes (src/game/cutscene.h): playing them, their actors, the editor
    // (included inside struct App)
    std::vector<Cutscene> cutscenes; bool cutsDirty = false;
    struct CutRun {
        bool on = false; Cutscene c; double t = 0, prevT = -1;
        bool ending = false; double endT = 0; CamPose endFrom;            // after the end: the view glides back to the player's
        double skipHeld = 0; float lookYaw = 0, lookPitch = 0;              // holding skip; looking around (Visor)
        std::map<int, size_t> typed;                                        // readouts: letters typed so far (a sound each)
    } cutRun;
    std::string cutQueued, cutAfterLoad; bool cutBootAfter = false, cutHoldKeys = false;   // a cutscene asked for (after this one / once loaded); the HUD boots up after it; skip keys still held
    struct CutActorRt { ModelGpu gpu; PoseBlender blend; bool posed = false; ActorPose pose; std::string model; };
    std::vector<CutActorRt> cutRt;   // per actor of the cutscene the world shows

    // ---- which cutscene the world shows now: the one playing, else the editor's (its window open)
    bool CutEdValid() const { return cutEd >= 0 && cutEd < (int)cutscenes.size(); }
    const Cutscene* CutShowing(float& t) const {
        if (cutRun.on) { t = (float)std::min(cutRun.t, (double)cutRun.c.length); return &cutRun.c; }
        if (cutsEdOpen && !playMode && CutEdValid()) { t = (float)cutT; return &cutscenes[cutEd]; }
        return nullptr;
    }
    bool CutBlocksInput() const { return cutRun.on || cutHoldKeys; }
    bool CutHidesHud() const { return cutRun.on && cutRun.c.view == CutView::Camera; }
    CamPose PlayerPose() const { CamPose p; p.pos = player.eye(); p.yaw = player.yaw * 180 / 3.14159265358979; p.pitch = player.pitch * 180 / 3.14159265358979; p.fov = settings.fov; p.valid = true; return p; }
    CamPose CutPoseRaw() const {   // the playing cutscene's view at its time (before gliding back)
        const Cutscene& c = cutRun.c; CamPose p = cameraAt(c, (float)std::min(cutRun.t, (double)c.length));
        if (!p.valid) p = PlayerPose();
        if (c.view == CutView::Visor) {
            if (c.lookOnly) p.pos = player.eye();
            if (c.lookAround > 0) { p.yaw += cutRun.lookYaw; p.pitch = std::clamp(p.pitch + cutRun.lookPitch, -89.0, 89.0); }
        }
        return p;
    }
    bool CutLooking() const { return !playMode && cutLook && cutsEdOpen && CutEdValid() && !cutscenes[cutEd].cam.empty(); }
    bool CutCamera(CamPose& out) const {   // the view a cutscene gives now (playing; or the editor looking through its camera)
        if (cutRun.on) {
            CamPose p = CutPoseRaw();
            if (cutRun.ending) { double k = std::clamp(cutRun.endT / std::max(0.01f, cutRun.c.blendOut), 0.0, 1.0); p = blendPose(cutRun.endFrom, PlayerPose(), k * k * (3 - 2 * k)); }
            out = p; return true;
        }
        if (CutLooking()) { out = cameraAt(cutscenes[cutEd], (float)cutT); return out.valid; }
        return false;
    }
    I3 CutStreamCenter() const {   // chunks load around the camera while a cutscene shows it somewhere else
        CamPose p; V3 at = CutCamera(p) ? p.pos : player.feet;
        return chunkOf(ifloor(at.x), ifloor(at.y), ifloor(at.z));
    }

    // ---- playing
    bool PlayCutscene(const std::string& name) {
        int i = findCutscene(cutscenes, name); if (i < 0) { Toast("There is no cutscene called \"" + name + "\""); return false; }
        if (cutRun.on) EndCutscene();
        cutRun = CutRun{}; cutRun.on = true; cutRun.c = cutscenes[i]; sortTracks(cutRun.c);
        Cutscene& c = cutRun.c;
        if (c.fromPlayer) {   // the first glide starts where the player looks
            CamPose pp = PlayerPose(); CamKey k; k.pos = pp.pos; k.yaw = (float)pp.yaw; k.pitch = (float)pp.pitch; k.fov = (float)pp.fov; k.t = 0;
            if (!c.cam.empty() && c.cam[0].t <= 0.001f) c.cam[0] = k; else c.cam.insert(c.cam.begin(), k);
        }
        cutRun.prevT = -1e-3; player.vel = { 0, 0, 0 }; lockId = 0;
        cutRt.clear(); cutRt.resize(c.actors.size());
        return true;
    }
    void CutAction(const EventDo& d) {
        switch (d.kind) {
        case Do::FlagOn: evRun.flags.insert(d.text.empty() ? d.target : d.text); break;
        case Do::FlagOff: evRun.flags.erase(d.text.empty() ? d.target : d.text); break;
        case Do::Wait: break;
        default: RunAction(d); break;
        }
    }
    void EndCutscene() {
        if (!cutRun.on) return;
        const Cutscene& c = cutRun.c;
        if (c.handOff) {   // the player stands where the camera ended, looking the same way
            CamPose p = cameraAt(c, c.length);
            if (p.valid) { player.feet = { p.pos.x, p.pos.y - player.eyeH, p.pos.z }; player.yaw = p.yaw * 3.14159265358979 / 180; player.pitch = std::clamp(p.pitch * 3.14159265358979 / 180, -1.55, 1.55); player.vel = { 0, 0, 0 }; }
        }
        cutRun.on = false; cutRt.clear(); cutHoldKeys = true;
        if (cutBootAfter) { cutBootAfter = false; if (BootLen() > 0.05) { bootOn = true; bootT = 0; } }   // the HUD boots up now (a new game)
    }
    void SkipCutscene() {
        for (const EventDo& d : skippedActions(cutRun.c, (float)cutRun.prevT)) CutAction(d);
        cutRun.t = cutRun.c.length; EndCutscene(); blackIn = 1;   // (a quick fade from black)
    }
    void UpdateCutscene(double dt) {
        if (!playMode) { if (cutRun.on) { cutRun.on = false; cutRt.clear(); } cutQueued.clear(); cutAfterLoad.clear(); cutBootAfter = cutHoldKeys = false; return; }
        bool fg = GetForegroundWindow() == hwnd;
        bool skipKey = fg && (ActDown(Act::Jump) || ActDown(Act::Fire) || Down(VK_RETURN));
        if (cutHoldKeys && !skipKey) cutHoldKeys = false;   // (after a cutscene: a held key does nothing until let go)
        if (!cutAfterLoad.empty() && !loadingOn) {   // a new game (or a test from the editor): it plays once the world is loaded; the HUD boots after it
            std::string n = cutAfterLoad; cutAfterLoad.clear();
            if (bootOn) { bootOn = false; cutBootAfter = true; }
            if (!PlayCutscene(n) && cutBootAfter) { cutBootAfter = false; bootOn = true; bootT = 0; }
        }
        if (!cutQueued.empty() && !cutRun.on) { std::string n = cutQueued; cutQueued.clear(); PlayCutscene(n); }
        if (!cutRun.on || paused || loadingOn) return;
        Cutscene& c = cutRun.c;
        if (c.view == CutView::Visor && c.lookAround > 0 && fg) {   // looking around a little
            double s = 0.0022 * settings.mouseSensitivity * 180 / 3.14159265358979;
            cutRun.lookYaw = (float)std::clamp(cutRun.lookYaw + mouseDX * s, (double)-c.lookAround, (double)c.lookAround);
            cutRun.lookPitch = (float)std::clamp(cutRun.lookPitch - mouseDY * s * (settings.invertY ? -1 : 1), (double)-c.lookAround, (double)c.lookAround);
        }
        if (c.skippable && !cutRun.ending) {   // hold to skip
            if (skipKey) cutRun.skipHeld += dt; else cutRun.skipHeld = std::max(0.0, cutRun.skipHeld - dt * 2);
            if (cutRun.skipHeld >= 0.8) { SkipCutscene(); return; }
        }
        if (cutRun.ending) { cutRun.endT += dt; if (cutRun.endT >= c.blendOut) EndCutscene(); return; }
        cutRun.t += dt;
        float to = (float)std::min(cutRun.t, (double)c.length);
        for (int i : eventsBetween(c, (float)cutRun.prevT, to)) { CutAction(c.events[i].action); if (!cutRun.on) return; }
        for (int i : soundsBetween(c, (float)cutRun.prevT, to)) PlaySoundIn(SND_WORLD, c.sounds[i].sound);
        for (int i = 0; i < (int)c.texts.size(); i++) {   // readouts: a sound for each letter typed
            const CutText& x = c.texts[i]; if (x.style != CutTextStyle::Readout || x.typeSpeed <= 0) continue;
            size_t n = cutTypedBytes(x, to), &was = cutRun.typed[i];
            if (n > was && n <= x.text.size() && !std::isspace((unsigned char)x.text[n - 1])) PlayUi(4);
            was = n;
        }
        cutRun.prevT = to;
        if (cutRun.t >= c.length) {
            if (c.handOff || c.blendOut <= 0.01f) EndCutscene();
            else { cutRun.endFrom = CutPoseRaw(); cutRun.ending = true; cutRun.endT = 0; }
        }
    }

    // ---- actors: posed every frame (the clip playing, blended when it changes)
    std::string CutActorModel(const CutActor& a) const {
        if (a.object.empty()) return a.model;
        for (const WorldObject& o : objects) if (o.name == a.object) return o.model;
        return "";
    }
    void UpdateCutActors(double dt) {
        float t; const Cutscene* c = CutShowing(t);
        if (!c) { if (!cutRt.empty()) cutRt.clear(); return; }
        if (cutRt.size() != c->actors.size()) { cutRt.clear(); cutRt.resize(c->actors.size()); }
        for (size_t i = 0; i < c->actors.size(); i++) {
            const CutActor& a = c->actors[i]; CutActorRt& r = cutRt[i];
            r.pose = actorAt(a, t, c->length);
            std::string model = CutActorModel(a);
            if (model != r.model) { r.model = model; r.blend.reset(); r.posed = false; }
            auto mi = meshModels.find(model);
            if (mi == meshModels.end() || mi->second.parts.empty()) { r.posed = false; continue; }
            const MeshModel& m = mi->second;
            int ci = r.pose.clip >= 0 ? m.findClip(r.pose.clipName) : -1;
            float ct = 0; if (ci >= 0) { float len = std::max(0.01f, m.clips[ci].length); ct = std::fmod(std::max(0.f, r.pose.clipTime), len); }
            const PartPose& p = r.blend.update(m.pose(ci, ct), ci, std::min(dt, 0.1), animBlends.of(model));
            UploadMeshVerts(r.gpu, m, m.posedFrom(p)); r.gpu.mesh = true; r.gpu.tex = GpuModel(model).tex; r.posed = true;
        }
    }
    // a world object a cutscene moves: where it is now (false: draw it as it is)
    const CutActorRt* CutObjectActor(const WorldObject& o, WorldObject& at, bool& hidden) const {
        float t; const Cutscene* c = CutShowing(t); hidden = false;
        if (!c || cutRt.size() != c->actors.size()) return nullptr;
        for (size_t i = 0; i < c->actors.size(); i++) {
            const CutActor& a = c->actors[i]; if (a.object.empty() || a.object != o.name) continue;
            const CutActorRt& r = cutRt[i];
            at = o; if (r.pose.keyed) { at.pos = r.pose.pos; at.yaw = r.pose.yaw; }
            hidden = !r.pose.shown && cutRun.on;   // (in the editor it still shows where it is)
            return &r;
        }
        return nullptr;
    }
    bool CutControls(const WorldObject& o) const { WorldObject at; bool h; return CutObjectActor(o, at, h) != nullptr; }
    void DrawCutModels(int pass) {   // the actors that are models of their own (Samus)
        float t; const Cutscene* c = CutShowing(t);
        if (!c || cutRt.size() != c->actors.size()) return;
        for (size_t i = 0; i < c->actors.size(); i++) {
            const CutActor& a = c->actors[i]; const CutActorRt& r = cutRt[i];
            if (!a.object.empty() || a.model.empty() || !r.pose.shown || !r.pose.keyed) continue;
            WorldObject o; o.id = 0xC0000000u + (uint32_t)i; o.name = a.name; o.model = a.model; o.pos = r.pose.pos; o.yaw = r.pose.yaw; o.scale = a.scale;
            if (!ModelOf(o)) continue;
            DrawObject(o, pass, camPos, r.posed ? &r.gpu : nullptr);
        }
    }

    // ---- over the picture: black bars, texts, fades, "hold to skip". True: nothing else of the HUD (a film camera)
    bool CutOverlay(ImDrawList* dl, float x0, float y0, float W, float H) {
        float t; const Cutscene* c = nullptr; bool live = cutRun.on;
        if (live) { c = &cutRun.c; t = (float)std::min(cutRun.t, (double)c->length); }
        else if (CutLooking()) { c = &cutscenes[cutEd]; t = (float)cutT; }
        if (!c) return false;
        float ui = std::max(0.5f, H / 1080.f) * 1.6f;
        float bars = cutRun.ending ? 0.f : letterboxAt(*c, t, c->blendOut);
        if (bars > 0.001f) {
            float bh = H * 0.11f * bars;
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + W, y0 + bh), IM_COL32(0, 0, 0, 255));
            dl->AddRectFilled(ImVec2(x0, y0 + H - bh), ImVec2(x0 + W, y0 + H), IM_COL32(0, 0, 0, 255));
        }
        ImFont* font = ImGui::GetFont();
        for (const CutText& x : c->texts) {
            float a = cutTextAlpha(x, t); if (a <= 0.001f) continue;
            std::string s = x.text.substr(0, cutTypedBytes(x, t)); int A = (int)(a * 255);
            switch (x.style) {
            case CutTextStyle::Subtitle: {
                float fs = 30 * ui; ImVec2 ts = font->CalcTextSizeA(fs, W * 0.8f, W * 0.8f, x.text.c_str());
                ImVec2 p(x0 + (W - ts.x) * 0.5f, y0 + H * (bars > 0.01f ? 0.905f : 0.86f) - ts.y * 0.5f);
                dl->AddText(font, fs, ImVec2(p.x + 2 * ui, p.y + 2 * ui), IM_COL32(0, 0, 0, A * 3 / 4), s.c_str(), nullptr, W * 0.8f);
                dl->AddText(font, fs, p, IM_COL32(240, 240, 240, A), s.c_str(), nullptr, W * 0.8f);
                break; }
            case CutTextStyle::Comms: {
                float fs = 24 * ui, bw = W * 0.42f, pad = 14 * ui; ImVec2 ts = font->CalcTextSizeA(fs, bw - pad * 2, bw - pad * 2, x.text.c_str());
                float bh = ts.y + pad * 2 + (x.speaker.empty() ? 0 : fs * 1.2f);
                ImVec2 a0(x0 + W * 0.05f, y0 + H * 0.84f - bh), a1(a0.x + bw, a0.y + bh);
                dl->AddRectFilled(a0, a1, IM_COL32(4, 22, 30, A * 200 / 255), 6 * ui); dl->AddRect(a0, a1, IM_COL32(90, 230, 255, A), 6 * ui, 0, 2 * ui);
                float ty = a0.y + pad;
                if (!x.speaker.empty()) { dl->AddText(font, fs, ImVec2(a0.x + pad, ty), IM_COL32(120, 240, 255, A), x.speaker.c_str()); ty += fs * 1.2f; }
                dl->AddText(font, fs, ImVec2(a0.x + pad, ty), IM_COL32(225, 245, 250, A), s.c_str(), nullptr, bw - pad * 2);
                break; }
            case CutTextStyle::Readout: {
                float fs = 24 * ui; ImVec2 p(x0 + W * 0.06f, y0 + H * (bars > 0.01f ? 0.16f : 0.1f));
                dl->AddText(font, fs, ImVec2(p.x + ui, p.y + ui), IM_COL32(0, 30, 10, A / 2), s.c_str(), nullptr, W * 0.5f);
                dl->AddText(font, fs, p, IM_COL32(110, 255, 160, A), s.c_str(), nullptr, W * 0.5f);
                if ((int)(t * 3) % 2 == 0 && s.size() < x.text.size() + 1) {   // a blinking cursor after the letters
                    ImVec2 ts = font->CalcTextSizeA(fs, W * 0.5f, W * 0.5f, s.c_str()); float lh = fs;
                    std::string last = s.substr(s.rfind('\n') == std::string::npos ? 0 : s.rfind('\n') + 1); float lw = font->CalcTextSizeA(fs, FLT_MAX, 0, last.c_str()).x;
                    dl->AddRectFilled(ImVec2(p.x + lw + 2 * ui, p.y + ts.y - lh), ImVec2(p.x + lw + 10 * ui, p.y + ts.y - 3 * ui), IM_COL32(110, 255, 160, A));
                }
                break; }
            case CutTextStyle::Title: default: {
                float fs = 64 * ui; ImVec2 ts = font->CalcTextSizeA(fs, W * 0.9f, W * 0.9f, x.text.c_str());
                ImVec2 p(x0 + (W - ts.x) * 0.5f, y0 + (H - ts.y) * 0.5f);
                dl->AddText(font, fs, ImVec2(p.x + 3 * ui, p.y + 3 * ui), IM_COL32(0, 0, 0, A * 2 / 3), s.c_str(), nullptr, W * 0.9f);
                dl->AddText(font, fs, p, IM_COL32(255, 255, 255, A), s.c_str(), nullptr, W * 0.9f);
                break; }
            }
        }
        FadeNow f = fadeAt(*c, t);
        if (f.amount > 0.001f) dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + W, y0 + H), f.white ? IM_COL32(255, 255, 255, (int)(f.amount * 255)) : IM_COL32(0, 0, 0, (int)(f.amount * 255)));
        if (live && c->skippable && cutRun.skipHeld > 0.02) {   // hold to skip: a ring that fills
            float r = 16 * ui; ImVec2 m(x0 + W - 60 * ui, y0 + H - 50 * ui); float k = (float)std::min(1.0, cutRun.skipHeld / 0.8);
            dl->AddCircle(m, r, IM_COL32(255, 255, 255, 90), 32, 3 * ui);
            dl->PathArcTo(m, r, -1.5708f, -1.5708f + k * 6.2832f, 32); dl->PathStroke(IM_COL32(255, 255, 255, 230), 0, 3 * ui);
            const char* lab = "Hold to skip"; ImVec2 ls = ImGui::CalcTextSize(lab);
            dl->AddText(ImVec2(m.x - r - 10 * ui - ls.x, m.y - ls.y * 0.5f), IM_COL32(255, 255, 255, 200), lab);
        }
        return live ? c->view == CutView::Camera : true;
    }

    // ================================================================== the Cutscenes editor
    bool cutsEdOpen = false, focusCutsEd = false, cutLook = false, cutPlaying = false, cutAudible = true;
    int cutEd = 0; double cutT = 0, cutPrevT = -1;
    int cutSelKind = 0, cutSelRow = 0, cutSelIdx = -1;   // 1 camera key, 2 actor key, 3 actor clip, 4 text, 5 sound, 6 fade, 7 action, 8 an actor (row: which actor)
    float cutPps = 40, cutScroll = 0, cutRowScroll = 0; int cutDrag = 0; float cutGrabT = 0, cutOrigT = 0, cutOrigLen = 0;
    void CutSelect(int kind, int row, int idx) { cutSelKind = kind; cutSelRow = row; cutSelIdx = idx; }
    void SaveCutscenes() { if (!worldDir.empty()) { std::error_code ec; if (!cutscenes.empty()) saveCutscenes(worldDir + "/cutscenes.txt", cutscenes); else fs::remove(worldDir + "/cutscenes.txt", ec); } cutsDirty = false; }
    void LoadCutscenesFile() { cutscenes.clear(); loadCutscenes(worldDir + "/cutscenes.txt", cutscenes); cutEd = std::clamp(cutEd, 0, std::max(0, (int)cutscenes.size() - 1)); cutRt.clear(); }
    CamKey CamKeyFromView(float t) const { CamKey k; k.t = t; k.pos = player.eye(); k.yaw = (float)(player.yaw * 180 / 3.14159265358979); k.pitch = (float)(player.pitch * 180 / 3.14159265358979); k.fov = settings.fov; return k; }
    bool PointedSpot(V3& at) const {   // where the editor camera points: on top of the block it looks at
        Hit h; if (!world || !world->raycast(player.eye(), player.forward(), 300.0, h)) return false;
        at = { h.px + h.nx * 0.001, h.py + h.ny * 0.001, h.pz + h.nz * 0.001 }; return true;
    }
    std::vector<std::string> CutsceneNames() const { std::vector<std::string> v; for (const Cutscene& c : cutscenes) v.push_back(c.name); return v; }
    void CutsceneSettings(Cutscene& c, bool& ch) {
        float ui = settings.uiScale;
        Prop("Name"); ch |= ImGui::InputText("##cname", &c.name);
        Prop("View"); if (ImGui::BeginCombo("##cview", cutViewName(c.view))) { for (int i = 0; i < (int)CutView::COUNT; i++) if (ImGui::Selectable(cutViewName((CutView)i), (int)c.view == i)) { c.view = (CutView)i; ch = true; } ImGui::EndCombo(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Camera: a film camera anywhere (the HUD hidden, black bars).\nVisor: first person, through the visor (the HUD stays; the player can't move).");
        Prop("Length"); { float w = ImGui::GetContentRegionAvail().x - ButtonW("Fit") - ImGui::GetStyle().ItemSpacing.x; ImGui::SetNextItemWidth(std::max(40.f, w)); ch |= ImGui::DragFloat("##clen", &c.length, 0.05f, 0.5f, 600, "%.2f s"); ImGui::SameLine(); if (ImGui::Button("Fit")) { c.length = std::max(0.5f, lastMoment(c)); ch = true; } if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ends where the last thing on the timeline ends"); }
        if (c.view == CutView::Camera) { Prop("Black bars"); ch |= ImGui::Checkbox("##cbars", &c.letterbox); }
        Prop("Skippable"); ch |= ImGui::Checkbox("##cskip", &c.skippable); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Holding Jump, Fire or Enter skips it.\nWhat lasts still happens (doors, flags, music); shakes and messages don't.");
        Prop("New game"); ch |= ImGui::Checkbox("Plays when a new game starts##cnew", &c.onNewGame); if (ImGui::IsItemHovered()) ImGui::SetTooltip("After the story screens: the world loads, this plays, then the HUD boots up.");
        Prop("Start"); ch |= ImGui::Checkbox("From the player's view##cfrom", &c.fromPlayer); if (ImGui::IsItemHovered()) ImGui::SetTooltip("The camera starts where the player is looking and glides to the first key.");
        if (c.view == CutView::Visor) {
            Prop("Stay in place"); ch |= ImGui::Checkbox("Only turn the view##clook", &c.lookOnly); if (ImGui::IsItemHovered()) ImGui::SetTooltip("The view stays at the player's eyes wherever they are;\nthe keys only turn it (a boss appears: look up at it).");
            Prop("Look around"); ch |= ImGui::SliderFloat("##caround", &c.lookAround, 0, 60, c.lookAround <= 0 ? "locked" : "%.0f degrees"); if (ImGui::IsItemHovered()) ImGui::SetTooltip("How far the player may look around with the mouse (0: the view is locked).");
        }
        Prop("End"); ch |= ImGui::Checkbox("Hand over to the player##chand", &c.handOff); if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: at the end the player stands where the camera ended, looking the same way\n(end on Samus's eyes: the game goes on from there).\nOff: the view glides back to the player.");
        if (!c.handOff) { Prop("Glide back"); ch |= ImGui::SliderFloat("##cblend", &c.blendOut, 0, 3, c.blendOut <= 0.01f ? "a cut" : "%.2f s"); }
        (void)ui;
    }
    void CutscenesEditor() {
        if (!cutsEdOpen) return;
        float ui = settings.uiScale; bool ch = false;
        if (focusCutsEd) { ImGui::SetNextWindowFocus(); focusCutsEd = false; }
        ImGui::SetNextWindowSize(ImVec2(1100 * ui, 640 * ui), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Cutscenes", &cutsEdOpen)) { ImGui::End(); return; }
        // ---- which one
        ImGui::SetNextItemWidth(260 * ui);
        if (ImGui::BeginCombo("##cuts", CutEdValid() ? cutscenes[cutEd].name.c_str() : "(none yet)")) {
            for (int i = 0; i < (int)cutscenes.size(); i++) if (ImGui::Selectable((cutscenes[i].name + "##" + std::to_string(i)).c_str(), i == cutEd)) { cutEd = i; cutT = 0; CutSelect(0, 0, -1); cutRt.clear(); }
            ImGui::EndCombo();
        }
        ImGui::SameLine(); if (ImGui::Button("New")) {
            Cutscene c; int n = (int)cutscenes.size() + 1; c.name = "Cutscene " + std::to_string(n);
            while (findCutscene(cutscenes, c.name) >= 0) c.name = "Cutscene " + std::to_string(++n);
            c.cam.push_back(CamKeyFromView(0)); c.cam[0].ease = true;
            cutscenes.push_back(c); cutEd = (int)cutscenes.size() - 1; cutT = 0; CutSelect(1, 0, 0); ch = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A new cutscene, its first camera key where the editor camera is now.");
        ImGui::BeginDisabled(!CutEdValid());
        ImGui::SameLine(); if (ImGui::Button("Copy")) { Cutscene c = cutscenes[cutEd]; c.name += " copy"; cutscenes.push_back(c); cutEd = (int)cutscenes.size() - 1; ch = true; }
        ImGui::SameLine(); if (ImGui::Button("Delete")) ImGui::OpenPopup("delcut");
        if (ImGui::BeginPopup("delcut")) { ImGui::Text("Delete \"%s\"?", cutscenes[cutEd].name.c_str()); if (ImGui::Button("Delete")) { cutscenes.erase(cutscenes.begin() + cutEd); cutEd = std::max(0, cutEd - 1); CutSelect(0, 0, -1); cutRt.clear(); ch = true; ImGui::CloseCurrentPopup(); } ImGui::SameLine(); if (ImGui::Button("Keep it")) ImGui::CloseCurrentPopup(); ImGui::EndPopup(); }
        ImGui::SameLine(); if (ImGui::Button("Test in game")) { SaveCutscenes(); std::string n = cutscenes[cutEd].name; StartPlay(); cutAfterLoad = n; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play mode, and this cutscene as soon as the world is loaded. F5: back to the editor.");
        ImGui::EndDisabled();
        ImGui::SameLine(); ImGui::Checkbox("Look through the camera", &cutLook);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The viewport shows what the cutscene's camera sees at the playhead\n(with its bars, texts and fades). Off: fly around freely; the path shows as a line.");
        if (!CutEdValid()) {
            Hint("A cutscene is a timeline: a camera that glides through its keys, actors (models, or objects of the world) that move and play animations, texts, sounds, fades and actions. Press New to start one where the editor camera is.");
            ImGui::End(); if (ch) SaveCutscenes(); return;
        }
        Cutscene& c = cutscenes[cutEd];
        cutT = std::clamp(cutT, 0.0, (double)std::max(c.length, lastMoment(c)) + 5);
        // ---- left: the settings and what is picked; right: the timeline
        float leftW = 330 * ui;
        ImGui::BeginChild("cutleft", ImVec2(leftW, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        if (ImGui::CollapsingHeader("Cutscene", ImGuiTreeNodeFlags_DefaultOpen)) CutsceneSettings(c, ch);
        if (ImGui::CollapsingHeader("Picked", ImGuiTreeNodeFlags_DefaultOpen)) CutPicked(c, ch);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("cutright", ImVec2(0, 0));
        CutTransport(c, ch);
        CutTimeline(c, ch, ImGui::GetContentRegionAvail().y);
        ImGui::EndChild();
        ImGui::End();
        if (ch) cutsDirty = true;
    }
    void CutTransport(Cutscene& c, bool& ch) {
        float ui = settings.uiScale;
        if (ImGui::Button(cutPlaying ? "Pause" : "Play", ImVec2(64 * ui, 0))) { cutPlaying = !cutPlaying; if (cutPlaying && cutT >= c.length) cutT = 0; cutPrevT = cutT - 0.001; }
        ImGui::SameLine(); if (ImGui::Button("|<")) { cutT = 0; cutPrevT = -1; }
        ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::Text("%6.2f s", cutT); ImGui::SameLine(); ImGui::TextDisabled("of %.1f s", c.length);
        ImGui::SameLine(); ImGui::Checkbox("Sound", &cutAudible);
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        float t = (float)cutT;
        if (ImGui::Button("+ Camera key")) {   // (a key at the playhead already: it moves to the view)
            int at = -1; for (int i = 0; i < (int)c.cam.size(); i++) if (std::fabs(c.cam[i].t - t) < 0.03f) at = i;
            CamKey k = CamKeyFromView(t);
            if (at >= 0) { k.ease = c.cam[at].ease; k.cut = c.cam[at].cut; k.roll = c.cam[at].roll; c.cam[at] = k; } else { c.cam.push_back(k); at = (int)c.cam.size() - 1; }
            CutSelect(1, 0, at); cutLook = false; ch = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A camera key at the playhead: where the editor camera is and looks now.\n(Fly there first. Move the playhead on, fly on, add the next.)");
        ImGui::SameLine(); if (ImGui::Button("+ Actor")) ImGui::OpenPopup("addactor");
        if (ImGui::BeginPopup("addactor")) {
            if (ImGui::BeginMenu("A model, shown only in the cutscene")) {
                for (const std::string& m : AllModelNames()) if (!IsBuiltin(m) && ImGui::MenuItem(m.c_str())) {
                    CutActor a; a.name = m; a.model = m; V3 at; ActorKey k; k.t = t; k.pos = PointedSpot(at) ? at : player.feet; k.yaw = (float)(player.yaw * 180 / 3.14159265358979 + 180); a.keys.push_back(k);
                    auto mi = meshModels.find(m); if (mi != meshModels.end() && !mi->second.clips.empty()) { int ci = mi->second.findClip("idle"); ActorClip cl; cl.t = 0; cl.length = c.length; cl.clip = mi->second.clips[ci >= 0 ? ci : 0].name; a.clips.push_back(cl); }
                    c.actors.push_back(a); CutSelect(8, (int)c.actors.size() - 1, -1); ch = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("An object of the world (it moves)")) {
                std::set<std::string> seen;
                for (const WorldObject& o : objects) if (!o.name.empty() && seen.insert(o.name).second && ImGui::MenuItem(o.name.c_str())) {
                    CutActor a; a.name = o.name; a.object = o.name; ActorKey k; k.t = t; k.pos = o.pos; k.yaw = (float)o.yaw; a.keys.push_back(k);
                    c.actors.push_back(a); CutSelect(8, (int)c.actors.size() - 1, -1); ch = true;
                }
                if (seen.empty()) ImGui::TextDisabled("No objects in this world yet");
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
        auto addAt = [&](int kind) {
            if (kind == 4) { CutText x; x.t = t; x.text = "New text"; c.texts.push_back(x); CutSelect(4, 0, (int)c.texts.size() - 1); }
            if (kind == 5) { CutSound s; s.t = t; auto f = SoundFiles(); if (!f.empty()) s.sound.file = f[0]; c.sounds.push_back(s); CutSelect(5, 0, (int)c.sounds.size() - 1); }
            if (kind == 6) { CutFade d; d.t = t; d.kind = t < 0.5f ? CutFadeKind::FromBlack : CutFadeKind::ToBlack; c.fades.push_back(d); CutSelect(6, 0, (int)c.fades.size() - 1); }
            if (kind == 7) { CutEvent e; e.t = t; e.action = { Do::Shake, "", "", 0.6f }; c.events.push_back(e); CutSelect(7, 0, (int)c.events.size() - 1); }
            ch = true;
        };
        ImGui::SameLine(); if (ImGui::Button("+ Text")) addAt(4);
        ImGui::SameLine(); if (ImGui::Button("+ Sound")) addAt(5);
        ImGui::SameLine(); if (ImGui::Button("+ Fade")) addAt(6);
        ImGui::SameLine(); if (ImGui::Button("+ Action")) addAt(7);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Anything an event card can do, at this moment: open a door, play music, shake the screen, set a flag...");
        ImGui::SameLine(); ImGui::SetNextItemWidth(std::max(60.f, ImGui::GetContentRegionAvail().x)); ImGui::SliderFloat("##cz", &cutPps, 8, 300, "zoom %.0f", ImGuiSliderFlags_Logarithmic);
    }
    // the rows of the timeline: the camera, each actor (its keys, its clips), texts, sounds, fades, actions
    struct CutRow { int kind; int actor; const char* label; };
    std::vector<CutRow> CutRows(const Cutscene& c) const {
        std::vector<CutRow> r; r.push_back({ 1, 0, "Camera" });
        for (int i = 0; i < (int)c.actors.size(); i++) { r.push_back({ 2, i, nullptr }); r.push_back({ 3, i, "   animations" }); }
        r.push_back({ 4, 0, "Texts" }); r.push_back({ 5, 0, "Sounds" }); r.push_back({ 6, 0, "Fades" }); r.push_back({ 7, 0, "Actions" });
        return r;
    }
    void CutTimeline(Cutscene& c, bool& ch, float height) {
        float ui = settings.uiScale; ImGuiIO& io = ImGui::GetIO();
        ImGui::BeginChild("cuttl", ImVec2(0, std::max(80.f, height)), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImVec2 o = ImGui::GetCursorScreenPos(), sz = ImGui::GetContentRegionAvail();
        float nameW = 140 * ui, rulerH = 22 * ui, rowH = 24 * ui, x0 = o.x + nameW, x1 = o.x + sz.x;
        ImGui::InvisibleButton("##ctl", ImVec2(std::max(1.f, sz.x), std::max(1.f, sz.y)), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        bool hov = ImGui::IsItemHovered(); ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 mp = io.MousePos;
        std::vector<CutRow> rows = CutRows(c); int n = (int)rows.size(); float rowsH = sz.y - rulerH;
        cutRowScroll = std::clamp(cutRowScroll, 0.f, std::max(0.f, n * rowH - rowsH));
        auto X = [&](float t) { return x0 + (t - cutScroll) * cutPps; };
        auto T = [&](float x) { return cutScroll + (x - x0) / cutPps; };
        auto rowY = [&](int i) { return o.y + rulerH + i * rowH - cutRowScroll; };
        if (hov && (io.MouseWheel != 0 || io.MouseWheelH != 0)) {
            if (io.KeyCtrl) { float tm = T(mp.x); cutPps = std::clamp(cutPps * std::pow(1.2f, io.MouseWheel), 8.f, 300.f); cutScroll = tm - (mp.x - x0) / cutPps; }
            else if (io.KeyShift || io.MouseWheelH != 0 || n * rowH <= rowsH) cutScroll -= (io.MouseWheel + io.MouseWheelH) * 60 / cutPps;
            else cutRowScroll -= io.MouseWheel * rowH * 2;
        }
        if (cutPlaying && cutDrag == 0) { if (X((float)cutT) > x1 - 30) cutScroll = (float)cutT - (x1 - x0) * 0.15f / cutPps; if (X((float)cutT) < x0) cutScroll = std::max(0.f, (float)cutT - 0.5f); }
        cutScroll = std::max(0.f, cutScroll);
        auto snap = [&](float v) { if (io.KeyShift) return std::max(0.f, v); float b = std::round(v / 0.05f) * 0.05f; if (std::fabs(X((float)cutT) - X(v)) < 7) b = (float)cutT; return std::max(0.f, b); };
        // ---- what is under the mouse: (kind, actor, index, part: 0 the thing / key, 1 its start edge, 2 its end edge)
        struct HitT { int kind = 0, actor = 0, idx = -1, part = 0; } hit;
        int mrow = (mp.y >= o.y + rulerH && mp.x >= x0) ? (int)std::floor((mp.y - (o.y + rulerH) + cutRowScroll) / rowH) : -1;
        if (hov && mrow >= 0 && mrow < n) {
            const CutRow& R = rows[mrow]; float best = 8 * ui;
            auto key = [&](float t, int idx) { float d = std::fabs(X(t) - mp.x); if (d < best) { best = d; hit = { R.kind, R.actor, idx, 0 }; } };
            auto bar = [&](float a, float len, int idx) { float xa = X(a), xb = X(a + len); if (mp.x >= xa - 4 && mp.x <= xb + 4 && hit.idx < 0) { hit = { R.kind, R.actor, idx, std::fabs(mp.x - xb) < 6 ? 2 : std::fabs(mp.x - xa) < 6 ? 1 : 0 }; } };
            if (R.kind == 1) for (int i = 0; i < (int)c.cam.size(); i++) key(c.cam[i].t, i);
            if (R.kind == 2) for (int i = 0; i < (int)c.actors[R.actor].keys.size(); i++) key(c.actors[R.actor].keys[i].t, i);
            if (R.kind == 5) for (int i = 0; i < (int)c.sounds.size(); i++) key(c.sounds[i].t, i);
            if (R.kind == 7) for (int i = 0; i < (int)c.events.size(); i++) key(c.events[i].t, i);
            if (R.kind == 3) for (int i = (int)c.actors[R.actor].clips.size() - 1; i >= 0; i--) bar(c.actors[R.actor].clips[i].t, c.actors[R.actor].clips[i].length, i);
            if (R.kind == 4) for (int i = (int)c.texts.size() - 1; i >= 0; i--) bar(c.texts[i].t, c.texts[i].length, i);
            if (R.kind == 6) for (int i = (int)c.fades.size() - 1; i >= 0; i--) bar(c.fades[i].t, c.fades[i].length, i);
        }
        auto timeOf = [&](int kind, int actor, int idx) -> float* {
            switch (kind) {
            case 1: return &c.cam[idx].t; case 2: return &c.actors[actor].keys[idx].t; case 3: return &c.actors[actor].clips[idx].t;
            case 4: return &c.texts[idx].t; case 5: return &c.sounds[idx].t; case 6: return &c.fades[idx].t; case 7: return &c.events[idx].t;
            }
            return nullptr;
        };
        auto lenOf = [&](int kind, int actor, int idx) -> float* { return kind == 3 ? &c.actors[actor].clips[idx].length : kind == 4 ? &c.texts[idx].length : kind == 6 ? &c.fades[idx].length : nullptr; };
        auto erase = [&](int kind, int actor, int idx) {
            switch (kind) {
            case 1: c.cam.erase(c.cam.begin() + idx); break; case 2: c.actors[actor].keys.erase(c.actors[actor].keys.begin() + idx); break;
            case 3: c.actors[actor].clips.erase(c.actors[actor].clips.begin() + idx); break; case 4: c.texts.erase(c.texts.begin() + idx); break;
            case 5: c.sounds.erase(c.sounds.begin() + idx); break; case 6: c.fades.erase(c.fades.begin() + idx); break; case 7: c.events.erase(c.events.begin() + idx); break;
            }
            CutSelect(0, 0, -1); ch = true;
        };
        // ---- the mouse: click picks (and moves the playhead on the ruler or empty space), drag moves in time / stretches, right-click deletes
        if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hit.idx >= 0) { CutSelect(hit.kind, hit.actor, hit.idx); cutDrag = hit.part == 2 ? 3 : 2; cutGrabT = T(mp.x); cutOrigT = *timeOf(hit.kind, hit.actor, hit.idx); float* l = lenOf(hit.kind, hit.actor, hit.idx); cutOrigLen = l ? *l : 0; }
            else if (mp.x >= x0) { cutDrag = 1; if (mrow >= 0 && mrow < n && rows[mrow].kind == 2) CutSelect(8, rows[mrow].actor, -1); }
            else if (mrow >= 0 && mrow < n && (rows[mrow].kind == 2 || rows[mrow].kind == 3)) CutSelect(8, rows[mrow].actor, -1);
        }
        if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hit.idx >= 0) { cutT = *timeOf(hit.kind, hit.actor, hit.idx); cutDrag = 0; }
        if (cutDrag && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (cutDrag == 1) { cutT = std::max(0.f, T(mp.x)); cutPlaying = false; }
            else if (cutSelIdx >= 0) {
                float d = T(mp.x) - cutGrabT;
                if (cutDrag == 2) { float* tt = timeOf(cutSelKind, cutSelRow, cutSelIdx); if (tt) { float nv = snap(cutOrigT + d); if (nv != *tt) { *tt = nv; ch = true; } } }
                else { float* l = lenOf(cutSelKind, cutSelRow, cutSelIdx); if (l) { float nv = std::max(0.1f, snap(cutOrigT + cutOrigLen + d) - cutOrigT); if (nv != *l) { *l = nv; ch = true; } } }
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) cutDrag = 0;
        if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hit.idx >= 0) { CutSelect(hit.kind, hit.actor, hit.idx); ImGui::OpenPopup("cutctx"); }
        if (ImGui::BeginPopup("cutctx")) {
            if (cutSelIdx >= 0 && ImGui::MenuItem("Delete")) erase(cutSelKind, cutSelRow, cutSelIdx);
            if (cutSelIdx >= 0 && ImGui::MenuItem("Move the playhead here")) { float* tt = timeOf(cutSelKind, cutSelRow, cutSelIdx); if (tt) cutT = *tt; }
            ImGui::EndPopup();
        }
        if (hov && hit.idx >= 0) ImGui::SetTooltip(hit.part ? "Drag: stretch it" : "Click: pick it (its settings on the left). Drag: move it in time. Double-click: playhead here. Right-click: delete");
        // ---- drawing
        dl->PushClipRect(o, ImVec2(x1, o.y + sz.y), true);
        dl->AddRectFilled(ImVec2(x0, o.y), ImVec2(x1, o.y + sz.y), IM_COL32(22, 25, 30, 255));
        dl->AddRectFilled(ImVec2(x0, o.y), ImVec2(x1, o.y + rulerH), IM_COL32(34, 38, 46, 255));
        if (X(c.length) < x1) dl->AddRectFilled(ImVec2(std::max(x0, X(c.length)), o.y + rulerH), ImVec2(x1, o.y + sz.y), IM_COL32(0, 0, 0, 90));   // past the end
        {   float major = cutPps >= 90 ? 1 : cutPps >= 35 ? 2 : cutPps >= 15 ? 5 : 10, minor = cutPps >= 160 ? 0.1f : cutPps >= 60 ? 0.25f : major / 2;
            for (float t = std::floor(T(x0) / minor) * minor; X(t) < x1; t += minor) {
                if (t < -1e-4f) continue; float x = X(t); bool big = std::fabs(std::remainder(t, major)) < 1e-3f;
                dl->AddLine(ImVec2(x, o.y + rulerH * (big ? 0.35f : 0.7f)), ImVec2(x, o.y + rulerH), IM_COL32(150, 160, 175, big ? 200 : 90));
                if (big) { char b[16]; snprintf(b, sizeof b, "%gs", std::round(t * 100) / 100); dl->AddText(ImVec2(x + 3, o.y + 1), IM_COL32(170, 180, 195, 220), b); dl->AddLine(ImVec2(x, o.y + rulerH), ImVec2(x, o.y + sz.y), IM_COL32(255, 255, 255, 12)); }
            } }
        auto rowColor = [](int kind, int a) { uint32_t col = kind == 1 ? 0xFFB347 : kind == 2 || kind == 3 ? 0x45B884 : kind == 4 ? 0x4C8DFF : kind == 5 ? 0xA875F0 : kind == 6 ? 0x9AA5B5 : 0xE36D6D; return IM_COL32((col >> 16) & 255, (col >> 8) & 255, col & 255, a); };
        auto diamond = [&](float x, float cy, float r, ImU32 col, bool sel, bool hollow) {
            ImVec2 p[4] = { ImVec2(x, cy - r), ImVec2(x + r, cy), ImVec2(x, cy + r), ImVec2(x - r, cy) };
            if (hollow) dl->AddPolyline(p, 4, col, ImDrawFlags_Closed, 2.f); else dl->AddConvexPolyFilled(p, 4, col);
            if (sel) dl->AddPolyline(p, 4, IM_COL32(255, 230, 120, 255), ImDrawFlags_Closed, 2.f);
        };
        for (int ri = 0; ri < n; ri++) {
            float y = rowY(ri); if (y + rowH < o.y + rulerH || y > o.y + sz.y) continue;
            const CutRow& R = rows[ri]; float cy = y + rowH * 0.5f, top = y + 3, bot = y + rowH - 3;
            dl->PushClipRect(ImVec2(o.x, std::max(y, o.y + rulerH)), ImVec2(x1, std::min(y + rowH, o.y + sz.y)), true);
            bool rowSel = (cutSelKind == R.kind && (R.kind > 3 || R.kind == 1 || cutSelRow == R.actor)) || (cutSelKind == 8 && (R.kind == 2 || R.kind == 3) && cutSelRow == R.actor);
            if (rowSel) dl->AddRectFilled(ImVec2(o.x, y), ImVec2(x1, y + rowH), IM_COL32(255, 200, 60, 18));
            if (ri % 2) dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + rowH), IM_COL32(255, 255, 255, 6));
            dl->AddRectFilled(ImVec2(o.x + 4, y + rowH * 0.3f), ImVec2(o.x + 8, y + rowH * 0.7f), rowColor(R.kind, 255), 1.f);
            std::string label = R.label ? R.label : c.actors[R.actor].name + (c.actors[R.actor].object.empty() ? "" : " (object)");
            dl->PushClipRect(ImVec2(o.x, y), ImVec2(x0 - 4, y + rowH), true);
            dl->AddText(ImVec2(o.x + 13, y + (rowH - ImGui::GetTextLineHeight()) * 0.5f), rowSel ? IM_COL32(255, 220, 120, 255) : IM_COL32(210, 215, 225, 255), label.c_str());
            dl->PopClipRect();
            dl->PushClipRect(ImVec2(x0, y), ImVec2(x1, y + rowH), true);
            auto barDraw = [&](float a, float len, const std::string& lab, bool sel) {
                float xa = X(a), xb = X(a + len);
                dl->AddRectFilled(ImVec2(xa, top), ImVec2(xb, bot), rowColor(R.kind, sel ? 230 : 150), 3.f);
                if (sel) dl->AddRect(ImVec2(xa, top), ImVec2(xb, bot), IM_COL32(255, 230, 120, 255), 3.f, 0, 2.f);
                dl->PushClipRect(ImVec2(std::max(xa, x0) + 4, top), ImVec2(xb - 2, bot), true);
                dl->AddText(ImVec2(std::max(xa, x0) + 5, y + (rowH - ImGui::GetTextLineHeight()) * 0.5f), IM_COL32(255, 255, 255, 235), lab.c_str());
                dl->PopClipRect();
            };
            float r = rowH * 0.28f;
            if (R.kind == 1) {
                for (int i = 0; i < (int)c.cam.size(); i++) diamond(X(c.cam[i].t), cy, r, rowColor(1, 255), cutSelKind == 1 && cutSelIdx == i, c.cam[i].cut);
            } else if (R.kind == 2) {
                const CutActor& a = c.actors[R.actor];
                float to = a.showTo < 0 ? c.length : a.showTo;
                dl->AddRectFilled(ImVec2(X(a.showFrom), cy - 2), ImVec2(X(to), cy + 2), rowColor(2, 70));   // when it is there
                for (int i = 0; i < (int)a.keys.size(); i++) diamond(X(a.keys[i].t), cy, r, rowColor(2, 255), cutSelKind == 2 && cutSelRow == R.actor && cutSelIdx == i, a.keys[i].cut);
            } else if (R.kind == 3) {
                const CutActor& a = c.actors[R.actor];
                for (int i = 0; i < (int)a.clips.size(); i++) barDraw(a.clips[i].t, a.clips[i].length, a.clips[i].clip, cutSelKind == 3 && cutSelRow == R.actor && cutSelIdx == i);
            } else if (R.kind == 4) {
                for (int i = 0; i < (int)c.texts.size(); i++) barDraw(c.texts[i].t, c.texts[i].length, c.texts[i].text.substr(0, c.texts[i].text.find('\n')), cutSelKind == 4 && cutSelIdx == i);
            } else if (R.kind == 5) {
                for (int i = 0; i < (int)c.sounds.size(); i++) { float x = X(c.sounds[i].t); diamond(x, cy, r, rowColor(5, 255), cutSelKind == 5 && cutSelIdx == i, false); dl->AddText(ImVec2(x + r + 3, y + (rowH - ImGui::GetTextLineHeight()) * 0.5f), IM_COL32(220, 220, 230, 200), c.sounds[i].sound.file.c_str()); }
            } else if (R.kind == 6) {
                for (int i = 0; i < (int)c.fades.size(); i++) barDraw(c.fades[i].t, c.fades[i].length, cutFadeName(c.fades[i].kind), cutSelKind == 6 && cutSelIdx == i);
            } else if (R.kind == 7) {
                for (int i = 0; i < (int)c.events.size(); i++) { float x = X(c.events[i].t); diamond(x, cy, r, rowColor(7, 255), cutSelKind == 7 && cutSelIdx == i, false); std::string d = describe(EventCard{ "", true, false, {}, {}, { c.events[i].action } }); d = d.substr(d.find(':') + 1); dl->AddText(ImVec2(x + r + 3, y + (rowH - ImGui::GetTextLineHeight()) * 0.5f), IM_COL32(220, 220, 230, 200), d.c_str()); }
            }
            dl->PopClipRect(); dl->PopClipRect();
        }
        {   float x = X((float)cutT); dl->AddLine(ImVec2(x, o.y), ImVec2(x, o.y + sz.y), IM_COL32(255, 90, 60, 255), 2.f);
            dl->AddTriangleFilled(ImVec2(x - 6, o.y), ImVec2(x + 6, o.y), ImVec2(x, o.y + 9), IM_COL32(255, 90, 60, 255)); }
        dl->PopClipRect();
        ImGui::EndChild();
    }
    // the picked thing's settings
    void CutPicked(Cutscene& c, bool& ch) {
        float ui = settings.uiScale; (void)ui;
        auto timeField = [&](float& t) { Prop("Time"); ch |= ImGui::DragFloat("##pt", &t, 0.02f, 0, 600, "%.2f s"); t = std::max(0.f, t); };
        auto delButton = [&](const char* what) { bool d = ImGui::Button((std::string("Delete this ") + what).c_str()); if (d) ch = true; return d; };
        if (cutSelKind == 1 && cutSelIdx >= 0 && cutSelIdx < (int)c.cam.size()) {
            CamKey& k = c.cam[cutSelIdx]; ImGui::TextDisabled("Camera key %d", cutSelIdx + 1);
            timeField(k.t);
            Prop("Position"); ch |= ImGui::DragScalarN("##pp", ImGuiDataType_Double, &k.pos.x, 3, 0.05f, nullptr, nullptr, "%.2f");
            Prop("Turn"); ch |= ImGui::DragFloat("##py", &k.yaw, 0.5f, -360, 360, "%.1f deg");
            Prop("Up / down"); ch |= ImGui::DragFloat("##pp2", &k.pitch, 0.5f, -89, 89, "%.1f deg");
            Prop("Tilt"); ch |= ImGui::DragFloat("##pr", &k.roll, 0.5f, -180, 180, "%.1f deg");
            Prop("View angle"); ch |= ImGui::SliderFloat("##pf", &k.fov, 10, 120, "%.0f deg"); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Small: zoomed in (a long lens). Big: wide.");
            Prop("Ease"); ch |= ImGui::Checkbox("Slow down here##pe", &k.ease); if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: the camera comes to rest at this key and starts slowly again.\nOff: it flies through it smoothly. (The first and last keys always ease.)");
            Prop("Cut"); ch |= ImGui::Checkbox("Jump here (a cut)##pc", &k.cut); if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: no glide from the key before - the camera holds, then jumps here at this moment.\nA new shot.");
            if (ImGui::Button("Set from the editor camera")) { CamKey v = CamKeyFromView(k.t); v.ease = k.ease; v.cut = k.cut; v.roll = k.roll; k = v; ch = true; }
            ImGui::SameLine(); if (ImGui::Button("Fly there")) { player.feet = { k.pos.x, k.pos.y - player.eyeH, k.pos.z }; player.yaw = k.yaw * 3.14159265358979 / 180; player.pitch = k.pitch * 3.14159265358979 / 180; player.vel = { 0, 0, 0 }; player.flying = true; cutLook = false; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The editor camera goes to this key (to look around from there, or change it)");
            if (delButton("key")) { c.cam.erase(c.cam.begin() + cutSelIdx); CutSelect(0, 0, -1); }
        } else if ((cutSelKind == 8 || cutSelKind == 2 || cutSelKind == 3) && cutSelRow >= 0 && cutSelRow < (int)c.actors.size()) {
            CutActor& a = c.actors[cutSelRow];
            if (cutSelKind == 2 && cutSelIdx >= 0 && cutSelIdx < (int)a.keys.size()) {
                ActorKey& k = a.keys[cutSelIdx]; ImGui::TextDisabled("%s: key %d", a.name.c_str(), cutSelIdx + 1);
                timeField(k.t);
                Prop("Position"); ch |= ImGui::DragScalarN("##ap", ImGuiDataType_Double, &k.pos.x, 3, 0.05f, nullptr, nullptr, "%.2f");
                Prop("Turn"); ch |= ImGui::DragFloat("##ay", &k.yaw, 0.5f, -360, 360, "%.1f deg");
                Prop("Ease"); ch |= ImGui::Checkbox("Slow down here##ae", &k.ease);
                Prop("Cut"); ch |= ImGui::Checkbox("Jump here##ac", &k.cut);
                if (ImGui::Button("Where you point")) { V3 at; if (PointedSpot(at)) { k.pos = at; ch = true; } else Toast("Point the editor camera at the ground"); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("On the block the editor camera looks at");
                ImGui::SameLine(); if (ImGui::Button("At the camera")) { k.pos = player.eye(); k.yaw = (float)(player.yaw * 180 / 3.14159265358979); ch = true; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where the editor camera is, facing the same way (for things that fly)");
                ImGui::SameLine(); if (ImGui::Button("Face the camera")) { V3 d = player.eye() - k.pos; k.yaw = (float)(std::atan2(d.x, d.z) * 180 / 3.14159265358979); ch = true; }
                if (delButton("key")) { a.keys.erase(a.keys.begin() + cutSelIdx); CutSelect(8, cutSelRow, -1); }
                ImGui::Separator();
            } else if (cutSelKind == 3 && cutSelIdx >= 0 && cutSelIdx < (int)a.clips.size()) {
                ActorClip& k = a.clips[cutSelIdx]; ImGui::TextDisabled("%s: animation", a.name.c_str());
                std::string model = CutActorModel(a); auto mi = meshModels.find(model);
                Prop("Animation");
                if (ImGui::BeginCombo("##acl", k.clip.empty() ? "(none)" : k.clip.c_str())) {
                    if (mi != meshModels.end()) for (const AnimClip& cl : mi->second.clips) if (ImGui::Selectable(cl.name.c_str(), cl.name == k.clip)) { k.clip = cl.name; ch = true; }
                    if (mi == meshModels.end() || mi->second.clips.empty()) ImGui::TextDisabled("This model has no animations");
                    ImGui::EndCombo();
                }
                timeField(k.t);
                Prop("Length"); ch |= ImGui::DragFloat("##al", &k.length, 0.02f, 0.1f, 600, "%.2f s");
                Prop("Speed"); ch |= ImGui::SliderFloat("##asp", &k.speed, 0.1f, 3, "%.2f x");
                if (mi != meshModels.end()) { int ci = mi->second.findClip(k.clip); if (ci >= 0) { float L = mi->second.clips[ci].length / std::max(0.01f, k.speed); ImGui::TextDisabled("Once through: %.2f s (longer: it loops)", L); if (ImGui::Button("Play it once")) { k.length = L; ch = true; } } }
                if (delButton("animation")) { a.clips.erase(a.clips.begin() + cutSelIdx); CutSelect(8, cutSelRow, -1); }
                ImGui::Separator();
            }
            if (cutSelRow < (int)c.actors.size()) {
                CutActor& b = c.actors[cutSelRow];
                ImGui::TextDisabled(b.object.empty() ? "Actor (a model)" : "Actor (moves a world object)");
                Prop("Name"); ch |= ImGui::InputText("##an", &b.name);
                if (b.object.empty()) { Prop("Model"); if (ImGui::BeginCombo("##am", b.model.c_str())) { for (auto& m : AllModelNames()) if (!IsBuiltin(m) && ImGui::Selectable(m.c_str(), m == b.model)) { b.model = m; ch = true; } ImGui::EndCombo(); }
                                        Prop("Size"); ch |= ImGui::DragFloat("##as", &b.scale, 0.01f, 0.05f, 20, "%.2f x"); }
                else { Prop("Object"); if (ImGui::BeginCombo("##ao", b.object.c_str())) { std::set<std::string> seen; for (const WorldObject& o : objects) if (!o.name.empty() && seen.insert(o.name).second && ImGui::Selectable(o.name.c_str(), o.name == b.object)) { b.object = o.name; ch = true; } ImGui::EndCombo(); }
                       Hint("After the cutscene the object is back at its own place: put it where the cutscene leaves it (the gunship on its pad)."); }
                Prop("Shown from"); ch |= ImGui::DragFloat("##af", &b.showFrom, 0.02f, 0, 600, "%.2f s");
                Prop("Shown until"); ch |= ImGui::DragFloat("##at", &b.showTo, 0.02f, -1, 600, b.showTo < 0 ? "the end" : "%.2f s");
                float t = (float)cutT;
                if (ImGui::Button("+ Key: where you point")) { V3 at; ActorKey k; k.t = t; ActorPose p = actorAt(b, t, c.length); k.yaw = (float)p.yaw; if (PointedSpot(at)) { k.pos = at; b.keys.push_back(k); CutSelect(2, cutSelRow, (int)b.keys.size() - 1); ch = true; } else Toast("Point the editor camera at the ground"); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("A key at the playhead, on the block the editor camera looks at");
                ImGui::SameLine(); if (ImGui::Button("+ Key: at the camera")) { ActorKey k; k.t = t; k.pos = player.eye(); k.yaw = (float)(player.yaw * 180 / 3.14159265358979); b.keys.push_back(k); CutSelect(2, cutSelRow, (int)b.keys.size() - 1); ch = true; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("A key at the playhead, where the editor camera is (for things that fly)");
                if (!b.object.empty()) { if (ImGui::Button("+ Key: its own place")) { for (const WorldObject& o : objects) if (o.name == b.object) { ActorKey k; k.t = t; k.pos = o.pos; k.yaw = (float)o.yaw; b.keys.push_back(k); CutSelect(2, cutSelRow, (int)b.keys.size() - 1); ch = true; break; } }
                                         if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where the object really stands (end the cutscene there)"); }
                std::string model = CutActorModel(b); auto mi = meshModels.find(model);
                if (mi != meshModels.end() && !mi->second.clips.empty()) {
                    if (!b.object.empty()) ImGui::SameLine();
                    if (ImGui::Button("+ Animation")) ImGui::OpenPopup("addclip");
                    if (ImGui::BeginPopup("addclip")) { for (const AnimClip& cl : mi->second.clips) if (ImGui::MenuItem(cl.name.c_str())) { ActorClip k; k.t = t; k.clip = cl.name; k.length = std::max(0.2f, cl.length); b.clips.push_back(k); CutSelect(3, cutSelRow, (int)b.clips.size() - 1); ch = true; } ImGui::EndPopup(); }
                }
                if (ImGui::Button("Delete this actor")) { c.actors.erase(c.actors.begin() + cutSelRow); CutSelect(0, 0, -1); cutRt.clear(); ch = true; }
            }
        } else if (cutSelKind == 4 && cutSelIdx >= 0 && cutSelIdx < (int)c.texts.size()) {
            CutText& x = c.texts[cutSelIdx];
            Prop("Style"); if (ImGui::BeginCombo("##ts", cutTextStyleName(x.style))) { for (int i = 0; i < (int)CutTextStyle::COUNT; i++) if (ImGui::Selectable(cutTextStyleName((CutTextStyle)i), (int)x.style == i)) { x.style = (CutTextStyle)i; ch = true; } ImGui::EndCombo(); }
            if (x.style == CutTextStyle::Comms) { Prop("Who speaks"); ch |= ImGui::InputTextWithHint("##tsp", "Outpost Control", &x.speaker); }
            ImGui::TextDisabled("Text"); ch |= ImGui::InputTextMultiline("##tt", &x.text, ImVec2(-1, ImGui::GetTextLineHeight() * 4));
            timeField(x.t);
            Prop("Shown for"); ch |= ImGui::DragFloat("##tl", &x.length, 0.02f, 0.2f, 120, "%.2f s");
            Prop("Typing"); ch |= ImGui::SliderFloat("##tsp2", &x.typeSpeed, 0, 80, x.typeSpeed <= 0 ? "all at once" : "%.0f letters a second");
            if (delButton("text")) { c.texts.erase(c.texts.begin() + cutSelIdx); CutSelect(0, 0, -1); }
        } else if (cutSelKind == 5 && cutSelIdx >= 0 && cutSelIdx < (int)c.sounds.size()) {
            CutSound& s = c.sounds[cutSelIdx];
            Prop("Sound"); ch |= SoundEdit("cutsnd", s.sound, false);
            timeField(s.t);
            if (delButton("sound")) { c.sounds.erase(c.sounds.begin() + cutSelIdx); CutSelect(0, 0, -1); }
        } else if (cutSelKind == 6 && cutSelIdx >= 0 && cutSelIdx < (int)c.fades.size()) {
            CutFade& d = c.fades[cutSelIdx];
            Prop("Fade"); if (ImGui::BeginCombo("##fk", cutFadeName(d.kind))) { for (int i = 0; i < (int)CutFadeKind::COUNT; i++) if (ImGui::Selectable(cutFadeName((CutFadeKind)i), (int)d.kind == i)) { d.kind = (CutFadeKind)i; ch = true; } ImGui::EndCombo(); }
            timeField(d.t);
            Prop("How long"); ch |= ImGui::DragFloat("##fl", &d.length, 0.02f, 0.05f, 30, "%.2f s");
            if (delButton("fade")) { c.fades.erase(c.fades.begin() + cutSelIdx); CutSelect(0, 0, -1); }
        } else if (cutSelKind == 7 && cutSelIdx >= 0 && cutSelIdx < (int)c.events.size()) {
            CutEvent& e = c.events[cutSelIdx]; EventDo& d = e.action;
            ImGui::PushID("cutev"); bool kc;
            KindRow(d.kind, 0, kc);
            if (kc) { const Need& nd = needOf(d.kind); if (nd.object && d.target.empty()) d.target = FirstName(nd.objectKind); ch = true; }
            if (d.kind == Do::PlayMusic) { ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); SoundUse u; u.file = d.text; if (MusicEdit("cutmus", u)) { d.text = u.file; ch = true; } }
            else { static const std::vector<std::string> glassNames = glassHitNames(); std::vector<std::string> cutNames = CutsceneNames();
                   inlineChoices = d.kind == Do::VisorEffect ? &glassNames : d.kind == Do::PlayCutscene && !cutNames.empty() ? &cutNames : nullptr;
                   ch |= InlineFields(needOf(d.kind), d.target, d.text, d.text, d.number, NameKind(d.kind), false, false, d.kind == Do::Particles, ImGui::GetContentRegionAvail().x); inlineChoices = nullptr; }
            ImGui::PopID();
            timeField(e.t);
            if (!isLasting(d.kind)) Hint("Skipping the cutscene leaves this one out.");
            if (delButton("action")) { c.events.erase(c.events.begin() + cutSelIdx); CutSelect(0, 0, -1); }
        } else Hint("Click a key or a bar on the timeline to change it here. Drag them to move them in time.\n\nThe camera: fly the editor camera to a spot, move the playhead, press \"+ Camera key\". It glides from key to key.\n\nActors: Samus (a model shown only here) or the gunship (an object of the world that moves).");
    }
    // the editor's guides in the viewport: the camera's path (and each actor's), the keys, the camera at the playhead
    void CutGuides(ImDrawList* dl) {
        if (!cutsEdOpen || playMode || !CutEdValid() || CutLooking()) return;
        const Cutscene& c = cutscenes[cutEd];
        auto path = [&](auto at, float from, float to, ImU32 col) {
            V3 prev; bool have = false;
            for (float t = from; t <= to + 1e-4f; t += 0.05f) { V3 p = at(t); if (have && length(p - prev) < 60) Line3D(dl, prev, p, col); prev = p; have = true; }
        };
        if (!c.cam.empty()) {
            float a = c.cam.front().t, b = c.cam.front().t; for (auto& k : c.cam) { a = std::min(a, k.t); b = std::max(b, k.t); }
            path([&](float t) { return cameraAt(c, t).pos; }, a, b, IM_COL32(255, 180, 70, 200));
            for (int i = 0; i < (int)c.cam.size(); i++) {
                ImVec2 s; if (!Project(c.cam[i].pos, s)) continue; bool sel = cutSelKind == 1 && cutSelIdx == i;
                dl->AddCircleFilled(s, sel ? 6.f : 4.5f, sel ? IM_COL32(255, 230, 120, 255) : IM_COL32(255, 180, 70, 255));
                char b2[8]; snprintf(b2, sizeof b2, "%d", i + 1); dl->AddText(ImVec2(s.x + 6, s.y - 14), IM_COL32(255, 220, 160, 230), b2);
            }
            CamPose p = cameraAt(c, (float)cutT);   // the camera at the playhead: a little pyramid
            if (p.valid) {
                V3 f = poseForward(p), r = normalize(cross(V3{ 0, 1, 0 }, f)), u = cross(f, r);
                double d = 1.2, w = std::tan(p.fov * 3.14159265358979 / 360) * d, h = w * 9 / 16;
                V3 cs[4] = { p.pos + f * d + r * w + u * h, p.pos + f * d - r * w + u * h, p.pos + f * d - r * w - u * h, p.pos + f * d + r * w - u * h };
                for (int i = 0; i < 4; i++) { Line3D(dl, p.pos, cs[i], IM_COL32(255, 90, 60, 255)); Line3D(dl, cs[i], cs[(i + 1) % 4], IM_COL32(255, 90, 60, 255)); }
                Line3D(dl, cs[0] + u * (h * 0.3), cs[1] + u * (h * 0.3), IM_COL32(255, 90, 60, 160));   // (which way is up)
            }
        }
        for (int i = 0; i < (int)c.actors.size(); i++) {
            const CutActor& a = c.actors[i]; if (a.keys.size() < 2) continue;
            float lo = a.keys.front().t, hi = lo; for (auto& k : a.keys) { lo = std::min(lo, k.t); hi = std::max(hi, k.t); }
            path([&](float t) { return actorAt(a, t, c.length).pos; }, lo, hi, IM_COL32(80, 220, 150, 170));
            for (int k = 0; k < (int)a.keys.size(); k++) { ImVec2 s; if (Project(a.keys[k].pos, s)) dl->AddCircleFilled(s, cutSelKind == 2 && cutSelRow == i && cutSelIdx == k ? 6.f : 4.f, IM_COL32(80, 220, 150, 255)); }
        }
    }
    void UpdateCutEditor(double dt) {   // the editor's playhead: playing on, its sounds
        if (!cutsEdOpen || playMode || !CutEdValid()) { cutPlaying = false; return; }
        const Cutscene& c = cutscenes[cutEd];
        if (cutPlaying) {
            double was = cutT; cutT += dt;
            if (cutAudible) for (int i : soundsBetween(c, (float)was, (float)cutT)) PlaySoundIn(SND_WORLD, c.sounds[i].sound);
            if (cutT >= c.length) { cutT = c.length; cutPlaying = false; }
        }
        if (cutsDirty && !ImGui::IsAnyItemActive()) SaveCutscenes();
    }
