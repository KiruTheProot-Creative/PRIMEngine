// scene.h : platform-independent engine content (no D3D, no Win32).
//   - procedural textures (so the demo needs zero asset files)
//   - mesh builder with *baked per-vertex lighting* (the "lightmap" stand-in)
//   - demo room, arm cannon, collision, particles, projectiles
// Everything here mirrors web/index.html so the WebGL harness and the D3D11 build look identical.
// Compiles on Linux too, so the content code can be unit-tested: see tests/scene_test.cpp.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace px {

// ------------------------------------------------------------------ tiny vector maths
struct V3 { float x, y, z; };
inline V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline V3 operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline float length(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 normalize(V3 a) { float l = length(a); if (l < 1e-8f) l = 1.f; return a * (1.f / l); }

// ------------------------------------------------------------------ procedural textures (128x128 RGBA8)
constexpr int TN = 128;
enum TexKind { T_FLOOR, T_WALL, T_TRIM, T_CANNON, T_CORE, T_STRIP, T_DOOR, T_ENV, T_SPRITE, T_BLOCK,
               T_ROCK, T_DIRT, T_MOSS, T_MOSS_SIDE, T_SAND, T_SNOW, T_SNOW_SIDE, T_ICE, T_BASALT, T_LAVA, T_CHOZO, T_COUNT };

inline uint32_t hash2(int x, int y, int s) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
inline double rnd(int x, int y, int s) { return (hash2(x, y, s) & 0xFFFFFF) / 16777216.0; }

inline double vnoise(double u, double v, int cells, int seed) {
    double x = u * cells, y = v * cells, xi = std::floor(x), yi = std::floor(y), xf = x - xi, yf = y - yi;
    double sx = xf * xf * (3 - 2 * xf), sy = yf * yf * (3 - 2 * yf);
    int ix = (int)xi, iy = (int)yi;
    int x0 = ((ix % cells) + cells) % cells, x1 = (x0 + 1) % cells, y0 = ((iy % cells) + cells) % cells, y1 = (y0 + 1) % cells;
    double a = rnd(x0, y0, seed), b = rnd(x1, y0, seed), c = rnd(x0, y1, seed), d = rnd(x1, y1, seed);
    double top = a + (b - a) * sx, bot = c + (d - c) * sx;
    return top + (bot - top) * sy;
}
inline double fbm(double u, double v, int s) { return vnoise(u, v, 4, s) * 0.5 + vnoise(u, v, 8, s + 1) * 0.3 + vnoise(u, v, 16, s + 2) * 0.2; }
// tileable cellular noise: distance to nearest and second-nearest jittered point (cells x cells grid)
inline void worley(double u, double v, int cells, int seed, double& d1, double& d2, int& id) {
    double x = u * cells, y = v * cells; int xi = (int)std::floor(x), yi = (int)std::floor(y);
    d1 = d2 = 9; id = 0;
    for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
        int cx = xi + i, cy = yi + j, wx = ((cx % cells) + cells) % cells, wy = ((cy % cells) + cells) % cells;
        double px_ = cx + rnd(wx, wy, seed), py_ = cy + rnd(wx, wy, seed + 7);
        double d = std::sqrt((px_ - x) * (px_ - x) + (py_ - y) * (py_ - y));
        if (d < d1) { d2 = d1; d1 = d; id = wy * cells + wx; } else if (d < d2) d2 = d;
    }
}
inline double smooth01(double e0, double e1, double x) { double t = std::max(0.0, std::min(1.0, (x - e0) / (e1 - e0))); return t * t * (3 - 2 * t); }
inline uint8_t clamp8(double x) { double r = std::floor(x * 255.0 + 0.5); return (uint8_t)std::max(0.0, std::min(255.0, r)); }

