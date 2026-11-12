#pragma once
// Imported models (G2): triangle meshes from Blockbench (.bbmodel), .obj and glTF (.gltf / .glb), with one texture.
// They are stored in the project as <name>.pmesh and placed like block models; collision comes from the shape itself
// (turned into small cubes, then merged into boxes).
#include "objects.h"
#include <string>
#include <vector>

namespace mc {

struct MeshVert { float p[3], uv[2], n[3]; };

struct MeshModel {
    std::string name, source;                         // source: the file it was imported from
    std::vector<MeshVert> raw;                        // positions in the file's units
    std::vector<uint32_t> idx;                        // triangles; cross(b - a, c - a) points outwards (like the block meshes)
    int tw = 1, th = 1; std::vector<uint8_t> tex{ 255, 255, 255, 255 };   // RGBA, one picture (several are packed into one)
    double unitScale = 1;                             // blocks per file unit (Blockbench: 1/16)
    // worked out by update()
    V3 lo{ 0, 0, 0 }, hi{ 0, 0, 0 }; double sx = 0, sy = 0, sz = 0; V3 pivot{ 0, 0, 0 }; bool transparent = false;
    void update();
    ModelFrame frame() const { return { sx, sy, sz, pivot }; }
    V3 blockPos(const MeshVert& v) const { return { (v.p[0] - lo.x) * unitScale, (v.p[1] - lo.y) * unitScale, (v.p[2] - lo.z) * unitScale }; }
    size_t triangles() const { return idx.size() / 3; }
    bool save(const std::string& path) const;
    bool load(const std::string& path);
};

// by the file's extension: .bbmodel, .obj, .gltf, .glb. On failure: false and a message for the user.
bool importModel(const std::string& path, MeshModel& out, std::string& error);
bool isModelFile(const std::string& path);
// collision: the inside of the shape as boxes, in model space (blocks, 0 .. size)
std::vector<Box3> meshBoxes(const MeshModel& m, int maxCells = 24);   // cubes of at least 1/8 block

// three small example models (for screenshots and tests): a Blockbench lamp post, an .obj rock, a glTF crystal
std::vector<std::string> writeDemoModels(const std::string& dir);

} // namespace mc
