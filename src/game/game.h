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
    float soundVolume = 1.f;   // all sounds
    bool vsync = true;
    bool blockPreview = true;  // see-through preview where the next block goes
    float timeOfDay = 10.f;    // hours, 0 .. 24
    bool dayCycle = false;     // the sun moves (one day = 20 minutes)
    int shadowQuality = 2;     // 0 off, 1 normal (4 x 1536), 2 high (4 x 2048): four maps, sharp near you, coarser farther out
    float shadowDistance = 160; // blocks: how far shadows reach (the sharpness near you does not depend on it, nor on the render distance)
    int shadowSoft = 2;        // shadow edges: 0 sharp, 1 smooth, 2 soft like real sunlight (sharp near what casts them, softer farther away)
    float shadowSoftness = 1;  // how soft (smooth / realistic)
    float cloudCover = 0.5f;   // 0 = no clouds, 1 = overcast
    float groundFog = 1.0f;    // 0 = none, 1 = normal (thick in the morning), 2 = a lot
    bool cloudShadows = true;
    float bloom = 0.5f;        // glow around bright things, 0 = off
    bool swaying = true;       // leaves and plants move in the wind
    int reflections = 1;       // 0 off, 1 normal, 2 high
    int ambientOcclusion = 1;  // soft shadow in corners and under things, for everything on screen: 0 off, 1 normal, 2 high
    float aoStrength = 1.0f;
    float bumps = 1.0f;        // generated normals: a bumpy surface made from each texture's light and dark pixels (0 = off)
    float coat = 1.0f;         // coated textures: a fine noise over each pixel (0 = off)
    bool materials = true;     // texture materials: shiny, metal, glowing
    bool hudGlow = true;
    bool guessMaterials = true; // textures nobody set up get a material guessed from their name       // the HUD's glow, and its light on the helmet's 3D parts
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
    float width = 0.6f, height = 1.8f, eyeHeight = 1.62f;   // the player's size (blocks): it fits through gaps wider and taller than this
    float stepHeight = 0.6f;                          // walks up ledges this high by itself
    float ballSize = 1.0f;                            // the morph ball's size (blocks)
    int morphBall = 1, bombs = 1;                     // 0 none, 1 from the start, 2 found later (a key pickup called "Morph Ball" / "Bombs")
    float ballSpeed = 7;                              // rolling speed (blocks / s)
    float bombJump = 2.2f;                            // how high a bomb right under the ball throws it (blocks)
    int bombsAtOnce = 3; float bombFuse = 0.9f, bombReach = 1.3f, bombSphere = 3.0f;   // bombSphere: the impact sphere (blocks across, 0: none)   // bombs out at the same time; seconds until they go off; how far they reach (blocks)
    std::string ballModel = "@morph-ball";
    float jumpSpeed() const { return std::sqrt(2 * gravity * std::max(0.1f, jumpHeight)); }
    float bombJumpSpeed() const { return std::sqrt(2 * gravity * std::max(0.f, bombJump)); }
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
    static constexpr double HALF_W = 0.3, HEIGHT = 1.8, EYE = 1.62;    // the default size (blocks)
    double halfW = HALF_W, height = HEIGHT, eyeH = EYE, stepH = 0.6;   // this player's size (from the game's player rules)
    void applySize(const PlayerRules& r);
    bool ball = false;                                // rolled up into the morph ball (a small round body that keeps rolling)
    V3 feet{ 0.5, 0, 0.5 }, vel{ 0, 0, 0 };
    double yaw = 0, pitch = 0;      // radians; yaw 0 looks toward +Z
    bool flying = true, onGround = false;
    V3 eye() const { return { feet.x, feet.y + eyeH, feet.z }; }
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