inline std::vector<uint8_t> makeTex(TexKind kind) {
    std::vector<uint8_t> d((size_t)TN * TN * 4);
    for (int py = 0; py < TN; py++) for (int px = 0; px < TN; px++) {
        double u = (double)px / TN, v = (double)py / TN, r = 0, g = 0, b = 0, a = 0;
        double& g_ = g;   // green channel (terrain cases use a local "g" for brightness)
        auto dim = [&](double k) { r *= k; g *= k; b *= k; };
        switch (kind) {
        case T_FLOOR: {
            double n = fbm(u, v, 10), gg = 0.17 + n * 0.15; r = gg * 0.92; g = gg; b = gg * 0.9;
            int sx = px % 64, sy = py % 64, e = std::min(std::min(sx, sy), std::min(63 - sx, 63 - sy));
            if (e < 2) dim(0.35); else if (e == 2) dim(1.5);
            int ix = std::min(sx - 12, 51 - sx), iy = std::min(sy - 12, 51 - sy);
            if (ix >= 0 && iy >= 0 && (ix < 1 || iy < 1)) dim(0.6);
            int dx = std::min(sx, 63 - sx) - 6, dy = std::min(sy, 63 - sy) - 6; if (dx * dx + dy * dy < 5) dim(1.6);
            int cx = sx - 32, cy = sy - 32; if (cx * cx + cy * cy < 6) { r = 1; g = 0.45; b = 0.08; a = 1; }
            break; }
        case T_WALL: {
            double n = fbm(u, v, 20), gg = 0.2 + n * 0.14; r = gg; g = gg; b = gg * 0.82;
            int row = py / 32, sy = py % 32, off = (row & 1) ? 32 : 0, sx = (px + off) % 64;
            if (sy == 0 || sx == 0) dim(0.4); else if (sy == 1 || sx == 1) dim(1.35);
            double gr = rnd((px + off) / 64, row, 5);
            if (gr < 0.4 && sx > 6 && sx < 58 && sy > 6 && sy < 26) dim((sx == 7 || sx == 57 || sy == 7 || sy == 25) ? 1.4 : 0.7);
            if (py >= 54 && py < 74) {
                if (py < 58 || py >= 70) { r = g = b = 0.03; }
                else { double k = 1 - std::fabs(py - 63.5) / 6.5; r = 1; g = 0.42; b = 0.07; a = std::min(1.0, 0.4 + k * 1.2); }
            }
            break; }
        case T_TRIM: {
            double n = fbm(u, v, 30), gg = 0.14 + n * 0.12; r = gg * 0.85; g = gg * 0.95; b = gg * 1.1;
            if (px % 16 == 0) dim(0.45); else if (px % 16 == 1) dim(1.3);
            if ((py >= 30 && py < 34) || (py >= 94 && py < 98)) { r = 0.08; g = 0.75; b = 0.95; a = 1; }
            else if ((py >= 28 && py < 30) || (py >= 34 && py < 36) || (py >= 92 && py < 94) || (py >= 98 && py < 100)) { r = g = b = 0.04; }
            break; }
        case T_CANNON: {
            double n = fbm(u, v, 40), gg = 0.2 + n * 0.12; r = gg; g = gg * 1.02; b = gg * 1.1;
            int m = py % 32; if (m < 2) dim(0.5);
            if (px % 32 < 1) dim(0.6);
            if (m >= 14 && m < 18) { r = 1; g = 0.42; b = 0.06; a = 1; }
            break; }
        case T_CORE: {
            double n = fbm(u, v, 50), n2 = vnoise(u, v, 6, 51), k = std::fabs(n2 - 0.5) * 2;
            r = 1; g = 0.45 + 0.4 * n; b = 0.1 + 0.35 * n * (1 - k); if (k < 0.12) dim(0.55); a = 1;
            break; }
        case T_STRIP: { r = 0.85; g = 0.97; b = 0.9; a = 1; break; }
        case T_DOOR: {
            double x = (px + 0.5) / TN * 2 - 1, y = (py + 0.5) / TN * 2 - 1, dd = std::sqrt(x * x + y * y);
            double n = fbm(u, v, 60), gg = 0.14 + n * 0.1; r = gg * 0.9; g = gg; b = gg * 1.1;
            if (dd > 0.62 && dd < 0.7) { r = 0.1; g = 0.8; b = 1; a = 1; }
            else if ((dd > 0.55 && dd <= 0.62) || (dd >= 0.7 && dd < 0.76)) { r = g = b = 0.03; }
            else if (dd < 0.5) { double k = 1 - dd / 0.5; r = 0.04; g = 0.35 + 0.3 * k; b = 0.65 + 0.3 * k; a = 0.25 + 0.5 * k; }
            if ((std::fabs(x) < 0.02 || std::fabs(y) < 0.02) && dd < 0.55) dim(0.3);
            break; }
        case T_ENV: { // sphere map: +y up, "facing" = +z
            double x = (px + 0.5) / TN * 2 - 1, y = 1 - (py + 0.5) / TN * 2, r2 = x * x + y * y; if (r2 > 1) r2 = 1;
            double z = std::sqrt(1 - r2);
            auto nd = [&](double ax, double ay, double az) { double l = std::sqrt(ax * ax + ay * ay + az * az); return (x * ax + y * ay + z * az) / l; };
            r = 0.02; g = 0.05; b = 0.06;
            double k = std::pow(std::max(nd(-0.5, 0.6, 0.6), 0.0), 12); r += k * 1.2; g += k * 0.66; b += k * 0.18;
            double f = std::pow(std::max(nd(0.7, -0.2, 0.5), 0.0), 6); r += f * 0.08; g += f * 0.4; b += f * 0.48;
            double e = std::pow(1 - z, 3); r += e * 0.2; g += e * 0.35; b += e * 0.4;
            double w = std::pow(std::max(nd(0.3, 0.8, 0.5), 0.0), 60) * 1.5; r += w; g += w; b += w;
            a = 1; break; }
        case T_SPRITE: {
            double x = (px + 0.5) / TN * 2 - 1, y = (py + 0.5) / TN * 2 - 1, dd = std::sqrt(x * x + y * y), s = std::pow(std::max(1 - dd, 0.0), 2.2);
            r = g = b = s; a = 1; break; }
        case T_BLOCK: { // one room-editor block = one texture: a bevelled metal panel (the bevel also reads as the grid)
            double n = fbm(u, v, 70), gg = 0.30 + n * 0.16; r = gg * 0.86; g = gg * 0.97; b = gg * 1.02;
            int e = std::min(std::min(px, py), std::min(127 - px, 127 - py));
            if (e < 3) dim(0.42);
            else if (e < 6) { if (px < py ? px < 64 : py < 64) dim(1.32); else dim(0.78); }   // bevel: lit top-left, shaded bottom-right
            int ix = std::min(px - 22, 105 - px), iy = std::min(py - 22, 105 - py);
            if (ix >= 0 && iy >= 0 && (ix < 2 || iy < 2)) dim(0.62);                          // inset panel seam
            for (int c = 0; c < 4; c++) { int cx = (c & 1) ? 13 : 114, cy = (c & 2) ? 13 : 114; int dx = px - cx, dy = py - cy;
                if (dx * dx + dy * dy < 7) dim(dx + dy < 0 ? 1.5 : 0.65); }                    // rivets
            a = 0; break; }
        case T_ROCK: {   // grey-brown stone with cracks and speckles
            double n = fbm(u, v, 80), n2 = vnoise(u, v, 32, 83), d1, d2; int id; worley(u, v, 5, 84, d1, d2, id);
            double g = 0.30 + n * 0.22 + (n2 - 0.5) * 0.06 + (rnd(id, 1, 85) - 0.5) * 0.08;
            r = g * 1.02; g_ = g * 0.97; b = g * 0.86;
            double edge = d2 - d1; if (edge < 0.05) dim(0.45 + edge * 6);
            if (rnd(px, py, 86) > 0.985) dim(1.35);
            break; }
        case T_DIRT: {   // brown soil with pebbles
            double n = fbm(u, v, 90), g = 0.24 + n * 0.16; r = g * 1.18; g_ = g * 0.86; b = g * 0.58;
            double d1, d2; int id; worley(u, v, 9, 91, d1, d2, id);
            if (d1 < 0.16 && rnd(id, 2, 92) > 0.55) { double k = 1.25 + rnd(id, 3, 93) * 0.3; r = g * k; g_ = g * k * 0.95; b = g * k * 0.85; if (d1 > 0.12) dim(0.7); }
            break; }
        case T_MOSS: {   // mossy ground, Tallon Overworld green
            double n = fbm(u, v, 100), n2 = vnoise(u, v, 32, 101), g = 0.28 + n * 0.2;
            r = g * 0.62; g_ = g * 1.12; b = g * 0.38;
            if (n2 > 0.62) { r *= 1.25; g_ *= 1.18; }
            if (rnd(px, py, 102) > 0.97) { r *= 0.7; g_ *= 0.75; b *= 0.7; }
            if (rnd(px / 3, py / 3, 103) > 0.992) { r = 0.75; g_ = 0.72; b = 0.25; }
            break; }
        case T_MOSS_SIDE: {   // dirt with a ragged moss fringe at the top
            double n = fbm(u, v, 90), g = 0.24 + n * 0.16; r = g * 1.18; g_ = g * 0.86; b = g * 0.58;
            double fringe = 18 + 10 * vnoise(u, 0.5, 16, 104) + 6 * vnoise(u, 0.5, 64, 105);
            if (py < fringe) { double m = 0.28 + fbm(u, v, 100) * 0.2; r = m * 0.62; g_ = m * 1.12; b = m * 0.38; if (py > fringe - 3) dim(0.7); }
            break; }
        case T_SAND: {   // warm sand with wind ripples
            double n = fbm(u, v, 110), rip = 0.5 + 0.5 * std::sin((v * 12 + vnoise(u, v, 4, 111) * 3.0) * 6.2831853);
            double g = 0.56 + n * 0.16 + rip * 0.05; r = g * 1.12; g_ = g * 0.96; b = g * 0.68;
            if (rnd(px, py, 112) > 0.97) dim(0.85);
            break; }
        case T_SNOW: {   // Phendrana snow: soft blue-white with sparkles
            double n = fbm(u, v, 120), g = 0.78 + n * 0.14; r = g * 0.92; g_ = g * 0.97; b = g * 1.04;
            if (rnd(px, py, 121) > 0.993) { r = g_ = b = 1.0; }
            break; }
        case T_SNOW_SIDE: {   // rock face with a snow cap
            double n = fbm(u, v, 80), g = 0.30 + n * 0.2; r = g * 0.95; g_ = g * 0.97; b = g * 1.02;
            double cap = 14 + 9 * vnoise(u, 0.5, 16, 122);
            if (py < cap) { double s2 = 0.8 + fbm(u, v, 120) * 0.12; r = s2 * 0.92; g_ = s2 * 0.97; b = s2 * 1.04; if (py > cap - 2) dim(0.8); }
            break; }
        case T_ICE: {   // translucent-looking blue ice with white fractures
            double n = fbm(u, v, 130), g = 0.5 + n * 0.25; r = g * 0.62; g_ = g * 0.86; b = g * 1.05;
            double d1, d2; int id; worley(u, v, 4, 131, d1, d2, id);
            if (d2 - d1 < 0.04) { r = 0.85; g_ = 0.95; b = 1.0; }
            double streak = std::fabs(std::sin((u * 3 + v * 7 + vnoise(u, v, 8, 132)) * 6.2831853));
            if (streak > 0.97) { r += 0.1; g_ += 0.1; b += 0.1; }
            break; }
        case T_BASALT: {   // Magmoor basalt: dark columns, glowing cracks (glow mask in alpha)
            double n = fbm(u, v, 140), d1, d2; int id; worley(u, v, 4, 141, d1, d2, id);
            double g = 0.11 + n * 0.08 + rnd(id, 4, 142) * 0.04; r = g * 1.05; g_ = g * 0.95; b = g * 0.92;
            double edge = d2 - d1;
            if (edge < 0.035) { double k = 1 - edge / 0.035; r = 1.0; g_ = 0.35 + 0.3 * k; b = 0.05; a = 0.55 + 0.45 * k; }
            else if (edge < 0.06) dim(0.6);
            break; }
        case T_LAVA: {   // molten lava: drifting dark crust plates, bright molten seams (glow mask in alpha)
            double n = fbm(u, v, 150), d1, d2; int id; worley(u, v, 4, 151, d1, d2, id);
            double edge = d2 - d1 + (n - 0.5) * 0.12;
            double molten = 1.0 - smooth01(0.03, 0.22, edge);          // 1 at the seams
            double crust = 0.16 + 0.10 * rnd(id, 5, 152) + n * 0.08;    // plate brightness
            double hot = 0.35 + 0.65 * molten;
            r = crust * 1.3 * (1 - molten) + 1.0 * molten;
            g_ = crust * 0.5 * (1 - molten) + (0.35 + 0.5 * molten * molten) * molten;
            b = crust * 0.2 * (1 - molten) + 0.06 * molten;
            a = hot;   // plates glow a little (red heat), seams glow fully
            break; }
        case T_CHOZO: {   // carved Chozo sandstone bricks
            int row = py / 32, sy = py % 32, off = (row & 1) ? 32 : 0, sx = (px + off) % 64, brick = (px + off) / 64 + row * 7;
            double n = fbm(u, v, 160), g = 0.50 + n * 0.14 + (rnd(brick, row, 161) - 0.5) * 0.06; r = g * 1.12; g_ = g * 0.98; b = g * 0.72;
            if (sy < 2 || sx < 2) dim(0.45); else if (sy < 3 || sx < 3) dim(1.18);
            if (rnd(brick, row, 162) < 0.35) {   // engraved glyph: nested squares or a circle
                int cx = sx - 32, cy = sy - 16, ax = std::abs(cx), ay = std::abs(cy);
                if (rnd(brick, row, 163) < 0.5) { int m = std::max(ax * 2, ay * 4) / 4; if (ax < 22 && ay < 11 && (m == 5 || m == 9 || (m < 3 && ((ax + ay) & 2)))) dim(0.62); }
                else { double dd = std::sqrt((double)cx * cx + (double)cy * cy * 4); if (std::fabs(dd - 18) < 1.6 || std::fabs(dd - 9) < 1.4 || (ax < 2 && ay < 9)) dim(0.62); }
            }
            break; }
        default: break;
        }
        size_t o = ((size_t)py * TN + px) * 4;
        d[o] = clamp8(r); d[o + 1] = clamp8(g); d[o + 2] = clamp8(b); d[o + 3] = clamp8(a);
    }
    return d;
}

