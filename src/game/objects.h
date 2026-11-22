#pragma once
// Objects (G2): things placed in the world that are not blocks - props now, doors, platforms, pickups later.
// A block model is a small box of blocks built in the editor ("Make into object"); an object places a model
// anywhere, turned and sized freely. Collision comes from the model's own blocks.
#include "game.h"
#include "platform.h"
#include "particles.h"
#include "sound.h"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mc {

// a model's box: size in blocks and the point that sits at the object's position
struct ModelFrame { double sx = 0, sy = 0, sz = 0; V3 pivot{ 0, 0, 0 }; };

struct BlockModel {
    std::string name;
    int sx = 0, sy = 0, sz = 0;
    std::vector<BlockId> cells;                       // x fastest, then z, then y
    V3 pivot{ 0, 0, 0 };                              // the point that sits at the object's position (bottom middle)
    BlockId at(int x, int y, int z) const { return x < 0 || y < 0 || z < 0 || x >= sx || y >= sy || z >= sz ? AIR : cells[((size_t)y * sz + z) * sx + x]; }
    void resize(int x, int y, int z) { sx = x; sy = y; sz = z; cells.assign((size_t)x * y * z, AIR); pivot = { x * 0.5, 0, z * 0.5 }; }
    void put(int x, int y, int z, BlockId v) { cells[((size_t)y * sz + z) * sx + x] = v; }
    int blockCount() const { int n = 0; for (BlockId v : cells) n += v != AIR; return n; }
    operator ModelFrame() const { return { (double)sx, (double)sy, (double)sz, pivot }; }
    bool save(const std::string& path) const;
    bool load(const std::string& path);
};

// copy the blocks between lo and hi (inclusive). trim: shrink the box to the blocks that are there.
BlockModel modelFromWorld(const World& w, I3 lo, I3 hi, const std::string& name, bool trim = true, I3* corner = nullptr);   // corner: where the copy starts
// the collision boxes of a model, in model space (blocks), merged into as few boxes as possible
std::vector<Box3> modelBoxes(const BlockModel& m, const BlockRegistry& reg);

enum class Collide { Exact, OneBox, None };
const char* collideName(Collide c);

enum class ObjKind { Prop, Door, Platform, Pickup, Checkpoint, Teleporter, Trigger, Light, Particles, MapCamera, MapStation, Enemy, SaveStation, Ambient, COUNT };
// the map: a Map Station (touch it) fires every map camera with the same ID; each camera looks all around and puts what it sees on the map
struct MapSettings { int id = 1; float range = 48; };
// an ambient light (like Skyrim's lighting templates): it fills the room it stands in (through the air, up to the walls and doors) with its
// own base light - a colour from each direction - its own fog, and its own colour look while you are in it
struct AmbientSettings {
    uint32_t up = 0x6F7F99, down = 0x2E2A26, north = 0x4A5466, south = 0x4A5466, east = 0x4A5466, west = 0x4A5466;   // the light from above, below, and each side
    float strength = 1;               // how bright
    float mix = 1;                    // how much it takes the place of the sky's light (0: only adds)
    uint32_t fog = 0x5A6E8A; float fogAmount = 0;   // its fog's colour and thickness (0: none)
    float reach = 48;                 // how far it fills (blocks)
    int blend = 3;                    // how many blocks it spills through doors and openings (soft change from one room to the next)
    std::string look;                 // its colour look while you are in it ("": the world's / the zone's)
};
const char* ambientPresetName(int i);   // presets to start from (nullptr past the last)
AmbientSettings ambientPreset(int i);
struct ScanInfo {   // can be scanned (Scan Visor): which scan entry it shows (scans.txt, by ID: objects share entries)
    bool on = false;
    int id = 0;                                       // the scan entry (0: none yet)
    std::string legacyName, legacyCategory, legacyText; bool legacyImportant = false; float legacyTime = 0;   // (older files kept the text on the object: made into an entry when loaded)
};
const char* kindName(ObjKind k);

// a door: its model is stretched to fill Width x Height blocks; a forcefield sits in its opening
struct DoorSettings {
    double w = 2, h = 3;                              // blocks
    uint32_t color = 0x3AA0FF;                        // the forcefield's colour (later: which weapon opens it)
    float stayOpen = 3;                               // seconds before it closes again (it waits while you stand in it)
    bool field = true;                                // false: a plain door that opens when you walk up to it
    std::string key;                                  // needs this key (a pickup) to open; empty: no key needed
    int fieldSide = 0;                                // 0 both sides, 1 front (the model's -Z side), 2 back
    float fieldGap = 0.06f;                           // how far in front of the door the field floats (blocks)
    bool fieldOnParts = false;                        // the field shimmers on the door's moving parts instead of a flat sheet
    std::string fieldParts;                           // which moving parts (names, comma separated); empty: all of them
    std::string openedBy;                             // which weapon opens it: empty any shot, a beam's name, or "Missile"
    SoundUse openSound, closeSound;                   // its own sounds (none: the game's door sounds, Player > Sounds)
};
bool doorOpensFor(const DoorSettings& d, const std::string& weapon, bool missile);

