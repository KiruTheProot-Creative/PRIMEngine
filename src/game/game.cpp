#include "game.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace mc {

// ------------------------------------------------------------------ settings
bool Settings::load(const std::string& path) {
    std::ifstream f(path); if (!f) return false;
    std::string k;
    while (f >> k) {
        if (k == "render_distance") f >> renderDistance; else if (k == "vertical_distance") f >> verticalDistance;
        else if (k == "fov") f >> fov; else if (k == "mouse_sensitivity") f >> mouseSensitivity; else if (k == "ui_scale") f >> uiScale;
        else if (k == "vsync") { int v; f >> v; vsync = v != 0; }
        else if (k == "block_preview") { int v; f >> v; blockPreview = v != 0; }
        else if (k == "time_of_day") f >> timeOfDay;
        else if (k == "day_cycle") { int v; f >> v; dayCycle = v != 0; }
        else if (k == "shadows") f >> shadowQuality;
        else if (k == "cloud_cover") f >> cloudCover;
        else if (k == "ground_fog") f >> groundFog;
        else if (k == "cloud_shadows") { int v; f >> v; cloudShadows = v != 0; }
        else if (k == "bloom") f >> bloom;
        else if (k == "swaying") { int v; f >> v; swaying = v != 0; }
        else if (k == "reflections") f >> reflections;
        else { std::string rest; std::getline(f, rest); }
    }
    renderDistance = std::max(2, std::min(48, renderDistance)); verticalDistance = std::max(1, std::min(16, verticalDistance));
    return true;
}
bool Settings::save(const std::string& path) const {
    std::ofstream f(path); if (!f) return false;
    f << "render_distance " << renderDistance << "\nvertical_distance " << verticalDistance << "\nfov " << fov << "\nmouse_sensitivity " << mouseSensitivity
      << "\nui_scale " << uiScale << "\nvsync " << (vsync ? 1 : 0) << "\nblock_preview " << (blockPreview ? 1 : 0)
      << "\ntime_of_day " << timeOfDay << "\nday_cycle " << (dayCycle ? 1 : 0) << "\nshadows " << shadowQuality
      << "\ncloud_cover " << cloudCover << "\nground_fog " << groundFog << "\ncloud_shadows " << (cloudShadows ? 1 : 0)
      << "\nbloom " << bloom << "\nswaying " << (swaying ? 1 : 0) << "\nreflections " << reflections << "\n";
    return (bool)f;
}

// ------------------------------------------------------------------ player (creative mode)
void Player::look(double dx, double dy) {
    yaw += dx; pitch = std::max(-1.55, std::min(1.55, pitch - dy));
}

void Player::update(const World& w, const Controls& c, double dt, const PlayerRules& r) {
    V3 fw{ std::sin(yaw), 0, std::cos(yaw) }, rt{ std::cos(yaw), 0, -std::sin(yaw) }, mv{ 0, 0, 0 };
    if (c.forward) mv += fw;
    if (c.back) mv = mv - fw;
    if (c.right) mv += rt;
    if (c.left) mv = mv - rt;
    double speed = flying ? (c.sprint ? 21.6 : 10.9) : (c.sneak ? r.walkSpeed * 0.3 : (c.sprint && r.canSprint) ? r.sprintSpeed : r.walkSpeed);   // blocks per second
    if (length(mv) > 0) mv = normalize(mv) * speed;
    if (flying) {
        // flying: quick to start and stop, Space up, Shift down
        double k = std::min(1.0, dt * 12);
        vel.x += (mv.x - vel.x) * k; vel.z += (mv.z - vel.z) * k;
        double vy = (c.jump ? 7.5 : 0) - (c.sneak ? 7.5 : 0);
        vel.y += (vy - vel.y) * k;
    } else if (int liq = w.liquidAt({ feet.x, feet.y + 0.6, feet.z })) {
        // swimming: slow, you sink a little, Space swims up; lava is much thicker
        double k = liq == 2 ? 0.3 : 0.55;
        vel.x = mv.x * k; vel.z = mv.z * k;
        vel.y = std::max(vel.y - (liq == 2 ? 3.0 : 6.0) * dt, liq == 2 ? -0.8 : -2.2);
        if (c.jump) vel.y = std::min(vel.y + (liq == 2 ? 10.0 : 22.0) * dt, liq == 2 ? 1.2 : 3.2);
        if (c.sneak) vel.y = std::max(vel.y - 10.0 * dt, -3.0);
        if (c.jump && onGround && !w.liquidAt(eye())) vel.y = 6.5;   // climb out at the shore
    } else {
        vel.x = mv.x; vel.z = mv.z;
        vel.y = std::max(vel.y - (double)r.gravity * dt, -78.0);
        if (c.jump && onGround) vel.y = r.jumpSpeed();   // default about 1.25 blocks high
    }
    feet = w.moveBox(feet, vel, HALF_W, HEIGHT, dt, onGround, flying ? 0.0 : 0.6);   // walking climbs half blocks and stairs by itself
    if (flying && onGround && c.sneak) flying = false;   // land by flying down onto the ground
}