// ------------------------------------------------------------------ geometry
struct Vertex { float p[3], n[3], uv[2], c[4]; };    // 48 bytes: POSITION, NORMAL, TEXCOORD0, COLOR0(baked light)
struct MeshData { std::vector<Vertex> v; std::vector<uint32_t> i; };

enum MatId { M_FLOOR, M_WALL, M_TRIM, M_STRIP, M_CORE, M_DOOR, M_CANNON, M_CANNONGLOW, M_COUNT };
struct Material { TexKind tex; float env, glow, dyn; float tint[3]; MeshData mesh; };

struct DemoBounds { float x0 = -7, x1 = 7, y0 = 0, y1 = 6, z0 = 0, z1 = 28; };
struct StaticLight { V3 p, c; float r, k; };
struct AABB2 { float x0, z0, x1, z1; };
struct Pillar { float x, z, r; };

struct Scene {
    DemoBounds room;
    Material mats[M_COUNT];
    std::vector<StaticLight> lights;
    std::vector<AABB2> aabbs;
    std::vector<Pillar> pillars;

    static constexpr float CELL = 1.0f;

    // ---- baked lighting: hemisphere ambient + corner AO + static point lights (no shadows), written into vertex colour
    V3 bake(V3 p, V3 n) const {
        float h = n.y * 0.5f + 0.5f; V3 sky{ 0.24f, 0.28f, 0.26f }, gnd{ 0.11f, 0.09f, 0.08f };
        V3 c = gnd + (sky - gnd) * h;
        float ao = 1.f;
        const float ds[6] = { p.x - room.x0, room.x1 - p.x, p.y - room.y0, room.y1 - p.y, p.z - room.z0, room.z1 - p.z };
        for (float di : ds) if (di > 0.02f) ao *= 0.55f + 0.45f * std::min(di / 1.6f, 1.f);
        c = c * ao;
        for (const auto& L : lights) {
            V3 d = L.p - p; float dist = std::max(length(d), 1e-4f), att = std::max(1.f - dist / L.r, 0.f); att *= att;
            float ndl = std::max(dot(n, d * (1.f / dist)), 0.f), k = att * ndl * L.k * std::sqrt(ao);
            c = c + L.c * k;
        }
        return c;
    }

