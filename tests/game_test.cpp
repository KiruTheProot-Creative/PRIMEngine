// Game logic tests: creative player, undo/redo, world info and settings files.
#include "../src/game/game.h"
#include "../src/game/project.h"
#include "../src/game/brush.h"
#include "../src/game/objects.h"
#include "../src/game/meshmodel.h"
#include "../src/core/json.h"
#include "../src/game/events.h"
#include <map>
#include <set>
#include <filesystem>
#include <cstring>
#include <functional>
#include <fstream>
#include "../src/game/weather.h"
#include <cstdio>
#include <filesystem>
using namespace mc;
int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)

int main() {
    BlockRegistry reg; registerBuiltinBlocks(reg);
    World w(reg); WorldInfo info; info.applyGenerator(w.gen, reg); w.stream({ 0, 0, 0 }, 4, 2, 100000);
    Player p; p.feet = { 0.5, 5, 0.5 }; p.flying = false; Controls c;
    for (int i = 0; i < 120; i++) p.update(w, c, 1 / 60.0);
    CHECK(p.onGround && std::fabs(p.feet.y) < 1e-3, "walking mode: falls onto the grass (y=%.3f)", p.feet.y);
    c.forward = true; for (int i = 0; i < 60; i++) p.update(w, c, 1 / 60.0);
    CHECK(std::fabs(p.feet.z - 0.5 - 4.32) < 0.05, "walks 4.3 blocks per second toward +Z (z=%.2f)", p.feet.z);
    c.forward = false; p.toggleFly(); c.jump = true; for (int i = 0; i < 60; i++) p.update(w, c, 1 / 60.0);
    CHECK(p.flying && p.feet.y > 5, "flying: Space rises (y=%.2f)", p.feet.y);
    c.jump = false; c.sneak = true; for (int i = 0; i < 120; i++) p.update(w, c, 1 / 60.0);
    CHECK(!p.flying && std::fabs(p.feet.y) < 1e-3, "flying down onto the ground lands and stops flying");
    p.yaw = 1.5707963; V3 f = p.forward(); CHECK(f.x > 0.99, "yaw 90 degrees looks toward +X");

    EditHistory h;
    BlockId stone = reg.find("stone"), grass = reg.find("grass");
    std::vector<Edit> g1{ { 1, 0, 1, AIR, stone }, { 2, 0, 1, AIR, stone } }; for (auto& e : g1) w.set(e.x, e.y, e.z, e.after); h.push(std::move(g1));
    std::vector<Edit> g2{ { 1, -1, 1, grass, AIR } }; w.set(1, -1, 1, AIR); h.push(std::move(g2));
    h.undo(w); CHECK(w.get(1, -1, 1) == grass && w.get(1, 0, 1) == stone, "undo brings back the broken block");
    h.undo(w); CHECK(w.get(1, 0, 1) == AIR && w.get(2, 0, 1) == AIR, "undo removes both placed blocks (one click = one step)");
    h.redo(w); CHECK(w.get(2, 0, 1) == stone, "redo places them again");

    std::string dir = "/tmp/mc_info_test"; std::filesystem::remove_all(dir);
    WorldInfo a; a.name = "My big build"; a.genKind = Generator::Void; a.player.feet = { 123456.25, -40.5, -98765.75 }; a.player.yaw = 2.5; a.hotbar[0] = "glass"; a.hotbar[4] = "bricks"; a.hotSel = 4; a.save(dir);
    WorldInfo b; b.load(dir);
    CHECK(b.name == "My big build" && b.genKind == Generator::Void && b.player.feet.x == 123456.25 && b.player.feet.z == -98765.75 && b.hotbar[4] == "bricks" && b.hotbar[1].empty() && b.hotSel == 4,
          "world info (name, generator, exact player position, hotbar) survives saving");
    Settings s; s.renderDistance = 24; s.fov = 90; s.save(dir + "/s.txt"); Settings s2; s2.load(dir + "/s.txt");
    CHECK(s2.renderDistance == 24 && s2.fov == 90, "settings survive saving");
    // weather
    {
        CHECK(weatherTypes().size() >= 10 && findWeather("intense_storm") >= 0 && findWeather("blizzard") >= 0, "%zu kinds of weather", weatherTypes().size());
        Weather w; w.set(findWeather("clear"), true);
        CHECK(w.now.rain == 0 && w.now.cover < 0.3f, "clear weather: no rain, few clouds");
        w.set(findWeather("rain"), false);
        w.update(1, { 0, 0, 0 }); bool fading = w.now.rain > 0.05f && w.now.rain < 0.3f;
        for (int i = 0; i < 60 * 30; i++) w.update(1.0 / 60, { 0, 0, 0 });
        CHECK(fading && w.now.rain > 0.65f, "rain fades in over about 20 seconds (%.2f after 1 s)", 0.0);
        for (int i = 0; i < 60 * 30; i++) w.update(1.0 / 60, { 0, 0, 0 });
        CHECK(w.wetness > 0.95f, "surfaces get wet in the rain (%.2f)", w.wetness);
        w.set(findWeather("intense_storm"), true); w.seed(7); int s0 = w.strikes;
        for (int i = 0; i < 60 * 120; i++) w.update(1.0 / 60, { 100, 5, -40 });
        int n = w.strikes - s0;
        CHECK(n > 15 && n < 45, "intense storm: %d lightning strikes in 2 minutes", n);
        V3 d = w.boltPos - V3{ 100, 5, -40 }; double dist = std::sqrt(d.x * d.x + d.z * d.z);
        CHECK(dist >= 39 && dist <= 261, "lightning strikes 40-260 blocks away (%.0f)", dist);
        w.set(findWeather("snow"), true); w.wetness = 1;
        for (int i = 0; i < 60 * 120; i++) w.update(1.0 / 60, { 0, 0, 0 });
        CHECK(w.snowCover > 0.85f && w.wetness < 0.1f, "snow builds up (%.2f), wetness dries (%.2f)", w.snowCover, w.wetness);
        Weather a; a.autoChange = true; a.seed(3); int start = a.target, changes = 0, last = a.target;
        for (int i = 0; i < 60 * 60; i++) { a.update(1, { 0, 0, 0 }); if (a.target != last) { changes++; last = a.target; } }
        CHECK(changes >= 3, "changing by itself: %d changes in an hour (started as %d)", changes, start);
        Weather b; for (int i = 0; i < 60 * 30; i++) b.update(1, { 0, 0, 0 });
        CHECK(b.target == 1, "without 'changes by itself' it stays");
        WorldInfo wi; wi.weather = "blizzard"; wi.autoWeather = true; wi.hotbar[0] = "Red Rock"; std::string dir = "/tmp/prim_weather_world"; wi.save(dir);
        WorldInfo wj; wj.load(dir); CHECK(wj.weather == "blizzard" && wj.autoWeather && wj.hotbar[0] == "Red Rock", "weather and hotbar names with spaces are saved with the world");
    }

    // environments
    {
        CHECK(envTypes().size() >= 7 && findEnv("space") >= 0 && findEnv("volcanic") >= 0, "%zu environments", envTypes().size());
        const EnvLook& sp = envTypes()[findEnv("space")].look;
        CHECK(sp.atmosphere == 0 && sp.planet == 1 && sp.dayTop[2] == 0, "space: no air, a planet, a black sky");
        CHECK(envTypes()[findEnv("arctic")].look.cold == 1 && envTypes()[findEnv("volcanic")].look.particleKind == 3, "arctic is cold, volcanic has embers");
        std::vector<EnvZone> zones = { { "big desert", "desert", { -100, -50, -100 }, { 100, 50, 100 } }, { "station", "space", { 10, 0, 10 }, { 19, 9, 19 } } };
        EnvState st; st.update(0, { 0, 0, 500 }, "temperate", zones, true);
        CHECK(st.active == findEnv("temperate") && !st.inZone, "outside the zones: the world's environment");
        st.update(0, { 0, 0, 0 }, "temperate", zones, true);
        CHECK(st.active == findEnv("desert") && st.inZone && st.zoneName == "big desert", "inside a zone: its environment");
        st.update(0, { 15.5, 3, 19.9 }, "temperate", zones, true);
        CHECK(st.active == findEnv("space"), "a small zone inside a big one wins (station)");
        st.update(0, { 15.5, 3, 20.1 }, "temperate", zones, true);
        CHECK(st.active == findEnv("desert"), "zone edges: both corner blocks are inside, the next block is not");
        EnvState fade; fade.update(0, { 0, 0, 500 }, "temperate", {}, true);
        fade.update(1, { 15, 3, 15 }, "temperate", zones); float mid = fade.now.atmosphere;
        for (int k = 0; k < 600; k++) fade.update(1.0 / 60, { 15, 3, 15 }, "temperate", zones);
        CHECK(mid > 0.2f && mid < 0.8f && fade.now.atmosphere < 0.01f, "walking into space fades over a few seconds (air %.2f after 1 s)", mid);
        WorldInfo wi; wi.environment = "martian"; wi.zones = zones; wi.save("/tmp/prim_env_world");
        WorldInfo wj; wj.load("/tmp/prim_env_world");
        CHECK(wj.environment == "martian" && wj.zones.size() == 2 && wj.zones[1].name == "station" && wj.zones[0].lo.x == -100 && wj.zones[1].hi.z == 19, "environment and zones are saved with the world");
    }

    // game maker: game settings, player rules, spawn point
    {
        GameSettings g; g.name = "Space Pirates"; g.startWorld = "Level 1"; g.player.jumpHeight = 3; g.player.maxHealth = 99; g.player.canSprint = false;
        g.save("/tmp/prim_game.txt"); GameSettings h; h.load("/tmp/prim_game.txt");
        CHECK(h.name == "Space Pirates" && h.startWorld == "Level 1" && h.player.jumpHeight == 3 && h.player.maxHealth == 99 && !h.player.canSprint, "game settings are saved");
        BlockRegistry r; registerBuiltinBlocks(r); World w(r); w.gen.layers = { { r.find("stone"), 3 } }; w.stream({ 0, 0, 0 }, 2, 2, 100000);
        PlayerRules hi; hi.jumpHeight = 3;
        Player p; p.flying = false; p.feet = { 0.5, 0, 0.5 }; p.onGround = true; Controls j; j.jump = true; double top = 0;
        for (int k = 0; k < 90; k++) { p.update(w, j, 1.0 / 60, hi); top = std::max(top, p.feet.y); j.jump = k < 2; }
        CHECK(top > 2.8 && top < 3.2, "jump height follows the rules (%.2f blocks)", top);
        WorldInfo wi; wi.hasSpawn = true; wi.spawn = { 3.5, 7, -2.5 }; wi.spawnYaw = 1.5; wi.save("/tmp/prim_spawn_world");
        WorldInfo wj; wj.load("/tmp/prim_spawn_world");
        CHECK(wj.hasSpawn && wj.spawn.y == 7 && wj.spawnYaw == 1.5, "the spawn point is saved with the world");
        CHECK(r.get(r.find("grass")).category == "Nature" && r.get(r.find("water")).category == "Liquids" && r.get(r.find("lamp")).category == "Lights", "blocks have categories");

        // play mode: lava hurts, falling out of the world kills, you come back at the spawn point
        PlayerRules pr; PlayState ps; Player q; V3 sp{ 0.5, 3, 0.5 };
        ps.start(q, pr, sp, 0.7);
        CHECK(ps.health == pr.maxHealth && !q.flying && q.feet.y == 3 && q.yaw == 0.7, "play starts at the spawn point with full health");
        w.set(8, 3, 8, r.liquidBlock(2));
        q.feet = { 8.5, 3, 8.5 }; q.vel = { 0, 0, 0 }; q.onGround = true;
        ps.tick(w, q, pr, sp, 0, 1.0);
        CHECK(ps.health > 50 && ps.health < 70, "lava hurts (%.0f health after 1 s)", ps.health);
        int ev = 0; for (int k = 0; k < 120 && ev != PlayState::Died; k++) ev = ps.tick(w, q, pr, sp, 0, 1.0 / 60);
        CHECK(ev == PlayState::Died && ps.dead() && ps.deaths == 1, "staying in lava kills");
        for (int k = 0; k < 200 && ev != PlayState::Respawned; k++) ev = ps.tick(w, q, pr, sp, 0, 1.0 / 60);
        CHECK(ev == PlayState::Respawned && ps.health == pr.maxHealth && q.feet.x == 0.5, "after dying you come back at the spawn point");
        q.feet = { 0.5, -70, 0.5 }; ev = ps.tick(w, q, pr, sp, 0, 1.0 / 60);
        CHECK(ev == PlayState::Died && std::string(ps.cause) == "fell out of the world", "falling out of the world kills");
        PlayerRules fr; fr.fallDamage = 5; PlayState fs; Player f; fs.start(f, fr, { 0.5, 20, 0.5 }, 0);
        for (int k = 0; k < 300; k++) { f.update(w, Controls{}, 1.0 / 60, fr); fs.tick(w, f, fr, { 0.5, 20, 0.5 }, 0, 1.0 / 60); }
        CHECK(fs.health < fr.maxHealth && fs.health > 0 && !fs.dead(), "a long fall hurts when fall damage is on (%.0f health)", fs.health);
    }

    // game projects
    {
        namespace fs = std::filesystem; std::error_code ec;
        std::string ex = "/tmp/prim_proj_exe", root = ex + "/projects"; fs::remove_all(ex, ec);
        fs::create_directories(ex + "/game/textures"); fs::create_directories(ex + "/worlds/Old World");
        { std::ofstream(ex + "/game/blocks.txt") << "next_id = 1\n"; std::ofstream(ex + "/game/textures/rock.png") << "x"; }
        WorldInfo ow; ow.name = "Old World"; ow.save(ex + "/worlds/Old World");
        std::string first = Projects::migrateOld(ex, root);
        CHECK(!first.empty() && fs::exists(first + "/blocks.txt") && fs::exists(first + "/textures/rock.png") && fs::exists(first + "/worlds/Old World/world.txt") && !fs::exists(ex + "/worlds"),
              "old blocks, textures and worlds move into a first project");
        CHECK(Projects::migrateOld(ex, root).empty(), "moving happens only once");
        std::string sp = Projects::create(root, "Space: Pirates!"), sp2 = Projects::create(root, "Space: Pirates!");
        auto L = Projects::list(root);
        CHECK(L.size() == 3 && fs::path(sp).filename() == "Space_ Pirates_" && fs::path(sp2).filename() == "Space_ Pirates_ (2)", "projects get safe, unique folders (%d projects)", (int)L.size());
        bool named = false; for (auto& p : L) named |= p.name == "Space: Pirates!" && p.worlds == 0;
        CHECK(named && Projects::countWorlds(first) == 1, "projects list their names and worlds");
        GameSettings g; g.lastWorld = "Level 2"; g.save(sp + "/game.txt"); GameSettings h; h.load(sp + "/game.txt");
        CHECK(h.lastWorld == "Level 2", "the last open world is remembered");
        CHECK(Projects::remove(sp2) && !fs::exists(sp2) && !Projects::remove(ex + "/nothing"), "deleting a project");
        CHECK(Projects::ago(1000, 1000 + 3 * 86400 + 5) == "3 days ago" && Projects::ago(1000, 1500) == "today", "how long ago");
    }

    // terrain brushes
    {
        BlockRegistry r; registerBuiltinBlocks(r); World w(r); w.gen.layers = { { r.find("stone"), 3 }, { r.find("dirt"), 2 }, { r.find("grass"), 1 } };
        w.stream({ 0, 0, 0 }, 3, 2, 100000);
        BlockId stone = r.find("stone"), grass = r.find("grass"), sand = r.find("sand");
        auto top = [&](int x, int z) { for (int y = 40; y > -40; y--) if (w.get(x, y, z) != AIR) return y; return -99; };
        int g = top(0, 0);
        BrushSettings b; int n = 0;
        for (int dy = -4; dy <= 4; dy++) for (int dz = -4; dz <= 4; dz++) for (int dx = -4; dx <= 4; dx++) n += insideBrush(b, dx, dy, dz);
        CHECK(n > 200 && n < 400 && insideBrush(b, 0, 4, 0) && !insideBrush(b, 4, 4, 0), "sphere brush of size 4 (%d blocks)", n);
        b.shape = BrushShape::Dome; CHECK(!insideBrush(b, 0, -1, 0) && insideBrush(b, 0, 3, 0), "dome is the top half");
        b.shape = BrushShape::Cylinder; b.height = 1; CHECK(insideBrush(b, 4, 1, 0) && !insideBrush(b, 0, 2, 0), "cylinder has its own height");
        for (int s = 0; s < (int)BrushShape::COUNT; s++) { b.shape = (BrushShape)s; CHECK(!brushOutline(b).empty() && insideBrush(b, 0, 0, 0), "%s brush has an outline and a middle", shapeInfo(b.shape).name); }

        BrushStroke st; b = BrushSettings{}; b.op = BrushOp::Add; b.radius = 3;
        st.apply(w, brushEdits(w, b, { 0, g + 5, 0 }, stone, 0)); st.apply(w, brushEdits(w, b, { 0, g + 5, 0 }, stone, 0));
        CHECK(w.get(0, g + 5, 0) == stone && w.get(0, g + 8, 0) == stone && w.get(0, g + 9, 0) == AIR, "add fills a ball of stone");
        auto stroke = st.finish(); EditHistory hist; size_t ballN = stroke.size(); hist.push(std::move(stroke));
        CHECK(ballN > 50, "one stroke is one undo step (%zu blocks)", ballN);
        hist.undo(w); CHECK(w.get(0, g + 5, 0) == AIR, "undo removes the whole stroke");

        b.op = BrushOp::Remove; b.radius = 2; st.apply(w, brushEdits(w, b, { 10, g, 10 }, AIR, 0));
        CHECK(w.get(10, g, 10) == AIR && w.get(10, g - 2, 10) == AIR && w.get(10, g - 3, 10) != AIR, "remove digs a hole");
        b.op = BrushOp::Fill; b.radius = 3; st.apply(w, brushEdits(w, b, { 10, g, 10 }, sand, g));
        CHECK(w.get(10, g, 10) == sand && w.get(10, g + 1, 10) == AIR, "fill fills the hole up to the clicked height");

        b = BrushSettings{}; b.op = BrushOp::Raise; b.radius = 4; b.strength = 1;
        for (int k = 0; k < 3; k++) st.apply(w, brushEdits(w, b, { -10, g, -10 }, AIR, 0));
        int hill = top(-10, -10), edge = top(-14, -10);
        CHECK(hill >= g + 6 && edge < hill && w.get(-10, hill, -10) == grass, "raise makes a hill with grass on top (%d high)", hill - g);
        b.op = BrushOp::Smooth; b.radius = 5; for (int k = 0; k < 3; k++) st.apply(w, brushEdits(w, b, { -10, hill, -10 }, AIR, 0));
        CHECK(top(-10, -10) <= hill && top(-10, -10) > g, "smooth rounds the hill off (%d -> %d)", hill - g, top(-10, -10) - g);
        b.op = BrushOp::Lower; b.radius = 3; st.apply(w, brushEdits(w, b, { 12, g, -12 }, AIR, 0));
        CHECK(top(12, -12) < g && w.get(12, top(12, -12), -12) == grass, "lower digs a dip and keeps the grass on top");
        b.op = BrushOp::Flatten; b.shape = BrushShape::Cylinder; b.radius = 6; b.height = 4;
        st.apply(w, brushEdits(w, b, { -10, g, -10 }, grass, g));
        CHECK(top(-10, -10) == g && top(-12, -9) == g, "flatten cuts the hill back to the ground");
        b = BrushSettings{}; b.op = BrushOp::Paint; b.radius = 3; st.apply(w, brushEdits(w, b, { 0, g, 0 }, sand, 0));
        CHECK(w.get(0, g, 0) == sand && w.get(0, g - 1, 0) != sand, "paint only changes the surface");
        b.op = BrushOp::Replace; b.replaceFrom = r.find("dirt"); st.apply(w, brushEdits(w, b, { 0, g - 1, 0 }, stone, 0));
        CHECK(w.get(0, g - 1, 0) == stone && w.get(0, g, 0) == sand, "replace swaps only the chosen block");
        b = BrushSettings{}; b.op = BrushOp::Roughen; b.radius = 5; b.strength = 1;
        auto rough = brushEdits(w, b, { 20, g, 0 }, AIR, 0, 7);
        CHECK(!rough.empty(), "roughen changes the surface (%zu blocks)", rough.size());
        CHECK(opposite(BrushOp::Raise) == BrushOp::Lower && opposite(BrushOp::Add) == BrushOp::Remove, "right click does the opposite");
    }

    // objects (G2)
    {
        BlockRegistry r; registerBuiltinBlocks(r); World w(r); w.gen.layers = { { r.find("stone"), 3 } };
        w.stream({ 0, 0, 0 }, 2, 2, 100000);
        BlockId stone = r.find("stone"), planks = r.find("planks");
        // a 3 x 3 x 3 cube of stone with a plank on top
        auto fe = fillEdits(w, { 10, 0, 10 }, { 12, 2, 12 }, stone); for (auto& e : fe) w.set(e.x, e.y, e.z, e.after);
        w.set(11, 3, 11, planks);
        CHECK(fe.size() == 27, "fill sets the whole box (%zu)", fe.size());
        BlockModel m = modelFromWorld(w, { 8, 0, 8 }, { 14, 6, 14 }, "Crate");
        CHECK(m.sx == 3 && m.sy == 4 && m.sz == 3 && m.blockCount() == 28 && m.at(1, 3, 1) == planks, "make into object keeps just the blocks (%d x %d x %d)", m.sx, m.sy, m.sz);
        CHECK(m.pivot.x == 1.5 && m.pivot.y == 0 && m.pivot.z == 1.5, "the model stands on its bottom middle");
        auto mb = modelBoxes(m, r);
        CHECK(mb.size() == 2, "collision from the blocks is merged into few boxes (%zu)", mb.size());
        std::string mp = "/tmp/prim_model.txt"; m.save(mp); BlockModel m2; m2.load(mp);
        CHECK(m2.name == "Crate" && m2.sx == 3 && m2.cells == m.cells, "models save and load");
        ModelLibrary lib; std::error_code ec; std::filesystem::remove_all("/tmp/prim_models", ec); lib.loadAll("/tmp/prim_models"); lib.store(m);
        ModelLibrary lib2; lib2.loadAll("/tmp/prim_models");
        CHECK(lib2.find("Crate") && lib2.uniqueName("Crate") == "Crate 2", "the model library");

        WorldObject o; o.id = 1; o.name = "Crate 1"; o.model = "Crate"; o.pos = { 0.5, 3, -5.5 }; o.yaw = 90; o.scale = 2;
        Box3 bb = objectBounds(o, m);
        CHECK(std::fabs(bb.hi.y - bb.lo.y - 8) < 1e-6 && std::fabs(bb.hi.x - bb.lo.x - 6) < 1e-6 && std::fabs((bb.lo.x + bb.hi.x) * 0.5 - 0.5) < 1e-6, "objects turn and grow around their bottom middle");
        w.objectSolids = objectBoxes(o, m, mb);
        CHECK(!w.boxFree({ 0.5, 4, -5.5 }, 0.3, 1.8) && w.boxFree({ 0.5, 12, -5.5 }, 0.3, 1.8), "objects are solid");
        Player pl; pl.feet = { 0.5, 20, -5.5 }; pl.flying = false;
        for (int k = 0; k < 240; k++) pl.update(w, Controls{}, 1.0 / 60);
        CHECK(std::fabs(pl.feet.y - 11) < 0.05, "you can stand on an object (feet at %.2f)", pl.feet.y);
        o.collide = Collide::None; w.objectSolids = objectBoxes(o, m, mb);
        CHECK(w.boxFree({ 0.5, 4, -5.5 }, 0.3, 1.8), "collision none: walk through it");
        double t; CHECK(rayBox({ 0.5, 6, -20 }, { 0, 0, 1 }, bb, t) && std::fabs(t - (bb.lo.z + 20)) < 1e-6, "clicking hits an object");
        std::vector<WorldObject> os{ o }, os2; o.name = "Big crate"; o.id = 2; os.push_back(o);
        saveObjects("/tmp/prim_objs.txt", os); loadObjects("/tmp/prim_objs.txt", os2);
        CHECK(os2.size() == 2 && os2[1].name == "Big crate" && os2[0].yaw == 90 && os2[0].collide == Collide::None, "objects save and load");
        auto pe = pasteEdits(w, m, { 20, 0, 20 });
        CHECK(pe.size() == 28, "paste puts every block of the copy (%zu)", pe.size());
    }

    // imported models (G2 attempt 2)
    {
        Json j; CHECK(parseJson("{\"a\":[1,2.5,{\"b\":\"x\\u00e9\"}],\"t\":true,\"n\":null}", j) && j["a"][1].num() == 2.5 && j["a"][2]["b"].str() == "x\xc3\xa9" && j["t"].b && j["n"].isNull(), "json reading");
        CHECK(base64Decode(base64Encode({ 1, 2, 3, 250, 0 })) == std::vector<uint8_t>({ 1, 2, 3, 250, 0 }), "base64 there and back");
        std::error_code ec; std::filesystem::remove_all("/tmp/prim_demo_models", ec);
        auto files = writeDemoModels("/tmp/prim_demo_models");
        CHECK(files.size() == 3, "example models written");
        auto outwards = [](const MeshModel& m) {   // every triangle faces away from the middle (true for these convex shapes)
            int bad = 0; V3 c{ m.sx * 0.5, m.sy * 0.5, m.sz * 0.5 };
            for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
                V3 a = m.blockPos(m.raw[m.idx[t]]), b = m.blockPos(m.raw[m.idx[t + 1]]), d = m.blockPos(m.raw[m.idx[t + 2]]), e1 = b - a, e2 = d - a;
                V3 n{ e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x }; V3 mid = (a + b + d) * (1.0 / 3) - c;
                if (n.x * mid.x + n.y * mid.y + n.z * mid.z < 0) bad++;
            }
            return bad;
        };
        std::string err; MeshModel lamp, rock, crys;
        bool ok = importModel(files[0], lamp, err);
        CHECK(ok && lamp.triangles() == 48 && std::fabs(lamp.sy - 30.0 / 16) < 0.05 && lamp.tw == 16, "Blockbench lamp post: 4 cubes, %.2f blocks tall (%s)", lamp.sy, err.c_str());
        ok = importModel(files[1], rock, err);
        CHECK(ok && rock.triangles() == 80 && rock.sx > 1.6 && rock.sx < 2.4 && rock.tw == 32 && outwards(rock) == 0, "obj rock: 80 triangles, all facing out, %.2f wide (%s)", rock.sx, err.c_str());
        ok = importModel(files[2], crys, err);
        CHECK(ok && crys.triangles() == 8 && std::fabs(crys.sy - 2.5) < 0.01 && std::fabs(crys.sx - 0.7071) < 0.02 && outwards(crys) == 0, "glTF crystal: turned and stretched by its node (%.2f x %.2f, %s)", crys.sx, crys.sy, err.c_str());
        CHECK(crys.tex.size() == 4 && crys.tex[2] > 200 && crys.tex[1] < 100, "glTF colour without a picture");
        auto rb = meshBoxes(rock);
        bool middle = false; for (auto& b : rb) middle |= b.lo.x <= rock.sx / 2 && b.hi.x >= rock.sx / 2 && b.lo.y <= rock.sy / 2 && b.hi.y >= rock.sy / 2 && b.lo.z <= rock.sz / 2 && b.hi.z >= rock.sz / 2;
        bool corner = false; for (auto& b : rb) corner |= b.lo.x < 0.05 && b.lo.y < 0.05 && b.lo.z < 0.05;
        CHECK(!rb.empty() && rb.size() < 400 && middle && !corner, "rock collision: solid inside, not in the empty corners (%zu boxes)", rb.size());
        rock.unitScale = 2; rock.update(); CHECK(rock.sx > 3.2, "import scale makes it bigger");
        rock.save("/tmp/prim_rock.pmesh"); MeshModel r2; CHECK(r2.load("/tmp/prim_rock.pmesh") && r2.idx == rock.idx && r2.unitScale == 2 && r2.tex == rock.tex, "imported models save and load");
        MeshModel bad; CHECK(!importModel("/tmp/nothing.obj", bad, err) && !err.empty(), "a missing file gives a message");
    }

    // FBX: the door from Blockbench (text FBX with two sliding parts), and a tiny binary FBX
    {
        std::string err; MeshModel door;
        bool ok = importModel(std::string(SOURCE_DIR) + "/assets/examples/DOOR_01.fbx", door, err);
        CHECK(ok, "the Blockbench door FBX imports (%s)", err.c_str());
        if (ok) {
            CHECK(door.tw == 64 && door.th == 64, "its picture comes from inside the file (%d x %d)", door.tw, door.th);
            CHECK(std::fabs(door.sy - 1.0) < 0.02 && std::fabs(door.sx - 1.0) < 0.02, "100 cm = 1 block (%.2f x %.2f x %.2f)", door.sx, door.sy, door.sz);
            CHECK(door.parts.size() == 2 && door.clips.size() == 2, "two moving parts and two animations (%zu parts, %zu clips)", door.parts.size(), door.clips.size());
            bool names = door.clips.size() == 2 && door.clips[0].name == "open" && door.clips[1].name == "Close";
            CHECK(names && std::fabs(door.clips[0].length - 0.25f) < 0.01f, "the animations are called open and Close, %.2f s long", door.clips.empty() ? 0.f : door.clips[0].length);
            int moving = 0, still = 0; for (uint16_t p : door.vpart) (p == 0xFFFF ? still : moving)++;
            CHECK(moving > 0 && still > 0 && door.vpart.size() == door.raw.size(), "the frame stays, the door halves move (%d / %d corners)", moving, still);
            int op = door.findClip("OPEN"), cl = door.findClip("close");
            auto shut = door.posed(cl, 1.0f), open = door.posed(op, door.clips[op].length);
            double maxMove = 0, xL = 1e9, xR = -1e9;
            for (size_t v = 0; v < shut.size(); v++) {
                double dx = open[v].p[0] - shut[v].p[0]; maxMove = std::max(maxMove, std::fabs(dx));
                if (door.vpart[v] != 0xFFFF) { xL = std::min(xL, (double)shut[v].p[0]); xR = std::max(xR, (double)shut[v].p[0]); }
            }
            CHECK(op == 0 && cl == 1 && std::fabs(maxMove - 31.25) < 0.01, "open slides the halves 31.25 cm apart (%.2f)", maxMove);
            CHECK(std::fabs(xL + 37.5) < 0.01 && std::fabs(xR - 37.5) < 0.01, "closed, the halves fill the doorway (%.2f .. %.2f)", xL, xR);
            door.save("/tmp/prim_door.pmesh"); MeshModel d2;
            CHECK(d2.load("/tmp/prim_door.pmesh") && d2.clips.size() == 2 && d2.clips[1].tracks.size() == door.clips[1].tracks.size() && d2.vpart == door.vpart && d2.parts[0].name == door.parts[0].name, "parts and animations save and load");
        }
        // doors: stretched to Width x Height, forcefield, open / close
        if (ok) {
            WorldObject d; d.kind = ObjKind::Door; d.model = "DOOR_01"; d.pos = { 0.5, 0, 0.5 }; d.door.w = 4; d.door.h = 2;
            Box3 bb = objectBounds(d, door.frame());
            CHECK(std::fabs(bb.hi.x - bb.lo.x - 4) < 1e-6 && std::fabs(bb.hi.y - bb.lo.y - 2) < 1e-6, "a door stretches to 4 x 2 blocks (%.2f x %.2f)", bb.hi.x - bb.lo.x, bb.hi.y - bb.lo.y);
            d.door.w = 1; d.door.h = 1; bb = objectBounds(d, door.frame()); CHECK(std::fabs(bb.hi.x - bb.lo.x - 1) < 1e-6, "or to 1 x 1 (morph ball)");
            d.door.w = 2; d.door.h = 3;
            Box3 op = doorOpening(door); Box3 fb = fieldBox(d, door.frame(), op);
            CHECK(std::fabs(fb.hi.x - fb.lo.x - 1.5) < 0.01 && std::fabs(fb.hi.y - fb.lo.y - 2.625) < 0.01 && fb.hi.z - fb.lo.z < 0.2, "the forcefield fills the opening (%.2f x %.2f)", fb.hi.x - fb.lo.x, fb.hi.y - fb.lo.y);
            auto still = meshBoxes(door, 24, true), all = meshBoxes(door);
            auto inside = [&](const std::vector<Box3>& bs) { V3 c{ door.sx * 0.5, door.sy * 0.4, door.sz * 0.5 }; for (auto& b : bs) if (c.x >= b.lo.x && c.x <= b.hi.x && c.y >= b.lo.y && c.y <= b.hi.y && c.z >= b.lo.z && c.z <= b.hi.z) return true; return false; };
            CHECK(!still.empty() && inside(all) && !inside(still), "collision without the moving halves leaves the doorway free (%zu / %zu boxes)", still.size(), all.size());
            WorldObject p2 = d; p2.kind = ObjKind::Prop; std::vector<WorldObject> v{ d }, v2; saveObjects("/tmp/prim_doors.txt", v); loadObjects("/tmp/prim_doors.txt", v2);
            CHECK(v2.size() == 1 && v2[0].kind == ObjKind::Door && v2[0].door.w == 2 && v2[0].door.h == 3 && v2[0].door.color == d.door.color, "doors save and load");
        }
        {
            DoorRuntime r; float L = 0.25f;
            CHECK(r.blocks() && r.field == 1, "a door starts closed with its field up");
            CHECK(r.shoot(0.3f, 0.6f) && r.state == DoorRuntime::Dissolving, "a shot starts the dissolve");
            for (int k = 0; k < 12; k++) r.tick(1.f / 30, false, L, L, 3);
            CHECK(r.state == DoorRuntime::Opening && r.field == 0 && r.clip == 0, "then it plays open");
            for (int k = 0; k < 10; k++) r.tick(1.f / 30, false, L, L, 3);
            CHECK(r.state == DoorRuntime::Open && !r.blocks(), "then it is open and you can walk through");
            for (int k = 0; k < 200; k++) r.tick(1.f / 30, true, L, L, 3);
            CHECK(r.state == DoorRuntime::Open, "it stays open while you stand in it");
            for (int k = 0; k < 100; k++) r.tick(1.f / 30, false, L, L, 3);
            CHECK(r.state == DoorRuntime::Closing || r.state == DoorRuntime::Restoring || r.state == DoorRuntime::Closed, "then it closes");
            for (int k = 0; k < 40; k++) r.tick(1.f / 30, false, L, L, 3);
            CHECK(r.state == DoorRuntime::Closed && r.field == 1 && r.blocks(), "and the field comes back");
        }
        // a binary FBX by hand: one triangle; its corner list packed with zlib (stored blocks)
        std::vector<uint8_t> f; auto u32 = [&](uint32_t v) { for (int k = 0; k < 4; k++) f.push_back((uint8_t)(v >> (8 * k))); };
        const char head[] = "Kaydara FBX Binary  "; f.insert(f.end(), head, head + 20); f.push_back(0); f.push_back(0x1a); f.push_back(0); u32(7400);
        auto zlibStored = [](const std::vector<uint8_t>& raw) { std::vector<uint8_t> z = { 0x78, 0x01, 1, (uint8_t)(raw.size() & 255), (uint8_t)(raw.size() >> 8), (uint8_t)(~raw.size() & 255), (uint8_t)((~raw.size() >> 8) & 255) };
            z.insert(z.end(), raw.begin(), raw.end()); uint32_t a = 1, b = 0; for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; } uint32_t ad = (b << 16) | a;
            for (int k = 3; k >= 0; k--) z.push_back((uint8_t)(ad >> (8 * k))); return z; };
        struct BN { std::string name; std::vector<std::vector<uint8_t>> props; std::vector<BN> kids; };
        auto pL = [](int64_t v) { std::vector<uint8_t> p{ 'L' }; for (int k = 0; k < 8; k++) p.push_back((uint8_t)(v >> (8 * k))); return p; };
        auto pS = [](const std::string& s) { std::vector<uint8_t> p{ 'S' }; uint32_t n = (uint32_t)s.size(); for (int k = 0; k < 4; k++) p.push_back((uint8_t)(n >> (8 * k))); p.insert(p.end(), s.begin(), s.end()); return p; };
        auto pD = [](const std::vector<double>& a) { std::vector<uint8_t> p{ 'd' }; uint32_t n = (uint32_t)a.size(), len = n * 8; for (uint32_t v : { n, 0u, len }) for (int k = 0; k < 4; k++) p.push_back((uint8_t)(v >> (8 * k)));
            const uint8_t* q = (const uint8_t*)a.data(); p.insert(p.end(), q, q + len); return p; };
        auto pIz = [&](const std::vector<int32_t>& a) { std::vector<uint8_t> raw((const uint8_t*)a.data(), (const uint8_t*)a.data() + a.size() * 4), z = zlibStored(raw); std::vector<uint8_t> p{ 'i' };
            for (uint32_t v : { (uint32_t)a.size(), 1u, (uint32_t)z.size() }) for (int k = 0; k < 4; k++) p.push_back((uint8_t)(v >> (8 * k))); p.insert(p.end(), z.begin(), z.end()); return p; };
        std::function<void(const BN&)> write = [&](const BN& n) {
            size_t start = f.size(); u32(0); u32((uint32_t)n.props.size()); u32(0); f.push_back((uint8_t)n.name.size()); f.insert(f.end(), n.name.begin(), n.name.end());
            size_t ps = f.size(); for (auto& p : n.props) f.insert(f.end(), p.begin(), p.end()); uint32_t plen = (uint32_t)(f.size() - ps);
            for (auto& k : n.kids) write(k); if (!n.kids.empty()) for (int k = 0; k < 13; k++) f.push_back(0);
            uint32_t end = (uint32_t)f.size(); std::memcpy(&f[start], &end, 4); std::memcpy(&f[start + 8], &plen, 4);
        };
        BN geo{ "Geometry", { pL(10), pS(std::string("tri\x00\x01Geometry", 14)), pS("Mesh") }, { { "Vertices", { pD({ 0, 0, 0, 2, 0, 0, 0, 3, 0 }) }, {} }, { "PolygonVertexIndex", { pIz({ 0, 1, -3 }) }, {} } } };
        BN model{ "Model", { pL(20), pS(std::string("tri\x00\x01Model", 11)), pS("Mesh") }, {} };
        BN objs{ "Objects", {}, { geo, model } };
        BN conn{ "Connections", {}, { { "C", { pS("OO"), pL(20), pL(0) }, {} }, { "C", { pS("OO"), pL(10), pL(20) }, {} } } };
        write(objs); write(conn); for (int k = 0; k < 13; k++) f.push_back(0);
        std::ofstream("/tmp/prim_tri.fbx", std::ios::binary).write((const char*)f.data(), (std::streamsize)f.size());
        MeshModel tri; ok = importModel("/tmp/prim_tri.fbx", tri, err);
        CHECK(ok && tri.triangles() == 1 && std::fabs(tri.sx - 0.02) < 1e-6 && std::fabs(tri.sy - 0.03) < 1e-6, "binary FBX with a packed list (%s, %.3f x %.3f)", err.c_str(), tri.sx, tri.sy);
    }

    // moving platforms and elevators
    {
        PathLine L({ { 0, 0, 0 }, { 4, 0, 0 }, { 4, 3, 0 } }, false);
        CHECK(std::fabs(L.length - 7) < 1e-9 && std::fabs(L.pos(5).y - 1) < 1e-9 && L.pointAt(4) == 1, "a path through points");
        PathLine Lp({ { 0, 0, 0 }, { 4, 0, 0 }, { 4, 0, 4 } }, true);
        CHECK(std::fabs(Lp.length - (8 + std::sqrt(32.0))) < 1e-9 && std::fabs(Lp.pos(Lp.length).x) < 1e-9, "a loop closes itself");
        PlatformSettings p; p.points = { { 0, 0, 0 }, { 6, 0, 0 } }; p.speed = 3; p.wait = 0.5f;
        PlatformRun run; run.reset(p);
        double maxX = 0; bool back = false;
        for (int k = 0; k < 400; k++) { run.tick(p, 1.0 / 60, {}, {}); double x = run.offsets(p)[0].x; maxX = std::max(maxX, x); if (maxX > 5.99 && x < 3) back = true; }
        CHECK(maxX > 5.99 && maxX < 6.01 && back, "back and forth between the ends (furthest %.2f)", maxX);
        p.trigger = PathTrigger::StandOn; p.points = { { 0, 0, 0 }, { 0, 8, 0 } }; run.reset(p);
        for (int k = 0; k < 60; k++) run.tick(p, 1.0 / 60, { false }, {});
        CHECK(run.offsets(p)[0].y == 0, "an elevator waits until you stand on it");
        run.tick(p, 1.0 / 60, { true }, {});
        for (int k = 0; k < 240; k++) run.tick(p, 1.0 / 60, { true }, {});
        CHECK(std::fabs(run.offsets(p)[0].y - 8) < 1e-6, "then it goes up and stops at the top");
        for (int k = 0; k < 120; k++) run.tick(p, 1.0 / 60, { false }, {});
        CHECK(std::fabs(run.offsets(p)[0].y - 8) < 1e-6, "and stays there (no come-back time)");
        p.comeBack = 1; for (int k = 0; k < 400; k++) run.tick(p, 1.0 / 60, { false }, {});
        CHECK(std::fabs(run.offsets(p)[0].y) < 1e-6, "with a come-back time it goes home by itself");
        p.trigger = PathTrigger::Always; p.mode = PathMode::Loop; p.points = { { 0, 0, 0 }, { 10, 0, 0 }, { 10, 0, 10 }, { 0, 0, 10 } }; p.copies = 4; run.reset(p);
        auto o = run.offsets(p);
        CHECK(o.size() == 4 && std::fabs(o[1].x - 10) < 1e-9 && std::fabs(o[2].z - 10) < 1e-9, "4 copies spread round a loop");
        p.spawner = true; p.every = 1; p.mode = PathMode::Once; p.points = { { 0, 0, 0 }, { 0, 0, 12 } }; run.reset(p);
        size_t most = 0; for (int k = 0; k < 600; k++) { run.tick(p, 1.0 / 60, {}, {}); most = std::max(most, run.movers.size()); }
        CHECK(most >= 4 && most <= 5 && run.movers.size() <= 5, "a spawner keeps sending new ones; old ones go at the end (%zu at most)", most);
        CHECK(standsOn({ { 0, 0, 0 }, { 2, 1, 2 } }, { 1, 1.02, 1 }, 0.3) && !standsOn({ { 0, 0, 0 }, { 2, 1, 2 } }, { 1, 1.5, 1 }, 0.3), "standing on a platform");
        WorldObject w; w.kind = ObjKind::Platform; w.platform = p; std::vector<WorldObject> v{ w }, v2; saveObjects("/tmp/prim_plat.txt", v); loadObjects("/tmp/prim_plat.txt", v2);
        CHECK(v2.size() == 1 && v2[0].kind == ObjKind::Platform && v2[0].platform.spawner && v2[0].platform.points.size() == 2 && v2[0].platform.points[1].z == 12, "platforms save and load");
    }

    // pickups, keys, teleporters, trigger zones
    {
        PlayInventory inv; float hp = 50; std::string said;
        WorldObject h; h.id = 1; h.kind = ObjKind::Pickup; h.pickup.type = PickupType::Health; h.pickup.amount = 25;
        CHECK(inv.collect(h, hp, 100, said) && hp == 75 && inv.has(1) && !inv.collect(h, hp, 100, said), "health pickups heal once (%s)", said.c_str());
        WorldObject h2 = h; h2.id = 2; float full = 100; CHECK(!inv.collect(h2, full, 100, said), "full health leaves it lying there");
        WorldObject k; k.id = 3; k.kind = ObjKind::Pickup; k.pickup.type = PickupType::Key; k.pickup.name = "Red key";
        CHECK(inv.collect(k, hp, 100, said) && inv.keys.count("Red key"), "keys go into your pocket");
        WorldObject g; g.kind = ObjKind::Pickup; g.pickup.type = PickupType::Collectible; g.pickup.name = "Gem"; g.pickup.respawn = 2;
        for (uint32_t id = 10; id < 13; id++) { g.id = id; inv.collect(g, hp, 100, said); }
        CHECK(inv.counts["Gem"] == 3 && said == "Gem 3", "collectibles are counted (%s)", said.c_str());
        inv.tick(2.5f); CHECK(!inv.has(10) && inv.has(1), "with a respawn time they come back, the others do not");
        WorldObject z; z.kind = ObjKind::Trigger; z.model = "@zone"; z.size = { 4, 3, 2 }; z.pos = { 0, 0, 0 }; z.trigger.action = TriggerAction::Kill; z.trigger.text = "Watch out!"; z.teleport.world = "Level 2"; z.teleport.target = "Arrival pad"; z.door.key = "Red key"; z.pickup.name = "Big gem";
        MeshModel zone; CHECK(builtinShape("@zone", zone) && std::fabs(zone.sx - 1) < 1e-3 && std::fabs(zone.sy - 1) < 1e-3, "the built-in zone shape is a 1-block cube");
        Box3 zb = objectBounds(z, zone.frame());
        CHECK(std::fabs(zb.hi.x - zb.lo.x - 4) < 1e-6 && std::fabs(zb.hi.y - 3) < 1e-6 && std::fabs(zb.hi.z - zb.lo.z - 2) < 1e-6, "a trigger zone is exactly its box (4 x 3 x 2)");
        std::vector<WorldObject> v{ z }, v2; saveObjects("/tmp/prim_helpers.txt", v); loadObjects("/tmp/prim_helpers.txt", v2);
        CHECK(v2.size() == 1 && v2[0].kind == ObjKind::Trigger && v2[0].size.x == 4 && v2[0].trigger.action == TriggerAction::Kill && v2[0].trigger.text == "Watch out!"
              && v2[0].teleport.world == "Level 2" && v2[0].teleport.target == "Arrival pad" && v2[0].door.key == "Red key" && v2[0].pickup.name == "Big gem", "helpers save and load (names with spaces too)");
        int shapes = 0; for (auto& n : builtinShapeNames()) { MeshModel m; shapes += builtinShape(n, m) && m.triangles() > 0; }
        CHECK(shapes == 8, "all built-in looks build (%d)", shapes);
    }

    // particles and lights
    {
        ParticleSettings fire = particlePreset("Fire"); ParticleEmitter e(fire, 7);
        for (int k = 0; k < 120; k++) e.update(1.0 / 60, { 0, 1, 0 }, 0, nullptr);
        CHECK(e.ps.size() > 25 && e.ps.size() < 60, "fire: about 45 a second, each lives about a second (%zu alive after 2 s)", e.ps.size());
        double avgY = 0; for (auto& p : e.ps) avgY += p.pos.y; avgY /= std::max<size_t>(1, e.ps.size());
        CHECK(avgY > 1.2, "fire rises (average height %.2f)", avgY);
        ParticleEmitter sp(particlePreset("Sparks"), 3);
        sp.update(1.0 / 60, { 0, 5, 0 }, 0, nullptr);
        CHECK(sp.ps.size() == 25, "sparks come in a burst of 25 (%zu)", sp.ps.size());
        for (int k = 0; k < 20; k++) sp.update(1.0 / 60, { 0, 5, 0 }, 0, nullptr, false);
        double vy = 0; for (auto& p : sp.ps) vy += p.vel.y; vy /= std::max<size_t>(1, sp.ps.size());
        CHECK(vy < 0, "sparks fall (gravity)");
        for (int k = 0; k < 120; k++) sp.update(1.0 / 60, { 0, 5, 0 }, 0, nullptr, false);
        CHECK(sp.ps.empty(), "and are gone when their life is over");
        BlockRegistry r; registerBuiltinBlocks(r); World w(r); w.gen.layers = { { r.find("stone"), 3 } }; w.stream({ 0, 0, 0 }, 2, 2, 100000);
        ParticleSettings drip = particlePreset("Drips"); drip.rate = 0; drip.burst = 10; ParticleEmitter d(drip, 5);
        for (int k = 0; k < 120; k++) d.update(1.0 / 60, { 0.5, 5, 0.5 }, 0, &w);
        CHECK(d.ps.empty(), "drips stop when they hit the ground");
        ParticleSettings b = particlePreset("Sparks"); b.collide = true; b.bounce = true; b.dir = { 0, -1, 0 }; b.spread = 0; b.burst = 5; b.gravity = -20; b.life = 3; b.lifeVar = 0;
        ParticleEmitter bb(b, 9); double lowest = 99;
        for (int k = 0; k < 120; k++) { bb.update(1.0 / 60, { 0.5, 3, 0.5 }, 0, &w, k == 0); for (auto& p : bb.ps) lowest = std::min(lowest, p.pos.y); }
        CHECK(lowest >= -0.01 && lowest < 1, "bouncing sparks do not go into the ground (lowest %.2f)", lowest);
        ParticleLook l0 = lookOf(e, e.ps.front());
        CHECK(l0.r > 0 && l0.a >= 0 && l0.a <= 1, "particles have a colour and see-through amount");
        CHECK(flickerAt(Flicker::None, 3, 1) == 1 && flickerAt(Flicker::Candle, 3, 1) < 1.01f && flickerAt(Flicker::Pulse, 0.5, 2) >= 0.09f, "light flicker");
        WorldObject o; o.kind = ObjKind::Particles; o.particles = particlePreset("Smoke"); o.light.type = LightType::Spot; o.light.cone = 20; o.light.flicker = Flicker::Broken;
        std::vector<WorldObject> v{ o }, v2; saveObjects("/tmp/prim_fx.txt", v); loadObjects("/tmp/prim_fx.txt", v2);
        CHECK(v2.size() == 1 && v2[0].kind == ObjKind::Particles && !v2[0].particles.additive && v2[0].particles.shape == PShape::Smoke && v2[0].particles.size1 == o.particles.size1
              && v2[0].light.type == LightType::Spot && v2[0].light.cone == 20 && v2[0].light.flicker == Flicker::Broken, "particles and lights save and load");
        CHECK(particlePresets().size() == 9, "nine presets");
    }

    // event cards (G3)
    {
        struct Host : EventHost {
            std::vector<std::string> log; std::set<std::string> keys; std::map<std::string, int> counts; float hp = 100;
            bool hasKey(const std::string& n) override { return keys.count(n) > 0; }
            int count(const std::string& n) override { return counts[n]; }
            float health() override { return hp; }
            void run(const EventDo& d) override { log.push_back(std::to_string((int)d.kind) + ":" + d.target + d.text); }
        } h;
        std::vector<EventCard> cards(1);
        cards[0].when = { When::EnterZone, "Hall" };
        cards[0].dos = { { Do::Message, "", "Hi" }, { Do::OpenDoor, "Gate" } };
        EventRunner run; run.start(cards, h);
        run.signal(cards, { When::EnterZone, "Cellar" }, h);
        CHECK(h.log.empty(), "a card waits for its own zone");
        run.signal(cards, { When::EnterZone, "Hall" }, h);
        CHECK(h.log.size() == 2 && h.log[0] == "0:Hi" && h.log[1] == "1:Gate", "entering the zone runs the actions in order");
        cards[0].ifs = { { If::HasKey, "Red" } }; h.log.clear();
        run.signal(cards, { When::EnterZone, "Hall" }, h);
        CHECK(h.log.empty(), "'only if' stops it without the key");
        h.keys.insert("Red"); run.signal(cards, { When::EnterZone, "Hall" }, h);
        CHECK(h.log.size() == 2, "and lets it run with the key");
        cards[0].once = true; h.log.clear(); run.signal(cards, { When::EnterZone, "Hall" }, h);
        CHECK(h.log.empty(), "'only once' cards do not run again");
        // wait
        std::vector<EventCard> c2(1); c2[0].when = { When::GameStarts };
        c2[0].dos = { { Do::Message, "", "A" }, { Do::Wait, "", "", 2 }, { Do::Message, "", "B" } };
        Host h2; EventRunner r2; r2.start(c2, h2);
        CHECK(h2.log.size() == 1, "'wait' holds back the actions after it");
        for (int k = 0; k < 60; k++) r2.tick(c2, 1.0 / 60, h2);
        CHECK(h2.log.size() == 1, "still waiting after 1 second");
        for (int k = 0; k < 70; k++) r2.tick(c2, 1.0 / 60, h2);
        CHECK(h2.log.size() == 2 && h2.log[1] == "0:B", "and they come after 2 seconds");
        // every, counters, flags
        std::vector<EventCard> c3(3);
        c3[0].when = { When::Every, "", 1 }; c3[0].dos = { { Do::Heal, "", "", 5 } };
        c3[1].when = { When::CountReaches, "Gem", 3 }; c3[1].dos = { { Do::FlagOn, "", "rich" } };
        c3[2].when = { When::Pickup, "" }; c3[2].ifs = { { If::FlagOn, "rich" } }; c3[2].dos = { { Do::Win, "", "Yay" } };
        Host h3; EventRunner r3; r3.start(c3, h3);
        for (int k = 0; k < 185; k++) r3.tick(c3, 1.0 / 60, h3);
        CHECK(h3.log.size() == 3, "'every 1 second' ran 3 times in 3 seconds (%zu)", h3.log.size());
        h3.log.clear(); r3.signal(c3, { When::Pickup, "Gem 1", "Gem" }, h3);
        CHECK(h3.log.empty(), "the flag is still off");
        h3.counts["Gem"] = 3; r3.tick(c3, 0.01, h3);
        CHECK(r3.flags.count("rich") && r3.firedCount(1) == 1, "counter reaching 3 turns the flag on");
        r3.tick(c3, 0.01, h3);
        CHECK(r3.firedCount(1) == 1, "and only once while it stays there");
        r3.signal(c3, { When::Pickup, "Gem 2", "Gem" }, h3);
        CHECK(!h3.log.empty() && h3.log.back() == "22:Yay", "with the flag on, picking up wins");
        // save / load
        c3[0].name = "Heal % over time"; c3[2].once = true; c3[2].dos[0].text = "You win!\nWell done";
        std::vector<EventCard> back; saveEvents("/tmp/prim_events.txt", c3); loadEvents("/tmp/prim_events.txt", back);
        CHECK(back.size() == 3 && back[0].name == "Heal % over time" && back[1].when.target == "Gem" && back[1].when.number == 3 && back[2].once
              && back[2].ifs.size() == 1 && back[2].dos[0].text == "You win!\nWell done" && back[2].dos[0].kind == Do::Win, "event cards save and load");
        CHECK(std::string(needOf(Do::OpenDoor).objectKind) == "door" && needOf(When::Every).number, "each part says what it needs");
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
