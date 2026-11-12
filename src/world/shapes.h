// shapes.h : the block shape collection (half blocks, ramps, stairs, pillars, arches, ...).
// Any block can be placed in any shape and in any of the 24 rotations (like 7 Days to Die).
// Every shape x rotation is prepared once at startup: triangles, which block sides it covers fully,
// and boxes for collision and aiming. Shape ids are stored in worlds: new shapes are only ever appended.
#pragma once
#include "blocks.h"
#include <bitset>
#include <string>
#include <vector>

namespace mc {

struct SBox { float lo[3], hi[3]; };
struct STri { float p[3][3]; float n[3][3]; float fn[3]; };   // positions in 0..1 block space, vertex normals, face normal
using SideMask = std::bitset<256>;                             // 16 x 16 samples of one block side

enum class Place : uint8_t {
    Fixed,     // never turned (full block, ball, cylinder ...)
    Yaw,       // turns to face away from you
    Facing,    // turns to face away from you, upside down when placed against a ceiling or the top half of a wall
    Surface,   // sticks to the side you click (planes, ladders)
    Axis,      // stands out of the side you click (pillars, poles)
};
struct ShapeInfo { std::string key, name, category; bool twoSided = false; Place place = Place::Facing; };

struct ShapeVariant {
    std::vector<STri> tris[7];     // [0..5]: lying flat on that block side (hidden by a neighbour covering it); [6]: the rest
    SideMask mask[6];              // what the shape covers on each side
    bool full[6] = {};             // covers that side completely
    std::vector<SBox> boxes;       // collision and aiming
    SBox bounds{};                 // around all boxes (block outline)
};

struct Rot { int m[3][3]; };       // a 90-degree rotation (columns: where +X, +Y, +Z go)

class Shapes {
public:
    enum : int { DEFAULT = 0, FULL = 1, CROSS = 2, PLANE = 3 };   // 0 = "the block's own shape"
    static const Shapes& get();
    int count() const { return (int)infos.size(); }               // valid ids: 1 .. count()-1
    const ShapeInfo& info(int id) const { return infos[valid(id) ? id : FULL]; }
    const ShapeVariant& variant(int id, int rot) const { return vars[(size_t)(valid(id) ? id : FULL) * 24 + (rot >= 0 && rot < 24 ? rot : 0)]; }
    int find(const std::string& key) const;                        // 0 if there is none
    std::vector<std::string> categories() const;
    bool valid(int id) const { return id >= 1 && id < (int)infos.size(); }

    // rotations: 0 = as designed. Faces are F_PX .. F_NZ.
    static const Rot& rot(int r);
    static int rotFor(int fromA, int toA, int fromB, int toB);   // the rotation moving direction A to A' and B to B' (-1: none)
    static int turn(int r, int axis);                            // r, then a quarter turn around world axis 0=X 1=Y 2=Z
    static int sideAfter(int r, int side);                       // where a shape side ends up after rotation r
    static int legacyPlaneRot(int facing);                       // M2 planes faced +X turned to `facing`
    // automatic rotation when placing: n = side clicked (its outward direction), look = the way you face (horizontal),
    // upperHalf = clicked on the upper half of a wall
    static int placeRot(Place p, int n, int look, bool upperHalf);

private:
    Shapes();
    std::vector<ShapeInfo> infos;
    std::vector<ShapeVariant> vars;
};

// The stored shape, or the block's own one (cube / cross / plane) when none was chosen.
inline int resolveShape(BlockId v, const BlockDef& d) {
    int s = shapeOf(v);
    if (s && Shapes::get().valid(s)) return s;
    return d.shape == Shape::Cross ? Shapes::CROSS : d.shape == Shape::Plane ? Shapes::PLANE : Shapes::FULL;
}

inline int faceOfNormal(const float n[3]) {
    float ax = n[0] < 0 ? -n[0] : n[0], ay = n[1] < 0 ? -n[1] : n[1], az = n[2] < 0 ? -n[2] : n[2];
    if (ay >= ax && ay >= az) return n[1] >= 0 ? F_PY : F_NY;
    if (ax >= az) return n[0] >= 0 ? F_PX : F_NX;
    return n[2] >= 0 ? F_PZ : F_NZ;
}
// texture coordinates of a point inside the block on a face looking along `face` (textures stay world-aligned)
inline void faceUV(float lx, float ly, float lz, int face, float& u, float& v) {
    int a = face / 2; bool pos = (face % 2) == 0;
    if (a == 1) { u = lx; v = lz; } else if (a == 0) { u = pos ? 1 - lz : lz; v = 1 - ly; } else { u = pos ? lx : 1 - lx; v = 1 - ly; }
}
// Minecraft-style directional light: top 1.0, bottom 0.5, east/west 0.6, north/south 0.8
inline float normalShade(const float n[3]) { return n[0] * n[0] * 0.6f + n[1] * n[1] * (n[1] > 0 ? 1.0f : 0.5f) + n[2] * n[2] * 0.8f; }

} // namespace mc