    struct Sample { V3 p, n; float u, v; bool hasCol; V3 col; };
    template <class F> void surf(MeshData& mb, int nu, int nv, F fn) {
        uint32_t base = (uint32_t)mb.v.size();
        for (int j = 0; j <= nv; j++) for (int i = 0; i <= nu; i++) {
            Sample s = fn((float)i / nu, (float)j / nv);
            V3 c = s.hasCol ? s.col : bake(s.p, s.n);
            mb.v.push_back({ { s.p.x, s.p.y, s.p.z }, { s.n.x, s.n.y, s.n.z }, { s.u, s.v }, { c.x, c.y, c.z, 1.f } });
        }
        int st = nu + 1;
        auto P = [&](uint32_t k) { const Vertex& q = mb.v[k]; return V3{ q.p[0], q.p[1], q.p[2] }; };
        auto N = [&](uint32_t k) { const Vertex& q = mb.v[k]; return V3{ q.n[0], q.n[1], q.n[2] }; };
        for (int j = 0; j < nv; j++) for (int i = 0; i < nu; i++) {
            uint32_t i00 = base + j * st + i, i10 = i00 + 1, i01 = i00 + st, i11 = i01 + 1;
            V3 p00 = P(i00), p10 = P(i10), p01 = P(i01), p11 = P(i11);
            V3 n = N(i00) + N(i10) + N(i01) + N(i11);
            // LH + clockwise-front: pick the triangle order whose numeric cross() points along the outward normal
            float s = dot(cross(p01 - p00, p10 - p00), n) + dot(cross(p01 - p10, p11 - p10), n);
            if (s > 0) mb.i.insert(mb.i.end(), { i00, i01, i10, i10, i01, i11 });
            else       mb.i.insert(mb.i.end(), { i00, i10, i01, i10, i11, i01 });
        }
    }

