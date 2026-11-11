#include "room.h"
#include <fstream>
#include <sstream>

namespace px {

// ------------------------------------------------------------------ shapes
static const ShapeInfo kShapes[B_COUNT] = {
    { "Air", false, 8, 8, 8, "", 8 },
    { "Cube", false, 8, 8, 8, "", 8 },
    { "Slab", false, 4, 1, 7, "thickness", 8 },
    { "Stairs", true, 2, 2, 8, "steps", 0 },
    { "Slope", true, 8, 1, 8, "height", 8 },
    { "Outer corner", true, 8, 1, 8, "height", 8 },
    { "Inner corner", true, 8, 1, 8, "height", 8 },
    { "Beam", false, 4, 1, 7, "width", 8 },
    { "Pillar", false, 6, 1, 8, "thickness", 8 },
    { "Panel", true, 2, 1, 7, "thickness", 8 },
    { "Step", true, 4, 1, 8, "height", 8 },
    { "Frame", false, 2, 1, 7, "border", 16 },
    { "Round edge", true, 8, 8, 8, "", 8 },
    { "Arch", true, 8, 8, 8, "", 8 },
};
const ShapeInfo& shapeInfo(uint8_t t) { return kShapes[t < B_COUNT ? t : 0]; }

static const MaterialInfo kMats[MAT_COUNT] = {
    { "Metal", T_BLOCK, T_BLOCK, T_BLOCK, 0.12f, 1.f, { 0.42f, 0.47f, 0.50f } },
    { "Rock", T_ROCK, T_ROCK, T_ROCK, 0.03f, 0.f, { 0.42f, 0.40f, 0.36f } },
    { "Dirt", T_DIRT, T_DIRT, T_DIRT, 0.f, 0.f, { 0.40f, 0.28f, 0.17f } },
    { "Moss", T_MOSS, T_MOSS_SIDE, T_DIRT, 0.f, 0.f, { 0.24f, 0.44f, 0.15f } },
    { "Sand", T_SAND, T_SAND, T_SAND, 0.f, 0.f, { 0.72f, 0.62f, 0.42f } },
    { "Snow", T_SNOW, T_SNOW_SIDE, T_ROCK, 0.06f, 0.f, { 0.86f, 0.90f, 0.96f } },
    { "Ice", T_ICE, T_ICE, T_ICE, 0.55f, 0.f, { 0.45f, 0.68f, 0.84f } },
    { "Basalt", T_BASALT, T_BASALT, T_BASALT, 0.05f, 1.f, { 0.20f, 0.12f, 0.10f } },
    { "Lava", T_LAVA, T_LAVA, T_LAVA, 0.f, 1.f, { 0.95f, 0.45f, 0.08f } },
    { "Chozo stone", T_CHOZO, T_CHOZO, T_CHOZO, 0.04f, 0.f, { 0.66f, 0.56f, 0.38f } },
};
const MaterialInfo& materialInfo(uint8_t m) { return kMats[m < MAT_COUNT ? m : 0]; }

// ------------------------------------------------------------------ mood presets
const char* const kParticleNames[PT_COUNT] = { "None", "Dust", "Spores", "Snow", "Embers", "Rain" };
const char* const kSkyNames[SKY_COUNT] = { "None (haze colour)", "Gradient", "Clouds", "Stars" };
const char* const kMoodNames[MP_COUNT] = { "Default", "Tallon rain", "Tallon sunny", "Phendrana snow", "Magmoor caverns", "Chozo ruins", "Phazon mines", "Space station" };
void moodPreset(int preset, RoomMood& m) {
    RoomMood d;
    auto set = [&](V3 haze, float hs, float he, float ha, V3 sky, V3 gnd, bool sun, float el, float az, V3 sc, float ss, float bloom, int pt, float pa, V3 pc, int skyT, V3 top, V3 hor) {
        d.haze = haze; d.hazeStart = hs; d.hazeEnd = he; d.hazeAmount = ha; d.skyAmbient = sky; d.groundAmbient = gnd; d.sun = sun; d.sunElev = el; d.sunAzim = az;
        d.sunColor = sc; d.sunStrength = ss; d.bloom = bloom; d.particles = pt; d.particleAmount = pa; d.particleColor = pc; d.sky = skyT; d.skyTop = top; d.skyHorizon = hor;
    };
    switch (preset) {
    case MP_TALLON_RAIN: set({ 0.33f, 0.38f, 0.36f }, 10, 90, 0.9f, { 0.55f, 0.62f, 0.60f }, { 0.20f, 0.24f, 0.18f }, false, 50, 35, { 1, 1, 1 }, 0, 0.7f, PT_RAIN, 0.7f, { 0.7f, 0.8f, 0.85f }, SKY_CLOUDS, { 0.25f, 0.30f, 0.32f }, { 0.38f, 0.43f, 0.42f }); break;
    case MP_TALLON_SUN: set({ 0.55f, 0.68f, 0.75f }, 30, 220, 0.7f, { 0.45f, 0.52f, 0.60f }, { 0.22f, 0.20f, 0.14f }, true, 55, 40, { 1, 0.93f, 0.78f }, 1.0f, 0.8f, PT_SPORES, 0.25f, { 0.8f, 1, 0.6f }, SKY_CLOUDS, { 0.25f, 0.45f, 0.75f }, { 0.62f, 0.75f, 0.82f }); break;
    case MP_PHENDRANA: set({ 0.62f, 0.70f, 0.80f }, 12, 120, 0.9f, { 0.70f, 0.78f, 0.90f }, { 0.40f, 0.45f, 0.55f }, true, 22, 210, { 0.9f, 0.85f, 1 }, 0.5f, 1.0f, PT_SNOW, 0.8f, { 1, 1, 1 }, SKY_CLOUDS, { 0.42f, 0.50f, 0.66f }, { 0.68f, 0.74f, 0.84f }); break;
    case MP_MAGMOOR: set({ 0.22f, 0.08f, 0.04f }, 10, 110, 0.85f, { 0.22f, 0.12f, 0.09f }, { 0.30f, 0.12f, 0.06f }, false, 50, 35, { 1, 1, 1 }, 0, 1.2f, PT_EMBERS, 0.6f, { 1, 0.55f, 0.2f }, SKY_NONE, { 0, 0, 0 }, { 0, 0, 0 }); d.glowLight = 1.6f; break;
    case MP_RUINS: set({ 0.62f, 0.55f, 0.42f }, 25, 200, 0.75f, { 0.55f, 0.52f, 0.46f }, { 0.30f, 0.25f, 0.18f }, true, 60, 120, { 1, 0.9f, 0.7f }, 1.0f, 0.8f, PT_DUST, 0.4f, { 0.9f, 0.8f, 0.6f }, SKY_GRADIENT, { 0.35f, 0.50f, 0.70f }, { 0.75f, 0.68f, 0.55f }); break;
    case MP_PHAZON: set({ 0.02f, 0.05f, 0.12f }, 8, 80, 0.9f, { 0.10f, 0.14f, 0.25f }, { 0.05f, 0.06f, 0.10f }, false, 50, 35, { 1, 1, 1 }, 0, 1.3f, PT_SPORES, 0.5f, { 0.3f, 0.6f, 1 }, SKY_NONE, { 0, 0, 0 }, { 0, 0, 0 }); break;
    case MP_STATION: set({ 0.03f, 0.04f, 0.06f }, 30, 260, 0.6f, { 0.40f, 0.44f, 0.50f }, { 0.18f, 0.20f, 0.24f }, true, 15, 300, { 0.95f, 0.95f, 1 }, 0.8f, 0.9f, PT_NONE, 0.f, { 1, 1, 1 }, SKY_STARS, { 0, 0, 0.02f }, { 0.03f, 0.04f, 0.08f }); break;
    default: break;
    }
    m = d;
}
V3 sunDirection(const RoomMood& m) {
    float el = m.sunElev * 0.0174533f, az = m.sunAzim * 0.0174533f;
    return normalize({ std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az) });
}