enum class PickupType { Health, Key, Collectible };
struct PickupSettings {
    PickupType type = PickupType::Health;
    float amount = 25;                                // health it gives
    std::string name = "Gem";                         // keys and collectibles: what it is ("Red key", "Gem" ...)
    float respawn = 0;                                // seconds until it comes back (0: never)
};
struct TeleportSettings {
    std::string world;                                // empty: this world; else a world folder of this game
    std::string target;                               // the name of an object there (empty: that world's spawn point)
};
enum class TriggerAction { Message, Kill, Heal, Checkpoint };
struct TriggerSettings {
    TriggerAction action = TriggerAction::Message;
    std::string text = "Hello!";
    float amount = 100;                               // heal: how much
    bool once = true;                                 // only the first time you walk in
};

struct WorldObject {
    uint32_t id = 0;
    std::string name, folder = "Props", model;
    V3 pos{ 0, 0, 0 };                                // where the model's pivot sits
    double yaw = 0;                                   // degrees, around the up axis
    double scale = 1;                                 // props: the same in every direction (doors use Width x Height instead)
    Collide collide = Collide::Exact;
    ObjKind kind = ObjKind::Prop;
    DoorSettings door;
    PlatformSettings platform;                        // platforms and elevators: the path it follows
    PickupSettings pickup; TeleportSettings teleport; TriggerSettings trigger;
    V3 size{ 2, 2, 2 };                               // trigger zones: their box (blocks)
    LightSettings light;                              // lights (and glowing emitters)
    ParticleSettings particles;                       // particle emitters
    MapSettings map;                                  // map cameras and Map Stations
    ScanInfo scan;                                    // can be scanned (Scan Visor)
    int enemyType = 0;                                // enemies: which enemy type (Player editor > Enemies, by ID)
    AmbientSettings ambient;                          // ambient lights
    struct { bool energy = true, ammo = true; std::string anim; std::string title, text; bool ask = false; } save;   // Save Stations: what they fill up; the model's animation while saving ("": one called "save", "-": none);
                                                                                                         // the "Game Saved!" pop-up's title and text ("": automatic); ask: the old way (a Yes / No question first)
};
V3 objectStretch(const WorldObject& o, const ModelFrame& m);   // how much it is sized along its own x, y, z

const char* pickupName(PickupType t);
const char* triggerName(TriggerAction a);

// what you carry in Play mode: keys, collectibles; which pickups are gone (and when they come back)
struct PlayInventory {
    std::set<std::string> keys;
    std::map<std::string, int> counts;
    std::map<uint32_t, float> taken;                  // object id -> seconds until it is back (< 0: gone for good)
    bool has(uint32_t id) const { return taken.count(id) > 0; }
    // picks it up (false if it is not there): health goes up to at most maxHealth
    bool collect(const WorldObject& o, float& health, float maxHealth, std::string& said);
    void tick(float dt);
};

// a door in Play mode: shot -> the field dissolves -> it opens -> stays open -> closes -> the field comes back
struct DoorRuntime {
    enum State { Closed, Dissolving, Opening, Open, Closing, Restoring };
    State state = Closed; float t = 0;
    float field = 1;                                  // 1: the forcefield is fully there
    float hitU = 0.5f, hitV = 0.5f;                   // where it was hit (0..1 across the opening), the dissolve starts there
    static constexpr float dissolveTime = 0.35f, restoreTime = 0.45f;
    bool shoot(float u, float v);                     // true if the shot does something
    void open() { shoot(0.5f, 0.5f); }               // plain doors (no forcefield) open when you come near
    void tick(float dt, bool playerNear, float openLen, float closeLen, float stayOpen);
    bool blocks() const { return state != Opening && state != Open; }
    int clip = -1; float clipTime = 0;                // which animation to show: 0 open, 1 close, -1 none (as imported = closed)
};

// the forcefield's place: a thin box filling the opening (opening: in model space)
Box3 fieldBox(const WorldObject& o, const ModelFrame& m, const Box3& opening);
void fieldQuad(const WorldObject& o, const ModelFrame& m, const Box3& opening, V3& origin, V3& across, V3& up, int side = 0);   // side 0 front, 1 back
Box3 turnedBoxPublic(const WorldObject& o, const ModelFrame& m, const Box3& modelBox);   // a model-space box, placed and turned like the object
bool fieldPartOn(const DoorSettings& d, const std::string& partName);    // is the field drawn on this moving part?
V3 objectPoint(const WorldObject& o, const ModelFrame& m, V3 local);              // model space -> world
std::vector<Box3> objectBoxes(const WorldObject& o, const ModelFrame& m, const std::vector<Box3>& mboxes);   // world space
Box3 objectBounds(const WorldObject& o, const ModelFrame& m);
bool rayBox(V3 o, V3 d, const Box3& b, double& t);                                  // t = where the ray enters

bool saveObjects(const std::string& path, const std::vector<WorldObject>& objs);
bool loadObjects(const std::string& path, std::vector<WorldObject>& objs);

// all block models of a project: <project>/models/<name>.txt
struct ModelLibrary {
    std::string dir;
    std::map<std::string, BlockModel> models;
    void loadAll(const std::string& folder);
    bool store(const BlockModel& m);                  // adds / replaces and saves
    bool remove(const std::string& name);
    std::string uniqueName(const std::string& base) const;
    const BlockModel* find(const std::string& n) const { auto it = models.find(n); return it == models.end() ? nullptr : &it->second; }
};

// selection box tool: what an action would change (for one undo step)
std::vector<Edit> fillEdits(const World& w, I3 lo, I3 hi, BlockId v);               // v = AIR deletes
std::vector<Edit> pasteEdits(const World& w, const BlockModel& m, I3 at, bool skipAir = true);   // at = the model's lowest corner

} // namespace mc
