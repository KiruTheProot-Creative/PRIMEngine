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
        else { std::string rest; std::getline(f, rest); }
    }
    renderDistance = std::max(2, std::min(48, renderDistance)); verticalDistance = std::max(1, std::min(16, verticalDistance));
    return true;
}
bool Settings::save(const std::string& path) const {
    std::ofstream f(path); if (!f) return false;
    f << "render_distance " << renderDistance << "\nvertical_distance " << verticalDistance << "\nfov " << fov << "\nmouse_sensitivity " << mouseSensitivity
      << "\nui_scale " << uiScale << "\nvsync " << (vsync ? 1 : 0) << "\nblock_preview " << (blockPreview ? 1 : 0) << "\n";
    return (bool)f;
}

// ------------------------------------------------------------------ player (creative mode)
void Player::look(double dx, double dy) {
    yaw += dx; pitch = std::max(-1.55, std::min(1.55, pitch - dy));
}

void Player::update(const World& w, const Controls& c, double dt) {
    V3 fw{ std::sin(yaw), 0, std::cos(yaw) }, rt{ std::cos(yaw), 0, -std::sin(yaw) }, mv{ 0, 0, 0 };
    if (c.forward) mv += fw;
    if (c.back) mv = mv - fw;
    if (c.right) mv += rt;
    if (c.left) mv = mv - rt;
    double speed = flying ? (c.sprint ? 21.6 : 10.9) : (c.sneak ? 1.31 : c.sprint ? 5.61 : 4.32);   // blocks per second, like Minecraft
    if (length(mv) > 0) mv = normalize(mv) * speed;
    if (flying) {
        // flying: quick to start and stop, Space up, Shift down
        double k = std::min(1.0, dt * 12);
        vel.x += (mv.x - vel.x) * k; vel.z += (mv.z - vel.z) * k;
        double vy = (c.jump ? 7.5 : 0) - (c.sneak ? 7.5 : 0);
        vel.y += (vy - vel.y) * k;
    } else {
        vel.x = mv.x; vel.z = mv.z;
        vel.y = std::max(vel.y - 32.0 * dt, -78.0);
        if (c.jump && onGround) vel.y = 8.4;    // about 1.25 blocks high
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

// ------------------------------------------------------------------ world info (world.txt)
bool WorldInfo::save(const std::string& dir) const {
    std::error_code ec; std::filesystem::create_directories(dir, ec);
    std::ofstream f(dir + "/world.txt"); if (!f) return false;
    f.precision(17);
    f << "name " << name << "\ngenerator " << genKind << "\nlayers " << layers.size();
    for (auto& l : layers) f << " " << l.first << " " << l.second;
    f << "\nplayer " << player.feet.x << " " << player.feet.y << " " << player.feet.z << " " << player.yaw << " " << player.pitch << " " << (player.flying ? 1 : 0) << "\nhotbar " << hotSel;
    for (auto& h : hotbar) f << " " << (h.empty() ? "-" : h);
    f << "\n";
    return (bool)f;
}
bool WorldInfo::load(const std::string& dir) {
    std::ifstream f(dir + "/world.txt"); if (!f) return false;
    std::string k;
    while (f >> k) {
        if (k == "name") { f >> std::ws; std::getline(f, name); }
        else if (k == "generator") f >> genKind;
        else if (k == "layers") { size_t n = 0; f >> n; layers.clear(); for (size_t i = 0; i < n && i < 64; i++) { std::string b; int t; f >> b >> t; layers.push_back({ b, t }); } }
        else if (k == "player") { int fl = 1; f >> player.feet.x >> player.feet.y >> player.feet.z >> player.yaw >> player.pitch >> fl; player.flying = fl != 0; }
        else if (k == "hotbar") { f >> hotSel; for (auto& h : hotbar) { f >> h; if (h == "-") h.clear(); } hotSel = std::max(0, std::min(8, hotSel)); }
        else { std::string rest; std::getline(f, rest); }
    }
    return true;
}
void WorldInfo::applyGenerator(Generator& g, const BlockRegistry& r) const {
    g.kind = genKind; g.layers.clear();
    for (auto& l : layers) { BlockId id = r.find(l.first); if (l.second > 0) g.layers.push_back({ id, l.second }); }
}

} // namespace mc