    struct BoxOpt { bool inward = false; unsigned skip = 0; bool hasCol = false; V3 col{ 0, 0, 0 }; };
    enum { F_NX = 1, F_PX = 2, F_NY = 4, F_PY = 8, F_NZ = 16, F_PZ = 32 };

    void box(MatId m, V3 mn, V3 mx, float tile) { box(m, mn, mx, tile, BoxOpt{}); }
    void box(MatId m, V3 mn, V3 mx, float tile, BoxOpt o) {
        MeshData& mb = mats[m].mesh; V3 d = mx - mn;
        struct Face { V3 org, du, dv, n; unsigned key; };
        Face F[6] = {
            { { mn.x, mn.y, mn.z }, { 0, 0, d.z }, { 0, d.y, 0 }, { -1, 0, 0 }, F_NX }, { { mx.x, mn.y, mn.z }, { 0, 0, d.z }, { 0, d.y, 0 }, { 1, 0, 0 }, F_PX },
            { { mn.x, mn.y, mn.z }, { d.x, 0, 0 }, { 0, 0, d.z }, { 0, -1, 0 }, F_NY }, { { mn.x, mx.y, mn.z }, { d.x, 0, 0 }, { 0, 0, d.z }, { 0, 1, 0 }, F_PY },
            { { mn.x, mn.y, mn.z }, { d.x, 0, 0 }, { 0, d.y, 0 }, { 0, 0, -1 }, F_NZ }, { { mn.x, mn.y, mx.z }, { d.x, 0, 0 }, { 0, d.y, 0 }, { 0, 0, 1 }, F_PZ } };
        for (auto& f : F) {
            if (o.skip & f.key) continue;
            V3 n = o.inward ? f.n * -1.f : f.n; float lu = length(f.du), lv = length(f.dv);
            surf(mb, std::max(1, (int)std::ceil(lu / CELL)), std::max(1, (int)std::ceil(lv / CELL)), [&](float u, float v) {
                return Sample{ f.org + f.du * u + f.dv * v, n, u * lu / tile, v * lv / tile, o.hasCol, o.col }; });
        }
    }
    void cylY(MatId m, float cx, float cz, float r, float y0, float y1, int seg, float tile) {
        surf(mats[m].mesh, seg, std::max(1, (int)std::ceil((y1 - y0) / CELL)), [&](float u, float v) {
            float a = u * 6.28318530718f, c = std::cos(a), s = std::sin(a);
            return Sample{ { cx + r * c, y0 + (y1 - y0) * v, cz + r * s }, { c, 0, s }, u * 6.28318530718f * r / tile, v * (y1 - y0) / tile, false, {} }; });
    }
    void cylZ(MatId m, float r, float z0, float z1, int seg, float tile, V3 col) {
        surf(mats[m].mesh, seg, 1, [&](float u, float v) {
            float a = u * 6.28318530718f, c = std::cos(a), s = std::sin(a);
            return Sample{ { r * c, r * s, z0 + (z1 - z0) * v }, { c, s, 0 }, u * 6.28318530718f * r / tile, v * (z1 - z0) / tile, true, col }; });
    }
    void discZ(MatId m, float r, float z, float facing, int seg, V3 col) {
        surf(mats[m].mesh, seg, 1, [&](float u, float v) {
            float a = u * 6.28318530718f;
            return Sample{ { r * v * std::cos(a), r * v * std::sin(a), z }, { 0, 0, facing }, 0.5f + 0.5f * v * std::cos(a), 0.5f + 0.5f * v * std::sin(a), true, col }; });
    }
    void sphere(MatId m, V3 c, float r, int nu, int nv, V3 col) {
        surf(mats[m].mesh, nu, nv, [&](float u, float v) {
            float ph = u * 6.28318530718f, th = v * 3.14159265359f;
            V3 n{ std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph) };
            return Sample{ c + n * r, n, u * 2.f, v, true, col }; });
    }
    void defMat(MatId id, TexKind tex, float env, float glow, float dyn, V3 tint = { 1, 1, 1 }) {
        Material& m = mats[id]; m.tex = tex; m.env = env; m.glow = glow; m.dyn = dyn; m.tint[0] = tint.x; m.tint[1] = tint.y; m.tint[2] = tint.z;
    }

    Scene() {
        // static lights used only for baking
        lights.push_back({ { 0, 2.4f, 10 }, { 1.0f, 0.55f, 0.2f }, 10, 1.1f });
        lights.push_back({ { 0, 3.0f, 26.5f }, { 0.1f, 0.7f, 0.9f }, 12, 1.0f });
        for (float z : { 4.f, 12.f, 20.f }) lights.push_back({ { 0, 5.0f, z }, { 0.7f, 0.8f, 0.6f }, 8, 0.5f });
        for (float z : { 2.f, 6.f, 10.f, 14.f, 18.f, 22.f, 26.f }) {
            lights.push_back({ { -6.4f, 3.2f, z }, { 0.9f, 0.4f, 0.1f }, 5, 0.5f });
            lights.push_back({ { 6.4f, 3.2f, z }, { 0.9f, 0.4f, 0.1f }, 5, 0.5f });
        }
        defMat(M_FLOOR, T_FLOOR, 0.0f, 1.0f, 1); defMat(M_WALL, T_WALL, 0.10f, 1.0f, 1); defMat(M_TRIM, T_TRIM, 0.40f, 1.0f, 1);
        defMat(M_STRIP, T_STRIP, 0.0f, 1.0f, 0); defMat(M_CORE, T_CORE, 0.35f, 0.9f, 0); defMat(M_DOOR, T_DOOR, 0.15f, 1.0f, 1);
        defMat(M_CANNON, T_CANNON, 0.5f, 0.8f, 1); defMat(M_CANNONGLOW, T_CORE, 0.2f, 1.1f, 0, { 1, 0.8f, 0.5f });

        V3 mn{ room.x0, room.y0, room.z0 }, mx{ room.x1, room.y1, room.z1 };
        BoxOpt in; in.inward = true;
        auto skipOnly = [&](unsigned keep) { BoxOpt o = in; o.skip = 63u & ~keep; return o; };
        box(M_FLOOR, mn, mx, 2, skipOnly(F_NY));
        box(M_TRIM, mn, mx, 4, skipOnly(F_PY));
        box(M_WALL, mn, mx, 6, skipOnly(F_NX | F_PX | F_NZ | F_PZ));
        { BoxOpt o; o.skip = F_NY | F_PZ | F_NX | F_PX | F_PY; box(M_DOOR, { -2.2f, 0, 27.75f }, { 2.2f, 4.4f, 28 }, 4.4f, o); }
        for (float z : { 3.f, 7.f, 11.f, 15.f, 19.f, 23.f }) {
            BoxOpt a; a.skip = F_NX; box(M_TRIM, { -7, 0, z - 0.3f }, { -6.4f, 6, z + 0.3f }, 3, a);
            BoxOpt b; b.skip = F_PX; box(M_TRIM, { 6.4f, 0, z - 0.3f }, { 7, 6, z + 0.3f }, 3, b);
        }
        for (float z : { 4.f, 12.f, 20.f }) {
            BoxOpt a; a.skip = F_PY; box(M_TRIM, { -7, 5.3f, z - 0.4f }, { 7, 6, z + 0.4f }, 3, a);
            BoxOpt b; b.skip = F_PY; b.hasCol = true; b.col = { 0.9f, 0.9f, 0.9f }; box(M_STRIP, { -2.5f, 5.2f, z - 0.12f }, { 2.5f, 5.3f, z + 0.12f }, 2, b);
        }
        for (float x : { -4.2f, 4.2f }) for (float z : { 6.f, 14.f, 22.f }) { cylY(M_TRIM, x, z, 0.55f, 0, 6, 16, 3); pillars.push_back({ x, z, 0.55f }); }
        BoxOpt ny; ny.skip = F_NY;
        box(M_TRIM, { -1.3f, 0, 8.7f }, { 1.3f, 0.6f, 11.3f }, 2.6f, ny);
        box(M_TRIM, { -0.9f, 0.6f, 9.1f }, { 0.9f, 0.9f, 10.9f }, 1.8f, ny);
        sphere(M_CORE, { 0, 1.75f, 10 }, 0.55f, 20, 14, { 0.6f, 0.6f, 0.6f });
        box(M_WALL, { -6.2f, 0, 12.6f }, { -4.8f, 1.4f, 14 }, 1.4f, ny);
        box(M_WALL, { 4.6f, 0, 17.5f }, { 5.8f, 1.2f, 18.7f }, 1.2f, ny);
        box(M_WALL, { 4.9f, 1.2f, 17.7f }, { 5.7f, 2.0f, 18.5f }, 0.8f, ny);
        // arm cannon in its own local space, +z forward
        V3 CC{ 0.2f, 0.21f, 0.25f }, GL{ 0.7f, 0.7f, 0.7f };
        cylZ(M_CANNON, 0.115f, -0.05f, 0.30f, 16, 0.5f, CC); discZ(M_CANNON, 0.115f, -0.05f, -1, 16, CC); discZ(M_CANNON, 0.065f, 0.88f, 1, 16, CC);
        cylZ(M_CANNON, 0.085f, 0.30f, 0.62f, 16, 0.5f, CC); cylZ(M_CANNON, 0.065f, 0.62f, 0.88f, 16, 0.5f, CC);
        { BoxOpt o; o.hasCol = true; o.col = CC; box(M_CANNON, { -0.075f, -0.14f, -0.45f }, { 0.075f, -0.03f, 0.2f }, 0.5f, o); }
        cylZ(M_CANNONGLOW, 0.092f, 0.40f, 0.46f, 16, 0.5f, GL); cylZ(M_CANNONGLOW, 0.072f, 0.66f, 0.69f, 16, 0.5f, GL);

        aabbs = { { -1.3f, 8.7f, 1.3f, 11.3f }, { -6.2f, 12.6f, -4.8f, 14.f }, { 4.6f, 17.5f, 5.8f, 18.7f } };
    }
};