// ------------------------------------------------------------------ undo / redo
void EditHistory::push(std::vector<Edit>&& g) {
    if (g.empty()) return;
    undos.push_back(std::move(g)); if (undos.size() > 1000) undos.erase(undos.begin());
    redos.clear();
}
bool EditHistory::undo(World& w) {
    if (undos.empty()) return false;
    auto g = std::move(undos.back()); undos.pop_back();
    for (auto it = g.rbegin(); it != g.rend(); ++it) w.set(it->x, it->y, it->z, it->before);
    redos.push_back(std::move(g)); return true;
}
bool EditHistory::redo(World& w) {
    if (redos.empty()) return false;
    auto g = std::move(redos.back()); redos.pop_back();
    for (auto& e : g) w.set(e.x, e.y, e.z, e.after);
    undos.push_back(std::move(g)); return true;
}

// ------------------------------------------------------------------ game settings (game/game.txt)
bool GameSettings::save(const std::string& path) const {
    std::ofstream f(path); if (!f) return false;
    const PlayerRules& p = player;
    f << "name " << name << "\nstart_world " << (startWorld.empty() ? "-" : startWorld)
      << "\nwalk_speed " << p.walkSpeed << "\nsprint_speed " << p.sprintSpeed << "\njump_height " << p.jumpHeight << "\ngravity " << p.gravity
      << "\nmax_health " << p.maxHealth << "\nlava_damage " << p.lavaDamage << "\nvoid_depth " << p.voidDepth << "\nrespawn_delay " << p.respawnDelay
      << "\nfall_damage " << p.fallDamage << "\ncan_sprint " << (p.canSprint ? 1 : 0) << "\n";
    return (bool)f;
}
bool GameSettings::load(const std::string& path) {
    std::ifstream f(path); if (!f) return false;
    std::string k; PlayerRules& p = player;
    while (f >> k) {
        if (k == "name") { f >> std::ws; std::getline(f, name); }
        else if (k == "start_world") { f >> std::ws; std::getline(f, startWorld); if (startWorld == "-") startWorld.clear(); }
        else if (k == "walk_speed") f >> p.walkSpeed; else if (k == "sprint_speed") f >> p.sprintSpeed; else if (k == "jump_height") f >> p.jumpHeight;
        else if (k == "gravity") f >> p.gravity; else if (k == "max_health") f >> p.maxHealth; else if (k == "lava_damage") f >> p.lavaDamage;
        else if (k == "void_depth") f >> p.voidDepth; else if (k == "respawn_delay") f >> p.respawnDelay; else if (k == "fall_damage") f >> p.fallDamage;
        else if (k == "can_sprint") { int v; f >> v; p.canSprint = v != 0; }
        else { std::string rest; std::getline(f, rest); }
    }
    return true;
}

