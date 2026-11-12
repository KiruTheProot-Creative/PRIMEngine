// Game logic tests: creative player, undo/redo, world info and settings files.
#include "../src/game/game.h"
#include "../src/game/project.h"
#include "../src/game/brush.h"
#include "../src/game/objects.h"
#include <filesystem>
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

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
