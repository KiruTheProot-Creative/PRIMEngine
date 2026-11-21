// game.h : creative-mode player, undo history, world info file and settings. No Windows code: testable anywhere.
#pragma once
#include "../world/world.h"
#include "environment.h"
#include <cmath>
#include <string>
#include <vector>

namespace mc {

// what the player's keys do (Options > Controls): two keys each (Windows key codes: 1 left mouse, 2 right mouse, 'W'...; 0: none)
enum class Act { Forward, Back, Left, Right, Jump, Sprint, Walk, Fire, Missile, LockOn, Use, Visor, Ball, NextMissile, Map, COUNT };
const char* actName(Act a);   // "Move forward"...
const char* actKey(Act a);    // "forward"... (in settings.txt)
struct KeyBinds {
    int key[(int)Act::COUNT][2];
    KeyBinds();               // the usual keys
    bool down(Act a, bool (*isDown)(int)) const { for (int k : key[(int)a]) if (k && isDown(k)) return true; return false; }
    bool uses(Act a, int vk) const { return vk && (key[(int)a][0] == vk || key[(int)a][1] == vk); }
    void set(Act a, int slot, int vk);   // another action using that key loses it (no key does two things)
};
const char* keyNameFallback(int vk);   // "Left mouse", "Space", "W", "F5"... ("" when unknown: the app asks Windows)

struct Settings {
    int renderDistance = 10;   // chunks around you, sideways
    int verticalDistance = 5;  // chunks above and below you
    float fov = 70, mouseSensitivity = 1.f, uiScale = 1.f;
    float soundVolume = 1.f;   // all sounds
    bool vsync = true;
    bool blockPreview = true;  // see-through preview where the next block goes
    float timeOfDay = 10.f;    // hours, 0 .. 24
    bool dayCycle = false;     // the sun moves (one day = 20 minutes)
    int shadowQuality = 1;     // 0 off, above: on (four maps, sharp near you, coarser farther out; their size: shadowRes)
    float shadowDistance = 160; // blocks: how far shadows reach (the sharpness near you does not depend on it, nor on the render distance)
    int shadowSoft = 2;        // shadow edges: 0 sharp, 1 smooth, 2 soft like real sunlight (sharp near what casts them, softer farther away)
    float shadowSoftness = 1;  // how soft (smooth / realistic)
    float cloudCover = 0.5f;   // 0 = no clouds, 1 = overcast
    float groundFog = 1.0f;    // 0 = none, 1 = normal (thick in the morning), 2 = a lot
    bool cloudShadows = true;
    float bloom = 0.5f;        // glow around bright things, 0 = off
    bool swaying = true;       // leaves and plants move in the wind
    int reflections = 1;       // 0 off, 1 normal, 2 high
    float renderScale = 1;     // the 3D world drawn at this part of the screen's size (0.5 .. 2: below 1 faster, above 1 supersampled); the HUD and menus stay sharp
    int antiAlias = 1;
    int upscaler = 1;          // fitting the world to the screen: 0 Sharp (this frame only), 1 PrimeScaler (rebuilt from several frames; at 100% the smoothest edges)
    float upSharp = 0.5f;      // PrimeScaler's sharpening
    int pixelShadows = 0;      // shadows snapped to the texture's pixels: 0 off, 16 or 32 a block
    int probes = 0;            // light probes (bounce light everywhere, through the blocks): 0 off, 1 wide (every 4 blocks), 2 fine (every 2 blocks)
    bool showFps = false;      // frames a second in the corner         // smoothing jagged edges: 0 off, 1 smooth, 2 smoother (looks further along each edge)
    int reflectRes = 0;        // reflection resolution: 0 full, 1 three quarters, 2 half, 3 a quarter (rays traced smaller, laid over the full picture)
    int ambientOcclusion = 1;  // soft shadow in corners and under things, for everything on screen: 0 off, 1 normal, 2 high
    float aoStrength = 1.0f;
    float bumps = 1.0f;        // generated normals: a bumpy surface made from each texture's light and dark pixels (0 = off)
    float coat = 1.0f;         // coated textures: a fine noise over each pixel (0 = off)
    bool materials = true;     // texture materials: shiny, metal, glowing
    bool hudGlow = true;
    bool guessMaterials = true; // textures nobody set up get a material guessed from their name       // the HUD's glow, and its light on the helmet's 3D parts
    int shadowRes = 2048;      // each shadow map's size (pixels)
    float brightness = 1.0f;   // the whole picture
    bool fullscreen = false;   // borderless, over the whole screen
    float volWeapons = 1, volMovement = 1, volEnemies = 1, volWorld = 1;   // sound groups (times soundVolume, the master)
    float volMenus = 1, volMusic = 0.8f;
    bool invertY = false;      // the mouse: up looks down
    int lockMode = 0;          // lock-on: 0 as the game says, 1 hold the button, 2 a press locks / the next lets go
    bool caveCulling = true;   // chunks hidden behind walls / in rock are not drawn
    float screenShake = 1;     // bosses, slams, roars shake the view this much (0: never)
    bool eyeAdapt = true;      // the picture gets brighter in dark places and darker in bright ones (as the world's look says)
    bool filmFx = true;        // the look's film grain and lens colour fringes
    bool rayTracing = true;    // rays through the blocks (a copy of the world on the graphics card): off: only what is on screen and shadow maps
    bool rtReflect = true, rtObjects = true, rtShadows = true;   // ... reflections of the blocks (off screen too), objects and enemies in them, lamp shadows through the blocks
    int lampBlocks = 2;        // glowing blocks: 0 classic (light spreads block by block), 1 real light, 2 real light and shadows
    float lampFill = 0.35f;    // real lamp blocks: how much of the classic spreading light stays as a soft fill (0 none .. 1 all)
    float motionBlur = 0;      // the picture smeared as the camera moves (0 off .. 1: as long as a sixtieth of a second)
    bool lightFog = true;
    bool planarFloors = true;
    bool lensFlares = true;    // the sun makes a lens flare (as much as the area's look says)  // the shiny floor or water near you is reflected exactly (the world drawn once more)
    int bounce = 1;            // bounce light (screen-space GI): 0 off, 1 normal, 2 far-reaching      // lights glow in the air (beams, halos)
    int lightShadows = 4;      // how many lights (the nearest that cast shadows) get real shadows (shadow maps): 0 .. 128
    float lightShadowDist = 48; // ... only lights this near (blocks); they fade out toward it
    int lightShadowRes = 512;  // each side of a light's shadow map: 128, 256, 512
    float lampDistance = 64;   // real lamp blocks shine this far away (blocks), fading out toward it
    KeyBinds keys;
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

struct Controls { bool forward = false, back = false, left = false, right = false, jump = false, sneak = false, sprint = false;
                  bool orbit = false; V3 orbitAround{ 0, 0, 0 };   // lock-on: strafing alone circles this point at the same distance
};

// the rules of the player while playing (the game maker sets these)
struct PlayerRules {
    float walkSpeed = 4.32f, sprintSpeed = 5.61f;   // blocks per second
    float accelTime = 0.12f, stopTime = 0.25f;        // seconds to get up to speed / to stop from walking speed (0: at once)
    float airControl = 0.6f;                          // how much you can steer and slow down in the air (1: as on the ground, 0: not at all)
    float jumpHeight = 1.25f;                         // blocks
    float gravity = 32;                               // blocks per second per second
    float maxHealth = 100;
    float lavaDamage = 40;                            // health per second in lava
    float voidDepth = -64;                            // fall below this height and you die
    float respawnDelay = 2;                           // seconds before you come back at the spawn point (at least as long as the death sequence)
    // the death sequence (like Metroid Prime): the hit (slow motion), the visor filling with static, the suit shutting down, the picture switching off
    bool deathFx = true;
    float deathHit = 0.4f, deathStatic = 1.2f, deathShutdown = 1.5f, deathOff = 0.5f;   // seconds of each part (0: left out)
    bool deathSlowMo = true, deathDrop = true, deathTvOff = true, deathSkip = true;     // slow motion; the view sinks and tilts; an old TV switching off; any button skips to the end
    float deathStaticAmount = 1; uint32_t deathStaticColor = 0x9FE8FF;
    float deathLength() const { return deathFx ? std::max(0.f, deathHit) + std::max(0.f, deathStatic) + std::max(0.f, deathShutdown) + std::max(0.f, deathOff) : 0.f; }
    float fallDamage = 0;                             // health per block fallen beyond 4 blocks (0 = off)
    bool canSprint = true;
    float width = 0.6f, height = 1.8f, eyeHeight = 1.62f;   // the player's size (blocks): it fits through gaps wider and taller than this
    float stepHeight = 0.6f;                          // walks up ledges this high by itself
    float ballSize = 1.0f;                            // the morph ball's size (blocks)
    uint32_t morphColor = 0xFFB040; float morphFx = 1; bool morphHudFx = true;   // morphing: an energy shell, sparks and a flash in this colour (0: none); the HUD glows too
    bool quickSave = false;                           // the pause menu's Quick save works anywhere (off: only Save Stations save, like Prime)
    float bounceDamage = 0.5f;                        // a shot bounced back by a door that needs another weapon: hurts you this much of its damage (0: never)
    int morphBall = 1, bombs = 1;                     // 0 none, 1 from the start, 2 found later (a key pickup called "Morph Ball" / "Bombs")
    float ballSpeed = 7;                              // rolling speed (blocks / s)
    float bombJump = 2.2f;                            // how high a bomb right under the ball throws it (blocks)
    int bombsAtOnce = 3; float bombFuse = 0.9f, bombReach = 1.3f, bombSphere = 3.0f;   // bombSphere: the impact sphere (blocks across, 0: none)   // bombs out at the same time; seconds until they go off; how far they reach (blocks)
    std::string ballModel = "@morph-ball";
    // how the ball rolls (Morph Ball 3D editor): the model's axle (its own directions) is pulled across the way it rolls,
    // so it rolls like a wheel around it and always comes back to the middle; pivot: the point it turns around (from the model's middle, in ball sizes)
    V3 ballAxle{ 1, 0, 0 }; float ballBalance = 0; V3 ballPivot{ 0, 0, 0 };   // balance: how fast it rights itself (0: tumbles freely)
    bool ballTrail = false; V3 trailPoint{ 0.5, 0, 0 };   // a glowing tail while rolling; where it starts: behind, up, to the side (blocks, from the ball's middle)
    uint32_t trailColor = 0x60C8FF; float trailSize = 0.16f, trailLife = 0.35f;
    int trailStyle = 0;                               // 0 glowing dots, 1 a ribbon of light (a streak behind the ball), 2 both
    float ribbonWidth = 0.8f, ribbonTaper = 0.6f, ribbonGlow = 1.5f; bool ribbonStreaks = true;
    int ribbonFacing = 3;                             // the ribbon's turn: 0 always facing the camera, 1 upright (a standing band), 2 flat (lying on the floor), 3 a cross of both (solid from every side)   // the ribbon: how wide (blocks), how much thinner at its end (0..1), how bright; thin lines along its edges that last longer
    // set while playing by the items you have (Items tab); not saved with the rules
    float damageTaken = 1;                            // suits: how much of the damage gets through
    bool freeInLiquid = false;                        // the Gravity Suit: water and lava do not slow you
    int airJumps = 0;                                 // Space Jump: jumps in the air
    bool speedBooster = false; float boostAfter = 1.5f, boostSpeed = 2;   // run this long, then this much faster
    bool springBall = false; float springJump = 1.2f; // jump while rolled up
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
    int airJumpsUsed = 0; bool jumpPrev = false;       // Space Jump
    float runTime = 0; bool boosting = false;          // Speed Booster: how long you have been running; at full speed
    V3 feet{ 0.5, 0, 0.5 }, vel{ 0, 0, 0 };
    double yaw = 0, pitch = 0;      // radians; yaw 0 looks toward +Z
    bool flying = true, onGround = false;
    V3 eye() const { return { feet.x, feet.y + eyeH, feet.z }; }
    V3 forward() const { return { std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch) }; }
    void look(double dx, double dy);                     // mouse movement in radians
    void update(const World& w, const Controls& c, double dt, const PlayerRules& rules = PlayerRules{});
    void toggleFly() { flying = !flying; vel.y = 0; }
    V3 dashVel{ 0, 0, 0 }; double dashT = 0; bool dashAir = false;   // lock-on dash: a hop to the side until you land
    void dash(V3 sideways, double maxTime = 0.9) { dashVel = { sideways.x, 0, sideways.z }; dashT = maxTime; dashAir = false; }
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
    std::string look = "Natural";                   // the colour look (Looks: tone curve, exposure, bloom, grading ...)
    bool trueDark = true;                           // no sky light under roofs and in caves: only lights, lamps and glowing things light them
    float nightDark = 0.85f;                        // how dark nights are: 0 bright (the old nights) .. 1 pitch dark (only the moon, faintly, and your lights)
    float minLight = 0; uint32_t minLightColor = 0x8C9AB0;   // true darkness: the least light in closed spaces (0 none .. 1 bright), in this colour
    float startHour = 10; bool keepClock = false;   // the clock when the world opens (and play starts): this hour, or (keepClock) where it was left
    bool hasSpawn = false; V3 spawn{ 0.5, 0, 0.5 }; double spawnYaw = 0;   // where the player starts when playing
    std::vector<EnvZone> zones;
    bool load(const std::string& dir);
    bool save(const std::string& dir) const;
    void applyGenerator(Generator& g, const BlockRegistry& r) const;
};

} // namespace mc
