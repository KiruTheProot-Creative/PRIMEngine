#include "blocks.h"
#include <algorithm>
#include <cmath>

namespace mc {

BlockRegistry::BlockRegistry() {
    BlockDef air; air.name = "air"; air.render = Render::Air; air.solid = false;
    defs.push_back(air);
}
int BlockRegistry::addTexture(const std::vector<uint8_t>& rgba) { textures.push_back(rgba); return (int)textures.size() - 1; }
BlockId BlockRegistry::add(const BlockDef& d) { defs.push_back(d); return (BlockId)(defs.size() - 1); }
BlockId BlockRegistry::find(const std::string& name) const {
    for (size_t i = 0; i < defs.size(); i++) if (defs[i].name == name) return (BlockId)i;
    return AIR;
}

// ------------------------------------------------------------------ procedural placeholder textures (16 x 16 pixel art)
namespace {
constexpr int N = BlockRegistry::TEX;
uint32_t hsh(int x, int y, int s) { uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)s * 2246822519u; h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16); }
float rnd(int x, int y, int s) { return (hsh(x, y, s) & 0xFFFF) / 65535.f; }
struct Img {
    std::vector<uint8_t> px = std::vector<uint8_t>(N * N * 4, 255);
    void put(int x, int y, float r, float g, float b, float a = 1) {
        uint8_t* p = &px[(y * N + x) * 4];
        auto c = [](float v) { return (uint8_t)std::max(0.f, std::min(255.f, v * 255.f + 0.5f)); };
        p[0] = c(r); p[1] = c(g); p[2] = c(b); p[3] = c(a);
    }
};
// a base colour with per-pixel brightness variation (the classic pixel-art noise look)
Img noisy(float r, float g, float b, float var, int seed) {
    Img im;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) { float k = 1 + (rnd(x, y, seed) - 0.5f) * 2 * var; im.put(x, y, r * k, g * k, b * k); }
    return im;
}
Img stone() {
    Img im = noisy(0.50f, 0.50f, 0.52f, 0.10f, 1);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) if (rnd(x / 2, y, 2) > 0.86f) { float k = 0.72f; im.put(x, y, 0.5f * k, 0.5f * k, 0.52f * k); }
    return im;
}
Img dirt() {
    Img im = noisy(0.47f, 0.33f, 0.22f, 0.14f, 3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) if (rnd(x, y, 4) > 0.92f) im.put(x, y, 0.58f, 0.45f, 0.32f);
    return im;
}
Img grassTop() { return noisy(0.36f, 0.58f, 0.24f, 0.13f, 5); }
Img grassSide() {
    Img im = dirt();
    for (int x = 0; x < N; x++) {
        int h = 3 + (int)(rnd(x, 0, 6) * 3);
        for (int y = 0; y < h; y++) { float k = 1 + (rnd(x, y, 7) - 0.5f) * 0.25f; im.put(x, y, 0.36f * k, 0.58f * k, 0.24f * k); }
    }
    return im;
}
Img cobble() {
    Img im;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        // irregular stones: nearest of a few jittered points per 5x5 cell, dark mortar between
        float best = 9, second = 9; int id = 0;
        for (int cy = -1; cy <= 4; cy++) for (int cx = -1; cx <= 4; cx++) {
            float px = cx * 5 + rnd(cx & 3, cy & 3, 8) * 5, py = cy * 5 + rnd(cx & 3, cy & 3, 9) * 5;
            float dx = std::min(std::fabs(x - px), N - std::fabs(x - px)), dy = std::min(std::fabs(y - py), N - std::fabs(y - py)), d = std::sqrt(dx * dx + dy * dy);
            if (d < best) { second = best; best = d; id = (cx & 3) * 4 + (cy & 3); } else if (d < second) second = d;
        }
        float base = 0.42f + rnd(id, 0, 10) * 0.16f + (rnd(x, y, 11) - 0.5f) * 0.06f;
        if (second - best < 1.0f) base *= 0.55f;
        im.put(x, y, base, base, base * 1.03f);
    }
    return im;
}
Img planks() {
    Img im;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int row = y / 4, off = (row & 1) ? 8 : 0;
        float k = 1 + (rnd(x / 3, y, 12) - 0.5f) * 0.12f + (rnd(row, 0, 13) - 0.5f) * 0.08f;
        if (y % 4 == 3 || (x + off) % 16 == 0) k *= 0.62f;
        im.put(x, y, 0.66f * k, 0.50f * k, 0.31f * k);
    }
    return im;
}
Img logSide() {
    Img im;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) { float k = 1 + (rnd(x, y / 4, 14) - 0.5f) * 0.3f; if (x % 4 == 0) k *= 0.7f; im.put(x, y, 0.40f * k, 0.30f * k, 0.19f * k); }
    return im;
}
Img logTop() {
    Img im;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float dx = x - 7.5f, dy = y - 7.5f, d = std::sqrt(dx * dx + dy * dy);
        bool bark = x == 0 || y == 0 || x == N - 1 || y == N - 1;
        float k = 1 + (rnd(x, y, 15) - 0.5f) * 0.08f; if (((int)d) % 2 == 0) k *= 0.86f;
        if (bark) im.put(x, y, 0.40f * k, 0.30f * k, 0.19f * k); else im.put(x, y, 0.70f * k, 0.56f * k, 0.36f * k);
    }
    return im;
}
Img sand() { return noisy(0.86f, 0.80f, 0.58f, 0.06f, 16); }
Img glass() {
    Img im;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        bool edge = x == 0 || y == 0 || x == N - 1 || y == N - 1;
        bool streak = (x + y == 10 || x + y == 11) && x > 2 && x < 9;
        if (edge) im.put(x, y, 0.78f, 0.88f, 0.92f, 1); else if (streak) im.put(x, y, 0.95f, 0.98f, 1, 1); else im.put(x, y, 0, 0, 0, 0);
    }
    return im;
}
Img leaves() {
    Img im;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float r = rnd(x, y, 17); float k = 0.75f + rnd(x, y, 18) * 0.4f;
        if (r < 0.22f) im.put(x, y, 0, 0, 0, 0); else im.put(x, y, 0.22f * k, 0.48f * k, 0.18f * k);
    }
    return im;
}
Img bricks() {
    Img im;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int row = y / 4, off = (row & 1) ? 4 : 0;
        bool mortar = y % 4 == 3 || (x + off) % 8 == 7;
        float k = 1 + (rnd((x + off) / 8, row, 19) - 0.5f) * 0.18f + (rnd(x, y, 20) - 0.5f) * 0.08f;
        if (mortar) im.put(x, y, 0.70f, 0.68f, 0.64f); else im.put(x, y, 0.62f * k, 0.30f * k, 0.24f * k);
    }
    return im;
}
Img metal() {
    Img im = noisy(0.62f, 0.66f, 0.70f, 0.05f, 21);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        if (x == 0 || y == 0) im.put(x, y, 0.80f, 0.84f, 0.88f);
        if (x == N - 1 || y == N - 1) im.put(x, y, 0.40f, 0.43f, 0.47f);
        if ((x == 2 || x == 13) && (y == 2 || y == 13)) im.put(x, y, 0.35f, 0.37f, 0.40f);
    }
    return im;
}
Img lamp() {
    Img im;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float dx = x - 7.5f, dy = y - 7.5f, d = std::sqrt(dx * dx + dy * dy), k = 1 + (rnd(x, y, 22) - 0.5f) * 0.12f;
        if (d < 6) im.put(x, y, 1.0f * k, 0.86f * k, 0.52f * k); else im.put(x, y, 0.55f * k, 0.42f * k, 0.25f * k);
    }
    return im;
}
} // namespace

