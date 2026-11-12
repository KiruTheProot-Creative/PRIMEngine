#pragma once
// Objects (G2): things placed in the world that are not blocks - props now, doors, platforms, pickups later.
// A block model is a small box of blocks built in the editor ("Make into object"); an object places a model
// anywhere, turned and sized freely. Collision comes from the model's own blocks.
#include "game.h"
#include <map>
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

enum class ObjKind { Prop, Door };
const char* kindName(ObjKind k);

// a door: its model is stretched to fill Width x Height blocks; a forcefield sits in its opening
struct DoorSettings {
    double w = 2, h = 3;                              // blocks
    uint32_t color = 0x3AA0FF;                        // the forcefield's colour (later: which weapon opens it)
    float stayOpen = 3;                               // seconds before it closes again (it waits while you stand in it)
    bool field = true;                                // false: a plain door that opens when you walk up to it
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
};
V3 objectStretch(const WorldObject& o, const ModelFrame& m);   // how much it is sized along its own x, y, z

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
void fieldQuad(const WorldObject& o, const ModelFrame& m, const Box3& opening, V3& origin, V3& across, V3& up);
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
