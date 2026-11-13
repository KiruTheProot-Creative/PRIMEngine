#pragma once
// Imported models (G2): triangle meshes from Blockbench (.bbmodel), .obj and glTF (.gltf / .glb), with one texture.
// They are stored in the project as <name>.pmesh and placed like block models; collision comes from the shape itself
// (turned into small cubes, then merged into boxes).
#include "objects.h"
#include <string>
#include <vector>

namespace mc {

struct MeshVert { float p[3], uv[2], n[3]; };

// a part that moves in an animation (FBX: a node with animation curves); its vertices are listed in MeshModel::vpart
struct MeshPart {
    std::string name;
    double parent[16];                                // the parent's place at rest (column-major, file units)
    double t[3], r[3], s[3], pre[3], pivot[3];        // its own rest move / turn (degrees, X then Y then Z) / size, pre-turn, turning point
    int parentPart = -1;                              // the nearest moving part above it (it moves along with that one), -1: none
    int order = 0;                                    // FBX rotation order (0 XYZ ...)
};
struct AnimTrack { int part = 0, prop = 0, axis = 0; std::vector<float> time, value; };   // prop: 0 move, 1 turn, 2 size
struct AnimClip { std::string name; float length = 0; std::vector<AnimTrack> tracks; };

struct MeshModel {
    std::string name, source;                         // source: the file it was imported from
    std::vector<MeshVert> raw;                        // positions in the file's units
    std::vector<uint32_t> idx;                        // triangles; cross(b - a, c - a) points outwards (like the block meshes)
    int tw = 1, th = 1; std::vector<uint8_t> tex{ 255, 255, 255, 255 };   // RGBA, one picture (several are packed into one)
    double unitScale = 1;                             // blocks per file unit (Blockbench: 1/16)
    std::vector<MeshPart> parts; std::vector<uint16_t> vpart;   // vpart: per vertex, 0xFFFF = does not move
    std::vector<AnimClip> clips;                      // e.g. "open" and "close" of a door
    // worked out by update()
    V3 lo{ 0, 0, 0 }, hi{ 0, 0, 0 }; double sx = 0, sy = 0, sz = 0; V3 pivot{ 0, 0, 0 }; bool transparent = false;
    void update();
    ModelFrame frame() const { return { sx, sy, sz, pivot }; }
    V3 blockPos(const MeshVert& v) const { return { (v.p[0] - lo.x) * unitScale, (v.p[1] - lo.y) * unitScale, (v.p[2] - lo.z) * unitScale }; }
    size_t triangles() const { return idx.size() / 3; }
    // the corners with the moving parts where an animation has them at `time` seconds (no clip: as imported)
    std::vector<MeshVert> posed(int clip, float time) const;
    int findClip(const std::string& name) const;   // not case-sensitive; -1 if there is none
    bool save(const std::string& path) const;
    bool load(const std::string& path);
};

// by the file's extension: .bbmodel, .obj, .gltf, .glb, .fbx (text or binary). On failure: false and a message for the user.
bool importModel(const std::string& path, MeshModel& out, std::string& error);
bool isModelFile(const std::string& path);
// collision: the inside of the shape as boxes, in model space (blocks, 0 .. size)
std::vector<Box3> meshBoxes(const MeshModel& m, int maxCells = 24, bool stillOnly = false);   // cubes of at least 1/8 block; stillOnly: without the moving parts
Box3 doorOpening(const MeshModel& m);   // a door's opening (model space, blocks): where its moving parts are, else the middle of it

// built-in looks for helper objects: "@health", "@key", "@gem" (pickups), "@checkpoint", "@teleporter" (pads), "@zone" (a 1-block cube), "@light", "@emitter"
bool builtinShape(const std::string& name, MeshModel& out);
std::vector<std::string> builtinShapeNames();

// three small example models (for screenshots and tests): a Blockbench lamp post, an .obj rock, a glTF crystal
std::vector<std::string> writeDemoModels(const std::string& dir);

} // namespace mc