V3 dirVec(int d) {
    static const V3 D[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    return D[d < 0 || d > 5 ? 2 : d];
}
static int dirIndex(int axis, int sign) { return axis * 2 + (sign < 0 ? 1 : 0); }
static int opposite(int d) { return d ^ 1; }
static int dirFromVec(V3 v) {
    float ax = std::fabs(v.x), ay = std::fabs(v.y), az = std::fabs(v.z);
    if (ax >= ay && ax >= az) return dirIndex(0, v.x >= 0 ? 1 : -1);
    if (ay >= az) return dirIndex(1, v.y >= 0 ? 1 : -1);
    return dirIndex(2, v.z >= 0 ? 1 : -1);
}
// the four directions perpendicular to `up`, in quarter-turn order around it
static int perpDir(int up, int spin) {
    int a = up / 2, b = (a + 1) % 3, c = (a + 2) % 3;
    const int seq[4][2] = { { b, 1 }, { c, 1 }, { b, -1 }, { c, -1 } };
    return dirIndex(seq[spin & 3][0], seq[spin & 3][1]);
}
struct Basis { V3 x, y, z; };   // local right / up / front in world space
static Basis basisOf(uint8_t rot) {
    int up = rotUp(rot); if (up > 5) up = 2;
    Basis b; b.y = dirVec(up); b.z = dirVec(perpDir(up, rotSpin(rot))); b.x = cross(b.y, b.z);
    return b;
}
static V3 toWorld(const Basis& b, V3 v) { return b.x * v.x + b.y * v.y + b.z * v.z; }
static V3 toLocal(const Basis& b, V3 v) { return { dot(v, b.x), dot(v, b.y), dot(v, b.z) }; }

uint8_t chooseRot(uint8_t type, int nx, int ny, int nz, V3 fwd, int spinOffset, bool flip) {
    const ShapeInfo& si = shapeInfo(type);
    int up;
    if (si.upright) up = (ny < 0) ? 3 : 2;
    else up = (nx || ny || nz) ? dirFromVec({ (float)nx, (float)ny, (float)nz }) : 2;
    if (flip) up = opposite(up);
    int best = 0; float bd = -1e9f;
    V3 f = fwd; if (up / 2 == 1) f.y = 0;   // upright shapes face along the ground
    for (int s = 0; s < 4; s++) { float d = dot(dirVec(perpDir(up, s)), f); if (d > bd) { bd = d; best = s; } }
    return makeRot(up, best + spinOffset);
}

// ---- local geometry, in a unit cell: x right, y up, z front (the high/back side of stairs and slopes is +Z)
static void localBoxes(uint8_t t, int size, std::vector<Box3>& o) {
    const ShapeInfo& si = shapeInfo(t);
    float f = si.sizeDiv ? (float)size / si.sizeDiv : 0.f;
    auto box = [&](float x0, float y0, float z0, float x1, float y1, float z1) { o.push_back({ { x0, y0, z0 }, { x1, y1, z1 } }); };
    const int N = 4;
    switch (t) {
    case B_CUBE: box(0, 0, 0, 1, 1, 1); break;
    case B_SLAB: box(0, 0, 0, 1, f, 1); break;
    case B_STAIRS: { int n = std::max(2, size); for (int i = 0; i < n; i++) box(0, (float)i / n, (float)i / n, 1, (float)(i + 1) / n, 1); break; }
    case B_SLOPE: for (int i = 0; i < N; i++) box(0, f * i / N, (float)i / N, 1, f * (i + 1) / N, 1); break;
    case B_CORNER: for (int i = 0; i < N; i++) box((float)i / N, f * i / N, (float)i / N, 1, f * (i + 1) / N, 1); break;
    case B_INNER: for (int i = 0; i < N; i++) { box((float)i / N, f * i / N, 0, 1, f * (i + 1) / N, 1); box(0, f * i / N, (float)i / N, (float)i / N, f * (i + 1) / N, 1); } break;
    case B_BEAM: case B_PILLAR: box(0.5f - f / 2, 0, 0.5f - f / 2, 0.5f + f / 2, 1, 0.5f + f / 2); break;
    case B_PANEL: box(0, 0, 0.5f - f / 2, 1, 1, 0.5f + f / 2); break;
    case B_STEP: box(0, 0, 0.5f, 1, f, 1); break;
    case B_FRAME: box(0, 0, 0, f, 1, 1); box(1 - f, 0, 0, 1, 1, 1); box(f, 0, 0, 1 - f, 1, f); box(f, 0, 1 - f, 1 - f, 1, 1); break;
    case B_ROUND: for (int i = 0; i < N; i++) { float y = (float)i / N; box(0, y, 1 - std::sqrt(std::max(0.f, 1 - y * y)), 1, y + 1.f / N, 1); } break;
    case B_ARCH: for (int i = 0; i < N; i++) { float y0 = (float)i / N, ym = y0 + 0.5f / N; box(0, y0, std::sqrt(std::max(0.f, 1 - ym * ym)), 1, y0 + 1.f / N, 1); } break;
    default: break;
    }
}
static unsigned fullMask(uint8_t t, int size) {   // local faces fully covered (bit = dir index)
    const unsigned PX = 1, NX = 2, PY = 4, NY = 8, PZ = 16, NZ = 32;
    switch (t) {
    case B_CUBE: return 63;
    case B_SLAB: return NY;
    case B_STAIRS: return NY | PZ;
    case B_SLOPE: return NY | (size >= 8 ? PZ : 0);
    case B_CORNER: return NY;
    case B_INNER: return NY | (size >= 8 ? PZ | PX : 0);
    case B_ROUND: return NY | PZ;
    case B_ARCH: return PY | PZ;
    default: (void)NX; (void)NZ; return 0;
    }
}

struct Poly { std::vector<V3> p, n; };   // convex polygon (or triangle) with per-vertex normals, local space
static void addQuad(std::vector<Poly>& o, V3 a, V3 b, V3 c, V3 d, V3 n) { o.push_back({ { a, b, c, d }, { n, n, n, n } }); }
static void addTri(std::vector<Poly>& o, V3 a, V3 b, V3 c, V3 n) { o.push_back({ { a, b, c }, { n, n, n } }); }
static V3 triNormal(V3 a, V3 b, V3 c, V3 inside) {   // outward normal of a face of a convex solid
    V3 n = normalize(cross(b - a, c - a)); if (dot(n, a - inside) < 0) n = n * -1.f; return n;
}
static void boxPolys(std::vector<Poly>& o, const Box3& b) {
    V3 m = b.mn, M = b.mx;
    addQuad(o, { M.x, m.y, m.z }, { M.x, M.y, m.z }, { M.x, M.y, M.z }, { M.x, m.y, M.z }, { 1, 0, 0 });
    addQuad(o, { m.x, m.y, m.z }, { m.x, m.y, M.z }, { m.x, M.y, M.z }, { m.x, M.y, m.z }, { -1, 0, 0 });
    addQuad(o, { m.x, M.y, m.z }, { m.x, M.y, M.z }, { M.x, M.y, M.z }, { M.x, M.y, m.z }, { 0, 1, 0 });
    addQuad(o, { m.x, m.y, m.z }, { M.x, m.y, m.z }, { M.x, m.y, M.z }, { m.x, m.y, M.z }, { 0, -1, 0 });
    addQuad(o, { m.x, m.y, M.z }, { M.x, m.y, M.z }, { M.x, M.y, M.z }, { m.x, M.y, M.z }, { 0, 0, 1 });
    addQuad(o, { m.x, m.y, m.z }, { m.x, M.y, m.z }, { M.x, M.y, m.z }, { M.x, m.y, m.z }, { 0, 0, -1 });
}
static void localPolys(uint8_t t, int size, std::vector<Poly>& o) {
    const ShapeInfo& si = shapeInfo(t);
    float h = si.sizeDiv ? (float)size / si.sizeDiv : 1.f;
    const int SEG = 8;
    switch (t) {
    case B_SLOPE: {
        V3 c{ 0.5f, h * 0.3f, 0.7f };
        addQuad(o, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 }, { 0, -1, 0 });
        addQuad(o, { 0, 0, 1 }, { 1, 0, 1 }, { 1, h, 1 }, { 0, h, 1 }, { 0, 0, 1 });
        addQuad(o, { 0, 0, 0 }, { 0, h, 1 }, { 1, h, 1 }, { 1, 0, 0 }, triNormal({ 0, 0, 0 }, { 0, h, 1 }, { 1, h, 1 }, c));
        addTri(o, { 0, 0, 0 }, { 0, 0, 1 }, { 0, h, 1 }, { -1, 0, 0 });
        addTri(o, { 1, 0, 0 }, { 1, 0, 1 }, { 1, h, 1 }, { 1, 0, 0 });
        break; }
    case B_CORNER: {
        V3 c{ 0.7f, h * 0.2f, 0.7f };
        addQuad(o, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 }, { 0, -1, 0 });
        addTri(o, { 0, 0, 1 }, { 1, 0, 1 }, { 1, h, 1 }, { 0, 0, 1 });
        addTri(o, { 1, 0, 0 }, { 1, 0, 1 }, { 1, h, 1 }, { 1, 0, 0 });
        addTri(o, { 0, 0, 0 }, { 1, 0, 0 }, { 1, h, 1 }, triNormal({ 0, 0, 0 }, { 1, 0, 0 }, { 1, h, 1 }, c));
        addTri(o, { 0, 0, 0 }, { 1, h, 1 }, { 0, 0, 1 }, triNormal({ 0, 0, 0 }, { 1, h, 1 }, { 0, 0, 1 }, c));
        break; }
    case B_INNER: {
        V3 c{ 0.7f, h * 0.4f, 0.7f };
        addQuad(o, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 }, { 0, -1, 0 });
        addQuad(o, { 0, 0, 1 }, { 1, 0, 1 }, { 1, h, 1 }, { 0, h, 1 }, { 0, 0, 1 });
        addQuad(o, { 1, 0, 0 }, { 1, 0, 1 }, { 1, h, 1 }, { 1, h, 0 }, { 1, 0, 0 });
        addTri(o, { 0, 0, 0 }, { 0, 0, 1 }, { 0, h, 1 }, { -1, 0, 0 });
        addTri(o, { 0, 0, 0 }, { 1, 0, 0 }, { 1, h, 0 }, { 0, 0, -1 });
        addTri(o, { 0, 0, 0 }, { 1, h, 0 }, { 1, h, 1 }, triNormal({ 0, 0, 0 }, { 1, h, 0 }, { 1, h, 1 }, { 0.5f, 0, 0.9f }));
        addTri(o, { 0, 0, 0 }, { 1, h, 1 }, { 0, h, 1 }, triNormal({ 0, 0, 0 }, { 1, h, 1 }, { 0, h, 1 }, { 0.9f, 0, 0.5f }));
        (void)c;
        break; }
    case B_PILLAR: {
        float r = h * 0.5f; const int S = 16;
        for (int i = 0; i < S; i++) {
            float a0 = 6.2831853f * i / S, a1 = 6.2831853f * (i + 1) / S;
            V3 n0{ std::cos(a0), 0, std::sin(a0) }, n1{ std::cos(a1), 0, std::sin(a1) };
            V3 p0{ 0.5f + r * n0.x, 0, 0.5f + r * n0.z }, p1{ 0.5f + r * n1.x, 0, 0.5f + r * n1.z };
            o.push_back({ { p0, p1, { p1.x, 1, p1.z }, { p0.x, 1, p0.z } }, { n0, n1, n1, n0 } });
            addTri(o, { 0.5f, 1, 0.5f }, { p0.x, 1, p0.z }, { p1.x, 1, p1.z }, { 0, 1, 0 });
            addTri(o, { 0.5f, 0, 0.5f }, p1, p0, { 0, -1, 0 });
        }
        break; }
    case B_ROUND: case B_ARCH: {
        bool round = t == B_ROUND;
        std::vector<V3> arc;   // (unused x), y, z profile points
        for (int i = 0; i <= SEG; i++) {
            float th = 1.5707963f * i / SEG;
            if (round) arc.push_back({ 0, std::sin(th), 1 - std::cos(th) });   // centre (z=1,y=0): solid under the curve
            else arc.push_back({ 0, std::sin(th), std::cos(th) });             // centre (z=0,y=0): solid outside the curve
        }
        if (round) {
            addQuad(o, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 }, { 0, -1, 0 });
            addQuad(o, { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 1 });
        } else {
            addQuad(o, { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 }, { 0, 1, 0 });
            addQuad(o, { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 1 });
        }
        V3 ctr = round ? V3{ 0, 0, 1 } : V3{ 0, 1, 1 };   // fan centre for the side caps (profile y,z)
        for (int i = 0; i < SEG; i++) {
            V3 a = arc[i], b = arc[i + 1];
            V3 na = round ? normalize({ 0, a.y, a.z - 1 }) : normalize({ 0, -a.y, -a.z });
            V3 nb = round ? normalize({ 0, b.y, b.z - 1 }) : normalize({ 0, -b.y, -b.z });
            o.push_back({ { { 0, a.y, a.z }, { 1, a.y, a.z }, { 1, b.y, b.z }, { 0, b.y, b.z } }, { na, na, nb, nb } });
            addTri(o, { 0, ctr.y, ctr.z }, { 0, a.y, a.z }, { 0, b.y, b.z }, { -1, 0, 0 });
            addTri(o, { 1, ctr.y, ctr.z }, { 1, a.y, a.z }, { 1, b.y, b.z }, { 1, 0, 0 });
        }
        break; }
    default: { std::vector<Box3> bx; localBoxes(t, size, bx); for (auto& b : bx) boxPolys(o, b); break; }
    }
}

