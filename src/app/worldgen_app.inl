    // ================================================================== the world generator (included inside struct App)
    // Its window: for a new world (then "Create the world") or the open one (then "Make the land again"). The settings on
    // the left, a live map from above and a cut through the land on the right (made on another thread as you change things).
    bool genEdOpen = false, genForNew = false, focusGenEd = false; TerrainSettings genEd; std::string genNewName = "New World";
    std::future<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> genFut; bool genBusy = false; uint64_t genHaveSig = 0, genBusySig = 0;
    ComPtr<ID3D11Texture2D> genMapTex, genCutTex; ComPtr<ID3D11ShaderResourceView> genMapSRV, genCutSRV; float genZoom = 2.0f;
    static constexpr int GEN_W = 384, GEN_H = 384, GEN_CW = 384, GEN_CH = 128;

    uint64_t GenSig() const { return std::hash<std::string>{}(terrainText(genEd)) ^ ((uint64_t)(genZoom * 1000) << 1); }
    void GenUpload(ComPtr<ID3D11Texture2D>& tex, ComPtr<ID3D11ShaderResourceView>& srv, int w, int h, const std::vector<uint8_t>& px) {
        if (px.size() != (size_t)w * h * 4) return;
        if (!tex) {
            D3D11_TEXTURE2D_DESC d{}; d.Width = (UINT)w; d.Height = (UINT)h; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA sd{ px.data(), (UINT)w * 4, 0 }; HR(dev->CreateTexture2D(&d, &sd, &tex)); HR(dev->CreateShaderResourceView(tex.Get(), nullptr, &srv)); return;
        }
        ctx->UpdateSubresource(tex.Get(), 0, nullptr, px.data(), (UINT)w * 4, 0);
    }
    void GenPreviewUpdate() {   // a finished preview goes up; a new one starts when the settings changed (one at a time: the newest wins)
        if (genBusy && genFut.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto r = genFut.get(); genBusy = false; genHaveSig = genBusySig;
            GenUpload(genMapTex, genMapSRV, GEN_W, GEN_H, r.first); GenUpload(genCutTex, genCutSRV, GEN_CW, GEN_CH, r.second);
        }
        uint64_t want = GenSig();
        if (!genBusy && want != genHaveSig) {
            auto gen = std::make_shared<TerrainGen>(genEd, reg); auto pal = std::make_shared<TerrainPalette>(terrainPalette(*gen, reg)); double z = genZoom;
            int lo = genEd.mode == TerrainMode::Islands ? genEd.islandY - 40 : genEd.mode == TerrainMode::Underground ? genEd.ceiling - 110 : (int)genEd.base - 56;
            genFut = std::async(std::launch::async, [gen, pal, z, lo] { return std::make_pair(terrainPreviewMap(*gen, *pal, GEN_W, GEN_H, 0, 0, z), terrainPreviewCut(*gen, *pal, GEN_CW, GEN_CH, 0, 0, z, lo, lo + 128)); });
            genBusy = true; genBusySig = want;
        }
    }
    std::vector<std::string> GenMissingBlocks(const TerrainSettings& t) const { std::vector<std::string> v; for (const std::string& n : terrainBlocksUsed(t)) if (reg.find(n) == AIR) v.push_back(n); return v; }
    void GenAddBlocks(const TerrainSettings& t) {   // the terrain kit's blocks the world needs and the game does not have yet
        std::vector<std::string> miss = GenMissingBlocks(t); if (miss.empty()) return;
        std::vector<std::string> added; ChangeBlocks([&] { added = library.addKitBlocks(reg, miss); });
        if (!added.empty()) { std::string s; for (auto& a : added) s += (s.empty() ? "" : ", ") + a; Toast("New blocks: " + s); }
    }
    void OpenGenerator(bool forNew) {
        genForNew = forNew; genEdOpen = true; focusGenEd = true; genHaveSig = 0;
        if (forNew) { genEd = terrainPreset("Overgrown"); genEd.seed = (uint32_t)(std::time(nullptr) % 100000); genNewName = "New World"; }
        else genEd = info.genKind == Generator::Terrain ? info.terrain : terrainPreset("Overgrown");
    }
    void CreateGeneratedWorld(const std::string& name, const TerrainSettings& t) {
        GenAddBlocks(t);
        std::string folder = Projects::uniqueFolder(WorldsDir(), name.empty() ? "World" : name);
        WorldInfo wi; wi.name = name; wi.genKind = Generator::Terrain; wi.terrain = t; wi.layers.clear();
        TerrainGen g(t, reg); V3 sp = g.spawnPoint();
        wi.hasSpawn = true; wi.spawn = sp; wi.player.feet = sp + V3{ 0, 3, 0 }; wi.player.flying = true;
        if (t.type == "Frozen") { wi.environment = "arctic"; wi.weather = "snow"; } else if (t.type == "Volcanic") wi.environment = "volcanic";
        else if (t.type == "Desert ruins") wi.environment = "desert"; else if (t.type == "Corrupted") wi.environment = "alien";
        const char* defaults[9] = { "grass", "dirt", "stone", "cobblestone", "planks", "log", "glass", "bricks", "lamp" };
        for (int i = 0; i < 9; i++) wi.hotbar[i] = defaults[i];
        std::vector<std::string> used = terrainBlocksUsed(t); for (size_t i = 0; i < used.size() && i < 4; i++) if (!reg.get(reg.find(used[i])).liquid) wi.hotbar[i] = used[i];   // (its own ground and rock first)
        std::string dir = WorldsDir() + "/" + folder; wi.save(dir);
        if (findEnv(wi.environment) < 0) { wi.environment = "temperate"; wi.save(dir); }
        if (findWeather(wi.weather) < 0) { wi.weather = "partly_cloudy"; wi.save(dir); }
        OpenWorld(folder); Toast("A new " + t.type + " world: " + name);
    }
    void RegenerateWorld(const TerrainSettings& t) {   // the open world: its land made again (what was built in it is gone; its objects stay)
        if (!world) return;
        GenAddBlocks(t);
        std::string folder = CurrentWorld(), dir = worldDir;
        CloseWorld();
        WorldInfo wi; wi.load(dir); wi.genKind = Generator::Terrain; wi.terrain = t;
        TerrainGen g(t, reg); V3 sp = g.spawnPoint(); wi.hasSpawn = true; wi.spawn = sp; wi.player.feet = sp + V3{ 0, 3, 0 }; wi.player.flying = true;
        wi.save(dir);
        std::error_code ec; fs::remove_all(fs::u8path(dir + "/region"), ec);
        OpenWorld(folder); Toast("The land is made again");
    }

    bool GenBlockCombo(const char* id, std::string& name, int what) {   // what: 0 solid blocks, 1 liquids (or none), 2 solid or none
        bool ch = false; std::string shown = name.empty() ? "(none)" : name;
        if (!name.empty() && reg.find(name) == AIR) shown += "  (new)";
        if (ImGui::BeginCombo(id, shown.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (what != 0 && ImGui::Selectable("(none)", name.empty())) { name.clear(); ch = true; }
            std::vector<std::string> seen;
            for (size_t i = 1; i < reg.count(); i++) {
                if (!reg.has((BlockId)i)) continue; const BlockDef& d = reg.get((BlockId)i);
                if ((what == 1) != (d.liquid != 0) || d.spring || d.render == Render::Air) continue;
                if (what != 1 && d.shape != Shape::Cube) continue;
                seen.push_back(d.name);
                if (ImGui::Selectable(d.name.c_str(), d.name == name)) { name = d.name; ch = true; }
            }
            if (what != 1) {
                bool head = false;
                for (const std::string& k : terrainKitNames()) {
                    if (std::find(seen.begin(), seen.end(), k) != seen.end()) continue;
                    if (!head) { ImGui::SeparatorText("New (added when the world is made)"); head = true; }
                    if (ImGui::Selectable((k + "##kit").c_str(), k == name)) { name = k; ch = true; }
                }
            }
            ImGui::EndCombo();
        }
        return ch;
    }

    void GeneratorWindow() {
        if (!genEdOpen) return;
        GenPreviewUpdate();
        float ui = settings.uiScale;
        ImGui::SetNextWindowSize(ImVec2(980 * ui, 700 * ui), ImGuiCond_FirstUseEver);
        if (focusGenEd) { ImGui::SetNextWindowFocus(); focusGenEd = false; }
        if (!ImGui::Begin(genForNew ? "World generator: a new world###worldgen" : "World generator###worldgen", &genEdOpen)) { ImGui::End(); return; }
        TerrainSettings& t = genEd;
        ImGui::BeginChild("genleft", ImVec2(430 * ui, -ImGui::GetFrameHeightWithSpacing() * 1.6f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        if (genForNew) { Prop("Name"); ImGui::InputText("##gnn", &genNewName); }
        Prop("World type"); if (ImGui::BeginCombo("##gtype", t.type.c_str())) {
            for (const std::string& p : terrainPresetNames()) if (ImGui::Selectable(p.c_str(), p == t.type)) { uint32_t sd = t.seed; int sz = t.size; TerrainEdge e = t.edge; t = terrainPreset(p); t.seed = sd; t.size = sz; t.edge = e; }
            ImGui::EndCombo(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A starting point: everything below can be changed.");
        Prop("Seed"); { int sd = (int)t.seed; ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70 * ui); if (ImGui::InputInt("##gseed", &sd)) t.seed = (uint32_t)std::max(0, sd);
                        ImGui::SameLine(); if (ImGui::Button("Dice")) t.seed = (uint32_t)((t.seed * 1103515245u + 12345u + (uint32_t)std::time(nullptr)) % 1000000u); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Another seed: another land with the same settings. The same seed always makes the same land.");
        Prop("Kind"); { int m = (int)t.mode; if (ImGui::Combo("##gmode", &m, "Land\0Floating islands\0Underground\0")) t.mode = (TerrainMode)m; }
        if (ImGui::CollapsingHeader("The land's shape", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (t.mode == TerrainMode::Underground) {
                Prop("Rock up to"); ImGui::SliderInt("##gceil", &t.ceiling, -20, 90, "y %d"); Hint("Solid rock up to this height, with caves and caverns in it. Build your rooms inside.");
            } else if (t.mode == TerrainMode::Islands) {
                Prop("How many"); ImGui::SliderFloat("##gisl", &t.islands, 0.05f, 1, "%.2f");
                Prop("Height"); ImGui::SliderInt("##gisy", &t.islandY, -20, 80, "y %d");
                Prop("Thickness"); ImGui::SliderFloat("##gist", &t.islandThick, 4, 40, "%.0f blocks");
            } else { Prop("Ground height"); ImGui::SliderFloat("##gbase", &t.base, -30, 40, "y %.0f"); }
            Prop("Hills"); ImGui::SliderFloat("##ghl", &t.hills, 0, 40, "%.0f blocks high");
            Prop("Hills' width"); ImGui::SliderFloat("##ghs", &t.hillSize, 10, 300, "%.0f blocks");
            if (t.mode == TerrainMode::Land) {
                Prop("Mountains"); ImGui::SliderFloat("##gmt", &t.mountains, 0, 70, "%.0f blocks high");
                if (t.mountains > 0) {
                    Prop("Mountains' width"); ImGui::SliderFloat("##gms", &t.mountainSize, 40, 600, "%.0f blocks");
                    Prop("How much is mountains"); ImGui::SliderFloat("##gmsh", &t.mountainShare, 0, 1, "%.2f");
                    Prop("Ridges, spires"); ImGui::SliderFloat("##grd", &t.ridged, 0, 1, "%.2f"); if (ImGui::IsItemHovered()) ImGui::SetTooltip("0: round mountains. 1: sharp ridges and spires.");
                }
                Prop("Terraces"); ImGui::SliderFloat("##gtr", &t.terrace, 0, 16, t.terrace < 1 ? "none" : "a step every %.0f blocks");
                if (t.terrace >= 1) { Prop("Cliffs"); ImGui::SliderFloat("##gts", &t.terraceSharp, 0, 1, "%.2f"); if (ImGui::IsItemHovered()) ImGui::SetTooltip("0: soft slopes between the steps. 1: sheer cliffs (mesas, ledges)."); }
            }
            Prop("Roughness"); ImGui::SliderFloat("##grg", &t.roughness, 0, 1, "%.2f");
        }
        if (ImGui::CollapsingHeader("Layers (what it is made of)", ImGuiTreeNodeFlags_DefaultOpen)) {
            Hint("From the surface down. Each: a block, how deep, and how much its depth varies.");
            int del = -1, up = -1;
            for (int i = 0; i < (int)t.layers.size(); i++) {
                ImGui::PushID(i); TerrainLayer& l = t.layers[i];
                ImGui::SetNextItemWidth(150 * ui); GenBlockCombo("##lb", l.block, 0); ImGui::SameLine();
                ImGui::SetNextItemWidth(70 * ui); ImGui::DragInt("##lt", &l.thick, 0.1f, 1, 40, "%d deep"); ImGui::SameLine();
                ImGui::SetNextItemWidth(60 * ui); ImGui::DragInt("##lv", &l.vary, 0.1f, 0, 10, "+-%d"); ImGui::SameLine();
                if (i > 0 && ImGui::SmallButton("^")) up = i; ImGui::SameLine(); if (ImGui::SmallButton("x")) del = i;
                ImGui::PopID();
            }
            if (del >= 0) t.layers.erase(t.layers.begin() + del);
            if (up > 0) std::swap(t.layers[up], t.layers[up - 1]);
            if (t.layers.size() < 16 && ImGui::SmallButton("+ Layer")) t.layers.push_back({ "dirt", 2, 0 });
            Prop("Then rock"); GenBlockCombo("##grock", t.rock, 0);
            ImGui::TextDisabled("Stripes in the rock (cliffs show them)");
            del = -1;
            for (int i = 0; i < (int)t.bands.size(); i++) { ImGui::PushID(100 + i); ImGui::SetNextItemWidth(170 * ui); GenBlockCombo("##bb", t.bands[i], 0); ImGui::SameLine(); if (ImGui::SmallButton("x")) del = i; ImGui::PopID(); }
            if (del >= 0) t.bands.erase(t.bands.begin() + del);
            if (t.bands.size() < 16 && ImGui::SmallButton("+ Stripe")) t.bands.push_back(t.bands.empty() ? t.rock : t.bands.back());
            if (!t.bands.empty()) { Prop("Stripe thickness"); ImGui::SliderInt("##gbt", &t.bandThick, 1, 12, "%d blocks"); }
            ImGui::TextDisabled("Veins (blobs of a block in the rock: crystals, ore, Phazon)");
            del = -1;
            for (int i = 0; i < (int)t.veins.size(); i++) {
                ImGui::PushID(200 + i); TerrainVein& v = t.veins[i];
                ImGui::SetNextItemWidth(140 * ui); GenBlockCombo("##vb", v.block, 0); ImGui::SameLine();
                ImGui::SetNextItemWidth(70 * ui); ImGui::SliderFloat("##va", &v.amount, 0.002f, 0.15f, "%.3f"); ImGui::SameLine();
                ImGui::SetNextItemWidth(60 * ui); ImGui::SliderFloat("##vs", &v.size, 1, 8, "size %.1f"); ImGui::SameLine(); if (ImGui::SmallButton("x")) del = i;
                ImGui::SetNextItemWidth(200 * ui); int r[2] = { v.minY, v.maxY }; if (ImGui::DragInt2("##vy", r, 0.5f, -128, 128, "y %d")) { v.minY = std::min(r[0], r[1]); v.maxY = std::max(r[0], r[1]); }
                ImGui::PopID();
            }
            if (del >= 0) t.veins.erase(t.veins.begin() + del);
            if (t.veins.size() < 8 && ImGui::SmallButton("+ Vein")) t.veins.push_back({ "phazon crystal", 0.03f, 3, -64, 40 });
            Prop("The world's floor"); ImGui::SliderInt("##gbot", &t.bottom, -128, 0, "y %d"); Prop("Floor block"); GenBlockCombo("##gbotb", t.bottomBlock, 0);
        }
        if (ImGui::CollapsingHeader("Lakes and seas", ImGuiTreeNodeFlags_DefaultOpen)) {
            Prop("Filled with"); GenBlockCombo("##gliq", t.liquid, 1);
            if (!t.liquid.empty()) {
                Prop("Up to"); ImGui::SliderInt("##glv", &t.liquidLevel, -60, 60, "y %d");
                Prop("Frozen over with"); GenBlockCombo("##glt", t.liquidTop, 2);
            }
            if (t.mode == TerrainMode::Underground) Hint("Underground: the caverns below this height hold it.");
        }
        if (ImGui::CollapsingHeader("Caves", ImGuiTreeNodeFlags_DefaultOpen)) {
            Prop("Tunnels"); ImGui::SliderFloat("##gcv", &t.caves, 0, 1, "%.2f");
            Prop("Big caverns"); ImGui::SliderFloat("##gcn", &t.caverns, 0, 1, "%.2f");
            Prop("Openings"); ImGui::SliderFloat("##gco", &t.openings, 0, 1, "%.2f"); if (ImGui::IsItemHovered()) ImGui::SetTooltip("How often caves break out at the surface (cave mouths, holes).");
        }
        if (ImGui::CollapsingHeader("Spikes (pillars, crystals)")) {
            Prop("How many"); ImGui::SliderFloat("##gsp", &t.spikes, 0, 1, "%.2f");
            if (t.spikes > 0) { Prop("Height"); ImGui::SliderFloat("##gsh", &t.spikeHeight, 2, 40, "%.0f blocks"); Prop("Made of"); GenBlockCombo("##gsb", t.spikeBlock, 0); }
        }
        if (ImGui::CollapsingHeader("The area's size", ImGuiTreeNodeFlags_DefaultOpen)) {
            Prop("Size"); ImGui::SliderInt("##gsz", &t.size, 0, 2000, t.size <= 0 ? "endless" : "%d blocks from the middle");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("An area like Prime's: the land ends this far from the middle. 0: it goes on for ever.");
            if (t.size > 0) { Prop("Its edge"); int e = (int)t.edge; if (ImGui::Combo("##gedge", &e, "Cliffs all round\0A drop into the void\0Open sea\0")) t.edge = (TerrainEdge)e; }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("genright", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.6f));
        {
            float avail = ImGui::GetContentRegionAvail().x, side = std::min(avail, (ImGui::GetContentRegionAvail().y - 150 * ui));
            side = std::max(120.f, side);
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            if (genMapSRV) ImGui::Image((ImTextureID)(intptr_t)genMapSRV.Get(), ImVec2(side, side)); else ImGui::Dummy(ImVec2(side, side));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 c(p0.x + side * 0.5f, p0.y + side * 0.5f);
            dl->AddLine(ImVec2(p0.x, c.y), ImVec2(p0.x + side, c.y), IM_COL32(255, 255, 255, 50));   // (where the cut is)
            dl->AddCircle(c, 5 * ui, IM_COL32(255, 230, 120, 230), 12, 2);
            if (genBusy) dl->AddText(ImVec2(p0.x + 8, p0.y + 6), IM_COL32(255, 255, 255, 200), "making the preview...");
            { char b[96]; snprintf(b, sizeof b, "%.0f x %.0f blocks", GEN_W * genZoom, GEN_H * genZoom); dl->AddText(ImVec2(p0.x + 8, p0.y + side - 22 * ui), IM_COL32(255, 255, 255, 200), b); }
            if (t.size > 0) { float r = (float)(t.size / genZoom * side / GEN_W); dl->AddCircle(c, r, IM_COL32(255, 120, 90, 160), 64, 1.5f); }
            if (genCutSRV) ImGui::Image((ImTextureID)(intptr_t)genCutSRV.Get(), ImVec2(side, side * GEN_CH / GEN_CW));
            ImGui::TextDisabled("A cut through the middle, from the side (128 blocks high)");
            ImGui::SetNextItemWidth(side); ImGui::SliderFloat("##gzoom", &genZoom, 0.5f, 8, "%.1f blocks a pixel", ImGuiSliderFlags_Logarithmic);
            std::vector<std::string> miss = GenMissingBlocks(t);
            if (!miss.empty()) { std::string s; for (auto& m : miss) s += (s.empty() ? "" : ", ") + m; Hint("New blocks this world brings (with their own textures): %s. The preview shows them as stone until then.", s.c_str());
                                 if (ImGui::SmallButton("Add them now")) GenAddBlocks(t); }
        }
        ImGui::EndChild();
        if (genForNew) {
            if (ImGui::Button("Create the world", ImVec2(180 * ui, 0)) && !genNewName.empty()) { std::string n = genNewName; TerrainSettings s = t; pending = [this, n, s] { CreateGeneratedWorld(n, s); }; genEdOpen = false; }
        } else {
            if (ImGui::Button("Make the land again...", ImVec2(200 * ui, 0))) ImGui::OpenPopup("genregen");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("This world's land is made again with these settings.");
            if (ImGui::BeginPopup("genregen")) {
                ImGui::TextUnformatted("Make this world's land again?");
                ImGui::TextDisabled("Every block in it is made new: what you built with blocks is gone.\nIts objects (doors, lights, enemies, the ship ...) stay where they are.");
                if (ImGui::Button("Make it again")) { TerrainSettings s = t; pending = [this, s] { RegenerateWorld(s); }; genEdOpen = false; ImGui::CloseCurrentPopup(); }
                ImGui::SameLine(); if (ImGui::Button("Keep it")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        ImGui::SameLine(); if (ImGui::Button("Close")) genEdOpen = false;
        ImGui::End();
    }