// ------------------------------------------------------------------ player + camera
struct Input { bool w = 0, a = 0, s = 0, d = 0, left = 0, right = 0, up = 0, down = 0, jump = 0; };
struct Camera {
    V3 p{ 0, 1.7f, 2.0f }; float yaw = 0, pitch = 0, bob = 0, recoil = 0;
    void basis(V3& f, V3& r, V3& u) const {
        float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
        f = { sy * cp, sp, cy * cp }; r = { cy, 0, -sy }; u = cross(f, r);
    }
    V3 eye() const { return { p.x, p.y + std::fabs(std::sin(bob)) * 0.04f, p.z }; }
    void move(const Scene& sc, const Input& in, float dt) {
        V3 f, r, u; basis(f, r, u); V3 fw = normalize({ f.x, 0, f.z }), mv{ 0, 0, 0 };
        if (in.w) mv = mv + fw;
        if (in.s) mv = mv - fw;
        if (in.d) mv = mv + r;
        if (in.a) mv = mv - r;
        float spd = length(mv) > 0 ? 4.5f : 0.f; if (spd > 0) mv = normalize(mv) * (spd * dt);
        if (in.left)  yaw -= 1.8f * dt;
        if (in.right) yaw += 1.8f * dt;
        if (in.up)    pitch = std::min(1.4f, pitch + 1.5f * dt);
        if (in.down)  pitch = std::max(-1.4f, pitch - 1.5f * dt);
        V3 q{ p.x + mv.x, p.y, p.z + mv.z };
        q.x = std::max(-6.5f, std::min(6.5f, q.x)); q.z = std::max(0.5f, std::min(27.5f, q.z));
        const float rad = 0.35f;
        for (auto& pl : sc.pillars) {
            float dx = q.x - pl.x, dz = q.z - pl.z, dd = std::sqrt(dx * dx + dz * dz), m = pl.r + rad;
            if (dd < m && dd > 1e-5f) { q.x = pl.x + dx / dd * m; q.z = pl.z + dz / dd * m; }
        }
        for (auto& b : sc.aabbs) {
            float ex0 = b.x0 - rad, ex1 = b.x1 + rad, ez0 = b.z0 - rad, ez1 = b.z1 + rad;
            if (q.x > ex0 && q.x < ex1 && q.z > ez0 && q.z < ez1) {
                float dl[4] = { q.x - ex0, ex1 - q.x, q.z - ez0, ez1 - q.z }; int mi = (int)(std::min_element(dl, dl + 4) - dl);
                if (mi == 0) q.x = ex0; else if (mi == 1) q.x = ex1; else if (mi == 2) q.z = ez0; else q.z = ez1;
            }
        }
        if (spd > 0) bob += dt * 9;
        p = q;
        recoil = std::max(0.f, recoil - dt * 0.5f);
    }
};

