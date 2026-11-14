// Game logic tests: creative player, undo/redo, world info and settings files.
#include "../src/game/game.h"
#include "../src/game/project.h"
#include "../src/game/brush.h"
#include "../src/game/objects.h"
#include "../src/game/meshmodel.h"
#include "../src/core/json.h"
#include "../src/game/events.h"
#include "../src/game/hud.h"
#include "../src/game/weapons.h"
#include "../src/game/breakable.h"
#include "../src/game/materials.h"
#include "../src/game/anim.h"
#include "../src/game/folders.h"
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

static double wrapTest(double d) { d = std::fmod(d + 180.0, 360.0); if (d < 0) d += 360.0; return d - 180.0; }
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
            CHECK(std::fabs(fb.hi.x - fb.lo.x - 1.5) < 0.01 && std::fabs(fb.hi.y - fb.lo.y - 2.625) < 0.01 && fb.hi.z - fb.lo.z < 1, "the forcefield fills the opening (%.2f x %.2f)", fb.hi.x - fb.lo.x, fb.hi.y - fb.lo.y);
            { Box3 ob = turnedBoxPublic(d, door.frame(), op); V3 o0, a0, u0, o1, a1, u1; fieldQuad(d, door.frame(), op, o0, a0, u0, 0); fieldQuad(d, door.frame(), op, o1, a1, u1, 1);
              CHECK(o0.z < ob.lo.z - 0.05 && o1.z > ob.hi.z + 0.05 && fb.lo.z <= o0.z + 1e-9 && fb.hi.z >= o1.z - 1e-9, "the fields float a bit in front of and behind the door (%.3f < %.3f, %.3f > %.3f)", o0.z, ob.lo.z, o1.z, ob.hi.z); }
            d.door.fieldParts = "group,group2"; CHECK(fieldPartOn(d.door, "group2") && !fieldPartOn(d.door, "group3") && fieldPartOn(DoorSettings{}, "x"), "the field can be on chosen parts");
            d.door.fieldSide = 2; d.door.fieldGap = 0.1f; d.door.fieldOnParts = true; { std::vector<WorldObject> v{ d }, v2; saveObjects("/tmp/prim_doorf.txt", v); loadObjects("/tmp/prim_doorf.txt", v2);
              CHECK(v2.size() == 1 && v2[0].door.fieldSide == 2 && std::fabs(v2[0].door.fieldGap - 0.1f) < 1e-6 && v2[0].door.fieldOnParts && v2[0].door.fieldParts == "group,group2", "forcefield placement saves and loads"); }
            d.door = DoorSettings{}; d.door.w = 2; d.door.h = 3;
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
        CHECK(particlePresets().size() == 11, "eleven presets"); CHECK(particlePreset("Muzzle flash").life < 0.15f, "a short muzzle flash");
    }

    // a door with moving parts inside moving parts (DOOR_02: the halves turn, then smaller pieces inside them slide)
    {
        MeshModel d; std::string err; bool ok = importModel(std::string(SOURCE_DIR) + "/assets/examples/DOOR_02.fbx", d, err);
        CHECK(ok && d.clips.size() == 1 && d.parts.size() == 4, "DOOR_02 imports: 1 animation, 4 moving parts (%s)", err.c_str());
        int g4 = -1, g2 = -1; for (int i = 0; i < (int)d.parts.size(); i++) { if (d.parts[i].name == "group4") g4 = i; if (d.parts[i].name == "group2") g2 = i; }
        CHECK(g4 >= 0 && g2 >= 0 && d.parts[g4].parentPart == g2, "group4 hangs from group2");
        auto centre = [&](const std::vector<MeshVert>& v) { V3 c{ 0, 0, 0 }; int n = 0; for (size_t i = 0; i < v.size(); i++) if (d.vpart[i] == g4) { c = c + V3{ v[i].p[0], v[i].p[1], v[i].p[2] }; n++; } return n ? c * (1.0 / n) : c; };
        V3 r = centre(d.raw), a = centre(d.posed(0, 0.75f));
        CHECK(std::fabs(r.x - 21.875) < 0.01 && std::fabs(r.y - 43.75) < 0.01 && std::fabs(r.z - 15.625) < 0.01, "group4 at rest (%.3f %.3f %.3f)", r.x, r.y, r.z);
        CHECK(std::fabs(a.x + 21.875) < 0.01 && std::fabs(a.y - 43.75) < 0.01 && std::fabs(a.z - 3.125) < 0.01, "at 0.75 s it has turned and moved with group2 (%.3f %.3f %.3f)", a.x, a.y, a.z);
        d.save("/tmp/prim_door2.pmesh"); MeshModel back; back.load("/tmp/prim_door2.pmesh");
        V3 b = centre(back.posed(0, 0.75f));
        CHECK(back.parts.size() == 4 && back.parts[g4].parentPart == g2 && std::fabs(b.x - a.x) < 1e-4, "and it saves and loads the same");
    }

    // the morph ball, bombs, breakable blocks
    {
        Breakables b; b.mark(3, 64, -2, ""); b.mark(4, 64, -2, "Bomb"); b.mark(5, 64, -2, "Wave Beam"); b.mark(6, 64, -2, "Missile"); b.mark(7, 64, -2, "Charged shot");
        CHECK(breaksWith(*b.breaker(3, 64, -2), "Power Beam", false, false) && breaksWith("", "Bomb", false, false), "a plain breakable block: any shot or bomb");
        CHECK(breaksWith("Bomb", "Bomb", false, false) && !breaksWith("Bomb", "Power Beam", false, true), "a bomb block: only bombs");
        CHECK(breaksWith("Wave Beam", "Wave Beam", false, true) && !breaksWith("Wave Beam", "Power Beam", false, false) && !breaksWith("Wave Beam", "Missile", true, false) && breaksWith("Super Missile", "Super Missile", true, false) && !breaksWith("Super Missile", "Missile", true, false), "a Wave Beam block: that beam, not missiles; a Super Missile block: only those");
        CHECK(breaksWith("Missile", "Power Beam", true, false) && !breaksWith("Missile", "Power Beam", false, true), "a missile block: missiles");
        CHECK(breaksWith("Charged shot", "Power Beam", false, true) && !breaksWith("Charged shot", "Power Beam", false, false), "a charged-shot block: only charged shots");
        saveBreakables("/tmp/prim_break.txt", b); Breakables back; loadBreakables("/tmp/prim_break.txt", back);
        CHECK(back.marks.size() == 5 && *back.breaker(5, 64, -2) == "Wave Beam" && back.breaker(3, 64, -2)->empty() && !back.has(8, 64, -2), "breakable marks save and load");
        auto reach = blocksInReach({ 10.5, 64.5, 10.5 }, 1.3); bool self = false, far = false;
        for (auto& p : reach) { if (p == std::array<int, 3>{ 10, 64, 10 }) self = true; if (std::abs(p[0] - 10) > 1) far = true; }
        CHECK(self && !far && reach.size() == 7, "a bomb reaches its own block and the ones around it (%zu)", reach.size());
        CHECK(bombLift({ 0, 0, 0 }, { 0, 0.4, 0 }, 1.3, 8) > 7 && bombLift({ 0, 0, 0 }, { 1.2, 0, 0 }, 1.3, 8) > 4 && bombLift({ 0, 0, 0 }, { 1.4, 0, 0 }, 1.3, 8) == 0, "a bomb throws the ball up, less at the edge, not at all beyond");
        BlockRegistry br; registerBuiltinBlocks(br); World w(br); w.gen.layers = { { br.find("stone"), 3 } }; w.stream({ 0, 0, 0 }, 3, 2, 100000);
        PlayerRules r; Player p; p.flying = false; p.ball = true; p.applySize(r); p.feet = { 0.5, 0, 0.5 }; p.yaw = 3.14159265358979 / 2; Controls go; go.forward = true;
        CHECK(p.height == 1 && p.halfW < 0.5, "rolled up, the player is a ball of the ball size");
        p.update(w, go, 0.05, r); double v1 = p.vel.x;
        for (int k = 0; k < 40; k++) p.update(w, go, 0.05, r);
        CHECK(v1 > 0.5 && v1 < r.ballSpeed * 0.5 && std::fabs(p.vel.x - r.ballSpeed) < 0.2, "the ball rolls up to its speed (%.2f, then %.2f)", v1, p.vel.x);
        for (int k = 0; k < 6; k++) p.update(w, Controls{}, 0.05, r);
        CHECK(p.vel.x > 2, "and keeps rolling a while after you let go (%.2f)", p.vel.x);
        Controls jump; jump.jump = true; p.vel = { 0, 0, 0 }; for (int k = 0; k < 4; k++) p.update(w, jump, 0.05, r);
        CHECK(p.onGround && p.feet.y < 0.01, "the ball cannot jump by itself (%.2f)", p.feet.y);
        CHECK(std::fabs(r.bombJumpSpeed() * r.bombJumpSpeed() / (2 * r.gravity) - r.bombJump) < 1e-4, "a bomb jump reaches its height");
        ImpactSphere is; is.radius = 2; is.life = 0.5f;
        float r0 = is.radiusNow(), a0 = is.alphaNow(); is.t = 0.1f; float r1 = is.radiusNow(); is.t = 0.25f; float r2 = is.radiusNow(), a2 = is.alphaNow(); is.t = 0.5f;
        CHECK(r0 == 0 && a0 == 1 && r1 > 0.9f && r2 > 1.7f && r2 < 2 && a2 < 0.3f && std::fabs(is.radiusNow() - 2) < 1e-6 && is.alphaNow() == 0 && is.done(), "an impact sphere grows fast, then slowly, and fades out (%.2f, %.2f)", r1, r2);
        GameSettings gs; gs.player.morphBall = 2; gs.player.bombSphere = 4.5f; gs.player.bombJump = 3; gs.player.ballModel = "my ball"; gs.save("/tmp/prim_game_ball.txt"); GameSettings gb; gb.load("/tmp/prim_game_ball.txt");
        CHECK(gb.player.morphBall == 2 && gb.player.bombSphere == 4.5f && gb.player.bombJump == 3 && gb.player.ballModel == "my ball", "the morph ball's rules save and load");
        { MeshModel mm; CHECK(builtinShape("@missile", mm) && std::fabs(mm.sz - 1) < 0.02 && mm.sx < 0.4 && mm.sy < 0.4, "the built-in missile builds: 1 block long, thin (%.2f x %.2f x %.2f)", mm.sx, mm.sy, mm.sz); }
        MeshModel mb; CHECK(builtinShape("@morph-ball", mb) && std::fabs(mb.sy - 1) < 0.01 && std::fabs(mb.sx - 1) < 0.02, "the built-in morph ball builds, 1 block across (%.2f)", mb.sx);
    }

    // weapons (Samus controls part 1)
    {
        WeaponPoint up; up.pitch = 90; V3 d = pointDir(up), f = pointDir(WeaponPoint{});
        CHECK(std::fabs(d.y - 1) < 1e-9 && std::fabs(f.z - 1) < 1e-9, "points aim: 0 0 is forward (+Z), pitch 90 is up");
        WeaponSet w = defaultWeapons(); Beam& b = w.beams[0];
        MissileType mt; WeaponState st; int missiles = 2;
        CHECK(st.fire(b, nullptr, 0.3f, missiles) && !st.fire(b, nullptr, 0.3f, missiles), "a beam shot, then the fire rate makes it wait");
        st.tick(1.f / b.fireRate + 0.01f);
        CHECK(st.fire(b, nullptr, 0.3f, missiles), "a beam may fire again during its animation (default)");
        st = WeaponState{}; st.tick(1);
        CHECK(st.fire(b, &mt, 1.2f, missiles) && missiles == 1, "a missile uses one up");
        st.tick(1.f / mt.rate + 0.01f);
        CHECK(!st.fire(b, &mt, 1.2f, missiles) && missiles == 1, "the next missile waits until the missile animation is done (default)");
        st.tick(0.6f); CHECK(st.fire(b, &mt, 1.2f, missiles) && missiles == 0, "then it fires");
        st.tick(5); CHECK(!st.fire(b, &mt, 1.2f, missiles), "no missiles left: none");
        Beam nw = b; nw.missileWait = false; WeaponState s2; int m2 = 5; s2.fire(nw, &mt, 3, m2); s2.tick(1.f / mt.rate + 0.01f);
        CHECK(s2.fire(nw, &mt, 3, m2), "with waiting off, it fires before the animation is done");
        { MissileType sup; sup.cost = 5; WeaponState s3; int m3 = 7; CHECK(s3.fire(b, &sup, 0, m3) && m3 == 2, "a Super Missile uses five");
          s3.tick(10); CHECK(!s3.fire(b, &sup, 0, m3) && m3 == 2, "not enough left for another: none (and none used up)"); }
        CHECK(bestOwned({ 1, 0, 1, 0 }, 0) == 2 && bestOwned({ 0, 0 }, 0) == 0 && bestOwned({ 1, 1, 1 }, 0) == 2, "no switching: always the best beam you have (the last one owned in the list)");
        CHECK(beamClip(b, Beam::Charged) == "charged shot" && beamClip(b, Beam::Charging) == "charging" && beamClip(b, Beam::MissileShot) == "missile", "each thing the beam does has its own cannon animation");
        { Beam nb = b; nb.chargedShotClip.clear(); CHECK(beamClip(nb, Beam::Charged) == "fire", "no charged-shot animation: the normal firing one"); }
        WeaponPose still, walking; WeaponMotion mo;
        for (int k = 0; k < 120; k++) { still.update(mo, 1.0 / 60, 0, false, 0, 0); walking.update(mo, 1.0 / 60, 4.3f, false, 0, 0); }
        double sx = 0, wx = 0; WeaponPose a = still, bw = walking;
        for (int k = 0; k < 60; k++) { a.update(mo, 1.0 / 60, 0, false, 0, 0); bw.update(mo, 1.0 / 60, 4.3f, false, 0, 0); sx = std::max(sx, std::fabs(a.offset.x)); wx = std::max(wx, std::fabs(bw.offset.x)); }
        CHECK(wx > 0.008 && sx < 1e-6, "walking sways the cannon side to side, standing only breathes (%.4f vs %.4f)", wx, sx);
        WeaponPose k1; k1.update(mo, 1.0 / 60, 0, false, 0, 1); for (int k = 0; k < 3; k++) k1.update(mo, 1.0 / 60, 0, false, 0, 0);
        CHECK(k1.offset.z < -0.005 && k1.pitch < -0.5f, "firing kicks it back and up (%.3f, %.2f deg)", k1.offset.z, k1.pitch);
        for (int k = 0; k < 120; k++) k1.update(mo, 1.0 / 60, 0, false, 0, 0);
        CHECK(std::fabs(k1.offset.z) < 1e-3, "and it settles again");
        WeaponPose l1; l1.update(mo, 1.0 / 60, 0, true, 20, 0); for (int k = 0; k < 4; k++) l1.update(mo, 1.0 / 60, 0, false, 0, 0);
        CHECK(l1.offset.y < -0.003, "landing dips it (%.4f)", l1.offset.y);
        w.beams[1].muzzle.yaw = 12.5f; w.beams[1].missileWait = false; w.beams[0].name = "Power % Beam"; w.motion.recoil = 0.3f; w.missileMax = 99; w.cannonInFront = true; w.beams[1].chargeTime = 2.5f; w.beams[0].charge = false; w.missileTypes[1].sphere = 3.5f; w.switchMode = 2; w.beams[1].chargedClip = "glow loop"; w.beams[0].shotStyle = 0;
        { SoundUse u; u.file = "done.wav"; w.beams[1].chargeDone.sounds = { u }; u.file = "hum.wav"; w.beams[1].chargedLoop.sounds = { u }; u.file = "zap hit.wav"; w.beams[0].hitSound.sounds = { u }; u.file = "boom.wav"; w.missileTypes[0].hit.sounds = { u }; u.file = "whoosh.wav"; w.missileTypes[1].launch.sounds = { u }; }
        saveWeapons("/tmp/prim_weapons.txt", w); WeaponSet back; loadWeapons("/tmp/prim_weapons.txt", back);
        CHECK(back.beams.size() == 2 && back.beams[0].name == "Power % Beam" && back.beams[1].muzzle.yaw == 12.5f && !back.beams[1].missileWait && back.beams[0].missileWait
              && back.beams[1].shotLook == "Magic" && back.motion.recoil == 0.3f && back.missileMax == 99 && back.beams[0].model == "@arm-cannon" && back.cannonInFront && back.missileTypes.size() == 2 && back.missileTypes[1].sphere == 3.5f && back.missileTypes[1].name == "Super Missile" && back.missileTypes[1].cost == 5 && !back.missileTypes[1].startsOwned
              && back.switchMode == 2 && back.beams[1].chargedClip == "glow loop" && back.beams[0].shotStyle == 0 && back.beams[1].shotStyle == 1
              && back.beams[1].chargeDone.sounds.size() == 1 && back.beams[1].chargedLoop.sounds[0].file == "hum.wav" && back.beams[0].hitSound.sounds[0].file == "zap hit.wav"
              && back.missileTypes[0].hit.sounds[0].file == "boom.wav" && back.missileTypes[1].launch.sounds[0].file == "whoosh.wav" && back.beams[0].chargedHitSound.empty() && back.beams[1].chargeTime == 2.5f && !back.beams[0].charge && back.beams[1].chargeLook == "Charge", "weapons save and load");
        {   // an older file (missiles were set on each beam): the first beam's become the missile
            std::ifstream in("/tmp/prim_weapons.txt"); std::ofstream out("/tmp/prim_weapons_old.txt"); std::string ln;
            while (std::getline(in, ln)) if (ln.rfind("missile ", 0) != 0 && ln.rfind("msound ", 0) != 0) out << ln << "\n";
        }
        { WeaponSet ow; loadWeapons("/tmp/prim_weapons_old.txt", ow); CHECK(ow.missileTypes.size() == 1 && ow.missileTypes[0].name == "Missile" && ow.missileTypes[0].speed == 30, "older weapon files: one plain missile for all beams"); }
        {   // the charge beam
            Beam cb = defaultWeapons().beams[0]; WeaponState cs; bool was = false; int normal = 0, charged = 0;
            auto step = [&](bool down, float dt) { auto r = cs.beamButton(cb, down, was, dt, 0.2f); was = down; cs.tick(dt); normal += r == WeaponState::Shot::Normal; charged += r == WeaponState::Shot::Charged; };
            step(true, 0.016f); for (int k = 0; k < 5; k++) step(true, 0.016f); step(false, 0.016f);
            CHECK(normal == 1 && charged == 0, "a tap fires one normal shot");
            for (int k = 0; k < 30; k++) step(false, 0.016f);
            step(true, 0.016f); for (int k = 0; k < 50; k++) step(true, 0.016f);
            CHECK(cs.charging && cs.charge > 0.3f && cs.charge < 1, "holding fills the charge (%.2f)", cs.charge);
            step(false, 0.016f); CHECK(charged == 0 && normal == 2 && cs.charge == 0, "letting go before it is full fires nothing more");
            for (int k = 0; k < 30; k++) step(false, 0.016f);
            step(true, 0.016f); for (int k = 0; k < 90; k++) step(true, 0.016f);
            CHECK(cs.charge >= 1, "after the charge time it is full");
            step(false, 0.016f); CHECK(charged == 1, "letting go when full fires the charged shot");
            Beam pl = cb; pl.charge = false; pl.auto_ = true; WeaponState ps; int n = 0;
            for (int k = 0; k < 60; k++) { if (ps.beamButton(pl, true, k > 0, 1.f / 60, 0) == WeaponState::Shot::Normal) n++; ps.tick(1.f / 60); }
            CHECK(n >= 4 && n <= 6, "without charging, holding keeps firing at its rate (%d shots in 1 s)", n);
            HudValues hv; hv.charge = 0.5f; HudElement he; he.value = HudValue::Charge; he.text = "{value}%";
            CHECK(hudText(he, hv) == "50%" && hudMax(he, hv) == 100, "the HUD can show the charge");
        }
        {   // free aim
            WeaponSet fw = defaultWeapons(); fw.aimMode = 1; FreeAim a; float ty, tp;
            a.update(fw, 200, 0, 1000, 600, 1.0 / 60, ty, tp);
            CHECK(std::fabs(a.x - 0.4f) < 1e-5 && ty == 0 && tp == 0, "free aim: the cursor moves, inside the dead zone the camera stays still");
            a.update(fw, 250, -300, 1000, 600, 1.0 / 60, ty, tp);
            CHECK(a.x > 0.85f && ty > 0 && a.y > 0.95f && tp > 0, "near the edge it turns (%.2f, %.2f deg)", ty, tp);
            a.update(fw, 5000, 0, 1000, 600, 1.0 / 60, ty, tp);
            CHECK(a.x == 1 && std::fabs(ty - fw.turnSpeed / 60.f) < 1e-3, "at the very edge: the full turning speed");
            FreeAim c0; V3 d0 = c0.dir(70, 16.f / 9); FreeAim c1; c1.x = 1; c1.y = 0; V3 d1 = c1.dir(70, 1);
            CHECK(std::fabs(d0.z - 1) < 1e-9 && std::fabs(std::atan2(d1.x, d1.z) * 180 / 3.14159265 - 35) < 0.01, "the aim goes through the cursor (edge of a square 70-degree view: 35 degrees)");
        }
        {   // sounds: WAV files, timelines, the player's sounds
            WavData w; w.channels = 2; w.rate = 22050; for (int i = 0; i < 2205 * 2; i++) w.samples.push_back(std::sin(i * 0.05f) * 0.5f);
            WavData r; std::string err; CHECK(readWav(wavBytes(w), r, err) && r.channels == 2 && r.rate == 22050 && r.samples.size() == w.samples.size() && std::fabs(r.samples[11] - w.samples[11]) < 1e-3 && std::fabs(r.seconds() - 0.1f) < 1e-4,
                  "a 16-bit stereo WAV reads back (%s)", err.c_str());
            std::vector<uint8_t> b8 = wavBytes(w); b8[34] = 8; b8[32] = 2; b8[28] = (uint8_t)((22050 * 2) & 255); b8[29] = (uint8_t)((22050 * 2) >> 8);   // pretend 8-bit: the data reads as twice as many samples
            CHECK(readWav(b8, r, err) && r.samples.size() == w.samples.size() * 2, "8-bit WAV too");
            std::vector<uint8_t> junk = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'A', 'V', 'I', ' ' }; CHECK(!readWav(junk, r, err), "not a WAV: refused (%s)", err.c_str());
            Timeline tl; tl.animSpeed = 2; tl.shotAt = 0.1f; SoundClip c1; c1.at = 0; c1.sound.file = "zap.wav"; SoundClip c2; c2.at = 0.3f; c2.sound.file = "clank.wav"; c2.sound.length = 0.5f; tl.sounds = { c1, c2 };
            CHECK(std::fabs(tl.length(1.0f, { 0.2f, 2.0f }) - 0.8f) < 1e-5, "a timeline lasts until its last part ends (%.2f s)", tl.length(1.0f, { 0.2f, 2.0f }));
            TimelinePlay tp; tp.start(tl); std::vector<int> now; bool shot; int sounds = 0, shots = 0; double shotT = -1, clankT = -1, t = 0;
            for (int k = 0; k < 120 && tp.running(); k++) { tp.step(1.0 / 60, now, shot, 0.8); for (int i : now) { sounds++; if (i == 1) clankT = t; } if (shot) { shots++; shotT = t; } t += 1.0 / 60; }
            CHECK(sounds == 2 && shots == 1 && std::fabs(shotT - 0.1) < 0.02 && std::fabs(clankT - 0.3) < 0.02 && !tp.running(), "playing it: each sound starts once at its time, the shot at 0.1 s, then it ends");
            uint32_t seed = 7; SoundUse su; su.pitch = 1; su.pitchVar = 0.1f; float lo = 9, hi = 0; for (int k = 0; k < 200; k++) { float p = randomPitch(su, seed); lo = std::min(lo, p); hi = std::max(hi, p); }
            CHECK(lo >= 0.9f && hi <= 1.1f && hi - lo > 0.15f, "random pitch stays within its spread (%.3f .. %.3f)", lo, hi);
            PlayerSounds ps; SoundUse s1; s1.file = "step metal.wav"; s1.pitchVar = 0.12f; SoundUse s2; s2.file = "step metal 2.wav"; SoundUse s3; s3.file = "step metal 3.wav";
            ps.step.sounds = { s1, s2, s3 }; SoundUse l1; l1.file = "land.wav"; l1.trimStart = 0.05f; ps.land.sounds = { l1 }; ps.stepEvery = 1.4f;
            CHECK(ps.landVolume(3) == 0 && ps.landVolume(20) == 1 && ps.landVolume(12) > 0.4f && ps.landVolume(12) < 0.6f, "landing louder the faster you fall");
            { uint32_t sd = 5; int seen[3] = {}; std::string prev; bool repeat = false;
              for (int k = 0; k < 300; k++) { const SoundUse* u = ps.step.pick(sd); if (u->file == prev) repeat = true; prev = u->file; for (int q = 0; q < 3; q++) if (u->file == ps.step.sounds[q].file) seen[q]++; }
              CHECK(!repeat && seen[0] > 60 && seen[1] > 60 && seen[2] > 60, "several step sounds: each gets used, never the same twice in a row (%d %d %d)", seen[0], seen[1], seen[2]); }
            { uint32_t sd = 1; CHECK(ps.land.pick(sd) && ps.land.pick(sd)->file == "land.wav" && !ps.jump.pick(sd) && ps.jump.empty(), "one sound: always that one; none: nothing"); }
            savePlayerSounds("/tmp/prim_psounds.txt", ps); PlayerSounds pb; loadPlayerSounds("/tmp/prim_psounds.txt", pb);
            CHECK(pb.step.sounds.size() == 3 && pb.step.sounds[0].file == "step metal.wav" && pb.step.sounds[2].file == "step metal 3.wav" && std::fabs(pb.step.sounds[0].pitchVar - 0.12f) < 1e-6
                  && pb.land.sounds.size() == 1 && pb.land.sounds[0].trimStart == 0.05f && pb.stepEvery == 1.4f && pb.jump.empty(), "the player's sounds save and load (several per thing)");
            { PlayerSounds pr; SoundUse r1; r1.file = "roll.wav"; pr.roll.sounds = { r1 }; SoundUse b1; b1.file = "bounce.wav"; pr.ballLand.sounds = { b1 }; pr.rollPitch = 0.5f;
              float v0, p0, vs, ps_, vf, pf; pr.rollMix(0, 8, v0, p0); pr.rollMix(2, 8, vs, ps_); pr.rollMix(8, 8, vf, pf);
              CHECK(v0 == 0 && vs > 0 && vs < vf && vf == 1 && ps_ < 1 && pf > 1 && std::fabs(pf - 1.25f) < 1e-5, "the roll loop: silent when still, louder and higher the faster it rolls");
              savePlayerSounds("/tmp/prim_proll.txt", pr); PlayerSounds pq; loadPlayerSounds("/tmp/prim_proll.txt", pq);
              CHECK(pq.roll.sounds.size() == 1 && pq.roll.sounds[0].file == "roll.wav" && pq.ballLand.sounds[0].file == "bounce.wav" && pq.rollPitch == 0.5f, "the morph ball's sounds save and load"); }
            { std::ofstream old("/tmp/prim_psounds_old.txt"); old << "sounds 1\nstep step.wav 1 1 0.05 0 0\njump - 1 1 0.05 0 0\nevery 1.7\n"; }
            PlayerSounds po; loadPlayerSounds("/tmp/prim_psounds_old.txt", po);
            CHECK(po.step.sounds.size() == 1 && po.step.sounds[0].file == "step.wav" && po.jump.empty(), "older files (one sound each) still load");
            WeaponSet ws = defaultWeapons(); ws.beams[0].timelines[Beam::MissileShot] = tl; ws.aimMode = 1; ws.deadZone = 0.3f;
            saveWeapons("/tmp/prim_wtl.txt", ws); WeaponSet wb; loadWeapons("/tmp/prim_wtl.txt", wb);
            const Timeline& bt = wb.beams[0].timelines[Beam::MissileShot];
            CHECK(bt.animSpeed == 2 && bt.shotAt == 0.1f && bt.sounds.size() == 2 && bt.sounds[1].sound.file == "clank.wav" && bt.sounds[1].at == 0.3f && wb.beams[1].timelines[Beam::MissileShot].sounds.empty()
                  && wb.aimMode == 1 && wb.deadZone == 0.3f, "timelines and aiming save and load with the weapons");
        }
        DoorSettings ds; CHECK(doorOpensFor(ds, "Wave Beam", false) && doorOpensFor(ds, "", true), "a plain door opens for any shot");
        ds.openedBy = "Wave Beam"; CHECK(doorOpensFor(ds, "Wave Beam", false) && !doorOpensFor(ds, "Power Beam", false) && !doorOpensFor(ds, "Missile", true), "a Wave Beam door only for the Wave Beam");
        { DoorSettings sd; sd.openedBy = "Super Missile"; CHECK(doorOpensFor(sd, "Super Missile", true) && !doorOpensFor(sd, "Missile", true) && !doorOpensFor(sd, "Power Beam", false), "a Super Missile door only for Super Missiles"); }
        { DoorSettings md; md.openedBy = "Missile"; CHECK(doorOpensFor(md, "Super Missile", true), "a missile door opens for any kind of missile"); }
        ds.openedBy = "Missile"; CHECK(doorOpensFor(ds, "Power Beam", true) && !doorOpensFor(ds, "Power Beam", false), "a missile door only for missiles");
        { WorldObject d; d.kind = ObjKind::Door; d.door.openedBy = "Wave Beam"; std::vector<WorldObject> v{ d }, v2; saveObjects("/tmp/prim_dw.txt", v); loadObjects("/tmp/prim_dw.txt", v2);
          CHECK(v2.size() == 1 && v2[0].door.openedBy == "Wave Beam", "what opens a door saves and loads"); }
        MeshModel can; CHECK(builtinShape("@arm-cannon", can) && can.triangles() == 60 && can.sz > 0.85, "the built-in arm cannon builds (%.2f long)", can.sz);
    }

    // the HUD (Player editor part 1)
    {
        HudPlane p; p.pos = { 0, 0, 1 }; p.width = 2; p.height = 1; p.bendX = 0; p.bendY = 0;
        V3 c = planePoint(p, 0.5f, 0.5f), tl = planePoint(p, 0, 0);
        CHECK(std::fabs(c.z - 1) < 1e-9 && std::fabs(tl.x + 1) < 1e-9 && std::fabs(tl.y - 0.5) < 1e-9 && std::fabs(tl.z - 1) < 1e-9, "a flat plane: middle and corner where they should be");
        p.bendX = 0.5f; V3 e = planePoint(p, 1, 0.5f), m = planePoint(p, 0.5f, 0.5f);
        CHECK(e.z < 0.9 && std::fabs(m.z - 1) < 1e-9 && e.x < 1 && e.x > 0.8, "bent: the edges come toward the camera (edge z %.3f, x %.3f)", e.z, e.x);
        p.yaw = 90; V3 t = planePoint(p, 0.5f, 0.5f);
        CHECK(std::fabs(t.z - 1) < 1e-9, "turning the plane keeps its middle in place");
        CHECK(rotateDeg({ 0, 0, 1 }, 90, 0, 0).x > 0.99, "yaw 90 turns forward to the right");
        HudPart a; a.pos = { -0.5, -0.2, 0.6 }; a.roll = 15; a.yaw = -10; HudPart b = a; mirrorPart(b);
        V3 ax, ay, az, bx, by, bz; partAxes(a, ax, ay, az); partAxes(b, bx, by, bz);
        CHECK(b.pos.x == 0.5 && b.mirror && std::fabs(bx.x + ax.x) < 1e-9 && std::fabs(by.x + ay.x) < 1e-9 && std::fabs(by.y - ay.y) < 1e-9, "a mirrored part is the other side, flipped");
        HudElement el; el.x = 0.1f; el.w = 0.2f; el.align = 0; mirrorElement(el);
        CHECK(std::fabs(el.x - 0.7f) < 1e-6 && el.align == 2, "a mirrored element goes to the other side, its text to the other edge");
        HudValues vals; vals.health = 73.4f; vals.counts["Missile"] = 12;
        HudElement tx; tx.text = "ENERGY {value}"; tx.value = HudValue::Health;
        HudElement mi; mi.text = "{value}/{max}"; mi.value = HudValue::Counter; mi.valueName = "Missile"; mi.max = 25;
        CHECK(hudText(tx, vals) == "ENERGY 73" && hudText(mi, vals) == "12/25", "texts show their values (%s, %s)", hudText(tx, vals).c_str(), hudText(mi, vals).c_str());
        // sway: turning leaves it behind, then it springs back
        HudSwaySettings s; HudSway sw;
        sw.update(s, 1.0 / 60, 20, 0, 0, false, 0);
        CHECK(sw.yaw < -5, "turning right: the HUD stays behind to the left (%.1f deg)", sw.yaw);
        for (int k = 0; k < 120; k++) sw.update(s, 1.0 / 60, 0, 0, 0, false, 0);
        CHECK(std::fabs(sw.yaw) < 0.3f, "and comes back within 2 seconds (%.2f deg)", sw.yaw);
        HudSwaySettings slow = s; slow.smoothness = 1; HudSway a1, a2;
        a1.update(s, 1.0 / 60, 20, 0, 0, false, 0); a2.update(slow, 1.0 / 60, 20, 0, 0, false, 0);
        for (int k = 0; k < 15; k++) { a1.update(s, 1.0 / 60, 0, 0, 0, false, 0); a2.update(slow, 1.0 / 60, 0, 0, 0, false, 0); }
        CHECK(std::fabs(a2.yaw) > std::fabs(a1.yaw), "smoother: it takes longer to come back");
        HudSwaySettings none = s; none.strength = 0; HudSway z; z.update(none, 1.0 / 60, 30, 10, 4, true, 10);
        CHECK(std::fabs(z.yaw) < 1e-6 && std::fabs(z.pitch) < 1e-6, "strength 0: no sway");
        HudSway land; land.update(s, 1.0 / 60, 0, 0, 0, true, 12); for (int k = 0; k < 4; k++) land.update(s, 1.0 / 60, 0, 0, 0, false, 0);
        CHECK(land.offset.y < -0.001, "landing: the HUD dips (%.4f)", land.offset.y);
        HudSway soft, hard; soft.update(s, 1.0 / 60, 0, 0, 0, true, 5); hard.update(s, 1.0 / 60, 0, 0, 0, true, 25);
        for (int k = 0; k < 4; k++) { soft.update(s, 1.0 / 60, 0, 0, 0, false, 0); hard.update(s, 1.0 / 60, 0, 0, 0, false, 0); }
        CHECK(hard.offset.y < soft.offset.y * 2 && hard.shake > 1 && soft.shake < 0.3f, "a hard landing dips more and shakes (shake %.2f vs %.2f deg)", hard.shake, soft.shake);
        for (int k = 0; k < 90; k++) hard.update(s, 1.0 / 60, 0, 0, 0, false, 0);
        CHECK(hard.shake < 0.01f, "and the shake dies down");
        // save / load
        HudLayout h = defaultHud(); h.sway.strength = 1.7f; h.sway.land = 0.4f; h.fov = 85; h.planes[0].elements[1].text = "E N E R G Y {value}"; h.parts[0].name = "Strut % left";
        saveHud("/tmp/prim_hud.txt", h); HudLayout back; loadHud("/tmp/prim_hud.txt", back);
        CHECK(back.planes.size() == 1 && back.planes[0].elements.size() == h.planes[0].elements.size() && back.parts.size() == 2 && back.parts[1].mirror
              && back.sway.strength == 1.7f && back.sway.land == 0.4f && back.fov == 85 && back.planes[0].elements[1].text == "E N E R G Y {value}" && back.parts[0].name == "Strut % left"
              && back.planes[0].elements[3].valueName == "Missile" && back.planes[0].bendX == h.planes[0].bendX, "the HUD saves and loads");
        MeshModel strut; CHECK(builtinShape("@visor-strut", strut) && strut.triangles() == 48, "the visor strut model builds");
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
        { EventCard t; t.when = { When::EnterZone, "Hall" }; t.ifs = { { If::HasKey, "Red key" } }; t.dos = { { Do::OpenDoor, "Gate" }, { Do::Wait, "", "", 2 }, { Do::Message, "", "Hi" } }; t.once = true;
          std::string d = describe(t);
          CHECK(d == "When the player walks into Hall, only if the player has the Red key: open Gate, wait 2 s, show \"Hi\" (once).", "a card reads as one sentence: %s", d.c_str()); }
        CHECK(std::string(needOf(Do::OpenDoor).objectKind) == "door" && needOf(When::Every).number, "each part says what it needs");
    }

    {   // materials per texture
        TexMaterial iron = guessMaterial("iron_block"), lamp = guessMaterial("Red Lamp"), dirt = guessMaterial("dirt"), glass = guessMaterial("glass");
        CHECK(iron.metal > 0.5f && iron.shine > 0.3f && lamp.glow > 0 && lamp.glowFrom > 0 && dirt == TexMaterial{} && glass.shine > 0.5f && glass.coat == 0, "a texture's name gives a first guess (iron: metal, lamp: glows, glass: shiny, dirt: plain)");
        CHECK(guessMaterial("grass_slice") == TexMaterial{} && guessMaterial("twilight_grass") == TexMaterial{} && guessMaterial("Grass Block Top") == TexMaterial{} && guessMaterial("ironworks").metal > 0,
              "guesses need a whole word (or its start): slice is not ice, twilight is not light");
        CHECK(guessMaterial("GlowStone").glow > 0 && guessMaterial("polishedMarble").shine > 0 && guessMaterial("Ice_Block").shine > 0, "words split at _ spaces and capitals");
        { Materials ng; ng.guess = false; CHECK(ng.of("iron_block") == TexMaterial{}, "guessing off: plain"); }
        Materials mats; TexMaterial gold; gold.metal = 1; gold.shine = 0.9f; gold.coat = 0.25f; mats.put("dirt", gold);
        CHECK(mats.of("dirt").metal == 1 && mats.of("iron_block").metal == iron.metal, "set up textures keep their settings, the others get the guess");
        BlockRegistry r; std::vector<uint8_t> px(16 * 16 * 4, 200); r.setTexture("dirt", 16, 16, px); r.setTexture("glow lamp", 16, 16, px);
        std::vector<float> tb = mats.table(r); int di = r.textureIndex("dirt"), li = r.textureIndex("glow lamp");
        CHECK(tb.size() == r.textureCount() * 8 && tb[di * 8 + 1] == 1 && tb[di * 8 + 5] == 0.25f && tb[li * 8 + 2] > 0 && tb[0] == 0, "the shaders get 8 numbers per texture layer");
        saveMaterials("/tmp/prim_mats.txt", mats); Materials back; loadMaterials("/tmp/prim_mats.txt", back);
        CHECK(back.isSet("dirt") && back.of("dirt") == gold && !back.isSet("iron_block"), "materials save and load");
        mats.reset("dirt"); CHECK(!mats.isSet("dirt") && mats.of("dirt") == TexMaterial{}, "reset: back to the guess");
        { Materials sp; TexMaterial a; a.glow = 0.5f; sp.put("my texture 2", a); saveMaterials("/tmp/prim_mats2.txt", sp); Materials b; loadMaterials("/tmp/prim_mats2.txt", b); CHECK(b.of("my texture 2").glow == 0.5f, "names with spaces save too"); }
    }

    {   // a HUD plane's light on the helmet
        HudPlane p; p.elements.clear(); CHECK(planeLight(p).x == 0, "an empty plane gives no light");
        HudElement big; big.color = 0xFF0000; big.alpha = 1; big.w = 0.5f; big.h = 0.5f; p.elements.push_back(big);
        V3 a = planeLight(p); CHECK(a.x > 0.2 && a.y == 0 && a.z == 0, "a red panel gives red light (%.2f)", a.x);
        HudElement ring = big; ring.kind = HudKind::Circle; ring.filled = false; HudPlane q; q.elements = { ring };
        CHECK(planeLight(q).x < a.x * 0.3, "an outline gives less than a filled panel");
        p.opacity = 0.5f; CHECK(std::fabs(planeLight(p).x - a.x * 0.5) < 1e-6, "half see-through: half the light");
        p.bloom = 0.2f; p.lightUp = 0.9f; HudLayout h; h.planes = { p }; saveHud("/tmp/prim_hudglow.txt", h); HudLayout hb; loadHud("/tmp/prim_hudglow.txt", hb);
        CHECK(hb.planes.size() == 1 && hb.planes[0].bloom == 0.2f && hb.planes[0].lightUp == 0.9f, "a plane's glow and light save and load");
    }

    {   // smooth changes between animations
        PartPose A(9, 0.0), B(9, 0.0); B[0] = 1; B[4] = 90;   // part 0: moved 1 along x, turned 90 degrees around y
        BlendSettings cut; cut.mode = BlendMode::Cut;
        { PoseBlender p; p.update(A, 1, 1 / 60.f, cut); CHECK(p.update(B, 2, 1 / 60.f, cut)[0] == 1, "cut: the new clip at once"); }
        BlendSettings cf; cf.mode = BlendMode::Crossfade; cf.curve = BlendCurve::Linear; cf.time = 0.2f;
        { PoseBlender p; p.update(A, 1, 0.1f, cf); const PartPose& h = p.update(B, 2, 0.1f, cf);
          CHECK(std::fabs(h[0] - 0.5) < 1e-6 && std::fabs(h[4] - 45) < 1e-4 && p.busy(), "crossfade: half way after half the time (%.2f, %.1f deg)", h[0], h[4]);
          p.update(B, 2, 0.15f, cf); CHECK(!p.busy() && p.update(B, 2, 0.01f, cf)[0] == 1, "then the new clip"); }
        { BlendSettings sm = cf; sm.curve = BlendCurve::Smooth; PoseBlender p; p.update(A, 1, 0.05f, sm); double e = p.update(B, 2, 0.05f, sm)[0];
          CHECK(e > 0 && e < 0.25, "smooth curve: starts slowly (%.3f after a quarter)", e); }
        { PartPose R1(9, 0.0), R2(9, 0.0); R1[3] = 170; R2[3] = -170; PoseBlender p; p.update(R1, 1, 0.1f, cf); double m = p.update(R2, 2, 0.1f, cf)[3];
          CHECK(std::fabs(std::fabs(wrapTest(m)) - 180) < 1e-3, "turns go the short way round (170 -> -170 passes 180, not 0: %.1f)", m); }
        BlendSettings in; in.time = 0.25f;
        { PoseBlender p; p.update(A, 1, 1 / 60.f, in); const PartPose& f0 = p.update(B, 2, 1 / 60.f, in);
          CHECK(f0[0] < 0.1 && f0[4] < 9, "inertialization: no jump when the clip changes (%.3f)", f0[0]);
          double last = f0[0]; bool steady = true; for (int k = 0; k < 30; k++) { double x = p.update(B, 2, 1 / 60.f, in)[0]; steady &= x >= last - 1e-9; last = x; }
          CHECK(steady && std::fabs(last - 1) < 1e-6 && !p.busy(), "then it settles onto the new clip, never going back (%.4f)", last); }
        {   // changing clips in the middle of a change: still no jump
            PoseBlender p; p.update(A, 1, 1 / 60.f, in); for (int k = 0; k < 5; k++) p.update(B, 2, 1 / 60.f, in);
            double before = p.now()[0]; double after = p.update(A, 3, 1 / 60.f, in)[0];
            CHECK(std::fabs(after - before) < 0.08, "switching again mid-way stays smooth (%.3f -> %.3f)", before, after);
        }
        { PoseBlender p; p.update(A, 1, 1 / 60.f, in); PartPose moving = A; for (int k = 1; k <= 6; k++) { moving[0] = k * 0.05; p.update(moving, 1, 1 / 60.f, in); }
          PartPose stopAt = moving; double x1 = p.update(stopAt, 2, 1 / 60.f, in)[0]; double x2 = p.update(stopAt, 2, 1 / 60.f, in)[0];
          CHECK(std::fabs(x1 - 0.3) < 1e-9 && std::fabs(x2 - 0.3) < 1e-9, "a clip ending where the next starts: nothing to blend"); }
        AnimBlends ab; ab.def.time = 0.3f; BlendSettings dm; dm.mode = BlendMode::Crossfade; dm.curve = BlendCurve::EaseOut; dm.time = 0.5f; ab.byModel["DOOR 02"] = dm;
        ab.setSpeed("DOOR 02", "Charged Shot", 2.5f); ab.setSpeed("DOOR 02", "open", 1.f);
        CHECK(ab.speedOf("DOOR 02", "charged shot") == 2.5f && ab.speedOf("DOOR 02", "open") == 1 && ab.speedOf("other", "charged shot") == 1, "each animation can have its own speed (not case-sensitive)");
        saveAnimBlends("/tmp/prim_blend.txt", ab); AnimBlends ab2; loadAnimBlends("/tmp/prim_blend.txt", ab2);
        CHECK(ab2.speedOf("DOOR 02", "Charged shot") == 2.5f, "animation speeds save and load");
        CHECK(ab2.of("DOOR 02") == dm && ab2.of("other").time == 0.3f && ab2.of("other").mode == BlendMode::Inertialize, "change-over settings save and load (per model, and the default)");
        MeshModel mm; CHECK(builtinShape("@arm-cannon", mm), "a model");
        CHECK(mm.pose(-1, 0).size() == mm.parts.size() * 9, "a model's pose: 9 numbers per moving part");
    }

    {   // particles sucked into the middle (the charge gathering)
        ParticleSettings cs = particlePreset("Charge"); CHECK(cs.pull > 0 && cs.area == PArea::Sphere, "the charge's particles are sucked in");
        ParticleEmitter e(cs, 5); V3 o{ 10, 5, 10 }; e.update(0.05, o, 0, nullptr, true);
        bool outside = !e.ps.empty(); for (const Particle& p : e.ps) { V3 d = p.pos - o; outside &= std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) > 0.2; }
        CHECK(outside, "they appear on the outside (%zu)", e.ps.size());
        double before = 0; for (const Particle& p : e.ps) { V3 d = p.pos - o; before += std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z); } before /= std::max<size_t>(1, e.ps.size());
        ParticleEmitter e2 = e; e2.update(0.1, o, 0, nullptr, false); double after = 0; for (const Particle& p : e2.ps) { V3 d = p.pos - o; after += std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z); } after /= std::max<size_t>(1, e2.ps.size());
        CHECK(e2.ps.empty() || after < before, "and fly into the middle (%.3f -> %.3f)", before, after);
        for (int k = 0; k < 60; k++) e.update(1 / 60.0, o, 0, nullptr, false);
        CHECK(e.ps.empty(), "once there they are gone");
        ParticleEmitter f(cs, 9); f.update(0.05, o, 0, nullptr, true); V3 o2{ 12, 5, 10 }; V3 p0 = f.ps.empty() ? V3{} : f.ps[0].pos; f.update(0.0001, o2, 0, nullptr, false);
        CHECK(!f.ps.empty() && std::fabs(f.ps[0].pos.x - p0.x - 2) < 0.01, "they move along when the middle moves (the cannon turns)");
    }

    {   // folders you make yourself
        CHECK(FolderTree::clean("  Weapons//Beams/ ") == "Weapons/Beams" && FolderTree::clean("a\\b") == "a/b" && FolderTree::parent("a/b/c") == "a/b" && FolderTree::leaf("a/b/c") == "c", "folder paths tidy up");
        CHECK(FolderTree::within("a/b", "a") && !FolderTree::within("ab", "a") && FolderTree::within("x", ""), "inside a folder (not just a name starting the same)");
        FolderTree t; t.add("Weapons/Beams/Power"); t.add("Doors");
        CHECK(t.paths.count("Weapons") && t.paths.count("Weapons/Beams") && t.children("").size() == 2 && t.children("").front() == "Doors", "making a folder makes its parents; sorted");
        CHECK(t.rename("Weapons", "Arsenal") && t.paths.count("Arsenal/Beams/Power") && !t.paths.count("Weapons"), "renaming a folder moves what is inside");
        CHECK(!t.rename("Arsenal", "Arsenal/Beams/x"), "a folder can not go inside itself");
        CHECK(t.rename("Arsenal/Beams", "Doors/Beams") && t.paths.count("Doors/Beams/Power") && t.paths.count("Arsenal"), "moving a folder into another");
        t.remove("Doors/Beams"); CHECK(!t.paths.count("Doors/Beams/Power") && t.paths.count("Doors"), "deleting a folder deletes the ones inside it");
        AssetFolders af; af.textureIn["laser"] = "Weapons/Beams"; af.textureIn["dirt"] = "Nature"; af.textures.add("Weapons/Beams"); af.textures.add("Nature");
        AssetFolders::renameIn(af.textureIn, "Weapons", "Arsenal"); CHECK(af.folderOf(af.textureIn, "laser") == "Arsenal/Beams" && af.folderOf(af.textureIn, "dirt") == "Nature", "what is in a renamed folder moves along");
        AssetFolders::dropIn(af.textureIn, "Arsenal/Beams"); CHECK(af.folderOf(af.textureIn, "laser") == "Arsenal", "what is in a deleted folder goes up one");
        af.modelIn["DOOR 02"] = "Doors/Big ones"; af.models.add("Doors/Big ones"); af.objects.add("Level 1/Enemies"); af.blocks.add("Empty folder");
        saveAssetFolders("/tmp/prim_folders.txt", af); AssetFolders b; loadAssetFolders("/tmp/prim_folders.txt", b);
        CHECK(b.folderOf(b.modelIn, "DOOR 02") == "Doors/Big ones" && b.models.paths.count("Doors") && b.objects.paths.count("Level 1/Enemies") && b.blocks.paths.count("Empty folder") && b.folderOf(b.textureIn, "laser") == "Arsenal",
              "folders save and load (names with spaces, empty folders too)");
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