// ------------------------------------------------------------------ play mode
void PlayState::start(Player& p, const PlayerRules& r, V3 spawn, double spawnYaw) {
    health = r.maxHealth; deadFor = -1; hurtFlash = 0; falling = false; cause = "";
    p.feet = spawn; p.vel = { 0, 0, 0 }; p.yaw = spawnYaw; p.pitch = 0; p.flying = false; p.onGround = false;
}
PlayState::Event PlayState::tick(const World& w, Player& p, const PlayerRules& r, V3 spawn, double spawnYaw, double dt) {
    hurtFlash = std::max(0.0, hurtFlash - dt);
    if (dead()) {
        deadFor += dt;
        if (deadFor >= r.respawnDelay) { start(p, r, spawn, spawnYaw); return Respawned; }
        return Nothing;
    }
    float before = health;
    if (w.liquidAt({ p.feet.x, p.feet.y + 0.3, p.feet.z }) == 2) { health -= r.lavaDamage * (float)dt; cause = "burned in lava"; }
    // falling: remember where it started, hurt on landing
    if (!p.onGround && p.vel.y < 0 && !w.liquidAt({ p.feet.x, p.feet.y + 0.3, p.feet.z })) { if (!falling) { falling = true; fallStart = p.feet.y; } }
    else if (falling) {
        falling = false; double h = fallStart - p.feet.y;
        if (p.onGround && r.fallDamage > 0 && h > 4) { health -= r.fallDamage * (float)(h - 4); cause = "fell too far"; }
    }
    if (p.feet.y < r.voidDepth) { health = 0; cause = "fell out of the world"; }
    if (health < before) hurtFlash = 0.4;
    if (health <= 0) { health = 0; deadFor = 0; deaths++; p.vel = { 0, 0, 0 }; return Died; }
    return health < before ? Hurt : Nothing;
}

// ------------------------------------------------------------------ world info (world.txt)
bool WorldInfo::save(const std::string& dir) const {
    std::error_code ec; std::filesystem::create_directories(dir, ec);
    std::ofstream f(dir + "/world.txt"); if (!f) return false;
    f.precision(17);
    f << "name " << name << "\ngenerator " << genKind << "\nlayers " << layers.size();
    for (auto& l : layers) f << " " << l.first << " " << l.second;
    f << "\nplayer " << player.feet.x << " " << player.feet.y << " " << player.feet.z << " " << player.yaw << " " << player.pitch << " " << (player.flying ? 1 : 0) << "\nhotbar " << hotSel;
    for (auto& h : hotbar) { std::string e = h; for (size_t p; (p = e.find(' ')) != std::string::npos;) e.replace(p, 1, "%20"); f << " " << (e.empty() ? "-" : e); }   // names may have spaces
    f << "\nweather " << weather << " " << (autoWeather ? 1 : 0) << "\nenvironment " << environment << "\n";
    if (hasSpawn) f << "spawn " << spawn.x << " " << spawn.y << " " << spawn.z << " " << spawnYaw << "\n";
    for (auto& z : zones) f << "zone " << z.env << " " << z.lo.x << " " << z.lo.y << " " << z.lo.z << " " << z.hi.x << " " << z.hi.y << " " << z.hi.z << " " << z.name << "\n";
    return (bool)f;
}
bool WorldInfo::load(const std::string& dir) {
    std::ifstream f(dir + "/world.txt"); if (!f) return false;
    zones.clear();
    std::string k;
    while (f >> k) {
        if (k == "name") { f >> std::ws; std::getline(f, name); }
        else if (k == "generator") f >> genKind;
        else if (k == "layers") { size_t n = 0; f >> n; layers.clear(); for (size_t i = 0; i < n && i < 64; i++) { std::string b; int t; f >> b >> t; layers.push_back({ b, t }); } }
        else if (k == "player") { int fl = 1; f >> player.feet.x >> player.feet.y >> player.feet.z >> player.yaw >> player.pitch >> fl; player.flying = fl != 0; }
        else if (k == "hotbar") { f >> hotSel; for (auto& h : hotbar) { f >> h; if (h == "-") h.clear(); for (size_t p; (p = h.find("%20")) != std::string::npos;) h.replace(p, 3, " "); } hotSel = std::max(0, std::min(8, hotSel)); }
        else if (k == "weather") { int a = 0; f >> weather >> a; autoWeather = a != 0; }
        else if (k == "environment") f >> environment;
        else if (k == "spawn") { f >> spawn.x >> spawn.y >> spawn.z >> spawnYaw; hasSpawn = true; }
        else if (k == "zone") { EnvZone z; f >> z.env >> z.lo.x >> z.lo.y >> z.lo.z >> z.hi.x >> z.hi.y >> z.hi.z >> std::ws; std::getline(f, z.name); zones.push_back(z); }
        else { std::string rest; std::getline(f, rest); }
    }
    return true;
}
void WorldInfo::applyGenerator(Generator& g, const BlockRegistry& r) const {
    g.kind = genKind; g.layers.clear();
    for (auto& l : layers) { BlockId id = r.find(l.first); if (l.second > 0) g.layers.push_back({ id, l.second }); }
}

} // namespace mc