void registerBuiltinBlocks(BlockRegistry& r) {
    auto cube = [&](const char* name, Img tex, Render rr = Render::Opaque, bool cullSame = false) {
        BlockDef d; d.name = name; d.render = rr; d.cullSame = cullSame; int t = r.addTexture(tex.px);
        for (int& f : d.tex) f = t;
        return r.add(d);
    };
    cube("stone", stone());
    { BlockDef d; d.name = "grass"; int top = r.addTexture(grassTop().px), side = r.addTexture(grassSide().px), bot = r.addTexture(dirt().px);
      d.tex[F_PX] = d.tex[F_NX] = d.tex[F_PZ] = d.tex[F_NZ] = side; d.tex[F_PY] = top; d.tex[F_NY] = bot; r.add(d); }
    cube("dirt", dirt());
    cube("cobblestone", cobble());
    cube("planks", planks());
    { BlockDef d; d.name = "log"; int side = r.addTexture(logSide().px), top = r.addTexture(logTop().px);
      d.tex[F_PX] = d.tex[F_NX] = d.tex[F_PZ] = d.tex[F_NZ] = side; d.tex[F_PY] = d.tex[F_NY] = top; r.add(d); }
    cube("sand", sand());
    cube("bricks", bricks());
    cube("metal", metal());
    cube("lamp", lamp());
    cube("glass", glass(), Render::Cutout, true);
    cube("leaves", leaves(), Render::Cutout, false);
}

} // namespace mc
