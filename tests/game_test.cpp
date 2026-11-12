// Game logic tests: creative player, undo/redo, world info and settings files.
#include "../src/game/game.h"
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

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