// ------------------------------------------------------------------ particles / projectiles
struct Particle { V3 p, v; float life, max, size; V3 c; float a, drag, grav; bool mote; float ph; };
struct Bolt { V3 p, v; float life; };

struct Sim {
    std::vector<Particle> parts; std::vector<Bolt> bolts; V3 flashP{ 0, 0, 0 }; float flashT = 0;
    bool demo = true;                                   // demo room effects (core sparks, dust motes)
    bool (*solidAt)(void* ctx, V3 p) = nullptr; void* solidCtx = nullptr;   // when set, shots explode on blocks
    uint32_t rng = 0x1234567u;
    float R(float a, float b) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return a + (rng & 0xFFFFFF) / 16777216.f * (b - a); }

    void spark(V3 p, V3 v, float life, float size, V3 c, float a, float drag, float grav) {
        if (parts.size() < 3000) parts.push_back({ p, v, life, life, size, c, a, drag, grav, false, 0 });
    }
    Sim() {
        for (int i = 0; i < 90; i++)
            parts.push_back({ { R(-6.5f, 6.5f), R(0.3f, 5.7f), R(0.3f, 27.7f) }, { R(-.08f, .08f), R(-.05f, .1f), R(-.08f, .08f) }, 1e9f, 1e9f, R(0.025f, 0.055f), { 0.55f, 0.85f, 0.8f }, R(0.3f, 0.7f), 0, 0, true, R(0, 6.28f) });
    }
    static bool inRoom(V3 p) { return p.x > -6.9f && p.x < 6.9f && p.y > 0.05f && p.y < 5.9f && p.z > 0.1f && p.z < 27.9f; }
    void fire(V3 muzzle, V3 aim) {
        bolts.push_back({ muzzle, aim * 26.f, 1.6f }); flashP = muzzle; flashT = 0.12f;
        for (int i = 0; i < 8; i++) spark(muzzle, aim * R(1, 4) + V3{ R(-1, 1), R(-1, 1), R(-1, 1) }, R(0.15f, 0.3f), R(0.06f, 0.12f), { 1, 0.75f, 0.35f }, 1, 3, 0);
    }
    void explode(V3 p) {
        flashP = p; flashT = 0.3f;
        for (int i = 0; i < 30; i++) { V3 d = normalize({ R(-1, 1), R(-1, 1), R(-1, 1) }); spark(p, d * R(2, 6), R(0.4f, 1.0f), R(0.07f, 0.16f), { 1, R(0.4f, 0.8f), 0.2f }, 1, 1.5f, 6); }
        spark(p, { 0, 0, 0 }, 0.18f, 1.5f, { 1, 0.6f, 0.25f }, 0.9f, 0, 0);
    }
    void update(float dt) {
        for (int i = (int)bolts.size() - 1; i >= 0; i--) {
            Bolt& b = bolts[i]; b.life -= dt; b.p = b.p + b.v * dt;
            spark(b.p, { R(-.3f, .3f), R(-.3f, .3f), R(-.3f, .3f) }, 0.35f, 0.13f, { 1, 0.55f, 0.15f }, 0.8f, 1, 0);
            bool hit = solidAt ? solidAt(solidCtx, b.p) : !inRoom(b.p);
            if (b.life <= 0 || hit) { explode(b.p); bolts.erase(bolts.begin() + i); }
        }
        if (flashT > 0) flashT -= dt;
        if (demo && R(0, 1) < dt * 16) spark({ R(-.15f, .15f), 1.95f, 10 + R(-.15f, .15f) }, { R(-.2f, .2f), R(0.5f, 1.1f), R(-.2f, .2f) }, R(2, 3.5f), R(0.1f, 0.18f), { 1, 0.5f, 0.12f }, 0.9f, 0.2f, -0.15f);
        for (int i = (int)parts.size() - 1; i >= 0; i--) {
            Particle& q = parts[i]; q.life -= dt; if (q.life <= 0) { parts.erase(parts.begin() + i); continue; }
            q.v.y -= q.grav * dt; q.v = q.v * std::max(0.f, 1.f - q.drag * dt); q.p = q.p + q.v * dt;
            if (q.mote) {
                float* P = &q.p.x; const float lo[3] = { -6.8f, 0.2f, 0.2f }, hi[3] = { 6.8f, 5.8f, 27.8f };
                for (int a = 0; a < 3; a++) { if (P[a] < lo[a]) P[a] = hi[a]; else if (P[a] > hi[a]) P[a] = lo[a]; }
            }
        }
    }
};

} // namespace px
