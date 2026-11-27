    // ================================================================== crosshairs (src/game/crosshair.h): drawing them in play, their editor
    // (included inside struct App)
    CrosshairSet xh; XhRun xhMain, xhLock, xhHint, xhPrev; uint32_t xhLockWas = 0, xhHintWas = 0;
    bool xhEdOpen = false; int xhSel = 0, xhLayerSel = 0, xhPrevState = 0; float xhPrevCharge = 0, xhPrevKick = 0; bool xhPrevCharging = false;
    void LoadCrosshairs() { xh = defaultCrosshairs(); loadCrosshairs(projectDir + "/crosshairs.txt", xh); xhMain = xhLock = xhHint = xhPrev = XhRun{}; }
    void SaveCrosshairs() { saveCrosshairs(projectDir + "/crosshairs.txt", xh); }

    // ---- one layer, its look now, round c
    void DrawXhLayer(ImDrawList* dl, const XhLayer& L, const XhNow& n, float turnDeg, ImVec2 c, float ui) {
        float a = std::clamp(n.alpha * n.shown, 0.f, 1.f); if (a < 0.004f) return;
        auto col = [&](float al) { return IM_COL32((int)(std::clamp(n.r, 0.f, 1.f) * 255), (int)(std::clamp(n.g, 0.f, 1.f) * 255), (int)(std::clamp(n.b, 0.f, 1.f) * 255), (int)(std::clamp(al, 0.f, 1.f) * 255)); };
        c.x += L.x * ui; c.y += L.y * ui;
        float R = std::max(0.5f, L.size * n.scale * ui), th = std::max(0.6f, L.thick * ui), rot = turnDeg * 3.14159265f / 180;
        int cnt = std::clamp(L.count, 1, 16); float step = 6.2831853f / cnt;
        auto at = [&](float ang, float r) { return ImVec2(c.x + std::cos(ang) * r, c.y + std::sin(ang) * r); };
        auto pass = [&](ImU32 k, float t, float grow) {
            switch (L.shape) {
            case XhShape::Dot: dl->AddCircleFilled(c, R + grow, k, 20); break;
            case XhShape::Ring: dl->AddCircle(c, R, k, 64, t); break;
            case XhShape::Arcs: {
                float open = std::clamp(L.gap, 0.f, 350.f / cnt) * 3.14159265f / 180, span = step - open;
                for (int i = 0; i < cnt; i++) { float a0 = rot + i * step + open * 0.5f; dl->PathArcTo(c, R, a0, a0 + span, 20); dl->PathStroke(k, 0, t); }
                break; }
            case XhShape::Brackets:
                for (int i = 0; i < cnt; i++) { float ang = rot + 0.7853982f + i * step; ImVec2 p = at(ang, R); float arm = R * 0.45f;
                    ImVec2 e1(p.x + std::cos(ang + 2.3561945f) * arm, p.y + std::sin(ang + 2.3561945f) * arm), e2(p.x + std::cos(ang - 2.3561945f) * arm, p.y + std::sin(ang - 2.3561945f) * arm);
                    ImVec2 pts[3] = { e1, p, e2 }; dl->AddPolyline(pts, 3, k, 0, t); }
                break;
            case XhShape::Ticks: for (int i = 0; i < cnt; i++) { float ang = rot + i * step; dl->AddLine(at(ang, R), at(ang, R * 0.62f), k, t); } break;
            case XhShape::Cross: { float hole = std::max(0.f, L.gap) * ui * n.scale; for (int i = 0; i < cnt; i++) { float ang = rot + i * step; dl->AddLine(at(ang, hole), at(ang, R), k, t); } break; }
            case XhShape::Triangles:
                for (int i = 0; i < cnt; i++) { float ang = rot + i * step; ImVec2 tip = at(ang, R * 0.66f), b0 = at(ang, R), pr(-std::sin(ang), std::cos(ang)); float w = R * 0.22f + grow;
                    dl->AddTriangleFilled(tip, ImVec2(b0.x + pr.x * w, b0.y + pr.y * w), ImVec2(b0.x - pr.x * w, b0.y - pr.y * w), k); }
                break;
            case XhShape::Picture:
                if (ID3D11ShaderResourceView* im = HudImage(L.picture)) {
                    float h = R + grow, cs = std::cos(rot), sn = std::sin(rot); auto P = [&](float x, float y) { return ImVec2(c.x + x * cs - y * sn, c.y + x * sn + y * cs); };
                    dl->AddImageQuad((ImTextureID)(intptr_t)im, P(-h, -h), P(h, -h), P(h, h), P(-h, h), ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), k);
                } else dl->AddRect(ImVec2(c.x - R, c.y - R), ImVec2(c.x + R, c.y + R), k, 0.f, 0, 1.f);
                break;
            default: break;
            }
        };
        if (L.glow > 0.01f && L.shape != XhShape::Picture) { pass(col(a * 0.16f * L.glow), th + 5 * ui * L.glow, 2.5f * ui * L.glow); pass(col(a * 0.3f * L.glow), th + 2 * ui * L.glow, 1.2f * ui * L.glow); }
        pass(col(a), th, 0);
    }
    void DrawXh(ImDrawList* dl, const Crosshair& c, const XhRun& run, ImVec2 at, float ui) {
        for (size_t i = 0; i < c.layers.size() && i < run.now.size(); i++) DrawXhLayer(dl, c.layers[i], run.now[i], run.turnOf(i), at, ui);
    }
    float XhUi(float screenH) const { return std::max(0.5f, screenH / 1080.f) * settings.uiScale; }

    // ---- play: the crosshair of the visor in use, in its state now
    bool DrawPlayCrosshair(ImDrawList* dl, ImVec2 m, float screenH) {   // false: none made (the plain one shows)
        const Crosshair* c = xh.forVisor(visor); if (!c || c->layers.empty()) return false;
        if (xh.hideWhileLocked && lockId && visor != 1) return true;
        float dt = std::min(0.1f, (float)ImGui::GetIO().DeltaTime);
        float charge = visor == 0 && weapState.charging ? std::clamp(weapState.charge, 0.f, 1.f) : 0.f;
        float kick = std::clamp(1.f - (float)(animTime - weapShotAt) / 0.16f, 0.f, 1.f);
        XhState s = XhState::Idle;
        bool onTarget = visor == 1 ? scanTargetId != 0 : (lockHint != 0 || lockId != 0);
        if (onTarget) s = XhState::OnTarget;
        else if (visor == 0 && world) { const Beam* b = CurrentBeam(); Hit h; if (b && !world->raycast(player.eye(), AimDir(), std::max(2.f, b->range), h)) s = XhState::Blocked; }   // nothing in reach
        if (charge >= 1) { s = XhState::Charging; charge = 0; }   // full: the charging look itself (it throbs)
        xhMain.update(*c, s, charge, kick, dt);
        float ui = XhUi(screenH);
        if (s == XhState::Charging) ui *= 1 + 0.06f * std::sin((float)animTime * 14);
        DrawXh(dl, *c, xhMain, m, ui);
        return true;
    }
    // ---- the lock-on reticle on the target (true: drawn - else the built-in one)
    bool DrawLockReticle(ImDrawList* dl, ImVec2 p, float screenH, bool hint) {
        const Crosshair* c = xh.find(hint ? xh.lockHint : xh.lockOn); if (!c || c->layers.empty()) return false;
        float dt = std::min(0.1f, (float)ImGui::GetIO().DeltaTime);
        XhRun& run = hint ? xhHint : xhLock; uint32_t& was = hint ? xhHintWas : xhLockWas; uint32_t id = hint ? lockHint : lockId;
        if (id != was) { was = id; run = XhRun{}; if (!hint) run.update(*c, XhState::Locking, 0, 0, 0); }   // a new target: from the locking look
        XhState s = hint ? XhState::Idle : lockLost > 0 ? XhState::Lost : XhState::Locked;
        run.update(*c, s, 0, 0, dt);
        DrawXh(dl, *c, run, p, XhUi(screenH));
        return true;
    }

    // ---- the editor
    void CrosshairEditor() {
        if (!xhEdOpen) return;
        float ui = settings.uiScale;
        ImGui::SetNextWindowSize(ImVec2(900 * ui, 620 * ui), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Crosshairs", &xhEdOpen)) { ImGui::End(); return; }
        bool ch = false;
        xhSel = std::clamp(xhSel, 0, std::max(0, (int)xh.list.size() - 1));
        // ---- left: the crosshairs, and which is used where
        ImGui::BeginChild("##xhl", ImVec2(250 * ui, 0), true);
        ImGui::SeparatorText("Crosshairs");
        for (int i = 0; i < (int)xh.list.size(); i++) { std::string l = xh.list[i].name + (xh.list[i].lock ? "  (lock-on)" : "") + "##xh" + std::to_string(i); if (ImGui::Selectable(l.c_str(), i == xhSel)) { xhSel = i; xhLayerSel = 0; xhPrev = XhRun{}; } }
        if (ImGui::Button("+ New")) { Crosshair c; c.name = "Crosshair " + std::to_string(xh.list.size() + 1); XhLayer d; d.shape = XhShape::Dot; d.size = 2; d.name = "Dot"; c.layers.push_back(d); xh.list.push_back(c); xhSel = (int)xh.list.size() - 1; xhLayerSel = 0; ch = true; }
        ImGui::SameLine(); if (ImGui::Button("Copy") && xhSel < (int)xh.list.size()) { Crosshair c = xh.list[xhSel]; c.name += " copy"; xh.list.push_back(c); xhSel = (int)xh.list.size() - 1; ch = true; }
        ImGui::SameLine(); if (ImGui::Button("Delete") && xhSel < (int)xh.list.size() && xh.list.size() > 1) { xh.list.erase(xh.list.begin() + xhSel); xhSel = 0; xhLayerSel = 0; ch = true; }
        ImGui::SeparatorText("Used for");
        auto pick = [&](const char* label, const char* id, std::string& v, bool lock) {
            ImGui::TextUnformatted(label); ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo(id, v.empty() ? (lock ? "(built-in)" : "(the first one)") : v.c_str())) {
                if (ImGui::Selectable(lock ? "(built-in)" : "(the first one)", v.empty())) { v.clear(); ch = true; }
                for (const Crosshair& c : xh.list) if (c.lock == lock && ImGui::Selectable(c.name.c_str(), c.name == v)) { v = c.name; ch = true; }
                ImGui::EndCombo(); }
        };
        for (int v = 0; v < VISORS; v++) pick((std::string(visorName((VisorKind)v)) + " Visor").c_str(), ("##xhv" + std::to_string(v)).c_str(), xh.visor[v], false);
        pick("Locked on (on the target)", "##xhlo", xh.lockOn, true);
        pick("What a press would lock onto", "##xhlh", xh.lockHint, true);
        ch |= ImGui::Checkbox("Hide the crosshair while locked on", &xh.hideWhileLocked);
        ImGui::EndChild();
        ImGui::SameLine();
        if (xh.list.empty()) { ImGui::End(); if (ch) SaveCrosshairs(); return; }
        Crosshair& c = xh.list[xhSel];
        // ---- middle: the crosshair's layers and the selected layer
        ImGui::BeginChild("##xhm", ImVec2(360 * ui, 0), true);
        { char b[128]; snprintf(b, sizeof b, "%s", c.name.c_str()); Prop("Name"); ImGui::SetNextItemWidth(-1); if (ImGui::InputText("##xhn", b, sizeof b)) {
            std::string old = c.name; c.name = b; for (int v = 0; v < VISORS; v++) if (xh.visor[v] == old) xh.visor[v] = c.name; if (xh.lockOn == old) xh.lockOn = c.name; if (xh.lockHint == old) xh.lockHint = c.name; ch = true; } }
        Prop("Lock-on reticle"); ch |= ImGui::Checkbox("##xhlk", &c.lock);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sits on the target you lock onto; its states are Locking, Locked and Losing it.");
        Prop("Glides"); ImGui::SetNextItemWidth(-1); ch |= ImGui::SliderFloat("##xhbl", &c.blend, 1, 40, "%.0f (bigger: quicker)");
        ImGui::SeparatorText("Layers (drawn first to last)");
        xhLayerSel = std::clamp(xhLayerSel, 0, std::max(0, (int)c.layers.size() - 1));
        for (int i = 0; i < (int)c.layers.size(); i++) { std::string l = c.layers[i].name + "   (" + xhShapeName(c.layers[i].shape) + ")##xly" + std::to_string(i); if (ImGui::Selectable(l.c_str(), i == xhLayerSel)) xhLayerSel = i; }
        if (ImGui::Button("+ Layer")) ImGui::OpenPopup("##xhadd");
        if (ImGui::BeginPopup("##xhadd")) { for (int s = 0; s < (int)XhShape::COUNT; s++) if (ImGui::Selectable(xhShapeName((XhShape)s))) { XhLayer L; L.shape = (XhShape)s; L.name = xhShapeName((XhShape)s); if (L.shape == XhShape::Dot) L.size = 2; if (L.shape == XhShape::Picture) { L.size = 24; L.glow = 0; auto f = HudImageFiles(); if (!f.empty()) L.picture = f[0]; } c.layers.push_back(L); xhLayerSel = (int)c.layers.size() - 1; ch = true; } ImGui::EndPopup(); }
        if (!c.layers.empty()) {
            ImGui::SameLine(); if (ImGui::Button("Up") && xhLayerSel > 0) { std::swap(c.layers[xhLayerSel], c.layers[xhLayerSel - 1]); xhLayerSel--; ch = true; }
            ImGui::SameLine(); if (ImGui::Button("Down") && xhLayerSel + 1 < (int)c.layers.size()) { std::swap(c.layers[xhLayerSel], c.layers[xhLayerSel + 1]); xhLayerSel++; ch = true; }
            ImGui::SameLine(); if (ImGui::Button("Copy##xl")) { c.layers.insert(c.layers.begin() + xhLayerSel + 1, c.layers[xhLayerSel]); xhLayerSel++; ch = true; }
            ImGui::SameLine(); if (ImGui::Button("Delete##xl")) { c.layers.erase(c.layers.begin() + xhLayerSel); xhLayerSel = std::max(0, xhLayerSel - 1); ch = true; }
        }
        if (xhLayerSel < (int)c.layers.size()) {
            XhLayer& L = c.layers[xhLayerSel];
            ImGui::SeparatorText("Layer");
            { char b[96]; snprintf(b, sizeof b, "%s", L.name.c_str()); Prop("Name"); ImGui::SetNextItemWidth(-1); if (ImGui::InputText("##xln", b, sizeof b)) { L.name = b; ch = true; } }
            Prop("Shape"); { int s = (int)L.shape; ImGui::SetNextItemWidth(-1); if (ImGui::BeginCombo("##xls", xhShapeName(L.shape))) { for (int k = 0; k < (int)XhShape::COUNT; k++) if (ImGui::Selectable(xhShapeName((XhShape)k), k == s)) { L.shape = (XhShape)k; ch = true; } ImGui::EndCombo(); } }
            if (L.shape == XhShape::Picture) {
                Prop("Picture"); ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##xlp", L.picture.empty() ? "(none)" : L.picture.c_str())) { for (const std::string& f : HudImageFiles()) if (ImGui::Selectable(f.c_str(), f == L.picture)) { L.picture = f; ch = true; } ImGui::EndCombo(); }
                if (ImGui::SmallButton("Add pictures...")) { std::error_code ec; fs::create_directories(fs::u8path(projectDir + "/hud"), ec);
                    for (const std::string& f : PickImages()) { fs::path s = fs::u8path(f); fs::copy_file(s, fs::u8path(projectDir + "/hud/" + s.filename().u8string()), fs::copy_options::overwrite_existing, ec); if (!ec) { L.picture = s.filename().u8string(); hudImages.erase(L.picture); ch = true; } } }
                Hint("PNG pictures (with see-through parts) in the project's hud folder; drawn tinted by the colour (white: as it is).");
            }
            Prop("Colour"); ch |= ColorU32("##xlc", L.color);
            Prop("Visible"); ImGui::SetNextItemWidth(-1); ch |= ImGui::SliderFloat("##xla", &L.alpha, 0, 1, "%.2f");
            Prop(L.shape == XhShape::Dot ? "Radius" : "Size"); ImGui::SetNextItemWidth(-1); ch |= ImGui::SliderFloat("##xlz", &L.size, 0.5f, 120, "%.1f px");
            if (L.shape != XhShape::Dot && L.shape != XhShape::Picture && L.shape != XhShape::Triangles) { Prop("Thickness"); ImGui::SetNextItemWidth(-1); ch |= ImGui::SliderFloat("##xlt", &L.thick, 0.5f, 12, "%.1f px"); }
            if (L.shape != XhShape::Picture) { Prop("Glow"); ImGui::SetNextItemWidth(-1); ch |= ImGui::SliderFloat("##xlg", &L.glow, 0, 2, "%.2f"); }
            if (L.shape == XhShape::Arcs || L.shape == XhShape::Brackets || L.shape == XhShape::Ticks || L.shape == XhShape::Triangles || L.shape == XhShape::Cross) { Prop("How many"); ImGui::SetNextItemWidth(-1); ch |= ImGui::SliderInt("##xlk", &L.count, 1, 12); }
            if (L.shape == XhShape::Arcs) { Prop("Gaps"); ImGui::SetNextItemWidth(-1); ch |= ImGui::SliderFloat("##xlgp", &L.gap, 0, 120, "%.0f deg"); }
            if (L.shape == XhShape::Cross) { Prop("Hole"); ImGui::SetNextItemWidth(-1); ch |= ImGui::SliderFloat("##xlh", &L.gap, 0, 40, "%.1f px"); }
            Prop("Turned"); ImGui::SetNextItemWidth(-1); ch |= ImGui::SliderFloat("##xlr", &L.turn, -180, 180, "%.0f deg");
            Prop("Turning"); ImGui::SetNextItemWidth(-1); ch |= ImGui::SliderFloat("##xlsp", &L.spin, -360, 360, "%.0f deg/s");
            Prop("Moved"); { float xy[2] = { L.x, L.y }; ImGui::SetNextItemWidth(-1); if (ImGui::DragFloat2("##xlo", xy, 0.2f, -200, 200, "%.1f")) { L.x = xy[0]; L.y = xy[1]; ch = true; } }
            // its look in each state
            ImGui::SeparatorText("In each state (times the above)");
            int s0 = c.lock ? (int)XhState::Locking : 0, s1 = c.lock ? XH_STATES : (int)XhState::Locking;
            if (ImGui::BeginTabBar("##xlst")) {
                for (int s = s0; s < s1; s++) if (ImGui::BeginTabItem(xhStateName((XhState)s))) {
                    XhLook& o = L.look[s]; ImGui::PushID(s);
                    Prop("Shown"); ch |= ImGui::Checkbox("##os", &o.shown);
                    Prop("Size"); ImGui::SetNextItemWidth(-1); ch |= ImGui::SliderFloat("##oz", &o.scale, 0, 4, "x %.2f");
                    Prop("Visible"); ImGui::SetNextItemWidth(-1); ch |= ImGui::SliderFloat("##oa", &o.alpha, 0, 2, "x %.2f");
                    Prop("Turned more"); ImGui::SetNextItemWidth(-1); ch |= ImGui::SliderFloat("##ot", &o.turn, -360, 360, "%.0f deg");
                    Prop("Turning more"); ImGui::SetNextItemWidth(-1); ch |= ImGui::SliderFloat("##osp", &o.spin, -720, 720, "%.0f deg/s");
                    Prop("Own colour"); ch |= ImGui::Checkbox("##ooc", &o.ownColor); if (o.ownColor) { ImGui::SameLine(); ch |= ColorU32("##oc", o.color); }
                    if (s == (int)XhState::Charging) Hint("While it charges it moves from its resting look toward this one as the beam fills; full: this look, throbbing.");
                    if (s == (int)XhState::Firing) Hint("A quick kick toward this look with each shot, then back.");
                    if (s == (int)XhState::Blocked) Hint("Nothing within the beam's reach under the crosshair (the sky, far away).");
                    if (s == (int)XhState::OnTarget) Hint("An enemy near the crosshair (what a press would lock onto); in the Scan Visor: something that can be scanned.");
                    ImGui::PopID(); ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        // ---- right: the preview
        ImGui::BeginChild("##xhp", ImVec2(0, 0), true);
        ImGui::SeparatorText("Preview");
        {
            int s0 = c.lock ? (int)XhState::Locking : 0, s1 = c.lock ? XH_STATES : (int)XhState::Charging;
            if (xhPrevState < s0 || xhPrevState >= s1) xhPrevState = s0;
            for (int s = s0; s < s1; s++) { if (s > s0) ImGui::SameLine(); if (ImGui::RadioButton(xhStateName((XhState)s), xhPrevState == s)) xhPrevState = s; }
            if (!c.lock) {
                ImGui::SameLine(); ImGui::RadioButton("Blocked##pb", &xhPrevState, (int)XhState::Blocked);
                ImGui::Checkbox("Charging", &xhPrevCharging); ImGui::SameLine(); ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##xpc", &xhPrevCharge, 0, 1, "full %.2f");
                if (ImGui::Button("Fire")) xhPrevKick = 1;
                if (c.lock) {} else { ImGui::SameLine(); if (ImGui::Button("Charge up")) { xhPrevCharging = true; xhPrevCharge = 0; } }
            } else if (ImGui::Button("Lock again")) { xhPrev = XhRun{}; xhPrev.update(c, XhState::Locking, 0, 0, 0); xhPrevState = (int)XhState::Locked; }
        }
        float dt = std::min(0.1f, (float)ImGui::GetIO().DeltaTime);
        if (xhPrevCharging && xhPrevCharge < 1) xhPrevCharge = std::min(1.f, xhPrevCharge + dt / 1.2f);
        xhPrevKick = std::max(0.f, xhPrevKick - dt / 0.16f);
        ImVec2 p0 = ImGui::GetCursorScreenPos(), av = ImGui::GetContentRegionAvail(); float side = std::max(120.f, std::min(av.x, av.y));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilledMultiColor(p0, ImVec2(p0.x + side, p0.y + side), IM_COL32(40, 60, 80, 255), IM_COL32(40, 60, 80, 255), IM_COL32(18, 24, 30, 255), IM_COL32(18, 24, 30, 255));
        dl->AddLine(ImVec2(p0.x, p0.y + side * 0.62f), ImVec2(p0.x + side, p0.y + side * 0.62f), IM_COL32(70, 90, 70, 120));
        {
            XhState s = (XhState)xhPrevState; float charge = xhPrevCharging ? xhPrevCharge : 0;
            if (!c.lock && xhPrevCharging && charge >= 1) { s = XhState::Charging; charge = 0; }
            xhPrev.update(c, s, charge, xhPrevKick, dt);
            float k = side / 260.f * 1.6f; if (s == XhState::Charging) k *= 1 + 0.06f * std::sin((float)uiClock * 14);
            dl->PushClipRect(p0, ImVec2(p0.x + side, p0.y + side), true);
            DrawXh(dl, c, xhPrev, ImVec2(p0.x + side * 0.5f, p0.y + side * 0.5f), k);
            dl->PopClipRect();
        }
        ImGui::Dummy(ImVec2(side, side));
        Hint("Shown bigger than in the game (1.6 times a 1080-line screen).");
        ImGui::EndChild();
        ImGui::End();
        if (ch) SaveCrosshairs();
    }
