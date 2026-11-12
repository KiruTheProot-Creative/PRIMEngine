// game.h : creative-mode player, undo history, world info file and settings. No Windows code: testable anywhere.
#pragma once
#include "../world/world.h"
#include <string>
#include <vector>

namespace mc {

struct Settings {
    int renderDistance = 10;   // chunks around you, sideways
    int verticalDistance = 5;  // chunks above and below you
    float fov = 70, mouseSensitivity = 1.f, uiScale = 1.f;
    bool vsync = true;
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

struct Controls { bool forward = false, back = false, left = false, right = false, jump = false, sneak = false, sprint = false; };

struct Player {
    static constexpr double HALF_W = 0.3, HEIGHT = 1.8, EYE = 1.62;    // Minecraft player size (blocks)
    V3 feet{ 0.5, 0, 0.5 }, vel{ 0, 0, 0 };
    double yaw = 0, pitch = 0;      // radians; yaw 0 looks toward +Z
    bool flying = true, onGround = false;
    V3 eye() const { return { feet.x, feet.y + EYE, feet.z }; }
    V3 forward() const { return { std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch) }; }
    void look(double dx, double dy);                     // mouse movement in radians
    void update(const World& w, const Controls& c, double dt);
    void toggleFly() { flying = !flying; vel.y = 0; }
};

struct Edit { int x, y, z; BlockId before, after; };
class EditHistory {
public:
    void push(std::vector<Edit>&& group);               // one click (or one tool use) = one undo step
    bool undo(World& w);
    bool redo(World& w);
    size_t undoCount() const { return undos.size(); }
    void clear() { undos.clear(); redos.clear(); }
private:
    std::vector<std::vector<Edit>> undos, redos;
};

struct WorldInfo {
    std::string name = "New World";
    int genKind = Generator::Flat;
    std::vector<std::pair<std::string, int>> layers = { { "stone", 3 }, { "dirt", 2 }, { "grass", 1 } };
    Player player;
    std::string hotbar[9]; int hotSel = 0;
    bool load(const std::string& dir);
    bool save(const std::string& dir) const;
    void applyGenerator(Generator& g, const BlockRegistry& r) const;
};

} // namespace mc
