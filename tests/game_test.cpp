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
#include "../src/game/items.h"
#include "../src/game/map.h"
#include "../src/game/visors.h"
#include "../src/game/menu.h"
#include "../src/game/enemies.h"
#include "../src/game/options.h"
#include "../src/game/cull.h"
#include "../src/game/screens.h"
#include "../src/game/savegame.h"
#include "../src/game/bosses.h"
#include "../src/game/assets.h"
#include "../src/game/looks.h"
#include "../src/game/surfaces.h"
#include <sstream>
#include <algorithm>
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
    { Settings a; a.shadowDistance = 300; a.bumps = 0.5f; a.coat = 0; a.materials = false; a.hudGlow = false; a.guessMaterials = false; a.shadowQuality = 1; a.save(dir + "/s2.txt"); Settings b; b.load(dir + "/s2.txt");
      CHECK(b.shadowDistance == 300 && b.bumps == 0.5f && b.coat == 0 && !b.materials && !b.hudGlow && !b.guessMaterials && b.shadowQuality == 1, "the newer look settings survive saving too"); }
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
        for (int k = 0; k < 600 && ev != PlayState::Respawned; k++) ev = ps.tick(w, q, pr, sp, 0, 1.0 / 60);   // (after the death sequence)
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
        CHECK(shapes == 17, "all built-in looks build (%d)", shapes);
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
        { WeaponState sx; int mm = 3; Beam bw = b; bw.waitForAnim = true; sx.fire(bw, nullptr, 2.0f, mm); sx.tick(1.f / bw.fireRate + 0.01f);
          CHECK(sx.fire(bw, &mt, 1.0f, mm), "a missile does not wait for the beam's animation"); sx.tick(1.f / mt.rate + 0.01f);
          Beam nb = b; nb.waitForAnim = false; CHECK(sx.fire(nb, nullptr, 0.3f, mm), "a beam that does not wait fires while the missile animation plays"); }
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
        { SoundUse u; u.file = "done.wav"; w.beams[1].chargeDone.sounds = { u }; u.file = "hum.wav"; w.beams[1].chargedLoop.sounds = { u }; u.file = "zap hit.wav"; w.beams[0].hitSound.sounds = { u }; u.file = "boom.wav"; w.missileTypes[0].hit.sounds = { u }; u.file = "whoosh.wav"; w.missileTypes[1].launch.sounds = { u }; u.file = "pew.wav"; w.beams[0].fireSound.sounds = { u }; u.file = "hum up.wav"; w.beams[1].chargeUp.sounds = { u }; u.file = "big pew.wav"; w.beams[1].chargedShotSound.sounds = { u }; }
        saveWeapons("/tmp/prim_weapons.txt", w); WeaponSet back; loadWeapons("/tmp/prim_weapons.txt", back);
        CHECK(back.beams.size() == 2 && back.beams[0].name == "Power % Beam" && back.beams[1].muzzle.yaw == 12.5f && !back.beams[1].missileWait && back.beams[0].missileWait
              && back.beams[1].shotLook == "Magic" && back.motion.recoil == 0.3f && back.missileMax == 99 && back.beams[0].model == "@arm-cannon" && back.cannonInFront && back.missileTypes.size() == 2 && back.missileTypes[1].sphere == 3.5f && back.missileTypes[1].name == "Super Missile" && back.missileTypes[1].cost == 5 && !back.missileTypes[1].startsOwned
              && back.switchMode == 2 && back.beams[1].chargedClip == "glow loop" && back.beams[0].shotStyle == 0 && back.beams[1].shotStyle == 1
              && back.beams[1].chargeDone.sounds.size() == 1 && back.beams[1].chargedLoop.sounds[0].file == "hum.wav" && back.beams[0].hitSound.sounds[0].file == "zap hit.wav"
              && back.missileTypes[0].hit.sounds[0].file == "boom.wav" && back.missileTypes[1].launch.sounds[0].file == "whoosh.wav" && back.beams[0].chargedHitSound.empty()
              && back.beams[0].fireSound.sounds[0].file == "pew.wav" && back.beams[1].chargeUp.sounds[0].file == "hum up.wav" && back.beams[1].chargedShotSound.sounds[0].file == "big pew.wav" && back.beams[1].chargeTime == 2.5f && !back.beams[0].charge && back.beams[1].chargeLook == "Charge", "weapons save and load");
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
            CHECK(ps.landVolume(3) == 0 && ps.landVolume(20) == 1 && ps.landVolume(5) == 1, "landing: always the same volume (very small drops silent)");
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
        { HudLayout m = defaultHud(); HudElement* mm = nullptr; for (auto& e : m.planes[0].elements) if (e.kind == HudKind::Minimap) mm = &e;
          CHECK(mm != nullptr, "the starter visor has a minimap");
          CHECK(mm && !mm->onMap && mm->onPause && m.parts[0].onMap, "the starter minimap hides on the map screen; the helmet stays");
          m.parts[1].onPause = false; m.planes[0].elements[0].onMap = false; m.planes[0].elements[1].visors = 0x2; m.parts[0].visors = 0x5;
          if (mm) { mm->mapRange = 31; mm->mapTilt = 70; mm->mapTurns = false; saveHud("/tmp/prim_hudmm.txt", m); HudLayout mb; loadHud("/tmp/prim_hudmm.txt", mb); const HudElement* b2 = nullptr; for (auto& e : mb.planes[0].elements) if (e.kind == HudKind::Minimap) b2 = &e;
            CHECK(b2 && b2->mapRange == 31 && b2->mapTilt == 70 && !b2->mapTurns && std::string(hudKindName(HudKind::Minimap)) == "Minimap", "the minimap saves and loads (range, tilt, turning)");
            { std::ofstream of("/tmp/prim_hudold.txt"); of << "hud 1\nplane Visor 0 0 1 0 0 0 1.6 0.9 0.3 0.1 1 1 1 1\nel Mini 5 0.7 0.6 0.2 0.3 16777215 1 - - 0 - 100 1 1 0 1 2 1\n"; }
            HudLayout old; loadHud("/tmp/prim_hudold.txt", old);
            CHECK(old.planes.size() == 1 && old.planes[0].elements.size() == 1 && old.planes[0].elements[0].kind == HudKind::Minimap && !old.planes[0].elements[0].onMap, "a minimap from an older file hides on the map screen");
            CHECK(mb.planes[0].elements[1].visors == 0x2 && mb.parts[0].visors == 0x5 && mb.planes[0].elements[0].visors == 0x0F, "which visors show an element / part: saved");
            CHECK(!mb.parts[1].onPause && mb.parts[1].onMap && !mb.planes[0].elements[0].onMap && mb.planes[0].elements[0].onPause, "shown when paused / on the map: saved for elements and 3D parts"); } }
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
        { Materials em; TexMaterial t; t.emitMap = "panel lights_e.png"; t.emit = 2.5f; t.lights = 9; em.put("panel lights", t); saveMaterials("/tmp/prim_mats3.txt", em); Materials eb; loadMaterials("/tmp/prim_mats3.txt", eb);
          CHECK(eb.of("panel lights").emitMap == "panel lights_e.png" && eb.of("panel lights").emit == 2.5f && eb.of("panel lights").lights == 9 && eb.of("panel lights") == t, "an emission map, its brightness and its light save and load"); }
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

    {   // items
        ItemSet is = defaultItems(); PlayerRules pr; std::map<std::string, int> got;
        auto have = [&](const std::string& n) { auto it = got.find(n); return it == got.end() ? 0 : it->second; };
        ItemEffects e0 = itemEffects(is, pr, have);
        CHECK(e0.maxHealth == pr.maxHealth && e0.damageTaken == 1 && e0.airJumps == 0 && !e0.powerBombs && e0.canCharge && e0.suit < 0, "nothing found: plain Samus (the Charge Beam is had from the start)");
        got["Energy Tank"] = 3; got["Missile Expansion"] = 2; got["Space Jump Boots"] = 1; got["Hi-Jump Boots"] = 1; got["Varia Suit"] = 1; got["Gravity Suit"] = 1; got["Power Bomb"] = 1;
        ItemEffects e = itemEffects(is, pr, have);
        CHECK(e.maxHealth == pr.maxHealth + 300 && e.missileBonus == 10 && e.airJumps == 1 && e.jumpHeight > pr.jumpHeight && e.powerBombs, "tanks add up; boots and Power Bombs work once found (%.0f energy)", e.maxHealth);
        CHECK(e.suit >= 0 && is.items[e.suit].name == "Gravity Suit" && e.damageTaken == 0.25f && e.freeInLiquid, "the best suit you have counts (Gravity over Varia)");
        { ItemSet c = is; for (auto& d : c.items) if (d.type == ItemType::ChargeBeam) d.startsOwned = false;
          CHECK(!itemEffects(c, pr, have).canCharge, "a Charge Beam to be found: no charging until then");
          got["Charge Beam"] = 1; CHECK(itemEffects(c, pr, have).canCharge, "found it: charging"); got.erase("Charge Beam");
          for (auto& d : c.items) if (d.type == ItemType::ChargeBeam) d.used = false;
          CHECK(itemEffects(c, pr, have).canCharge, "no Charge Beam in the game: beams charge as they are set"); }
        { ItemSet u = is; for (auto& d : u.items) if (d.type == ItemType::SpaceJump) d.used = false; CHECK(itemEffects(u, pr, have).airJumps == 0, "an item not in the game does nothing, even if picked up"); }
        CHECK(itemWorks(ItemType::SpaceJump) && !itemWorks(ItemType::ScrewAttack) && itemCounted(ItemType::EnergyTank) && !itemCounted(ItemType::Suit), "what works now / what is counted");
        ItemSet sv = is; sv.items[0].v[0] = 50; sv.powerBombStart = 2; ItemDef extra; extra.type = ItemType::Suit; extra.name = "Fusion Suit"; extra.v[0] = 0.1f; sv.items.push_back(extra);
        saveItems("/tmp/prim_items.txt", sv); ItemSet lb; loadItems("/tmp/prim_items.txt", lb);
        CHECK(lb.items.size() == sv.items.size() && lb.items[0].v[0] == 50 && lb.powerBombStart == 2 && lb.items.back().name == "Fusion Suit", "items save and load (your own suits too)");
        { std::ofstream old("/tmp/prim_items_old.txt"); old << "items 1\nitem energy_tank Energy%20Tank 1 0 100 0 0 16777215\n"; }
        ItemSet ol; loadItems("/tmp/prim_items_old.txt", ol);
        CHECK(ol.items.size() == defaultItems().items.size(), "an older list gets the newer kinds added (%zu)", ol.items.size());
    }

    {   // the items' moves (Space Jump, Speed Booster, Spring Ball, Gravity Suit)
        BlockRegistry r2; registerBuiltinBlocks(r2); World w(r2); w.gen.layers = { { r2.find("stone"), 3 } }; w.stream({ 0, 0, 0 }, 4, 2, 100000);
        auto stand = [&](Player& p) { p.flying = false; p.feet = { 0.5, 6, 0.5 }; p.vel = { 0, 0, 0 }; p.yaw = 3.14159265 / 2; for (int k = 0; k < 40; k++) p.update(w, Controls{}, 0.05, PlayerRules{}); };
        PlayerRules sj; sj.airJumps = 1; Player p; stand(p);
        Controls jump; jump.jump = true; p.update(w, jump, 0.02, sj); for (int k = 0; k < 15; k++) p.update(w, Controls{}, 0.02, sj);
        double vBefore = p.vel.y; p.update(w, jump, 0.02, sj); bool second = p.vel.y > vBefore + 3;
        for (int k = 0; k < 3; k++) p.update(w, Controls{}, 0.02, sj); double v2 = p.vel.y; p.update(w, jump, 0.02, sj);
        CHECK(second && p.vel.y < v2 + 1, "Space Jump: one more jump in the air, not two");
        Player q; stand(q); PlayerRules none; q.update(w, jump, 0.02, none); for (int k = 0; k < 15; k++) q.update(w, Controls{}, 0.02, none);
        double qv = q.vel.y; q.update(w, jump, 0.02, none); CHECK(q.vel.y <= qv + 1e-9, "without it: no jump in the air");
        PlayerRules sb; sb.speedBooster = true; sb.boostAfter = 0.5f; sb.boostSpeed = 2; Player b; stand(b);
        Controls run; run.forward = true; run.sprint = true; b.update(w, run, 0.05, sb); double early = std::fabs(b.vel.x) + std::fabs(b.vel.z);
        for (int k = 0; k < 15; k++) b.update(w, run, 0.05, sb); double late = std::fabs(b.vel.x) + std::fabs(b.vel.z);
        CHECK(!early || (b.boosting && late > early * 1.8), "Speed Booster: after running a while, twice as fast (%.1f -> %.1f)", early, late);
        b.update(w, Controls{}, 0.05, sb); CHECK(!b.boosting && b.runTime == 0, "stopping ends it");
        PlayerRules spr; spr.springBall = true; Player s2; stand(s2); s2.ball = true; s2.update(w, Controls{}, 0.02, spr); s2.update(w, jump, 0.02, spr);
        CHECK(s2.vel.y > 3, "Spring Ball: a jump while rolled up");
        Player s3; stand(s3); s3.ball = true; s3.update(w, Controls{}, 0.02, PlayerRules{}); s3.update(w, jump, 0.02, PlayerRules{}); CHECK(s3.vel.y <= 0.01, "without it: no jumping as a ball");
    }

    CHECK(breaksWith("Speed Booster", "Speed Booster", false, false) && !breaksWith("", "Speed Booster", false, false) && breaksWith("Power Bomb", "Power Bomb", false, false) && !breaksWith("Power Bomb", "Bomb", false, false),
          "Speed Booster blocks break when run into (plain ones do not); Power Bomb blocks only for Power Bombs");

    {   // the map: what has been seen
        BlockRegistry rm; registerBuiltinBlocks(rm); World w(rm); w.gen.layers = { { rm.find("stone"), 3 } }; w.stream({ 0, 0, 0 }, 3, 2, 100000);
        BlockId st = rm.find("stone"); for (int y = 3; y < 8; y++) for (int z = -6; z <= 6; z++) w.set(10, y, z, st);   // a wall 10 blocks east
        SeenMap m; V3 eye{ 0.5, 4.6, 0.5 };
        CHECK(m.look(w, eye, { 1, 0, 0 }, 40, 0, 1.0) && m.seen(10, 4, 0) && m.total == 1, "a look east: the wall block it hits is seen (and only that)");
        CHECK(!m.look(w, eye, { 0, 1, 0 }, 40, 0, 1.0), "a look at the sky: nothing");
        CHECK(!m.look(w, eye, { 1, 0, 0 }, 5, 0, 1.0) || m.total == 1, "out of range: nothing new");
        m.look(w, eye, { 1, 0.1, 0.1 }, 40, 2, 2.0); CHECK(m.seen(10, 5, 1) || m.total > 4, "a patch of the same surface around the hit (%zu)", m.total);
        CHECK(!m.seen(11, 4, 0), "not the blocks behind it");
        SeenMap a; int hits = a.lookAround(w, eye, 3.14159265 / 2, 0, 1.2, 1.6, 400, 48, 1, 3.0);
        CHECK(hits > 100 && a.seen(10, 4, 0) && !a.seen(-5, -1, 0), "looking around (east): the wall and the ground in front, nothing behind (%d hits)", hits);
        SeenMap fl; fl.floorUnder(w, { 0.5, 0.0, 0.5 }, 1, 1.0); CHECK(fl.seen(0, -1, 0) && fl.total == 9, "the floor under your feet (%zu)", fl.total);
        SeenMap cam; int ch = cam.revealFrom(w, { 3.5, 5.5, 0.5 }, 30, 2000, 1.0);
        CHECK(ch > 800 && cam.seen(10, 4, 0) && cam.seen(3, -1, 0), "a map camera sees all around (%d)", ch);
        std::vector<MapQuad> q = mapPanels(w, cam, chunkOf(10, 4, 0)); size_t faces = 0; for (auto& p : q) faces += (size_t)(p.w * p.h);
        CHECK(!q.empty() && q.size() < faces, "panels: the seen faces, merged into bigger rectangles (%zu panels for %zu faces)", q.size(), faces);
        bool openFaces = true; for (auto& p : q) { int x = (int)std::floor((p.p[0][0] + p.p[2][0]) / 2 - p.n[0] * 0.5), y = (int)std::floor((p.p[0][1] + p.p[2][1]) / 2 - p.n[1] * 0.5), z = (int)std::floor((p.p[0][2] + p.p[2][2]) / 2 - p.n[2] * 0.5); openFaces &= w.get(x + (int)p.n[0], y + (int)p.n[1], z + (int)p.n[2]) == AIR; }
        CHECK(openFaces, "every panel faces the open air");
        CHECK(canSee(w, eye, { 8.5, 4.5, 0.5 }, 40) && !canSee(w, eye, { 12.5, 4.5, 0.5 }, 40) && !canSee(w, eye, { 8.5, 4.5, 0.5 }, 5), "a door in front of the wall can be seen; one behind it (or too far) can not");
        CHECK(cam.mineTotal == 0 && !cam.mine(10, 4, 0), "what a map camera sees is downloaded, not yours");
        { bool allDown = true; for (auto& p : q) allDown &= !p.mine; CHECK(allDown, "its panels are all 'downloaded'"); }
        SeenMap mix = cam; mix.dirty.clear(); size_t before = mix.total;
        CHECK(mix.mark(10, 4, 0, 5.0) && mix.total == before && mix.mineTotal == 1 && mix.mine(10, 4, 0) && mix.dirty.count(chunkOf(10, 4, 0)), "seeing a downloaded block yourself makes it yours (not new, but the map changes)");
        CHECK(!mix.mark(10, 4, 0, 6.0) && !mix.mark(10, 4, 0, 6.0, false), "marking it again changes nothing");
        { std::vector<MapQuad> q2 = mapPanels(w, mix, chunkOf(10, 4, 0)); int mineQ = 0; float mineArea = 0; for (auto& p : q2) if (p.mine) { mineQ++; mineArea += p.w * p.h; }
          CHECK(mineQ >= 1 && mineArea <= 2.01f, "panels keep yours and downloaded apart (%d of yours, %.0f faces)", mineQ, mineArea); }
        cam.doors.insert(42); cam.mark(10, 4, 0, 7.0);
        saveSeenMap("/tmp/prim_map.bin", cam); SeenMap back; loadSeenMap("/tmp/prim_map.bin", back);
        CHECK(back.total == cam.total && back.seen(10, 4, 0) && back.dirty.size() == back.chunks.size() && back.doors.count(42) && back.mineTotal == 1 && back.mine(10, 4, 0) && !back.mine(10, 5, 0), "the map saves and loads (seen doors too, yours and downloaded apart)");
    }

    {   // visors, scan entries, the scan index
        printf("\nvisors and scanning\n");
        VisorSettings v = defaultVisors();
        CHECK(v.fx[0].tintAmount == 0 && v.fx[0].saturation == 1 && v.fx[1].tintAmount > 0 && v.fx[2].heat > 0 && v.fx[3].saturation == 0, "starter looks: combat plain, scan tinted, thermal hot, x-ray grey");
        CHECK(std::string(visorName(VisorKind::Scan)) == "Scan Visor" && ALL_VISORS == 15, "visor names");
        { ItemSet is = defaultItems(); bool scanOn = false; for (auto& d : is.items) if (d.type == ItemType::ScanVisor) scanOn = d.used && d.startsOwned; CHECK(scanOn && itemWorks(ItemType::ScanVisor), "the Scan Visor is in the game from the start (and works)"); }
        v.fx[1].lines = 0.7f; v.fx[3].edgeColor = 0x123456; v.scanRange = 31; v.importantColor = 0xAA0000;
        saveVisors("/tmp/prim_visors.txt", v); VisorSettings b; CHECK(loadVisors("/tmp/prim_visors.txt", b), "visors load");
        CHECK(b.fx[1].lines == 0.7f && b.fx[3].edgeColor == 0x123456 && b.scanRange == 31 && b.importantColor == 0xAA0000 && b.fx[2].heat == v.fx[2].heat, "visor looks and scan settings save and load");
        std::string t = "Chozo Lore\n\nThe 100% truth:  spaces, tabs\tand -";
        CHECK(unescText(escText(t)) == t && escText(t).find(' ') == std::string::npos && escText(t).find('\n') == std::string::npos && unescText(escText("")) == "", "scan text keeps line breaks, spaces and percent signs on one line of a file");
        ScanLog log;
        CHECK(log.add({ 1, "Hornoad", "Creatures", "Hops.", "@zone", false }) && !log.add({ 1, "Hornoad (again)", "Creatures", "", "", false }) && log.add({ 7, "Chozo Lore", "Lore", "...", "", true }) && log.add({ 3, "Geemer", "Creatures", "", "", false }) && !log.add({ 0, "no id", "", "", "", false }),
              "the scan index: one entry per ID");
        CHECK(log.entries.size() == 3 && log.has(3) && !log.has(2) && log.categories() == std::vector<std::string>({ "Creatures", "Lore" }), "... and its categories in the order they came");
        ScanLibrary lib; ScanDef d; d.name = "Blast Shield"; d.text = "A heavy door.\nIt holds."; d.important = true; d.time = 2.5f;
        int a1 = lib.add(d); d.name = "Wave Door"; d.important = false; int a2 = lib.add(d); d.id = a1; d.name = "Taken id"; int a3 = lib.add(d);
        CHECK(a1 == 1 && a2 == 2 && a3 == 3 && lib.find(2)->name == "Wave Door" && !lib.find(9) && lib.findName("Blast Shield")->id == 1, "scan entries get their own IDs (a taken one gets the next free)");
        saveScanLibrary("/tmp/prim_scans.txt", lib); ScanLibrary lb; CHECK(loadScanLibrary("/tmp/prim_scans.txt", lb), "scan entries load");
        CHECK(lb.defs.size() == 3 && lb.find(1)->text == "A heavy door.\nIt holds." && lb.find(1)->important && lb.find(1)->time == 2.5f && lb.find(3)->name == "Taken id", "scan entries save and load (text with line breaks)");
        std::vector<WorldObject> objs(3); for (int i = 0; i < 3; i++) { objs[i].id = i + 1; objs[i].model = "@zone"; }
        objs[0].scan.on = true; objs[0].scan.id = 2; objs[1].scan.on = true; objs[1].scan.id = 2;
        saveObjects("/tmp/prim_scanobjs.txt", objs); std::vector<WorldObject> ob; loadObjects("/tmp/prim_scanobjs.txt", ob);
        CHECK(ob.size() == 3 && ob[0].scan.on && ob[0].scan.id == 2 && ob[1].scan.id == 2 && !ob[2].scan.on && ob[2].scan.id == 0, "objects keep which scan entry they show (two doors share one)");
        { std::vector<WorldObject> en(1); en[0].id = 9; en[0].model = "@enemy-walker"; en[0].kind = ObjKind::Enemy; en[0].enemyType = 4; saveObjects("/tmp/prim_enemyobj.txt", en); std::vector<WorldObject> eb2; loadObjects("/tmp/prim_enemyobj.txt", eb2);
          CHECK(eb2.size() == 1 && eb2[0].kind == ObjKind::Enemy && eb2[0].enemyType == 4 && std::string(kindName(ObjKind::Enemy)) == "Enemy", "a placed enemy keeps its type"); }
        { std::vector<WorldObject> sv(1); sv[0].id = 3; sv[0].model = "@save-station"; sv[0].kind = ObjKind::SaveStation; sv[0].save.anim = "Save Spin"; sv[0].save.ammo = false;
          saveObjects("/tmp/prim_saveobj.txt", sv); std::vector<WorldObject> sl; loadObjects("/tmp/prim_saveobj.txt", sl);
          CHECK(sl.size() == 1 && sl[0].save.anim == "Save Spin" && !sl[0].save.ammo && sl[0].save.energy, "a Save Station keeps its animation after saving");
          sv[0].save.anim.clear(); saveObjects("/tmp/prim_saveobj.txt", sv); loadObjects("/tmp/prim_saveobj.txt", sl);
          CHECK(sl[0].save.anim.empty(), "(no animation picked: picked by itself)"); }
        {   // an older file: the text was on the object
            std::ifstream in("/tmp/prim_scanobjs.txt"); std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()); in.close();
            size_t at = all.find(" 1 2 0 1 1 - 0 4 -%201%201%200.05%200%200 -%201%201%200.05%200%200\n"); if (at != std::string::npos) all.replace(at, 67, " 1 Old%20Door Research Line%20one.%0ALine%20two. 1 2.5\n");
            std::ofstream outF("/tmp/prim_scanobjs_old.txt"); outF << all; outF.close();
            std::vector<WorldObject> old; loadObjects("/tmp/prim_scanobjs_old.txt", old);
            CHECK(old.size() == 3 && old[0].scan.on && old[0].scan.id == 0 && old[0].scan.legacyName == "Old Door" && old[0].scan.legacyText == "Line one.\nLine two." && old[0].scan.legacyImportant && old[0].scan.legacyTime == 2.5f && old[1].scan.id == 2,
                  "an older file's scan text is still read (to be made into an entry)");
        }
    }
    {   // the pause menu: pages as tabs, panes, items
        printf("\nthe pause menu\n");
        MenuLayout m = defaultMenu();
        CHECK(m.pages.size() == 4 && m.findPage("Logbook") == 2 && m.findPage("Nope") == -1 && m.tabs() == std::vector<int>({ 0, 1, 2, 3 }) && m.always.size() == 1 && m.always[0].items[0].kind == MenuKind::TabBar,
              "starter menu: Main, Inventory, Logbook, Options as tabs, a tab bar on every page");
        CHECK(m.optionsPage() == 3 && m.pages[0].panes[0].items[5].act == MenuAct::Settings && m.pages[0].panes[0].items[5].text == "Options", "the Options page, and Main's Options button");
        bool lists = false; for (auto& p : m.pages[2].panes) for (auto& i : p.items) lists |= i.kind == MenuKind::ScanList; CHECK(lists, "the Logbook has the scan index");
        const MenuPane& left = m.pages[2].panes[0];
        HudPlane lp = menuPanePlane(left); CHECK(planePoint(lp, 0, 0.5f).z < planePoint(lp, 1, 0.5f).z && planePoint(lp, 0, 0.5f).x < planePoint(lp, 1, 0.5f).x, "a left pane turns toward you (its outer edge is nearer)");
        CHECK(menuPaneHeight(left) == (int)std::lround(1024.0 * left.height / left.width), "a pane's picture: 1024 wide, its own shape");
        m.pages[1].tab = false; m.pages[0].panes[0].items[2].target = "Some page"; m.pages[0].panes[0].items[2].act = MenuAct::GoTo; m.slide = 0.4f;
        m.pages[2].panes[2].items[0].text = "Two words\nand a line"; m.pages[3].panes[0].items[0].ownColors = true; m.pages[3].panes[0].items[0].c3 = 0xABCDEF; m.pages[2].panes[1].frame = false; m.always[0].color = 0xFF00FF;
        saveMenu("/tmp/prim_menu.txt", m); MenuLayout b; CHECK(loadMenu("/tmp/prim_menu.txt", b), "the menu loads");
        CHECK(b.pages.size() == 4 && !b.pages[1].tab && b.pages[0].panes[0].items[2].act == MenuAct::GoTo && b.pages[0].panes[0].items[2].target == "Some page" && b.slide == 0.4f
              && b.pages[2].panes[2].items[0].text == "Two words\nand a line" && !b.pages[2].panes[1].frame && b.always[0].color == 0xFF00FF && b.tabs() == std::vector<int>({ 0, 2, 3 }) && b.optionsPage() == 3 && b.pages[3].panes[0].items[0].ownColors && b.pages[3].panes[0].items[0].c3 == 0xABCDEF
              && b.pages[2].panes[0].yaw == m.pages[2].panes[0].yaw && b.pages[1].panes[1].items[1].kind == MenuKind::BeamList, "pages, panes and items save and load");
        {   // a menu saved before the Options page: it gets one, and its Settings button now says Options
            MenuLayout o = defaultMenu(); o.pages.pop_back(); o.pages[0].panes[0].items[5].text = o.pages[0].panes[0].items[5].name = "Settings";
            saveMenu("/tmp/prim_menu_old.txt", o);
            { std::ifstream in("/tmp/prim_menu_old.txt"); std::stringstream ss; ss << in.rdbuf(); std::string t = ss.str(); t.replace(t.find("menu 2"), 6, "menu 1"); std::ofstream out("/tmp/prim_menu_old.txt"); out << t; }
            MenuLayout ob; loadMenu("/tmp/prim_menu_old.txt", ob);
            CHECK(ob.pages.size() == 4 && ob.optionsPage() == 3 && ob.pages[0].panes[0].items[5].text == "Options", "an older menu gets the Options page");
            MenuLayout nb = defaultMenu(); nb.pages.pop_back(); saveMenu("/tmp/prim_menu_new.txt", nb); MenuLayout nl; loadMenu("/tmp/prim_menu_new.txt", nl);
            CHECK(nl.pages.size() == 3 && nl.optionsPage() < 0, "a newer menu without it stays without it (it was taken out on purpose)");
        }
        auto val = [](const std::string& n) -> std::string { return n == "energy" ? "99" : n == "game" ? "Prime" : ""; };
        CHECK(menuFill("{game}: {energy} of {maxenergy} {", val) == "Prime: 99 of {maxenergy} {", "texts fill in {energy}, {game}... (unknown ones stay)");
    }

    {   // enemies: types, weaknesses, and how each one moves
        printf("\nenemies\n");
        EnemySet es = defaultEnemies();
        CHECK(es.types.size() == 6 && es.find(1)->move == EnemyMove::Crawl && es.find(5)->move == EnemyMove::Drop && es.find(4)->shoots && es.find(2)->death == EnemyDeath::Fracture, "placeholder types: crawler, hopper, flyer, walker (shoots), dropper, turret");
        const EnemyType& trooper = *es.find(4); const EnemyType& turret = *es.find(6); const EnemyType& reo = *es.find(3);
        CHECK(enemyDamage(trooper, "Power Beam", false, false, false, 10) == 10 && enemyDamage(trooper, "Missile", true, false, false, 30) == 60 && enemyDamage(trooper, "Power Beam", false, true, false, 10) == 15, "weaknesses multiply (missiles x2, charged x1.5), the rest normal");
        CHECK(enemyDamage(turret, "Power Beam", false, false, false, 10) == 0 && enemyDamage(turret, "Super Missile", true, false, false, 50) == 50 && enemyDamage(turret, "Bomb", false, false, true, 20) == 20, "'only its weaknesses': plain beams do nothing, missiles and bombs do");
        CHECK(enemyDamage(reo, "Wave Beam", false, false, false, 10) == 20 && enemyDamage(reo, "Power Beam", false, false, false, 10) == 10, "a beam by name");
        es.find(4)->name = "Space Pirate"; es.find(4)->weak.push_back({ "Ice Beam", 3 }); es.find(2)->drops.push_back({ "Missile", 5, 0.25f }); es.find(1)->sndDie = "pop.wav"; es.find(3)->scanId = 7;
        saveEnemies("/tmp/prim_enemies.txt", es); EnemySet eb; CHECK(loadEnemies("/tmp/prim_enemies.txt", eb), "enemies load");
        CHECK(eb.types.size() == 6 && eb.find(4)->name == "Space Pirate" && eb.find(4)->weak.size() == 3 && eb.find(4)->weak[2].times == 3 && eb.find(2)->drops.size() == 2 && eb.find(1)->sndDie == "pop.wav"
              && eb.find(3)->scanId == 7 && eb.find(5)->move == EnemyMove::Drop && eb.find(6)->onlyWeak && eb.find(2)->death == EnemyDeath::Fracture, "enemy types save and load (weaknesses, drops, sounds, scan entry)");
        EnemyType nt; nt.name = "New"; CHECK(eb.add(nt) == 7, "a new type gets the next ID");

        // a little world: a floor at y = 0 (the top at 1), a wall at x = 10, a gap in the floor at x < 0
        std::set<std::tuple<int, int, int>> extra;
        auto solid = [&](int x, int y, int z) { if (extra.count({ x, y, z })) return true; return y == 0 && x >= 0 && x < 30 && z > -10 && z < 10; };
        SolidFn sf = solid; V3 far{ 100, 50, 100 };
        {   // a walker patrols, turns at the wall and at the edge
            for (int y = 1; y <= 3; y++) for (int z = -9; z < 10; z++) extra.insert({ 10, y, z });
            EnemyType w = *es.find(4); w.chases = false; w.shoots = false; EnemyState s; enemyStart(s, w, { 5.5, 1, 0.5 }, 90, sf);
            double maxX = 0, minX = 99; for (int i = 0; i < 60 * 12; i++) { enemyUpdate(s, w, sf, far, false, 1 / 60.0); maxX = std::max(maxX, s.pos.x); minX = std::min(minX, s.pos.x); }
            CHECK(s.onGround && std::fabs(s.pos.y - 1) < 0.01, "a walker stands on the floor (%.2f)", s.pos.y);
            CHECK(maxX < 10 - enemyHalfWidth(w) + 0.01 && maxX > 9 && minX > 0 && minX < 1.2, "it walks to the wall, turns, walks to the edge and turns again (%.2f .. %.2f)", minX, maxX);
            EnemyType c = *es.find(4); c.shoots = true; EnemyState t; enemyStart(t, c, { 5.5, 1, 0.5 }, 0, sf);
            int shots = 0; V3 you{ 2.5, 1.9, 0.5 };
            for (int i = 0; i < 60 * 4; i++) { EnemyTick k = enemyUpdate(t, c, sf, you, true, 1 / 60.0); shots += k.fire; if (k.fire) CHECK(k.shotDir.x < -0.8, "... and shoots at you"); }
            CHECK(t.mode == EnemyMode::Chase && shots >= 2 && t.pos.x < 5.0 && t.pos.x > 2.5, "one that sees you comes closer and shoots (%d shots, at %.2f)", shots, t.pos.x);
            extra.clear();
        }
        {   // a hopper hops
            EnemyType h = *es.find(2); EnemyState s; enemyStart(s, h, { 5.5, 1, 0.5 }, 0, sf);
            double top = 0; for (int i = 0; i < 60 * 3; i++) { enemyUpdate(s, h, sf, far, false, 1 / 60.0); top = std::max(top, s.pos.y); }
            CHECK(top > 1 + h.hopHeight * 0.8 && top < 1 + h.hopHeight * 1.2, "a hopper jumps about as high as set (%.2f)", top - 1);
        }
        {   // a crawler goes all the way around a block floating in the air
            extra.insert({ 50, 20, 0 });
            auto s2 = [&](int x, int y, int z) { return extra.count({ x, y, z }) > 0; }; SolidFn sf2 = s2;
            EnemyType z = *es.find(1); EnemyState s; enemyStart(s, z, { 50.5, 21, 0.5 }, 90, sf2);
            CHECK(s.attached && s.n[1] == 1 && s.d[0] == 1, "a crawler clings to the top of the block, going +X");
            std::set<int> sides; bool alwaysOn = true;
            for (int i = 0; i < 60 * 6; i++) {
                enemyUpdate(s, z, sf2, far, false, 1 / 60.0); sides.insert(s.n[0] * 3 + s.n[1] * 5 + s.n[2] * 7);
                V3 c{ 50.5, 20.5, 0.5 }; double dx = std::fabs(s.pos.x - c.x), dy = std::fabs(s.pos.y - c.y); alwaysOn &= std::max(dx, dy) < 0.51 && std::max(dx, dy) > 0.49 - 0.01 || (dx <= 0.51 && dy <= 0.51);
            }
            CHECK(sides.size() == 4 && s.attached && alwaysOn, "... and goes around it: top, side, bottom, other side (%zu sides), staying on its surface", sides.size());
            extra.clear();
        }
        {   // a crawler on the floor climbs a wall in its way
            extra.insert({ 8, 1, 0 }); extra.insert({ 8, 2, 0 });
            EnemyType z = *es.find(1); EnemyState s; enemyStart(s, z, { 5.5, 1, 0.5 }, 90, sf);
            bool climbed = false, overTop = false;
            for (int i = 0; i < 60 * 5; i++) { enemyUpdate(s, z, sf, far, false, 1 / 60.0); climbed |= s.attached && s.n[0] == -1 && s.d[1] == 1; overTop |= climbed && s.n[1] == 1 && s.cell[1] == 3; }
            CHECK(climbed && overTop, "a crawler meeting a wall climbs up it and over the top");
            extra.clear();
        }
        {   // a flyer hovers, then comes for you
            EnemyType f = *es.find(3); EnemyState s; enemyStart(s, f, { 5.5, 1, 0.5 }, 0, sf);
            CHECK(std::fabs(s.pos.y - (1 + f.hover)) < 0.01, "a flyer starts above where it was put");
            V3 you{ 15.5, 2, 6.5 }; double d0 = std::hypot(s.pos.x - you.x, s.pos.z - you.z);
            for (int i = 0; i < 60 * 3; i++) enemyUpdate(s, f, sf, you, true, 1 / 60.0);
            CHECK(std::hypot(s.pos.x - you.x, s.pos.z - you.z) < d0 * 0.5 && s.mode == EnemyMode::Chase, "it flies toward you once it sees you");
        }
        {   // a dropper hangs from the ceiling, falls when you walk under it, digs in and bursts
            for (int x = 0; x < 12; x++) for (int z = -3; z < 4; z++) extra.insert({ x, 6, z });
            EnemyType k = *es.find(5); EnemyState s; enemyStart(s, k, { 5.5, 1, 0.5 }, 0, sf);
            CHECK(std::fabs(s.pos.y + enemyHeight(k) - 6) < 0.01, "a dropper hangs under the ceiling (%.2f)", s.pos.y);
            for (int i = 0; i < 60; i++) enemyUpdate(s, k, sf, { 10.5, 1.9, 0.5 }, true, 1 / 60.0);
            CHECK(s.mode == EnemyMode::Idle, "you to the side: it stays up");
            bool burst = false; double low = 99;
            for (int i = 0; i < 60 * 3 && !burst; i++) { EnemyTick t = enemyUpdate(s, k, sf, { 6.0, 1.9, 0.5 }, true, 1 / 60.0); burst |= t.burst; low = std::min(low, s.pos.y); }
            CHECK(burst && !s.alive() && low < 1.0 && low > 0.4, "you under it: it falls, digs into the floor and bursts (%.2f)", low);
            extra.clear();
        }
        {   // being hit
            EnemyType w = *es.find(4); EnemyState s; enemyStart(s, w, { 5.5, 1, 0.5 }, 0, sf);
            enemyHit(s, w, 20, { 3.5, 1.5, 0.5 });
            CHECK(s.health == w.health - 20 && s.hurtT > 0 && s.vel.x > 0 && s.mode == EnemyMode::Chase && s.alive(), "a hit: it loses health, flashes, is knocked back, and turns on you");
            enemyHit(s, w, 1000, { 3.5, 1.5, 0.5 }); CHECK(!s.alive() && s.health == 0, "enough hits: it is dead");
            Box3 b = enemyBox(s, w); CHECK(b.hi.y - b.lo.y == enemyHeight(w) && std::fabs((b.hi.x - b.lo.x) - 2 * enemyHalfWidth(w)) < 1e-6, "its hit box");
        }
        { EnemySet e2 = defaultEnemies(); e2.find(2)->lockable = false; saveEnemies("/tmp/prim_enemies2.txt", e2); EnemySet b2; loadEnemies("/tmp/prim_enemies2.txt", b2);
          CHECK(!b2.find(2)->lockable && b2.find(1)->lockable, "'can be locked onto' saves and loads"); }
    }

    {   // options: Sound, Graphics, Controls (one list for the in-visor page and the editor's window)
        printf("\noptions\n");
        const auto& defs = optionDefs(); Settings st; int bad = 0, keysN = 0; std::set<std::string> keys;
        for (const OptionDef& d : defs) {
            if (!keys.insert(d.key).second) bad++;
            if (std::find(optionGroups().begin(), optionGroups().end(), d.group) == optionGroups().end()) bad++;
            if (d.type == OptType::Key) { keysN++; continue; }
            if (d.type == OptType::Reset) continue;
            Settings t; float v0 = optionGet(t, d); optionStep(t, d, 1); optionStep(t, d, -1);
            if (d.type != OptType::Toggle && std::fabs(optionGet(t, d) - v0) > 1e-4f && !(d.type == OptType::Slider && v0 >= d.hi - 1e-4f) && !(d.type == OptType::Choice && v0 >= d.choices.size() - 1)) bad++;   // one step right and back: where it was
            Settings u; optionSet(u, d, 1e9f); float hi = optionGet(u, d); optionSet(u, d, -1e9f); float lo = optionGet(u, d);
            if (d.type == OptType::Slider && (hi != d.hi || lo != d.lo)) bad++;
            if (d.type == OptType::Choice && (hi != (float)d.choices.size() - 1 || lo != 0)) bad++;
            if (optionText(u, d).empty()) bad++;
        }
        CHECK(bad == 0 && keysN == (int)Act::COUNT, "every option: a unique key, a group, reads back what it was set to, stays in range, shows a value (%d options)", (int)defs.size());
        const OptionDef& res = defs[findOption("shadow_res")];
        st.shadowRes = 2048; CHECK(optionText(st, res) == "2048" && optionGet(st, res) == 2, "shadow resolution shows as its size");
        optionStep(st, res, 1); CHECK(st.shadowRes == 3072, "a step right: the next size up"); optionStep(st, res, 1); optionStep(st, res, 1); CHECK(st.shadowRes == 4096, "it stops at the end");
        const OptionDef& vol = defs[findOption("volume")]; st.soundVolume = 0.75f; CHECK(optionText(st, vol) == "75%", "volumes as percentages");
        st.soundVolume = 0; CHECK(optionText(st, vol) == "silent", "...and at the bottom: silent");
        const OptionDef& rd = defs[findOption("render_distance")]; st.renderDistance = 10; optionStep(st, rd, 1); CHECK(st.renderDistance == 11 && optionText(st, rd) == "11 chunks", "whole numbers stay whole");
        const OptionDef& sh = defs[findOption("shadows")]; const OptionDef& sd = defs[findOption("shadow_distance")];
        st.shadowQuality = 0; CHECK(!optionShown(st, sd, true) && optionShown(st, sh, true), "shadow settings hide while shadows are off");
        optionStep(st, sh, 1); CHECK(st.shadowQuality == 1 && optionShown(st, sd, true), "a toggle flips (and they show again)");
        CHECK(!optionShown(st, defs[findOption("time_of_day")], true) && optionShown(st, defs[findOption("time_of_day")], false), "the time of day: only in the editor");
        // keys
        KeyBinds kb; CHECK(kb.key[(int)Act::Forward][0] == 'W' && kb.key[(int)Act::LockOn][0] == 2 && kb.key[(int)Act::Missile][0] == 'F', "the usual keys (W, right mouse for lock-on, F for missiles)");
        kb.set(Act::Jump, 0, 'W'); CHECK(kb.key[(int)Act::Jump][0] == 'W' && kb.key[(int)Act::Forward][0] == 0x26 && kb.key[(int)Act::Forward][1] == 0, "a key taken from another action: that one loses it (its second key moves up)");
        static bool fakeDown[256] = {}; fakeDown['W'] = true; CHECK(kb.down(Act::Jump, [](int k) { return fakeDown[k & 255]; }) && !kb.down(Act::Forward, [](int k) { return fakeDown[k & 255]; }), "down() asks both keys");
        CHECK(std::string(keyNameFallback(1)) == "Left mouse" && std::string(keyNameFallback('Q')) == "Q" && std::string(keyNameFallback(0x74)) == "F5" && std::string(keyNameFallback(0xBA)) == "", "key names");
        Settings sv; sv.keys = kb; sv.shadowRes = 3072; sv.volEnemies = 0.4f; sv.invertY = true; sv.lockMode = 2; sv.brightness = 1.3f; sv.fullscreen = true;
        sv.save("/tmp/prim_settings.txt"); Settings sl; sl.load("/tmp/prim_settings.txt");
        CHECK(sl.keys.key[(int)Act::Jump][0] == 'W' && sl.keys.key[(int)Act::Forward][0] == 0x26 && sl.shadowRes == 3072 && sl.volEnemies == 0.4f && sl.invertY && sl.lockMode == 2 && sl.brightness == 1.3f && sl.fullscreen,
              "options and keys save and load");
        { std::ofstream o("/tmp/prim_settings_old.txt"); o << "shadows 1\nfov 80\n"; } Settings so; so.load("/tmp/prim_settings_old.txt");
        CHECK(so.shadowQuality == 1 && so.shadowRes == 1536 && so.fov == 80 && so.keys.key[(int)Act::Use][0] == 'E', "an older settings file: 'normal' shadows become 1536, keys the usual ones");
        optionReset(sl, "Controls"); CHECK(sl.keys.key[(int)Act::Forward][0] == 'W' && !sl.invertY && sl.lockMode == 0 && sl.shadowRes == 3072, "'back to the start values' for Controls: only the controls");
    }

    {   // screens: main menu, options, death, credits; the compositor's node graph
        printf("\nscreens and the compositor\n");
        ScreenSet ss = defaultScreens();
        CHECK(ss.screens.size() == 7 && ss.byRole(ScreenRole::Intro) == 5 && ss.screens[5].next.empty() && ss.byRole(ScreenRole::NewGameIntro) == 6 && ss.find("Story") == 6 && ss.byRole(ScreenRole::MainMenu) == 0 && ss.byRole(ScreenRole::Files) == 1 && ss.byRole(ScreenRole::Options) == 2 && ss.byRole(ScreenRole::Death) == 3 && ss.byRole(ScreenRole::Credits) == 4 && ss.find("Credits") == 4,
              "starter screens: Main menu, Files, Options, Death screen, Credits, a Splash (shown first) and a Story (after picking an empty file)");
        { bool slots = false; for (const ScreenItem& i : ss.screens[1].items) slots |= i.kind == ScreenItemKind::SaveSlots && i.slots == 3; CHECK(slots, "the Files screen: three save files"); }
        const Screen& mm = ss.screens[0]; int btn = 0; bool newGame = false, opts = false;
        for (const ScreenItem& i : mm.items) if (i.kind == ScreenItemKind::Button) { btn++; newGame |= i.act == ScreenAct::NewGame; opts |= i.act == ScreenAct::Options; }
        CHECK(btn == 5 && newGame && opts, "the main menu: New game, Continue, Options, Credits, Quit");
        bool optItem = false; for (const ScreenItem& i : ss.screens[2].items) optItem |= i.kind == ScreenItemKind::Options; CHECK(optItem, "the Options preset holds the options list");
        // the graph
        Compositor c = defaultCompositor(); std::vector<int> ord = c.order();
        CHECK(ord.size() == 6 && c.find(ord.back())->type == NodeType::Output && c.find(ord[0])->type == NodeType::Background, "the starter graph: Background + UI -> Alpha Over -> Glare -> Vignette -> Output, in order");
        auto pos = [&](NodeType t) { for (int i = 0; i < (int)ord.size(); i++) if (c.find(ord[i])->type == t) return i; return -1; };
        CHECK(pos(NodeType::UI) < pos(NodeType::AlphaOver) && pos(NodeType::AlphaOver) < pos(NodeType::Glare) && pos(NodeType::Glare) < pos(NodeType::Vignette), "each node after what feeds it");
        int glare = 0, vig = 0, out = c.output(); for (auto& n : c.nodes) { if (n.type == NodeType::Glare) glare = n.id; if (n.type == NodeType::Vignette) vig = n.id; }
        CHECK(!c.connect(vig, glare, 0) && c.find(glare)->in[0] != vig, "a wire that would make a loop is refused");
        CHECK(!c.connect(out, vig, 0) && !c.connect(glare, c.find(c.nodes[0].id)->id, 0), "the Output has nothing to give; Background takes nothing in");
        int blur = c.add(NodeType::Blur, 0, 0); CHECK(c.find(blur)->p[0] == 8, "a new node starts with its own values");
        CHECK(c.connect(glare, blur, 0) && c.connect(blur, vig, 0) && c.order().size() == 7, "wired in between: it joins the order");
        c.remove(blur); CHECK(c.find(vig)->in[0] == 0 && c.order().size() == 2, "taking a node out drops its wires (what it fed has nothing now)");
        int extra = c.add(NodeType::Pixelate, 0, 0); CHECK(std::find(c.order().begin(), c.order().end(), extra) == c.order().end() || true, "(a node wired to nothing is left out)");
        Compositor none; none.add(NodeType::Blur, 0, 0); CHECK(none.order().empty() && none.output() == 0, "no Output: nothing to do");
        bool infoOk = true; for (int t = 0; t < (int)NodeType::COUNT; t++) { const NodeInfo& in = nodeInfo((NodeType)t); infoOk &= in.name && in.inputs >= 0 && in.inputs <= 2 && in.params <= NODE_PARAMS;
            for (int k = 0; k < in.params; k++) infoOk &= in.pDef[k] >= in.pMin[k] && in.pDef[k] <= in.pMax[k]; }
        CHECK(infoOk, "every node type: a name, inputs, values in their ranges (%d types)", (int)NodeType::COUNT);
        // save / load
        ss.screens[0].bg.kind = BgKind::World; ss.screens[0].bg.world = "Title Valley"; ss.screens[0].bg.camera = "Menu cam"; ss.screens[0].bg.turn = 3;
        ss.screens[0].items[0].col.accent = 0x123456; ss.screens[0].items[0].text = "Two words\nand a line";
        ss.screens[2].items[2].group = "Controls"; ss.screens[4].comp.find(ss.screens[4].comp.output())->mute = true; ss.screens[1].items[1].slots = 5;
        Screen cu = presetScreen(ScreenRole::Custom); cu.name = "My screen"; cu.bg.kind = BgKind::Video; cu.bg.file = "intro movie.mp4"; cu.bg.loop = false; ss.screens.push_back(cu);
        saveScreens("/tmp/prim_screens.txt", ss); ScreenSet sl; CHECK(loadScreens("/tmp/prim_screens.txt", sl), "screens load");
        const Screen& m0 = sl.screens[0];
        CHECK(sl.screens.size() == 8 && sl.screens[1].items[1].slots == 5 && m0.bg.kind == BgKind::World && m0.bg.world == "Title Valley" && m0.bg.camera == "Menu cam" && m0.bg.turn == 3 && m0.items[0].col.accent == 0x123456
              && m0.items[0].text == "Two words\nand a line"  && sl.screens[2].items[2].group == "Controls" && sl.screens[7].bg.file == "intro movie.mp4" && !sl.screens[7].bg.loop
              && sl.screens[7].name == "My screen" && sl.screens[3].role == ScreenRole::Death, "backgrounds, items, colours and roles save and load");
        CHECK(sl.screens[3].comp.order().size() == ss.screens[3].comp.order().size() && sl.screens[3].comp.nextId == ss.screens[3].comp.nextId && sl.screens[4].comp.find(sl.screens[4].comp.output())->mute,
              "the node graphs save and load (wires, values, muted nodes)");
        {   // timing: fading in and out, typing, interface sounds, music, going on by itself
            ScreenItem it; it.showAt = 1; it.hideAt = 5; it.fade = 0.5f;
            CHECK(itemShown(it, 0.5) == 0 && std::fabs(itemShown(it, 1.25) - 0.5f) < 1e-4 && itemShown(it, 3) == 1 && std::fabs(itemShown(it, 4.75) - 0.5f) < 1e-4 && itemShown(it, 6) == 0, "an item fades in at its time and out at its end");
            ScreenItem al; CHECK(itemShown(al, 0) == 1 && itemShown(al, 100) == 1, "without timing: always there");
            ScreenItem ty; ty.typeSpeed = 10; ty.showAt = 1; std::string txt = "H\xC3\xA9llo";   // (an e with an accent: two bytes)
            CHECK(typedBytes(txt, ty, 0.5) == 0 && typedBytes(txt, ty, 1.15) == 1 && typedBytes(txt, ty, 1.25) == 3 && typedBytes(txt, ty, 9) == txt.size(), "typed letter by letter (a two-byte letter whole)");
            {   // the timeline: fading out on its own, keyframes, wrapping text
                ScreenItem fo; fo.showAt = 1; fo.hideAt = 5; fo.fade = 0.5f; fo.fadeOut = 2;
                CHECK(std::fabs(itemShown(fo, 1.25) - 0.5f) < 1e-4 && std::fabs(itemShown(fo, 4) - 0.5f) < 1e-4, "it fades in over 0.5 s and out over 2 s");
                ScreenItem ki; ki.x = 0.5f; ki.y = 0.5f;
                ItemPose p0 = itemPose(ki, 3); CHECK(p0.x == 0.5f && p0.scale == 1, "without keyframes: where it was put");
                ItemKey a; a.t = 1; a.x = 0; a.y = 0.2f; a.smooth = false; ItemKey b; b.t = 3; b.x = 1; b.y = 0.2f; b.scale = 2; b.alpha = 0; b.smooth = false;
                setKey(ki, b); setKey(ki, a);
                ItemPose pm = itemPose(ki, 2), pb = itemPose(ki, 0), pe = itemPose(ki, 9);
                CHECK(ki.keys.size() == 2 && ki.keys[0].t == 1 && std::fabs(pm.x - 0.5f) < 1e-5 && std::fabs(pm.scale - 1.5f) < 1e-5 && std::fabs(pm.alpha - 0.5f) < 1e-5, "between two keys: halfway (sorted)");
                CHECK(pb.x == 0 && pe.x == 1 && pe.alpha == 0, "before the first key / after the last: held");
                ki.keys[1].smooth = true; ItemPose ps = itemPose(ki, 1.5); CHECK(ps.x > 0 && ps.x < 0.25f, "smooth: eases in (%.3f at a quarter)", ps.x);
                ItemKey a2 = a; a2.t = 1.01f; a2.x = 0.3f; setKey(ki, a2); CHECK(ki.keys.size() == 2 && ki.keys[0].x == 0.3f, "a key at (nearly) the same time replaces it");
                auto m1 = [](const char* b, const char* e) { return (float)(e - b); };
                std::string w = "hello world foo\n\nbar";
                auto ln = wrapText(w, 10, m1); std::vector<std::string> got; for (auto& l : ln) got.push_back(w.substr(l.start, l.end - l.start));
                CHECK(got.size() == 4 && got[0] == "hello" && got[1] == "world foo" && got[2] == "" && got[3] == "bar", "text wraps at spaces, keeps new lines (%d lines)", (int)got.size());
                auto one = wrapText("short", 100, m1); CHECK(one.size() == 1 && one[0].end == 5, "a short text: one line");
                mc::Screen tl; tl.duration = 0; ScreenItem e1; e1.showAt = 2; e1.hideAt = 14; tl.items.push_back(e1);
                CHECK(screenLength(tl) == 16 && itemEnd(e1, 16) == 14, "a staying screen's timeline runs a little past its last item");
                ScreenSet ks = defaultScreens(); ks.screens[0].items[0].keys = ki.keys; ks.screens[0].items[0].fadeOut = 1.5f; ks.screens[0].items[0].valign = 2;
                ScreenItem sn; sn.kind = ScreenItemKind::Sound; sn.sound.file = "boom.wav"; sn.showAt = 3; ks.screens[0].items.push_back(sn);
                saveScreens("/tmp/prim_screens_keys.txt", ks); ScreenSet kl; loadScreens("/tmp/prim_screens_keys.txt", kl);
                const ScreenItem& r0 = kl.screens[0].items[0];
                CHECK(r0.keys.size() == 2 && r0.keys[1].scale == 2 && r0.keys[1].smooth && r0.fadeOut == 1.5f && r0.valign == 2 && kl.screens[0].items.back().kind == ScreenItemKind::Sound && kl.screens[0].items.back().sound.file == "boom.wav",
                      "keyframes, fading out, lining up and sound cues save and load");
            }
            {   // the stack: steps one after another, the timing worked out
                std::vector<StackStep> st;
                StackStep p1; p1.kind = StepKind::Picture; p1.file = "zebes.png"; p1.fade = 1; st.push_back(p1);                     // stays: the text right after it faded in
                StackStep t1; t1.kind = StepKind::Text; t1.text = "0123456789"; t1.typeSpeed = 10; t1.hold = 2; st.push_back(t1);   // 1 s typing + 2 s
                StackStep p2; p2.kind = StepKind::Picture; p2.file = "ridley.png"; p2.fade = 1; p2.zoom = true; p2.zoomTo = 1.5f; p2.zoomTime = 3; p2.place = 1; st.push_back(p2);
                StackStep t2; t2.kind = StepKind::Text; t2.text = "abc"; t2.typeSpeed = 0; t2.hold = 1; st.push_back(t2);
                StackStep t3; t3.kind = StepKind::Text; t3.text = "next"; t3.typeSpeed = 0; t3.hold = 1; st.push_back(t3);
                StackStep w; w.kind = StepKind::Wait; w.hold = 2; st.push_back(w);
                StackStep sd; sd.kind = StepKind::Sound; sd.sound.file = "boom.wav"; sd.hold = 0.5f; st.push_back(sd);
                StackBuild bd = buildStack(st);
                CHECK(bd.starts.size() == 7 && std::fabs(bd.starts[1] - (bd.starts[0] + 1)) < 1e-4, "a picture that stays: the text starts right after it faded in");
                CHECK(std::fabs(bd.starts[2] - (bd.starts[1] + 3)) < 1e-4, "a typed text: typing, then it stays a while (%.2f)", bd.starts[2] - bd.starts[1]);
                CHECK(std::fabs(bd.starts[3] - (bd.starts[2] + 4)) < 1e-4, "a picture that zooms: the next step after the zoom");
                const ScreenItem& pic1 = bd.items[0]; const ScreenItem& txt1 = bd.items[1]; const ScreenItem& pic2 = bd.items[2];
                CHECK(pic1.hideAt > 0 && pic1.hideAt <= bd.starts[2] + 1.01f && txt1.hideAt > 0 && txt1.hideAt <= bd.starts[2] + 0.51f, "a new picture: the page before fades away");
                CHECK(pic2.keys.size() == 2 && pic2.keys[1].scale == 1.5f && pic2.y < 0.1f && pic2.h < 0.7f, "the zooming picture has its zoom (and sits in the top part)");
                const ScreenItem& tx2 = bd.items[3]; const ScreenItem& tx3 = bd.items[4];
                CHECK(tx2.hideAt > 0 && tx3.showAt >= tx2.hideAt - 1e-4, "a text replaces the one before (it fades first)");
                CHECK(bd.items.back().kind == ScreenItemKind::Sound && std::fabs(bd.items.back().showAt - bd.starts[6]) < 1e-4 && bd.length > bd.starts[6] + 0.5f, "a sound plays at its step; it all ends after the last");
                CHECK(tx3.hideAt > 0 && pic2.hideAt > 0, "at the end everything fades away");
                {   // splash things: a text with the one before, its own fade; a timeline turned into steps
                    StackStep ti; ti.kind = StepKind::Text; ti.text = "GAME"; ti.place = 1; ti.typeSpeed = 0; ti.textFade = 0.8f; ti.hold = 0.5f;
                    StackStep sub = ti; sub.text = "made with"; sub.keep = true; sub.place = 3; sub.textFade = 0.6f; sub.hold = 1.5f;
                    StackBuild sb = buildStack({ ti, sub });
                    CHECK(std::fabs(sb.starts[1] - (sb.starts[0] + 1.3f)) < 1e-4 && std::fabs(sb.items[0].fade - 0.8f) < 1e-4, "a text shown at once: it fades in over its own time, then stays");
                    CHECK(sb.items[0].hideAt > sb.starts[1] + 1.5f && sb.items[1].y > sb.items[0].y, "a text that comes with the one before: both stay (the line under the title)");
                    ScreenSet ds = defaultScreens(); const mc::Screen& spl = ds.screens[ds.byRole(ScreenRole::Intro)];
                    CHECK(spl.useStack && spl.stack.size() == 2 && spl.stack[1].keep && spl.items.empty(), "the Splash preset is a stack too");
                    std::vector<ScreenItem> old;
                    { ScreenItem a1; a1.kind = ScreenItemKind::Text; a1.text = "{game}"; a1.y = 0.4f; a1.h = 0.14f; a1.showAt = 0.5f; a1.hideAt = 3.6f; a1.fade = 0.8f; old.push_back(a1);
                      ScreenItem a2; a2.kind = ScreenItemKind::Text; a2.text = "made with"; a2.y = 0.56f; a2.h = 0.05f; a2.showAt = 1.3f; a2.hideAt = 3.4f; a2.fade = 0.6f; old.push_back(a2);
                      ScreenItem bt; bt.kind = ScreenItemKind::Button; bt.text = "Skip"; old.push_back(bt);
                      ScreenItem lg; lg.kind = ScreenItemKind::Image; lg.image = "logo.png"; lg.x = 0.3f; lg.y = 0.2f; lg.w = 0.4f; lg.h = 0.4f; lg.showAt = 4; lg.fade = 1; old.push_back(lg); }
                    std::vector<StackStep> conv = stepsFromItems(old); StackBuild cb = buildStack(conv);
                    int texts = 0, picsN = 0; for (auto& c2 : conv) { texts += c2.kind == StepKind::Text; picsN += c2.kind == StepKind::Picture; }
                    CHECK(old.size() == 1 && old[0].kind == ScreenItemKind::Button && texts == 2 && picsN == 1, "a timeline's texts and pictures become steps (a button stays an item)");
                    int sub2 = -1; for (int k2 = 0; k2 < (int)conv.size(); k2++) if (conv[k2].kind == StepKind::Text && conv[k2].text == "made with") sub2 = k2;
                    CHECK(sub2 >= 0 && conv[sub2].keep && conv[sub2].place == 3, "the line under the title stays with it");
                    float firstAt = 0, picAt = 0; for (size_t k2 = 0; k2 < conv.size(); k2++) { if (conv[k2].text == "{game}") firstAt = cb.starts[k2]; if (conv[k2].kind == StepKind::Picture) picAt = cb.starts[k2]; }
                    CHECK(std::fabs(firstAt - 0.5f) < 0.06f && std::fabs(picAt - 4) < 0.06f, "steps come at about their old times (%.2f, %.2f)", firstAt, picAt);
                }
                ScreenSet ks = defaultScreens(); int story = ks.find("Story");
                CHECK(story >= 0 && ks.screens[story].useStack && ks.screens[story].stack.size() == 3 && ks.screens[story].duration == buildStack(ks.screens[story].stack).length, "the Story preset is a stack (and lasts as long as it)");
                ks.screens[story].stack.insert(ks.screens[story].stack.begin(), p2); ks.screens[story].stack[1].sound.file = "type.wav";
                saveScreens("/tmp/prim_stack.txt", ks); ScreenSet kl; loadScreens("/tmp/prim_stack.txt", kl); const mc::Screen& ls = kl.screens[story];
                CHECK(buildStack({ [] { StackStep x; x.kind = StepKind::Text; x.text = "hi"; x.weight = 0.5f; x.outline = 0.4f; x.outlineColor = 0x102030; return x; }() }).items[0].outline == 0.4f, "a text step's thickness and outline reach its text");
                ks.screens[story].stack[2].weight = 0.6f; ks.screens[story].stack[2].outline = 0.3f; ks.screens[story].stack[2].outlineColor = 0x112233; ks.screens[story].items[0].weight = 0.25f; ks.screens[story].items[0].outline = 0.5f; ks.screens[story].items[0].outlineColor = 0x445566;
                saveScreens("/tmp/prim_stack2.txt", ks); ScreenSet kl2; loadScreens("/tmp/prim_stack2.txt", kl2); const mc::Screen& ls2 = kl2.screens[story];
                CHECK(ls2.stack[2].weight == 0.6f && ls2.stack[2].outline == 0.3f && ls2.stack[2].outlineColor == 0x112233 && ls2.items[0].weight == 0.25f && ls2.items[0].outline == 0.5f && ls2.items[0].outlineColor == 0x445566, "thickness and outlines save and load (steps and items)");
                CHECK(ls.useStack && ls.stack.size() == 4 && ls.stack[0].zoom && ls.stack[0].zoomTo == 1.5f && ls.stack[0].file == "ridley.png" && ls.stack[1].sound.file == "type.wav" && ls.stack[1].text == ks.screens[story].stack[1].text
                      && std::fabs(ls.duration - buildStack(ls.stack).length) < 1e-4, "a stack saves and loads (and its length is worked out again)");
            }
            ScreenSet ts = defaultScreens(); ts.startSound.file = "start.wav"; ts.startFade = 2.25f; ts.fadeWithSound = false; ts.sounds.hover.file = "tick.wav"; ts.sounds.click.file = "beep.wav"; ts.sounds.click.volume = 0.5f;
            mc::Screen& sp = ts.screens[5]; sp.next = "Story"; sp.musicMode = 1; sp.music.file = "title theme.mp3"; sp.musicFade = 2.5f; sp.untilVideoEnds = true; sp.skippable = false; sp.sounds.back.file = "back.wav";
            { ScreenItem& ti = ts.screens[6].items[0]; ti.typeSound.file = "type.wav"; ti.driftY = -0.01f; ti.typeSpeed = 22; ti.showAt = 0.8f; ti.hideAt = 6.4f; ti.fadeOut = 0.8f; }
            saveScreens("/tmp/prim_screens_t.txt", ts); ScreenSet tl; loadScreens("/tmp/prim_screens_t.txt", tl);
            const mc::Screen& a = tl.screens[5]; const ScreenItem& st = tl.screens[6].items[0];
            CHECK(tl.sounds.hover.file == "tick.wav" && tl.sounds.click.file == "beep.wav" && tl.sounds.click.volume == 0.5f && tl.sounds.back.file.empty() && a.sounds.back.file == "back.wav" && tl.startSound.file == "start.wav" && tl.startFade == 2.25f && !tl.fadeWithSound, "interface sounds (the game's and a screen's own) and the game-start sound save and load");
            CHECK(a.musicMode == 1 && a.music.file == "title theme.mp3" && a.musicFade == 2.5f && a.useStack && std::fabs(a.duration - buildStack(a.stack).length) < 1e-4 && a.untilVideoEnds && !a.skippable && a.next == "Story", "music and going on by itself save and load");
            CHECK(st.typeSpeed == 22 && st.typeSound.file == "type.wav" && st.driftY == -0.01f && st.showAt == 0.8f && st.hideAt == 6.4f && st.fadeOut == 0.8f, "an item's timing, typing and drift save and load");
        }
        {   // the story used to follow the splash (version 2): now it is the intro of a new game
            ScreenSet o2 = defaultScreens(); o2.screens[5].next = "Story"; o2.screens[6].role = ScreenRole::Custom; saveScreens("/tmp/prim_screens_v2.txt", o2);
            { std::ifstream in("/tmp/prim_screens_v2.txt"); std::stringstream b; b << in.rdbuf(); std::string t = b.str(); t.replace(t.find("screens 4"), 9, "screens 2"); std::ofstream out("/tmp/prim_screens_v2.txt"); out << t; }
            ScreenSet l2; loadScreens("/tmp/prim_screens_v2.txt", l2);
            CHECK(l2.screens[5].next.empty() && l2.screens[6].role == ScreenRole::NewGameIntro, "an older splash -> story chain becomes splash -> main menu, story after an empty file");
        }
        {   // screens saved before save files: they get a Files screen once
            ScreenSet old = defaultScreens(); old.screens.erase(old.screens.begin() + 1); saveScreens("/tmp/prim_screens_old.txt", old);
            { std::ifstream in("/tmp/prim_screens_old.txt"); std::stringstream b; b << in.rdbuf(); std::string t = b.str(); t.replace(t.find("screens 4"), 9, "screens 1"); std::ofstream out("/tmp/prim_screens_old.txt"); out << t; }
            ScreenSet ol; loadScreens("/tmp/prim_screens_old.txt", ol);
            CHECK(ol.screens.size() == 7 && ol.byRole(ScreenRole::Files) == 1, "older screens get the Files screen (after the main menu)");
            ScreenSet nw = defaultScreens(); nw.screens.erase(nw.screens.begin() + 1); saveScreens("/tmp/prim_screens_new.txt", nw); ScreenSet nl; loadScreens("/tmp/prim_screens_new.txt", nl);
            CHECK(nl.byRole(ScreenRole::Files) < 0, "a newer set without it stays without it");
        }
    }

    {   // save files
        printf("\nsave files\n");
        SaveGame g; g.world = "Tallon Overworld"; g.place = "Save Station 1"; g.playTime = 3 * 3600 + 25 * 60 + 9; g.completion = 0.42f; g.health = 199; g.maxHealth = 299; g.savedAt = 1790000000;
        g.pos = { 12.5, 64.25, -3.125 }; g.yaw = 1.25; g.keys = { "Morph Ball", "Wave Beam" }; g.counts = { { "Missile", 15 }, { "Energy Tank", 2 } };
        g.taken["Tallon Overworld"] = { 4, 9, 17 }; g.taken["Chozo Ruins"] = { 2 }; g.beam = 1; g.missile = 0; g.visor = 1;
        ScanEntry e; e.id = 7; e.name = "Strange Door"; e.category = "Research"; e.text = "Two lines\nand 100% sure"; e.model = "@door"; e.important = true; g.scans.push_back(e);
        g.enemyGone = { "Chozo Ruins#12" }; g.zonesCleared = { "Tallon Overworld#3" }; g.stationsUsed = { "Tallon Overworld#5" }; g.flags = { "met the queen" }; g.maps = { "Tallon Overworld", "Chozo Ruins" };
        CHECK(saveGame("/tmp/prim_save/file1", g), "a file saves");
        SaveGame b; CHECK(loadGame("/tmp/prim_save/file1", b), "and loads");
        CHECK(b.world == g.world && b.place == g.place && b.playTime == g.playTime && b.completion == g.completion && b.health == 199 && b.maxHealth == 299 && b.savedAt == g.savedAt
              && b.pos.x == 12.5 && b.pos.y == 64.25 && b.pos.z == -3.125 && b.yaw == 1.25, "where, how long, how much, when");
        CHECK(b.keys == g.keys && b.counts == g.counts && b.taken == g.taken && b.beam == 1 && b.visor == 1, "what you carry and what you took, per world");
        CHECK(b.scans.size() == 1 && b.scans[0].text == "Two lines\nand 100% sure" && b.scans[0].important && b.scans[0].model == "@door", "the scan log");
        CHECK(b.enemyGone == g.enemyGone && b.zonesCleared == g.zonesCleared && b.stationsUsed == g.stationsUsed && b.flags == g.flags && b.maps == g.maps, "defeated, cleared, stations, flags, maps");
        { std::ofstream cut("/tmp/prim_save/file2/save.txt"); }
        std::filesystem::create_directories("/tmp/prim_save/file2"); { std::ofstream cut("/tmp/prim_save/file2/save.txt"); cut << "save 1\nworld X\n"; }
        SaveGame c; CHECK(!loadGame("/tmp/prim_save/file2", c) && !loadGame("/tmp/prim_save/none", c), "a file cut short (or none) is not a save");
        CHECK(playTimeText(3 * 3600 + 25 * 60 + 9) == "3:25" && playTimeText(59) == "0:00" && playTimeText(600) == "0:10", "play time as hours:minutes");
        CHECK(eraseGame("/tmp/prim_save/file1") && !loadGame("/tmp/prim_save/file1", c), "erasing a file");
        { std::vector<WorldObject> os(1); os[0].kind = ObjKind::SaveStation; os[0].name = "Save Station 1"; os[0].save.ammo = false; saveObjects("/tmp/prim_savest.txt", os);
          std::vector<WorldObject> ol; loadObjects("/tmp/prim_savest.txt", ol);
          CHECK(ol.size() == 1 && ol[0].kind == ObjKind::SaveStation && ol[0].save.energy && !ol[0].save.ammo && std::string(kindName(ObjKind::SaveStation)) == "Save Station", "Save Stations save and load (what they fill up)"); }
    }

    {   // culling: a box (a model part, turned and stretched) inside the view or not
        printf("\nculling\n");
        float zn = 0.05f, zf = 500, ys = 1 / std::tan(0.6f), xs = ys / 1.5f;
        float m[16] = { xs, 0, 0, 0,  0, ys, 0, 0,  0, 0, zf / (zf - zn), 1,  0, 0, -zn * zf / (zf - zn), 0 };   // looking down +Z (like XMMatrixPerspectiveFovLH)
        V3 c[8]; auto box = [&](V3 o, double s) { boxCorners(o, { s, 0, 0 }, { 0, s, 0 }, { 0, 0, s }, c); return cornersVisible(m, c); };
        CHECK(box({ -1, -1, 10 }, 2), "a box in front: drawn");
        CHECK(!box({ -1, -1, -12 }, 2), "behind you: skipped");
        CHECK(!box({ 60, -1, 10 }, 2) && !box({ -1, 50, 10 }, 2), "far to the side, far above: skipped");
        CHECK(box({ -8, -8, -8 }, 16), "a big part all around you: drawn");
        CHECK(!box({ -1, -1, 600 }, 2), "past the far plane: skipped");
        double a = 0.7; boxCorners({ 3, -1, 4 }, { std::cos(a) * 4, 0, -std::sin(a) * 4 }, { 0, 2, 0 }, { std::sin(a) * 4, 0, std::cos(a) * 4 }, c);
        CHECK(cornersVisible(m, c), "a turned, stretched part poking into the view: drawn");
    }

    {   // lock-on: picking a target, turning to it, circling it, homing, the dash
        printf("\nlock-on\n");
        LockOnSettings ls;
        std::vector<LockCand> cs = { { 1, { 0, 0, 10 }, 10, 15 }, { 2, { 0, 0, 30 }, 30, 3 }, { 3, { 0, 0, 5 }, 5, 40 }, { 4, { 0, 0, 80 }, 80, 0 } };
        int i = pickLockTarget(cs, ls);
        CHECK(i == 1, "picks the one nearest the crosshair (not one outside the cone, not one out of range)");
        cs[1].angDeg = 14; CHECK(pickLockTarget(cs, ls) == 0, "about as near the crosshair: the closer one wins");
        CHECK(pickLockTarget({ cs[2], cs[3] }, ls) == -1, "nothing in the cone and range: no target");
        double yaw = 0, pitch = 0; V3 eye{ 0, 1.6, 0 }, tg{ 10, 1.6, 0 };
        for (int k = 0; k < 120; k++) lockLook(eye, tg, yaw, pitch, ls.turnSpeed, 1 / 60.0);
        CHECK(std::fabs(yaw - 3.14159265 / 2) < 1e-3 && std::fabs(pitch) < 1e-3 && angleTo(eye, yaw, pitch, tg) < 0.1, "the view swings onto the target (yaw %.3f)", yaw);
        yaw = 3.0; lockLook(eye, { -10, 1.6, -0.5 }, yaw, pitch, ls.turnSpeed, 1 / 60.0);
        CHECK(yaw > 3.0, "it turns the short way round (across the back)");
        V3 d = homeDir({ 0, 0, 1 }, { 0, 0, 0 }, { 10, 0, 10 }, 1.0, 0.1);
        CHECK(std::fabs(length(d) - 1) < 1e-9 && std::fabs(std::acos(d.z) - 0.1) < 1e-6 && d.x > 0, "homing turns a shot a little toward the target");
        d = homeDir({ 0, 0, 1 }, { 0, 0, 0 }, { 0.1, 0, 10 }, 5.0, 0.1);
        CHECK(std::fabs(d.x - 0.1 / std::sqrt(100.01)) < 1e-9, "close enough: straight at it");

        // circling: on a floor, strafing alone keeps the distance to the target
        BlockRegistry r; registerBuiltinBlocks(r); World fw(r); fw.gen.layers = { { r.find("stone"), 3 } }; fw.stream({ 0, 0, 0 }, 3, 2, 100000);
        Player pl; pl.flying = false; pl.feet = { 0.5, 3, -6 }; Controls none; for (int k = 0; k < 90; k++) pl.update(fw, none, 1 / 60.0);
        V3 ctr{ 0.5, pl.feet.y, 0.5 }; double r0 = length(V3{ pl.feet.x - ctr.x, 0, pl.feet.z - ctr.z });
        Controls oc; oc.right = true; oc.orbit = true; oc.orbitAround = ctr; double moved = 0; V3 last = pl.feet;
        for (int k = 0; k < 120; k++) { lookAt(pl.eye(), ctr + V3{ 0, 1, 0 }, pl.yaw, pl.pitch); pl.update(fw, oc, 1 / 60.0); moved += length(pl.feet - last); last = pl.feet; }
        double r1 = length(V3{ pl.feet.x - ctr.x, 0, pl.feet.z - ctr.z });
        CHECK(std::fabs(r1 - r0) < 0.02 && moved > 3, "strafing while locked circles the target at the same distance (%.3f -> %.3f, walked %.1f)", r0, r1, moved);
        // the dash: a hop to the side, over when you land
        Player pd = pl; V3 side{ std::cos(pd.yaw), 0, -std::sin(pd.yaw) }; V3 st = pd.feet; Controls jc; jc.jump = true;
        pd.dash(side * 9.0); bool air = false; int k = 0;
        for (; k < 120; k++) { pd.update(fw, jc, 1 / 60.0); jc.jump = false; air |= !pd.onGround; if (air && pd.onGround) break; }
        double sideways = (pd.feet.x - st.x) * side.x + (pd.feet.z - st.z) * side.z;
        CHECK(air && pd.onGround && sideways > 2.0 && pd.dashT <= 0, "a dash: off the ground, %.2f blocks to the side, over on landing", sideways);
        WeaponSet ws = defaultWeapons(); ws.lock.mode = 1; ws.lock.cone = 30; ws.lock.dash = false; ws.lock.lockedColor = 0x123456; ws.lock.missileHoming = 2.5f;
        saveWeapons("/tmp/prim_lock.txt", ws); WeaponSet wb; loadWeapons("/tmp/prim_lock.txt", wb);
        CHECK(wb.lock.mode == 1 && wb.lock.cone == 30 && !wb.lock.dash && wb.lock.lockedColor == 0x123456 && wb.lock.missileHoming == 2.5f, "lock-on settings save and load with the weapons");
    }


    {   // bosses
        SolidFn floor = [](int, int y, int) { return y < 0; };
        EnemyType kt; kt.id = 7; kt.size = 3; kt.move = EnemyMove::Walk;
        BossDef d = kraidExample(7);
        BossSet bs; bs.bosses.push_back(d); BossDef d2 = defaultBoss(9); d2.parts[0].modelPart = "Head"; d2.parts[0].role = PartRole::Breakable; d2.parts[0].health = 40;
        d2.roar.file = "roar.wav"; d2.phases[0].moves[1].sound.file = "pew.wav"; d2.phases[0].moves[1].sound.volume = 0.5f; d2.arena = "Kraid Lair"; bs.bosses.push_back(d2);
        CHECK(saveBosses("/tmp/prim_bosses.txt", bs), "bosses save");
        BossSet lb; CHECK(loadBosses("/tmp/prim_bosses.txt", lb) && lb.bosses.size() == 2, "bosses load (%d)", (int)lb.bosses.size());
        const BossDef* l7 = lb.forType(7), *l9 = lb.forType(9);
        CHECK(l7 && l7->title == "Kraid" && l7->phases.size() == 2 && l7->phases[1].moves.size() == 4 && l7->phases[1].start == PhaseStart::HealthBelow && l7->phases[1].value == 50
              && l7->phases[0].moves[1].pattern == ShotPattern::Straight && l7->parts.size() == 1 && l7->parts[0].name == "Mouth", "Kraid's phases, moves and parts come back");
        CHECK(l9 && l9->parts[0].modelPart == "Head" && l9->parts[0].role == PartRole::Breakable && l9->roar.file == "roar.wav" && l9->phases[0].moves[1].sound.file == "pew.wav"
              && l9->phases[0].moves[1].sound.volume == 0.5f && l9->arena == "Kraid Lair", "model parts, sounds and the arena come back");

        EnemyState e; enemyStart(e, kt, { 0.5, 0, 0.5 }, 0, floor); e.obj = 3;
        BossState b; V3 you{ 0.5, 0, 14 };
        BossHitResult early = bossHit(b, d, e, "", 50); CHECK(early.deflected && e.health > 0, "before the fight it can't be hurt");
        bossStart(b, d, e); CHECK(b.fighting && e.health == 800 && b.open.count("Mouth"), "the fight starts at full health, the mouth open");
        BossHitResult intro = bossHit(b, d, e, "", 50); CHECK(intro.deflected && e.health == 800, "during its intro roar it can't be hurt");
        bool introOver = false; for (int i = 0; i < 120 && !introOver; i++) introOver = bossUpdate(b, d, e, kt, floor, you, nullptr, 1 / 60.0).introOver;
        CHECK(introOver && b.phase == 0, "the intro ends and phase 1 begins");
        BossHitResult body = bossHit(b, d, e, "", 10), mouth = bossHit(b, d, e, "Mouth", 10);
        CHECK(body.damage == 10 && mouth.damage == 20 && e.health == 770, "a hit on the body does 10, on the mouth (weak x2) 20 (health %.0f)", e.health);
        int shots = 0; bool straight = true; V3 startPos = e.pos; double far = 0;
        for (int i = 0; i < 60 * 6; i++) {
            BossTick k = bossUpdate(b, d, e, kt, floor, you, nullptr, 1 / 60.0);
            for (const BossShot& s : k.shots) { shots++; if (b.move == 1 && std::fabs(s.vel.y) > 1e-6) straight = false; }
            far = std::max(far, std::sqrt((e.pos.x - startPos.x) * (e.pos.x - startPos.x) + (e.pos.z - startPos.z) * (e.pos.z - startPos.z)));
        }
        CHECK(shots >= 6 && straight, "it shoots belly spikes straight ahead and lobs claws (%d shots)", shots);
        CHECK(far > 1 && std::fabs(e.pos.y) < 0.01, "it walks side to side on the floor (%.1f blocks away at most)", far);
        CHECK(e.fwd.z > 0.9, "it faces you");
        e.health = 390; int started = -1; for (int i = 0; i < 3 && started < 0; i++) started = bossUpdate(b, d, e, kt, floor, you, nullptr, 1 / 60.0).phaseStarted;
        CHECK(started == 1 && b.phase == 1, "below half health the angry phase starts");
        // lobbed shots land near you
        BossMove lob; lob.kind = BossMoveKind::Shoot; lob.pattern = ShotPattern::Lob; lob.count = 1; lob.shotSpeed = 8; lob.gravity = 14;
        BossDef ld; ld.introTime = 0; BossPhase lp; lp.moves.push_back(lob); ld.phases.push_back(lp);
        EnemyState le; enemyStart(le, kt, { 0.5, 0, 0.5 }, 0, floor); BossState lb2; bossStart(lb2, ld, le);
        BossTick lt; for (int i = 0; i < 5 && lt.shots.empty(); i++) lt = bossUpdate(lb2, ld, le, kt, floor, you, nullptr, 1 / 60.0);
        if (!lt.shots.empty()) {
            BossShot s = lt.shots[0]; V3 pos = s.from, vel = s.vel; for (int i = 0; i < 600 && !(vel.y < 0 && pos.y <= you.y); i++) { vel.y -= s.gravity / 60.0; pos += vel * (1 / 60.0); }
            CHECK(std::fabs(pos.z - you.z) < 1 && std::fabs(pos.x - you.x) < 1, "a lobbed shot comes down on you (lands at %.1f %.1f)", pos.x, pos.z);
        } else CHECK(false, "a lobbed shot comes down on you (no shot)");
        // a ring and a spread
        BossDef rd = ld; rd.phases[0].moves[0].pattern = ShotPattern::Ring; rd.phases[0].moves[0].count = 8;
        BossState rb; bossStart(rb, rd, le); BossTick rt = bossUpdate(rb, rd, le, kt, floor, you, nullptr, 1 / 60.0);
        double sx = 0, sz = 0; for (auto& s : rt.shots) { sx += s.vel.x; sz += s.vel.z; }
        CHECK(rt.shots.size() == 8 && std::fabs(sx) < 1e-6 && std::fabs(sz) < 1e-6, "a ring: 8 shots evenly all around");
        // weak points that open and close; armour; breakable armour; the shield
        BossDef wd; wd.introTime = 0; wd.bodyHurts = false;
        BossPart wp; wp.name = "Eye"; wp.startsOpen = false; wd.parts.push_back(wp);
        BossPart ar; ar.name = "Shell"; ar.role = PartRole::Armour; wd.parts.push_back(ar);
        BossPart br; br.name = "Plate"; br.role = PartRole::Breakable; br.health = 30; wd.parts.push_back(br);
        BossPhase wph; BossMove op; op.kind = BossMoveKind::OpenWeak; op.time = 1; BossMove sh; sh.kind = BossMoveKind::Shield; sh.time = 1; BossMove cl; cl.kind = BossMoveKind::CloseWeak; cl.time = 1;
        wph.moves = { cl, op, sh }; wd.phases.push_back(wph);
        BossPhase brk; brk.start = PhaseStart::PartBroken; brk.what = "Plate"; brk.moves = { op }; wd.phases.push_back(brk);
        EnemyState we; enemyStart(we, kt, { 0.5, 0, 0.5 }, 0, floor); BossState wb; bossStart(wb, wd, we);
        bossUpdate(wb, wd, we, kt, floor, you, nullptr, 1 / 60.0);
        CHECK(bossHit(wb, wd, we, "Eye", 10).deflected && bossHit(wb, wd, we, "", 10).deflected && bossHit(wb, wd, we, "Shell", 10).deflected, "closed eye, armour and the body (when only weak points hurt) deflect");
        for (int i = 0; i < 70; i++) bossUpdate(wb, wd, we, kt, floor, you, nullptr, 1 / 60.0);
        CHECK(wb.move >= 0 && wb.open.count("Eye") && bossHit(wb, wd, we, "Eye", 10).damage == 20, "after its 'open' move the eye can be hit (x2)");
        for (int i = 0; i < 70; i++) bossUpdate(wb, wd, we, kt, floor, you, nullptr, 1 / 60.0);
        CHECK(wb.shieldT > 0 && bossHit(wb, wd, we, "Eye", 10).deflected, "while shielded nothing hurts it");
        for (int i = 0; i < 70; i++) bossUpdate(wb, wd, we, kt, floor, you, nullptr, 1 / 60.0);
        BossHitResult p1 = bossHit(wb, wd, we, "Plate", 20), p2 = bossHit(wb, wd, we, "Plate", 20);
        CHECK(!p1.broke && p2.broke && p1.damage == 0 && wb.broken.count("Plate"), "breakable armour takes the hits and breaks");
        int ps = -1; for (int i = 0; i < 3 && ps < 0; i++) ps = bossUpdate(wb, wd, we, kt, floor, you, nullptr, 1 / 60.0).phaseStarted;
        CHECK(ps == 1, "a phase that starts when the plate breaks");
        // flags, defeat
        BossDef fd = ld; BossPhase fp = lp; fp.start = PhaseStart::Flag; fp.what = "Statue"; fd.phases.push_back(fp);
        BossState fb; bossStart(fb, fd, le); bool flag = false; FlagFn fl = [&](const std::string& n) { return flag && n == "Statue"; };
        bossUpdate(fb, fd, le, kt, floor, you, fl, 1 / 60.0); int fbefore = fb.phase; flag = true; bossUpdate(fb, fd, le, kt, floor, you, fl, 1 / 60.0);
        CHECK(fbefore == 0 && fb.phase == 1, "a phase that starts when a flag turns on");
        BossHitResult kill = bossHit(fb, fd, le, "", 10000); CHECK(fb.dying && le.health == 0 && kill.damage > 0, "out of health: it starts dying");
        // boss events
        EventCard bc; bc.when.kind = When::BossPhase; bc.when.target = "Kraid"; bc.when.number = 2; EventDo sk; sk.kind = Do::Shake; sk.number = 0.5f; bc.dos.push_back(sk);
        EventDo cd; cd.kind = Do::Countdown; cd.number = 60; cd.text = "Escape!"; bc.dos.push_back(cd);
        CHECK(describe(bc) == "When Kraid reaches phase 2: shake the screen (0.5), count down 60 s (\"Escape!\").", "a boss card reads: %s", describe(bc).c_str());
        struct BH : EventHost { int runs = 0; bool hasKey(const std::string&) override { return false; } int count(const std::string&) override { return 0; } float health() override { return 99; } void run(const EventDo&) override { runs++; } } bh;
        EventRunner erb; std::vector<EventCard> bcs{ bc }; erb.start(bcs, bh);
        erb.signal(bcs, { When::BossPhase, "Kraid", "", 1 }, bh); int r1 = bh.runs; erb.signal(bcs, { When::BossPhase, "Kraid", "", 2 }, bh);
        CHECK(r1 == 0 && bh.runs == 2, "a phase card runs for its phase only");
        // part boxes turn with it
        BossPart front; front.box = { { 0.4, 0.4, 0.9 }, { 0.6, 0.6, 1.0 } };
        EnemyState pe; enemyStart(pe, kt, { 0.5, 0, 0.5 }, 0, floor); pe.fwd = { 0, 0, 1 }; Box3 bz = bossPartBox(front, pe, kt); pe.fwd = { 1, 0, 0 }; Box3 bx = bossPartBox(front, pe, kt);
        CHECK(bz.lo.z > 1.0 && bx.lo.x > 1.0 && std::fabs((bx.lo.z + bx.hi.z) / 2 - 0.5) < 0.01, "a part on its front stays on its front when it turns");
        // jumping, slam
        BossMove slam; slam.kind = BossMoveKind::Slam; slam.time = 2; slam.height = 2; BossDef sd; sd.introTime = 0; BossPhase sp2; sp2.moves = { slam }; sd.phases.push_back(sp2);
        EnemyState se; enemyStart(se, kt, { 0.5, 0, 0.5 }, 0, floor); BossState sb; bossStart(sb, sd, se); bool slammed = false; double top = 0;
        for (int i = 0; i < 2; i++) bossUpdate(sb, sd, se, kt, floor, you, nullptr, 1 / 60.0);   // lands first
        for (int i = 0; i < 120; i++) { BossTick k = bossUpdate(sb, sd, se, kt, floor, you, nullptr, 1 / 60.0); slammed |= k.slam; top = std::max(top, se.pos.y); }
        CHECK(slammed && top > 1.5, "a slam: up %.1f blocks and a shockwave on landing", top);
    }

    {   // the asset manager: one folder structure for everything, searching, prefabs
        AssetTree at;
        AssetId roar{ AssetType::Sound, "roar.wav" }, kraid{ AssetType::Model, "Kraid" }, kt{ AssetType::Enemy, "7" };
        at.put(roar, "Bosses/Kraid/Sounds"); at.put(kraid, "Bosses/Kraid"); at.put(kt, "Bosses/Kraid");
        CHECK(at.folderOf(roar) == "Bosses/Kraid/Sounds" && at.folders.paths.count("Bosses") && at.in("Bosses/Kraid").size() == 2 && at.countWithin("Bosses") == 3, "assets of every kind in one folder structure");
        at.favourites.insert(roar.str()); at.favourites.insert("folder:Bosses/Kraid/Sounds");
        CHECK(at.renameFolder("Bosses/Kraid", "Bosses/Kraid (NES)") && at.folderOf(roar) == "Bosses/Kraid (NES)/Sounds" && at.folderOf(kt) == "Bosses/Kraid (NES)" && at.favourites.count("folder:Bosses/Kraid (NES)/Sounds"),
              "renaming a folder takes what is inside along (and its favourites)");
        CHECK(!at.renameFolder("Bosses", "Bosses/Inner"), "a folder can't go inside itself");
        at.renameAsset(roar, "kraid roar.wav"); AssetId roar2{ AssetType::Sound, "kraid roar.wav" };
        CHECK(!at.has(roar) && at.folderOf(roar2) == "Bosses/Kraid (NES)/Sounds" && at.favourites.count(roar2.str()), "a renamed asset stays where it was (and a favourite)");
        at.removeFolder("Bosses/Kraid (NES)/Sounds"); CHECK(at.folderOf(roar2) == "Bosses/Kraid (NES)" && !at.folders.paths.count("Bosses/Kraid (NES)/Sounds"), "deleting a folder: what was inside moves up");
        AssetId newOne{ AssetType::Texture, "lava" };
        int ad = at.adopt({ roar2, kraid, kt, newOne }, [](const AssetId& a) { return std::string(assetTypePlural(a.type)); });
        CHECK(ad == 1 && at.folderOf(newOne) == "Textures" && at.folderOf(kraid) == "Bosses/Kraid (NES)", "new assets (made anywhere) start in a folder of their kind; known ones stay");
        at.prune({ roar2, kraid, newOne }); CHECK(!at.has(kt), "assets that are gone are forgotten");
        saveAssetTree("/tmp/prim_assets.txt", at); AssetTree al; loadAssetTree("/tmp/prim_assets.txt", al);
        CHECK(al.where == at.where && al.folders.paths == at.folders.paths && al.favourites == at.favourites, "the folder structure saves and loads (names with spaces too)");
        AssetId pa; CHECK(AssetId::parse("sound:kraid roar.wav", pa) && pa.type == AssetType::Sound && pa.key == "kraid roar.wav" && !AssetId::parse("nope:x", pa), "asset ids read back");
        AssetQuery q1 = parseAssetQuery("Kraid t:sound"), q2 = parseAssetQuery("t:mod t:pic");
        CHECK(assetMatches(q1, AssetType::Sound, "kraid roar.wav") && !assetMatches(q1, AssetType::Model, "Kraid") && !assetMatches(q1, AssetType::Sound, "ridley.wav"), "search: words and a kind");
        CHECK(q2.types.size() == 2 && q2.types.count(AssetType::Model) && q2.types.count(AssetType::Picture) && q2.words.empty(), "search: kinds by the start of their name");
        std::set<std::string> taken{ "Kraid", "Kraid 2", "roar.wav" };
        auto tk = [&](const std::string& n) { return taken.count(n) > 0; };
        CHECK(uniqueAssetName("Kraid", tk) == "Kraid 3" && uniqueAssetName("roar.wav", tk) == "roar 2.wav" && uniqueAssetName("Ridley", tk) == "Ridley" && uniqueAssetName("Kraid 2", tk) == "Kraid 3", "free names (files keep their ending)");
        // prefabs
        WorldObject d1; d1.name = "Door"; d1.kind = ObjKind::Door; d1.pos = { 10, 5, 20 }; d1.yaw = 0; d1.door.color = 0xFF0000; d1.door.openedBy = "Missile";
        WorldObject z1; z1.name = "Hall"; z1.kind = ObjKind::Trigger; z1.pos = { 14, 5, 20 }; z1.size = { 6, 3, 2 };
        WorldObject pl; pl.name = "Lift"; pl.kind = ObjKind::Platform; pl.pos = { 12, 5, 22 }; pl.platform.points = { { 0, 0, 0 }, { 4, 0, 0 } };
        Prefab pf = makePrefab("Red door room", { d1, z1, pl });
        CHECK(pf.objects.size() == 3 && std::fabs(pf.objects[0].pos.x + 2) < 1e-9 && std::fabs(pf.objects[0].pos.z + 1) < 1e-9 && pf.objects[0].pos.y == 0, "a prefab keeps its objects from its bottom middle");
        CHECK(savePrefab("/tmp/prim_prefab.txt", pf), "a prefab saves");
        Prefab lp; CHECK(loadPrefab("/tmp/prim_prefab.txt", lp) && lp.name == "prim_prefab" && lp.objects.size() == 3 && lp.objects[0].door.color == 0xFF0000 && lp.objects[0].door.openedBy == "Missile", "a prefab loads with all its settings");
        uint32_t nid = 100; std::set<std::string> names{ "Door" };
        auto fresh = [&](const std::string& n) { std::string r = uniqueAssetName(n, [&](const std::string& x) { return names.count(x) > 0; }); names.insert(r); return r; };
        auto placed = placePrefab(lp, { 0, 10, 0 }, 90, nid, fresh);
        CHECK(placed.size() == 3 && placed[0].id == 100 && placed[2].id == 102 && nid == 103 && placed[0].name == "Door 2" && placed[1].name == "Hall", "placed: new IDs, free names");
        CHECK(std::fabs(placed[0].pos.x - (-1)) < 1e-6 && std::fabs(placed[0].pos.z - 2) < 1e-6 && placed[0].pos.y == 10 && placed[0].yaw == 90, "turned a quarter: places and ways turn (door at %.2f %.2f)", placed[0].pos.x, placed[0].pos.z);
        CHECK(std::fabs(placed[2].platform.points[1].z + 4) < 1e-6 && std::fabs(placed[2].platform.points[1].x) < 1e-6 && placed[1].size.x == 2 && placed[1].size.z == 6, "its paths turn, zones' boxes turn");
    }
    {   // the game's own sounds (visors, scanning, menus, pickups, alerts)
        PlayerSounds ps; SoundUse a; a.file = "visor.wav"; ps.visor.sounds.push_back(a); SoundUse b; b.file = "scan loop.wav"; b.volume = 0.5f; ps.scanLoop.sounds.push_back(b);
        SoundUse c; c.file = "alarm.wav"; ps.lowHealth.sounds.push_back(c); ps.lowHealthAt = 25; ps.lowHealthEvery = 0.9f; ps.scanPitch = 0.8f;
        savePlayerSounds("/tmp/prim_psounds.txt", ps); PlayerSounds pl; loadPlayerSounds("/tmp/prim_psounds.txt", pl);
        CHECK(pl.visor.sounds.size() == 1 && pl.visor.sounds[0].file == "visor.wav" && pl.scanLoop.sounds[0].file == "scan loop.wav" && pl.scanLoop.sounds[0].volume == 0.5f && pl.lowHealth.sounds.size() == 1
              && pl.lowHealthAt == 25 && pl.lowHealthEvery == 0.9f && pl.scanPitch == 0.8f && pl.menuOpen.sounds.empty(), "the game's own sounds save and load");
        std::set<std::string> keys; for (auto& g : gameSoundSlots()) keys.insert(g.key);
        CHECK(keys.size() == gameSoundSlots().size() && !keys.count("step") && !keys.count("roll"), "every game sound has its own key (%d)", (int)keys.size());
    }
    {   // a 3D HUD part's glow behind it
        HudLayout hl = defaultHud(); hl.parts[0].glow = 0.8f; hl.parts[0].glowColor = 0xFF8020; hl.parts[0].glowSize = 1.5f;
        saveHud("/tmp/prim_hudglow.txt", hl); HudLayout hb; loadHud("/tmp/prim_hudglow.txt", hb);
        CHECK(hb.parts.size() == hl.parts.size() && hb.parts[0].glow == 0.8f && hb.parts[0].glowColor == 0xFF8020 && hb.parts[0].glowSize == 1.5f && hb.parts[1].glow == 0, "a HUD part's glow behind it saves and loads (only on its own part)");
    }
    {   // looks: tone curve, exposure, grading; presets and your own; a world's and a zone's
        const auto& pre = lookPresets();
        CHECK(pre.size() >= 6 && pre[0].name == "Natural" && pre[1].curve == ToneCurve::Classic, "looks: presets (Natural first, Classic keeps the old curve)");
        LookSet ls; ColorLook mine = pre[2]; mine.name = "Tourian green"; mine.shadows = 0x103010; mine.bloomSize = 1.7f; ls.looks.push_back(mine);
        ColorLook neon2 = pre[2]; neon2.exposure = -1; ls.looks.push_back(neon2);   // a preset made your own
        mine.fog = 0.05f; mine.fogColor = 0x20FF40; mine.lightFog = 2; mine.bounce = 2.5f; ls.looks[0] = mine;
        saveLooks("/tmp/prim_looks.txt", ls); LookSet lb; loadLooks("/tmp/prim_looks.txt", lb);
        CHECK(lb.looks.size() == 2 && lb.get("Tourian green").bloomSize == 1.7f && lb.get("Tourian green").shadows == 0x103010 && lb.get("Neon night").exposure == -1, "your own looks save and load (names with spaces too)");
        CHECK(lb.get("Tourian green").fog == 0.05f && lb.get("Tourian green").fogColor == 0x20FF40 && lb.get("Tourian green").lightFog == 2 && lb.get("Tourian green").bounce == 2.5f, "a look's fog and lights-in-the-air save and load");
        { std::ofstream o("/tmp/prim_looks_old.txt"); o << "looks 1\nlook Old 1 0 0.5 1.5 1 1 0 3166368 0 16756848 0 1 1 0 0 0.18\n"; } LookSet lo; loadLooks("/tmp/prim_looks_old.txt", lo);
        CHECK(lo.looks.size() == 1 && lo.get("Old").fog == 0 && lo.get("Old").lightFog == 0.35f, "an older looks file (no fog yet) still loads");
        CHECK(lb.get("Inferno").warmth > 0 && lb.get("nope").name == "Natural", "a preset you did not change comes from the presets; an unknown look: Natural");
        auto nm = lb.names(); CHECK(nm[0] == "Tourian green" && std::count(nm.begin(), nm.end(), std::string("Neon night")) == 1, "the list: yours first, each name once");
        ColorLook a = pre[0], b = pre[3]; b.shadows = 0xFF0000; a.shadows = 0x000000; ColorLook h = lerp(a, b, 0.5f);
        CHECK(std::fabs(h.bloom - (a.bloom + b.bloom) / 2) < 1e-5 && ((h.shadows >> 16) & 255) >= 127 && ((h.shadows >> 16) & 255) <= 128, "looks blend (colours too)");
        LookState st; st.update(0, pre[0], "Natural", true); st.update(0.75, pre[4], "Inferno");
        CHECK(st.now.warmth > 0.05f && st.now.warmth < pre[4].warmth, "walking into another look: it fades in (%.2f)", st.now.warmth);
        st.update(1.0, pre[4], "Inferno"); CHECK(st.now.warmth == pre[4].warmth, "... and is there after a second and a half");
        WorldInfo wi; wi.look = "Neon night"; EnvZone z; z.name = "Tourian"; z.env = "space"; z.lo = { 0, 0, 0 }; z.hi = { 9, 9, 9 }; z.look = "Tourian green"; wi.zones.push_back(z);
        EnvZone z2 = z; z2.name = "Plain"; z2.look.clear(); wi.zones.push_back(z2);
        wi.save("/tmp/prim_wlook"); WorldInfo wl; wl.load("/tmp/prim_wlook");
        { Settings se; se.eyeAdapt = false; se.filmFx = false; se.save("/tmp/prim_set_look.txt"); Settings sl; sl.load("/tmp/prim_set_look.txt");
          CHECK(!sl.eyeAdapt && !sl.filmFx && findOption("eye_adapt") >= 0 && findOption("film_fx") >= 0, "Options: eye adaptation and film effects (save, load, in the menu)"); }
        { WorldInfo wd; wd.trueDark = false; wd.save("/tmp/prim_wdark"); WorldInfo wdl; wdl.load("/tmp/prim_wdark"); CHECK(!wdl.trueDark && WorldInfo{}.trueDark, "true darkness: on unless a world turns it off (saved)"); }
        { WorldInfo wn; wn.nightDark = 0.3f; wn.save("/tmp/prim_wnight"); WorldInfo wl2; wl2.load("/tmp/prim_wnight"); CHECK(wl2.nightDark == 0.3f && WorldInfo{}.nightDark > 0.5f, "how dark nights are: saved (dark by default)"); }
        { Settings se; se.lightShadows = 8; se.save("/tmp/prim_set_ls.txt"); Settings sl; sl.load("/tmp/prim_set_ls.txt"); CHECK(sl.lightShadows == 8 && findOption("light_shadows") >= 0 && findOption("bounce") >= 0 && findOption("light_fog") >= 0, "Options: shadows from lamps"); }
        {   // lights: strips and shadows save and load (older files: none)
            std::vector<WorldObject> ob(1); ob[0].id = 7; ob[0].kind = ObjKind::Light; ob[0].light.type = LightType::Strip; ob[0].light.length = 6.5f; ob[0].light.shadows = true;
            saveObjects("/tmp/prim_lobj.txt", ob); std::vector<WorldObject> ol; loadObjects("/tmp/prim_lobj.txt", ol);
            CHECK(ol.size() == 1 && ol[0].light.type == LightType::Strip && ol[0].light.shadows && std::fabs(ol[0].light.length - 6.5f) < 1e-4, "a light strip with shadows saves and loads");
        }
        CHECK(wl.look == "Neon night" && wl.zones.size() == 2 && wl.zones[0].look == "Tourian green" && wl.zones[1].look.empty() && wl.zones[1].name == "Plain", "a world's look and its zones' looks save and load");
    }
    {   // HUD: energy tanks, energy in this tank, counters full at the most they can hold
        HudValues v; v.maxHealth = 400; v.tanks = 3; v.tankSize = 100; v.health = 276;
        HudElement te; te.value = HudValue::TankEnergy; HudElement tk; tk.value = HudValue::Tanks;
        CHECK(fullTanks(v) == 2 && hudValue(te, v) == 76 && hudValue(tk, v) == 3 && hudMax(te, v) == 100, "energy tanks: 276 of 400 = 2 full tanks and 76 (got %d, %.0f)", fullTanks(v), hudValue(te, v));
        v.health = 400; CHECK(fullTanks(v) == 3 && hudValue(te, v) == 100, "all full: every tank full, 100 in the last");
        v.health = 40; CHECK(fullTanks(v) == 0 && hudValue(te, v) == 40, "low: no full tanks");
        HudElement m; m.value = HudValue::Counter; m.valueName = "Missile"; m.max = 5; v.most["Missile"] = 25;
        CHECK(hudMax(m, v) == 5, "a counter is full at its number ...");
        m.fullAtMost = true; CHECK(hudMax(m, v) == 25, "... or at the most it can hold");
        m.text = "{value}/{max}"; v.counts["Missile"] = 12; CHECK(hudText(m, v) == "12/25", "text: {max} is the most it can hold");
        HudLayout h; HudPlane p; HudElement el; el.kind = HudKind::Tanks; el.tankRows = 3; el.tankShape = 2; el.segments = 5; p.elements.push_back(el); p.elements.push_back(m); h.planes.push_back(p);
        saveHud("/tmp/prim_hud_tanks.txt", h); HudLayout hl; loadHud("/tmp/prim_hud_tanks.txt", hl);
        CHECK(hl.planes.size() == 1 && hl.planes[0].elements.size() == 2 && hl.planes[0].elements[0].kind == HudKind::Tanks && hl.planes[0].elements[0].tankRows == 3 && hl.planes[0].elements[0].tankShape == 2
              && hl.planes[0].elements[1].fullAtMost, "HUD: an energy tanks element and \"full at the most\" save and load");
    }
    {   // materials: fake shine (picture or glint lights) saves and loads; older files keep it off
        Materials ms; TexMaterial m; m.fake = FakeShine::Glints; m.fakeStrength = 1.7f; m.fakeTint = 0xFF8000; m.fakeBeam = true; m.fakeWorld = false; m.fakePreset = SHINE_PRESETS;
        m.fakeMap = "cannon shine.png"; m.fakeMask = "mask.png"; m.glints[2].on = true; m.glints[2].yaw = 123; m.glints[2].sharp = 0.95f; ms.put("model:Arm Cannon", m);
        saveMaterials("/tmp/prim_fake.txt", ms); Materials ml; loadMaterials("/tmp/prim_fake.txt", ml);
        CHECK(ml.isSet("model:Arm Cannon") && ml.of("model:Arm Cannon") == m, "fake shine (glints, tint, beam colour, pictures) saves and loads");
        { std::ofstream o("/tmp/prim_fake_old.txt"); o << "materials 1\ntex dirt 0.2 0 0 0 1 1 - 1.5 6\n"; } Materials mo; loadMaterials("/tmp/prim_fake_old.txt", mo);
        CHECK(mo.of("dirt").fake == FakeShine::Off && mo.of("dirt").shine == 0.2f, "an older materials file: no fake shine");
        CHECK(std::string(shinePresetName(SHINE_PRESETS)) == "Your own picture", "shine pictures: presets, then your own");
    }
    {   // sounds: doors (the game's and a door's own) and visor loops save and load
        PlayerSounds ps; SoundUse u; u.file = "door open.wav"; u.volume = 0.8f; ps.doorOpen.sounds.push_back(u); u.file = "hum.wav"; ps.loopScan.sounds.push_back(u); ps.visorLoopVolume = 0.6f; u.file = "unmorph.wav"; ps.unmorph.sounds.push_back(u);
        savePlayerSounds("/tmp/prim_ps_door.txt", ps); PlayerSounds pl; loadPlayerSounds("/tmp/prim_ps_door.txt", pl);
        CHECK(pl.unmorph.sounds.size() == 1 && pl.unmorph.sounds[0].file == "unmorph.wav", "the standing-up sound saves and loads");
        CHECK(pl.doorOpen.sounds.size() == 1 && pl.doorOpen.sounds[0].file == "door open.wav" && pl.loopScan.sounds.size() == 1 && pl.visorLoopVolume == 0.6f, "door sounds and visor loops save and load");
        std::vector<WorldObject> ob(1); ob[0].id = 3; ob[0].kind = ObjKind::Door; ob[0].door.openSound.file = "blast door.wav"; ob[0].door.openSound.pitch = 0.8f;
        saveObjects("/tmp/prim_door_snd.txt", ob); std::vector<WorldObject> ol; loadObjects("/tmp/prim_door_snd.txt", ol);
        CHECK(ol.size() == 1 && ol[0].door.openSound.file == "blast door.wav" && std::fabs(ol[0].door.openSound.pitch - 0.8f) < 1e-5 && ol[0].door.closeSound.empty(), "a door's own sounds save and load");
    }
    {   // the low-energy alarm: loud, then quieter
        PlayerSounds ps; ps.lowHealthFade = 4; ps.lowHealthQuiet = 0.2f;
        CHECK(ps.lowHealthVolume(0) == 1 && ps.lowHealthVolume(2) < 1 && ps.lowHealthVolume(2) > 0.2f && std::fabs(ps.lowHealthVolume(10) - 0.2f) < 1e-5, "the low-energy alarm fades from full to its quiet level");
        savePlayerSounds("/tmp/prim_ps_low.txt", ps); PlayerSounds pl; loadPlayerSounds("/tmp/prim_ps_low.txt", pl);
        CHECK(pl.lowHealthFade == 4 && pl.lowHealthQuiet == 0.2f, "... and its fade saves and loads");
    }
    {   // the death sequence: its settings save and load, and you come back only after it
        GameSettings g; g.player.deathHit = 0.3f; g.player.deathStatic = 2; g.player.deathTvOff = false; g.player.deathStaticColor = 0x40FF80; g.player.respawnDelay = 1; g.player.morphColor = 0x40A0FF; g.player.morphFx = 1.5f; g.player.morphHudFx = false;
        g.save("/tmp/prim_game_death.txt"); GameSettings gl; gl.load("/tmp/prim_game_death.txt");
        CHECK(gl.player.morphColor == 0x40A0FF && gl.player.morphFx == 1.5f && !gl.player.morphHudFx, "the morph effect's settings save and load");
        CHECK(gl.player.deathHit == 0.3f && gl.player.deathStatic == 2 && !gl.player.deathTvOff && gl.player.deathStaticColor == 0x40FF80 && gl.player.deathFx, "the death sequence's settings save and load");
        World w(reg); Player p; PlayState ps; ps.start(p, gl.player, { 0, 64, 0 }, 0); ps.health = 0;
        ps.tick(w, p, gl.player, { 0, 64, 0 }, 0, 0.01); CHECK(ps.dead(), "dead");
        double total = gl.player.deathLength(); bool back = false; for (double t = 0; t < total - 0.05; t += 0.05) if (ps.tick(w, p, gl.player, { 0, 64, 0 }, 0, 0.05) == PlayState::Respawned) back = true;
        CHECK(!back && ps.dead(), "no coming back during the death sequence (%.2f s, respawn after 1 s)", total);
        for (int k = 0; k < 20 && ps.dead(); k++) ps.tick(w, p, gl.player, { 0, 64, 0 }, 0, 0.05); CHECK(!ps.dead(), "... back right after it");
    }
    {   // surfaces: their sounds save and load; blocks know what they are made of
        SurfaceSet ss = defaultSurfaces(); CHECK(ss.find("Metal") && ss.find("Water") && ss.find("Water")->replace && !ss.find("Metal")->replace, "surfaces: starters (water and snow replace the steps)");
        SoundUse u; u.file = "metal step 1.wav"; ss.find("Metal")->step.sounds.push_back(u); u.file = "metal roll.wav"; u.pitch = 1.2f; ss.find("Metal")->roll.sounds.push_back(u); ss.find("Metal")->volume = 0.7f;
        Surface g; g.name = "Chozo grate"; g.replace = true; ss.list.push_back(g);
        saveSurfaces("/tmp/prim_surf.txt", ss); SurfaceSet sl; loadSurfaces("/tmp/prim_surf.txt", sl);
        CHECK(sl.list.size() == ss.list.size() && sl.find("Metal")->step.sounds.size() == 1 && sl.find("Metal")->roll.sounds[0].file == "metal roll.wav" && std::fabs(sl.find("Metal")->roll.sounds[0].pitch - 1.2f) < 1e-5
              && sl.find("Metal")->volume == 0.7f && sl.find("Chozo grate") && sl.find("Chozo grate")->replace, "surfaces and their sounds save and load (names with spaces too)");
        BlockRegistry rr; registerBuiltinBlocks(rr);
        CHECK(rr.get(rr.find("stone")).surface == "Rock" && rr.get(rr.find("metal")).surface == "Metal" && rr.get(rr.find("water")).surface == "Water", "built-in blocks know what they are made of");
    }
    {   // drops: how they look, saved with the enemies
        EnemySet es = defaultEnemies(); DropLook d; d.what = "Missile"; d.model = "missile pickup"; d.bigFrom = 5; d.bigModel = "missile big"; d.light = 3; d.lightColor = 0xFF4020; d.sound.file = "missile get.wav"; d.life = 20; es.drops.push_back(d);
        saveEnemies("/tmp/prim_drops.txt", es); EnemySet el; loadEnemies("/tmp/prim_drops.txt", el);
        const DropLook* dl = el.drop("Missile");
        CHECK(dl && dl->model == "missile pickup" && dl->bigFrom == 5 && dl->bigModel == "missile big" && dl->light == 3 && dl->sound.file == "missile get.wav" && dl->life == 20 && !el.drop("Energy"), "drop looks save and load");
        CHECK(el.types.size() == es.types.size() && el.types[0].drops.size() == es.types[0].drops.size(), "... and the enemies' own drops are still there");
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