void shapeBoxes(const Block& b, int x, int y, int z, std::vector<Box3>& out) {
    out.clear(); if (b.type == B_AIR) return;
    std::vector<Box3> loc; localBoxes(b.type, b.size, loc);
    Basis B = basisOf(b.rot); V3 cell{ (float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f };
    for (auto& l : loc) {
        V3 a = toWorld(B, l.mn - V3{ 0.5f, 0.5f, 0.5f }), c = toWorld(B, l.mx - V3{ 0.5f, 0.5f, 0.5f });
        out.push_back({ { std::min(a.x, c.x) + cell.x, std::min(a.y, c.y) + cell.y, std::min(a.z, c.z) + cell.z },
                        { std::max(a.x, c.x) + cell.x, std::max(a.y, c.y) + cell.y, std::max(a.z, c.z) + cell.z } });
    }
}

// ------------------------------------------------------------------ room basics
void Room::resize(int x, int y, int z, int oX, int oY, int oZ) {
    sx = x; sy = y; sz = z; ox = oX; oy = oY; oz = oZ;
    size_t n = (size_t)sx * sy * sz; cells.assign(n, B_AIR); rots.assign(n, 8); sizes.assign(n, 8); mats.assign(n, 0);
}
void Room::create(int w, int h, int d, bool floor, uint8_t floorMat) {
    w = std::max(8, std::min(kMaxW, w)); d = std::max(8, std::min(kMaxW, d)); h = std::max(8, std::min(kMaxH, h));
    resize(w, h, d, -w / 2, -1, -d / 2);
    if (floor) { Block b; b.type = B_CUBE; b.mat = floorMat; for (int z = oz; z < oz + sz; z++) for (int x = ox; x < ox + sx; x++) set(x, -1, z, b); }
}
void Room::resizeKeep(int w, int h, int d) {
    w = std::max(8, std::min(kMaxW, w)); d = std::max(8, std::min(kMaxW, d)); h = std::max(8, std::min(kMaxH, h));
    Room old = *this;
    resize(w, h, d, -w / 2, old.oy, -d / 2);
    for (int y = std::max(oy, old.oy); y < std::min(oy + sy, old.oy + old.sy); y++)
        for (int z = std::max(oz, old.oz); z < std::min(oz + sz, old.oz + old.sz); z++)
            for (int x = std::max(ox, old.ox); x < std::min(ox + sx, old.ox + old.sx); x++) set(x, y, z, old.block(x, y, z));
}
void Room::newFlat(int half) {
    resize(64, 32, 64, -32, -1, -32);
    for (int z = -half; z < half; z++) for (int x = -half; x < half; x++) set(x, -1, z, (uint8_t)B_CUBE);
}
size_t Room::count() const { size_t n = 0; for (auto c : cells) n += c != B_AIR; return n; }

bool Room::fullFace(int x, int y, int z, int worldDir) const {
    Block b = block(x, y, z);
    if (b.type == B_AIR) return false;
    if (b.type == B_CUBE) return true;
    Basis B = basisOf(b.rot); int ld = dirFromVec(toLocal(B, dirVec(worldDir)));
    return (fullMask(b.type, b.size) >> ld) & 1;
}
bool Room::pointSolid(V3 p) const {
    int x = (int)std::floor(p.x), y = (int)std::floor(p.y), z = (int)std::floor(p.z);
    Block b = block(x, y, z); if (b.type == B_AIR) return false; if (b.type == B_CUBE) return true;
    std::vector<Box3> bx; shapeBoxes(b, x, y, z, bx);
    for (auto& q : bx) if (p.x >= q.mn.x && p.x <= q.mx.x && p.y >= q.mn.y && p.y <= q.mx.y && p.z >= q.mn.z && p.z <= q.mx.z) return true;
    return false;
}

// ------------------------------------------------------------------ file
static std::string quoted(const std::string& s) { std::string o = "\""; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o + "\""; }

bool Room::save(const std::string& path) const {
    std::ofstream f(path, std::ios::trunc); if (!f) return false;
    f << "PRIMROOM 4\n" << "name " << quoted(name) << "\n";
    auto v3s = [](V3 v) { char b[64]; snprintf(b, sizeof b, "%g %g %g", v.x, v.y, v.z); return std::string(b); };
    const RoomMood& m = mood;
    f << "haze " << v3s(m.haze) << " " << m.hazeStart << " " << m.hazeEnd << " " << m.hazeAmount << "\n";
    f << "ambient " << v3s(m.skyAmbient) << " " << v3s(m.groundAmbient) << " " << m.ambient << "\n";
    f << "sun " << (m.sun ? 1 : 0) << " " << m.sunElev << " " << m.sunAzim << " " << v3s(m.sunColor) << " " << m.sunStrength << "\n";
    f << "glow " << m.bloom << " " << m.glowLight << "\n";
    f << "particles " << m.particles << " " << m.particleAmount << " " << v3s(m.particleColor) << "\n";
    f << "sky " << m.sky << " " << v3s(m.skyTop) << " " << v3s(m.skyHorizon) << "\n";
    for (auto& L : lights) f << "light " << v3s(L.p) << " " << v3s(L.color) << " " << L.intensity << " " << L.radius << " " << (L.shadows ? 1 : 0) << " " << (L.glow ? 1 : 0) << "\n";
    f << "size " << sx << " " << sy << " " << sz << "\norigin " << ox << " " << oy << " " << oz << "\ndata\n";
    size_t i = 0, runs = 0;
    while (i < cells.size()) {
        size_t j = i; while (j < cells.size() && cells[j] == cells[i] && rots[j] == rots[i] && sizes[j] == sizes[i] && mats[j] == mats[i]) j++;
        f << (int)cells[i] << " " << (int)rots[i] << " " << (int)sizes[i] << " " << (int)mats[i] << " " << (j - i) << ((++runs % 16) ? " " : "\n");
        i = j;
    }
    f << "\nend\n";
    return (bool)f;
}

bool Room::load(const std::string& path, std::string& err) {
    std::ifstream f(path); if (!f) { err = "cannot open file"; return false; }
    std::string magic; int ver = 0; f >> magic >> ver;
    if (magic != "PRIMROOM" || ver < 1 || ver > 4) { err = "not a room file (or made by a newer PRIMEngine)"; return false; }
    Room r; std::string key;
    while (f >> key) {
        if (key == "name") {
            f >> std::ws; std::string s; char c;
            if (f.peek() == '"') { f.get(); while (f.get(c) && c != '"') { if (c == '\\') f.get(c); s += c; } }
            else f >> s;
            r.name = s;
        }
        else if (key == "size") f >> r.sx >> r.sy >> r.sz;
        else if (key == "origin") f >> r.ox >> r.oy >> r.oz;
        else if (key == "haze") f >> r.mood.haze.x >> r.mood.haze.y >> r.mood.haze.z >> r.mood.hazeStart >> r.mood.hazeEnd >> r.mood.hazeAmount;
        else if (key == "ambient") f >> r.mood.skyAmbient.x >> r.mood.skyAmbient.y >> r.mood.skyAmbient.z >> r.mood.groundAmbient.x >> r.mood.groundAmbient.y >> r.mood.groundAmbient.z >> r.mood.ambient;
        else if (key == "sun") { int on = 0; f >> on >> r.mood.sunElev >> r.mood.sunAzim >> r.mood.sunColor.x >> r.mood.sunColor.y >> r.mood.sunColor.z >> r.mood.sunStrength; r.mood.sun = on != 0; }
        else if (key == "glow") f >> r.mood.bloom >> r.mood.glowLight;
        else if (key == "particles") f >> r.mood.particles >> r.mood.particleAmount >> r.mood.particleColor.x >> r.mood.particleColor.y >> r.mood.particleColor.z;
        else if (key == "sky") f >> r.mood.sky >> r.mood.skyTop.x >> r.mood.skyTop.y >> r.mood.skyTop.z >> r.mood.skyHorizon.x >> r.mood.skyHorizon.y >> r.mood.skyHorizon.z;
        else if (key == "light") { RoomLight L; int sh = 1, gl = 1; f >> L.p.x >> L.p.y >> L.p.z >> L.color.x >> L.color.y >> L.color.z >> L.intensity >> L.radius >> sh >> gl; L.shadows = sh != 0; L.glow = gl != 0; r.lights.push_back(L); }
        else if (key == "data") {
            if (r.sx <= 0 || r.sy <= 0 || r.sz <= 0 || (size_t)r.sx * r.sy * r.sz > 64u * 1024 * 1024) { err = "bad room size"; return false; }
            r.resize(r.sx, r.sy, r.sz, r.ox, r.oy, r.oz);
            size_t pos = 0; std::string a;
            while (f >> a && a != "end") {
                int t = std::atoi(a.c_str()), ro = 8, sz_ = shapeInfo((uint8_t)t).def, ma = 0; size_t n = 0;
                if (ver >= 2) f >> ro >> sz_;
                if (ver >= 3) f >> ma;
                f >> n;
                if (t < 0 || t >= B_COUNT) t = B_CUBE;
                if (pos + n > r.cells.size()) { err = "room data is too long"; return false; }
                std::fill(r.cells.begin() + pos, r.cells.begin() + pos + n, (uint8_t)t);
                std::fill(r.rots.begin() + pos, r.rots.begin() + pos + n, (uint8_t)(ro % 24));
                std::fill(r.sizes.begin() + pos, r.sizes.begin() + pos + n, (uint8_t)sz_);
                std::fill(r.mats.begin() + pos, r.mats.begin() + pos + n, (uint8_t)(ma < MAT_COUNT ? ma : 0));
                pos += n;
            }
            if (pos != r.cells.size()) { err = "room data is incomplete"; return false; }
            *this = std::move(r); return true;
        }
    }
    err = "no data in room file"; return false;
}

// ------------------------------------------------------------------ baked light
static bool glowInfo(uint8_t mat, V3& col, float& radius) {
    if (mat == MAT_LAVA) { col = { 1.0f, 0.42f, 0.10f }; radius = 4.5f; return true; }
    if (mat == MAT_BASALT) { col = { 0.55f, 0.18f, 0.05f }; radius = 2.5f; return true; }
    return false;
}
V3 Room::lightAt(V3 p, V3 n, float ao, int cx, int cy, int cz, const std::vector<std::pair<V3, int>>& glowers) const {
    const RoomMood& m = mood;
    float hemi = n.y * 0.5f + 0.5f;
    V3 c = (m.groundAmbient + (m.skyAmbient - m.groundAmbient) * hemi) * (m.ambient * ao);
    if (m.sun && m.sunStrength > 0) {
        V3 L = sunDirection(m); float ndl = dot(n, L);
        if (ndl > 0 && !blocked(p, p + L * 120.f, cx, cy, cz)) c = c + m.sunColor * (m.sunStrength * ndl);
    }
    for (const RoomLight& L : lights) {
        V3 d = L.p - p; float dist = length(d);
        if (dist >= L.radius || dist < 1e-4f) continue;
        float ndl = dot(n, d * (1.f / dist)); if (ndl <= 0) continue;
        float att = 1.f - dist / L.radius; att *= att;
        if (L.shadows && blocked(p, L.p, cx, cy, cz)) continue;
        c = c + L.color * (L.intensity * att * (0.15f + 0.85f * ndl));
    }
    for (auto& g : glowers) {
        V3 col; float rad; if (!glowInfo((uint8_t)g.second, col, rad)) continue;
        V3 d = g.first - p; float dist = length(d); if (dist >= rad) continue;
        float att = 1.f - dist / rad; att *= att;
        float ndl = dist > 1e-4f ? std::max(0.f, dot(n, d * (1.f / dist))) * 0.7f + 0.3f : 1.f;
        c = c + col * (m.glowLight * att * ndl * 0.9f);
    }
    return { std::min(c.x, 2.2f), std::min(c.y, 2.2f), std::min(c.z, 2.2f) };
}

// ------------------------------------------------------------------ mesh
static void planarUV(V3 p, V3 n, float& u, float& v) {   // world-aligned texture: blocks and shapes line up like Copycats+
    float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
    if (ay >= ax && ay >= az) { u = p.x; v = p.z; }
    else if (ax >= az) { u = p.z; v = -p.y; }
    else { u = p.x; v = -p.y; }
}

void Room::buildMesh(MeshData& out) const {
    std::vector<SubMesh> subs; buildMeshes(subs); out.v.clear(); out.i.clear();
    for (auto& sm : subs) { uint32_t b = (uint32_t)out.v.size(); out.v.insert(out.v.end(), sm.mesh.v.begin(), sm.mesh.v.end()); for (auto i : sm.mesh.i) out.i.push_back(b + i); }
}

void Room::buildMeshes(std::vector<SubMesh>& subs) const { buildMeshesBox(ox, oy, oz, ox + sx, oy + sy, oz + sz, subs); }

void Room::buildMeshesBox(int bx0, int by0, int bz0, int bx1, int by1, int bz1, std::vector<SubMesh>& subs) const {
    subs.clear();
    bx0 = std::max(bx0, ox); by0 = std::max(by0, oy); bz0 = std::max(bz0, oz);
    bx1 = std::min(bx1, ox + sx); by1 = std::min(by1, oy + sy); bz1 = std::min(bz1, oz + sz);
    // glowing blocks near this box light it (only ones touching air)
    std::vector<std::pair<V3, int>> glowers;
    if (mood.glowLight > 0)
        for (int y = std::max(oy, by0 - 5); y < std::min(oy + sy, by1 + 5); y++) for (int z = std::max(oz, bz0 - 5); z < std::min(oz + sz, bz1 + 5); z++)
            for (int x = std::max(ox, bx0 - 5); x < std::min(ox + sx, bx1 + 5); x++) {
                size_t i = idx(x, y, z); V3 col; float rad;
                if (cells[i] == B_AIR || !glowInfo(mats[i], col, rad)) continue;
                // light leaves through the open faces: put the emitter just outside them
                V3 open{ 0, 0, 0 }; int nOpen = 0;
                for (int d = 0; d < 6; d++) { V3 v = dirVec(d); if (!solid(x + (int)v.x, y + (int)v.y, z + (int)v.z)) { open = open + v; nOpen++; } }
                if (!nOpen) continue;
                V3 e = length(open) > 1e-3f ? normalize(open) : V3{ 0, 1, 0 };
                glowers.push_back({ V3{ x + 0.5f, y + 0.5f, z + 0.5f } + e * 0.75f, mats[i] });
            }
    int slot[MAT_COUNT * 3]; for (auto& s_ : slot) s_ = -1;
    uint8_t curMat = 0;
    auto target = [&](V3 n) -> MeshData& {
        int fk = n.y > 0.5f ? 0 : n.y < -0.5f ? 2 : 1, key = curMat * 3 + fk;
        if (slot[key] < 0) {
            const MaterialInfo& mi = materialInfo(curMat); SubMesh sm; sm.mat = curMat; sm.env = mi.env; sm.glow = mi.glow;
            sm.tex = fk == 0 ? mi.top : fk == 1 ? mi.side : mi.bottom; slot[key] = (int)subs.size(); subs.push_back(std::move(sm));
        }
        return subs[slot[key]].mesh;
    };
    static const float faceShade[6] = { 0.80f, 0.80f, 1.00f, 0.55f, 0.70f, 0.70f };   // +X -X +Y -Y +Z -Z
    std::vector<Poly> polys;
    auto emitTri = [&](const Vertex& a, const Vertex& b, const Vertex& c, V3 n) {
        V3 p0{ a.p[0], a.p[1], a.p[2] }, p1{ b.p[0], b.p[1], b.p[2] }, p2{ c.p[0], c.p[1], c.p[2] };
        MeshData& out = target(n);
        uint32_t base = (uint32_t)out.v.size();
        out.v.push_back(a);
        if (dot(cross(p1 - p0, p2 - p0), n) < 0) { out.v.push_back(c); out.v.push_back(b); } else { out.v.push_back(b); out.v.push_back(c); }
        out.i.insert(out.i.end(), { base, base + 1, base + 2 });
    };
    for (int y = by0; y < by1; y++) for (int z = bz0; z < bz1; z++) for (int x = bx0; x < bx1; x++) {
        size_t ci = idx(x, y, z); uint8_t type = cells[ci];
        if (type == B_AIR) continue;
        curMat = mats[ci];
        if (type == B_CUBE) {
            // ---- full block: exact Minecraft-style corner shading per face
            int c[3] = { x, y, z };
            for (int f = 0; f < 6; f++) {
                int a = f / 2, sgn = (f % 2 == 0) ? 1 : -1;
                int n[3] = { 0, 0, 0 }; n[a] = sgn;
                if (fullFace(x + n[0], y + n[1], z + n[2], opposite(f))) continue;
                int ua = (a + 1) % 3, va = (a + 2) % 3;
                int L[3] = { x + n[0], y + n[1], z + n[2] };
                Vertex q[4]; int ao[4];
                const int du[4] = { 0, 1, 1, 0 }, dv[4] = { 0, 0, 1, 1 };
                V3 nn{ (float)n[0], (float)n[1], (float)n[2] };
                for (int k = 0; k < 4; k++) {
                    float p[3] = { (float)c[0], (float)c[1], (float)c[2] };
                    p[a] += sgn > 0 ? 1.f : 0.f; p[ua] += (float)du[k]; p[va] += (float)dv[k];
                    int s1c[3] = { L[0], L[1], L[2] }, s2c[3] = { L[0], L[1], L[2] }, cc[3] = { L[0], L[1], L[2] };
                    int su = du[k] ? 1 : -1, sv = dv[k] ? 1 : -1;
                    s1c[ua] += su; s2c[va] += sv; cc[ua] += su; cc[va] += sv;
                    int s1 = solid(s1c[0], s1c[1], s1c[2]), s2 = solid(s2c[0], s2c[1], s2c[2]), co = solid(cc[0], cc[1], cc[2]);
                    ao[k] = (s1 && s2) ? 0 : 3 - (s1 + s2 + co);
                    float aoF = (0.38f + 0.62f * ao[k] / 3.f) * (0.6f + 0.4f * faceShade[f]);
                    V3 pv{ p[0], p[1], p[2] };
                    V3 fc{ (float)c[0] + 0.5f + 0.5f * nn.x, (float)c[1] + 0.5f + 0.5f * nn.y, (float)c[2] + 0.5f + 0.5f * nn.z };   // face centre
                    V3 lt = lightAt(pv + (fc - pv) * 0.03f + nn * 0.04f, nn, aoF, x + n[0], y + n[1], z + n[2], glowers);
                    float u, v; planarUV(pv, nn, u, v);
                    q[k] = { { p[0], p[1], p[2] }, { nn.x, nn.y, nn.z }, { u, v }, { lt.x, lt.y, lt.z, 1.f } };
                }
                if (ao[0] + ao[2] > ao[1] + ao[3]) { emitTri(q[0], q[1], q[2], nn); emitTri(q[0], q[2], q[3], nn); }
                else { emitTri(q[0], q[1], q[3], nn); emitTri(q[1], q[2], q[3], nn); }
            }
            continue;
        }
        // ---- shaped block: local polygons, rotated into place; flat faces on the cell border are hidden by full neighbours
        Basis B = basisOf(rots[ci]); V3 cell{ (float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f };
        polys.clear(); localPolys(type, sizes[ci], polys);
        for (auto& pl : polys) {
            // is this polygon on the cell border (all points on one face plane)?
            int border = -1;
            for (int ax = 0; ax < 3 && border < 0; ax++) for (float side : { 0.f, 1.f }) {
                bool all = true; for (auto& p : pl.p) { float v = ax == 0 ? p.x : ax == 1 ? p.y : p.z; if (std::fabs(v - side) > 1e-4f) { all = false; break; } }
                if (all) { border = dirIndex(ax, side > 0.5f ? 1 : -1); break; }
            }
            if (border >= 0) {
                int wd = dirFromVec(toWorld(B, dirVec(border))); V3 d = dirVec(wd);
                if (fullFace(x + (int)d.x, y + (int)d.y, z + (int)d.z, opposite(wd))) continue;
            }
            std::vector<Vertex> vs;
            V3 pc{ 0, 0, 0 }; for (auto& q : pl.p) pc = pc + q; pc = toWorld(B, pc * (1.f / pl.p.size()) - V3{ 0.5f, 0.5f, 0.5f }) + cell;
            for (size_t k = 0; k < pl.p.size(); k++) {
                V3 wp = toWorld(B, pl.p[k] - V3{ 0.5f, 0.5f, 0.5f }) + cell, wn = normalize(toWorld(B, pl.n[k]));
                // shading: face direction + darkening from full blocks in front of the point (cheap corner shading)
                float s = wn.y >= 0 ? 0.80f + 0.20f * wn.y : 0.80f + 0.25f * wn.y; s -= 0.08f * std::fabs(wn.z);
                int occ = 0; int qx = (int)std::lround(wp.x), qy = (int)std::lround(wp.y), qz = (int)std::lround(wp.z);
                for (int dz = -1; dz <= 0; dz++) for (int dy = -1; dy <= 0; dy++) for (int dx = -1; dx <= 0; dx++) {
                    int cx = qx + dx, cy = qy + dy, cz = qz + dz; if (cx == x && cy == y && cz == z) continue;
                    V3 cc{ cx + 0.5f, cy + 0.5f, cz + 0.5f };
                    if (dot(cc - wp, wn) > 0.05f && get(cx, cy, cz) == B_CUBE) occ++;
                }
                float aoF = std::max(0.45f, 1.f - 0.2f * occ) * (0.6f + 0.4f * s);
                V3 lt = lightAt(wp + (pc - wp) * 0.03f + wn * 0.04f, wn, aoF, x, y, z, glowers);
                float u, v; planarUV(wp, wn, u, v);
                vs.push_back({ { wp.x, wp.y, wp.z }, { wn.x, wn.y, wn.z }, { u, v }, { lt.x, lt.y, lt.z, 1.f } });
            }
            V3 fn = normalize(toWorld(B, pl.n[0] + pl.n[pl.n.size() / 2]));
            for (size_t k = 1; k + 1 < vs.size(); k++) emitTri(vs[0], vs[k], vs[k + 1], fn);
        }
    }
}

// ------------------------------------------------------------------ ray cast (grid walk, exact boxes inside shaped cells)
static bool rayBox(V3 o, V3 d, const Box3& b, float& tHit, int& face) {
    float t0 = -1e30f, t1 = 1e30f; int f = -1;
    const float oo[3] = { o.x, o.y, o.z }, dd[3] = { d.x, d.y, d.z }, mn[3] = { b.mn.x, b.mn.y, b.mn.z }, mx[3] = { b.mx.x, b.mx.y, b.mx.z };
    for (int a = 0; a < 3; a++) {
        if (std::fabs(dd[a]) < 1e-9f) { if (oo[a] < mn[a] || oo[a] > mx[a]) return false; continue; }
        float ta = (mn[a] - oo[a]) / dd[a], tb = (mx[a] - oo[a]) / dd[a];
        int fa = dirIndex(a, -1), fb = dirIndex(a, 1);
        if (ta > tb) { std::swap(ta, tb); std::swap(fa, fb); }
        if (ta > t0) { t0 = ta; f = fa; }
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    if (t0 < 0) return false;
    tHit = t0; face = f; return true;
}

bool Room::raycast(V3 o, V3 d, float maxT, RayHit& hit) const { return rayCore(o, d, maxT, hit, INT32_MIN, 0, 0, true); }

bool Room::blocked(V3 a, V3 b, int sx_, int sy_, int sz_) const {
    V3 d = b - a; float len = length(d); if (len < 1e-4f) return false;
    RayHit h; return rayCore(a, d, len - 0.05f, h, sx_, sy_, sz_, false);
}

bool Room::rayCore(V3 o, V3 d, float maxT, RayHit& hit, int skx, int sky, int skz, bool ground) const {
    d = normalize(d);
    int x = (int)std::floor(o.x), y = (int)std::floor(o.y), z = (int)std::floor(o.z);
    int stx = d.x > 0 ? 1 : -1, sty = d.y > 0 ? 1 : -1, stz = d.z > 0 ? 1 : -1;
    auto tDelta = [](float dd) { return std::fabs(dd) > 1e-9f ? std::fabs(1.f / dd) : 1e30f; };
    auto tFirst = [](float oo, float dd, int cell) {
        if (std::fabs(dd) < 1e-9f) return 1e30f;
        float b = dd > 0 ? (float)(cell + 1) : (float)cell; return (b - oo) / dd;
    };
    float tdx = tDelta(d.x), tdy = tDelta(d.y), tdz = tDelta(d.z);
    float tmx = tFirst(o.x, d.x, x), tmy = tFirst(o.y, d.y, y), tmz = tFirst(o.z, d.z, z);
    int nx = 0, ny = 0, nz = 0; float t = 0;
    thread_local std::vector<Box3> bx;
    for (int it = 0; it < 4096 && t <= maxT; it++) {
        if (x == skx && y == sky && z == skz) goto step;
        {
        Block b = block(x, y, z);
        if (b.type == B_CUBE) { hit = { x, y, z, nx, ny, nz, t, false }; return true; }
        if (b.type != B_AIR) {
            shapeBoxes(b, x, y, z, bx); float best = 1e30f; int bf = -1;
            for (auto& q : bx) { float th; int fc; if (rayBox(o, d, q, th, fc) && th < best) { best = th; bf = fc; } }
            if (bf >= 0 && best <= maxT) { V3 n = dirVec(bf); hit = { x, y, z, (int)n.x, (int)n.y, (int)n.z, best, false }; return true; }
        }
        }
    step:
        if (tmx < tmy && tmx < tmz) { x += stx; t = tmx; tmx += tdx; nx = -stx; ny = 0; nz = 0; }
        else if (tmy < tmz) { y += sty; t = tmy; tmy += tdy; nx = 0; ny = -sty; nz = 0; }
        else { z += stz; t = tmz; tmz += tdz; nx = 0; ny = 0; nz = -stz; }
    }
    if (ground && d.y < -1e-6f) {   // nothing hit: the bottom of the room is a building surface
        float tp = ((float)oy - o.y) / d.y;
        if (tp > 0 && tp <= maxT) {
            V3 p = o + d * tp; int gx = (int)std::floor(p.x), gz = (int)std::floor(p.z);
            if (inBounds(gx, oy, gz)) { hit = { gx, oy - 1, gz, 0, 1, 0, tp, true }; return true; }
        }
    }
    return false;
}

// ------------------------------------------------------------------ collision
bool Room::boxFree(V3 f, float h, float H) const {
    const float e = 1e-4f;
    V3 mn{ f.x - h + e, f.y + e, f.z - h + e }, mx{ f.x + h - e, f.y + H - e, f.z + h - e };
    std::vector<Box3> bx;
    for (int y = (int)std::floor(mn.y); y <= (int)std::floor(mx.y); y++)
        for (int z = (int)std::floor(mn.z); z <= (int)std::floor(mx.z); z++)
            for (int x = (int)std::floor(mn.x); x <= (int)std::floor(mx.x); x++) {
                Block b = block(x, y, z);
                if (b.type == B_AIR) continue;
                if (b.type == B_CUBE) return false;
                shapeBoxes(b, x, y, z, bx);
                for (auto& q : bx)
                    if (mn.x < q.mx.x && mx.x > q.mn.x && mn.y < q.mx.y && mx.y > q.mn.y && mn.z < q.mx.z && mx.z > q.mn.z) return false;
            }
    return true;
}

V3 Room::moveBox(V3 feet, V3& vel, float h, float H, float dt, bool& onGround, float stepUp) const {
    bool wasGround = onGround; onGround = false;
    V3 total = vel * dt;
    int steps = std::max(1, (int)std::ceil(std::max(std::fabs(total.x), std::max(std::fabs(total.y), std::fabs(total.z))) / 0.2f));
    V3 step = total * (1.f / steps);
    const float e = 1e-3f;
    for (int s = 0; s < steps; s++) {
        // y first (gravity / landing), then x, then z
        for (int axis : { 1, 0, 2 }) {
            float d = axis == 0 ? step.x : axis == 1 ? step.y : step.z;
            if (d == 0) continue;
            V3 np = feet; (axis == 0 ? np.x : axis == 1 ? np.y : np.z) += d;
            if (boxFree(np, h, H)) { feet = np; continue; }
            if (axis == 1) {
                // land on (or bump into) whatever is there: close the gap in small steps
                float lo = 0, hi = std::fabs(d); V3 best = feet;
                for (int k = 0; k < 10; k++) { float m = (lo + hi) * 0.5f; V3 t = feet; t.y += d > 0 ? m : -m; if (boxFree(t, h, H)) { lo = m; best = t; } else hi = m; }
                feet = best; if (d < 0) onGround = true;
                vel.y = 0; step.y = 0;
            } else {
                // blocked sideways: try to step up a small ledge (stairs, slopes) first
                bool stepped = false;
                if (stepUp > 0 && (wasGround || onGround)) {
                    V3 up = feet; up.y += stepUp;
                    V3 upMoved = up; (axis == 0 ? upMoved.x : upMoved.z) += d;
                    if (boxFree(up, h, H) && boxFree(upMoved, h, H)) {
                        float lo = 0, hi = stepUp; V3 best = upMoved;   // settle back down onto the ledge
                        for (int k = 0; k < 10; k++) { float m = (lo + hi) * 0.5f; V3 t = upMoved; t.y -= m; if (boxFree(t, h, H)) { lo = m; best = t; } else hi = m; }
                        feet = best; stepped = true; onGround = true;
                    }
                }
                if (!stepped) {
                    float lo = 0, hi = std::fabs(d); V3 best = feet;
                    for (int k = 0; k < 10; k++) { float m = (lo + hi) * 0.5f; V3 t = feet; (axis == 0 ? t.x : t.z) += d > 0 ? m : -m; if (boxFree(t, h, H)) { lo = m; best = t; } else hi = m; }
                    feet = best;
                    if (axis == 0) { vel.x = 0; step.x = 0; } else { vel.z = 0; step.z = 0; }
                }
            }
        }
    }
    (void)e;
    if (!onGround) { V3 below = feet; below.y -= 0.02f; onGround = !boxFree(below, h, H); }
    return feet;
}

} // namespace px
