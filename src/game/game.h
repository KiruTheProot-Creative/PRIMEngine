// game.h : creative-mode player, undo history, world info file and settings. No Windows code: testable anywhere.
#pragma once
#include "../world/world.h"
#include "environment.h"
#include <cmath>
#include <string>
#include <vector>

namespace mc {

struct Settings {
    int renderDistance = 10;   // chunks around you, sideways
    int verticalDistance = 5;  // chunks above and below you
    float fov = 70, mouseSensitivity = 1.f, uiScale = 1.f;
    bool vsync = true;
    bool blockPreview = true;  // see-through preview where the next block goes
    float timeOfDay = 10.f;    // hours, 0 .. 24
    bool dayCycle = false;     // the sun moves (one day = 20 minutes)
    int shadowQuality = 2;     // 0 off, 1 normal (2048), 2 high (4096)
    float cloudCover = 0.5f;   // 0 = no clouds, 1 = overcast
    float groundFog = 1.0f;    // 0 = none, 1 = normal (thick in the morning), 2 = a lot
    bool cloudShadows = true;
    float bloom = 0.5f;        // glow around bright things, 0 = off
    bool swaying = true;       // leaves and plants move in the wind
    int reflections = 1;       // 0 off, 1 normal, 2 high
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

struct Controls { bool forward = false, back = false, left = false, right = false, jump = false, sneak = false, sprint = false; };

// the rules of the player while playing (the game maker sets these)
struct PlayerRules {
    float walkSpeed = 4.32f, sprintSpeed = 5.61f;   // blocks per second
    float jumpHeight = 1.25f;                         // blocks
    float gravity = 32;                               // blocks per second per second
    float maxHealth = 100;
    float lavaDamage = 40;                            // health per second in lava
    float voidDepth = -64;                            // fall below this height and you die
    float respawnDelay = 2;                           // seconds before you come back at the spawn point
    float fallDamage = 0;                             // health per block fallen beyond 4 blocks (0 = off)
    bool canSprint = true;
    float jumpSpeed() const { return std::sqrt(2 * gravity * std::max(0.1f, jumpHeight)); }
};

// the game being made: its name, where it starts, the player's rules (game/game.txt)
struct GameSettings {
    std::string name = "My Game";
    std::string startWorld;                           // world folder the game starts in
    std::string lastWorld;                            // the world the editor had open last
    PlayerRules player;
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

struct Player {
    static constexpr double HALF_W = 0.3, HEIGHT = 1.8, EYE = 1.62;    // Minecraft player size (blocks)
    V3 feet{ 0.5, 0, 0.5 }, vel{ 0, 0, 0 };
    double yaw = 0, pitch = 0;      // radians; yaw 0 looks toward +Z
    bool flying = true, onGround = false;
    V3 eye() const { return { feet.x, feet.y + EYE, feet.z }; }
    V3 forward() const { return { std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch) }; }
    void look(double dx, double dy);                     // mouse movement in radians
    void update(const World& w, const Controls& c, double dt, const PlayerRules& rules = PlayerRules{});
    void toggleFly() { flying = !flying; vel.y = 0; }
};

// Play mode: health, lava, falling out of the world, coming back at the spawn point
struct PlayState {
    enum Event { Nothing, Hurt, Died, Respawned };
    float health = 100; double deadFor = -1, hurtFlash = 0; int deaths = 0; const char* cause = "";
    double fallStart = 0; bool falling = false;
    bool dead() const { return deadFor >= 0; }
    void start(Player& p, const PlayerRules& r, V3 spawn, double spawnYaw);   // health full, standing at the spawn point
    Event tick(const World& w, Player& p, const PlayerRules& r, V3 spawn, double spawnYaw, double dt);
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
    std::string weather = "partly_cloudy"; bool autoWeather = false;
    std::string environment = "temperate";
    bool hasSpawn = false; V3 spawn{ 0.5, 0, 0.5 }; double spawnYaw = 0;   // where the player starts when playing
    std::vector<EnvZone> zones;
    bool load(const std::string& dir);
    bool save(const std::string& dir) const;
    void applyGenerator(Generator& g, const BlockRegistry& r) const;
};

} // namespace mc
