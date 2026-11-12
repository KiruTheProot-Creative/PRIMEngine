#include "blocks.h"
#include <algorithm>
#include <cmath>

namespace mc {

BlockRegistry::BlockRegistry() {
    BlockDef air; air.name = "air"; air.render = Render::Air; air.solid = false;
    defs.push_back(air);
    // texture 0: magenta/black checkerboard for anything missing
    TexImage m; m.name = ""; m.w = m.h = TEX; m.rgba.resize(TEX * TEX * 4);
    for (int y = 0; y < TEX; y++) for (int x = 0; x < TEX; x++) {
        bool on = ((x / 8) + (y / 8)) & 1; uint8_t* p = &m.rgba[(y * TEX + x) * 4];
        p[0] = on ? 250 : 20; p[1] = 0; p[2] = on ? 250 : 20; p[3] = 255;
    }
    textures.push_back(m);
    missing.name = "missing block";
}

static bool hasSeeThrough(const std::vector<uint8_t>& px) { for (size_t i = 3; i < px.size(); i += 4) if (px[i] < 128) return true; return false; }
int BlockRegistry::setTexture(const std::string& name, int w, int h, std::vector<uint8_t> rgba) {
    TexImage t; t.name = name; t.w = w; t.h = h; t.rgba = std::move(rgba); t.transparent = hasSeeThrough(t.rgba);
    int i = textureIndex(name);
    if (i > 0) { textures[i] = std::move(t); return i; }
    textures.push_back(std::move(t)); return (int)textures.size() - 1;
}
int BlockRegistry::addTexture(const std::vector<uint8_t>& rgba16) { return setTexture("tex" + std::to_string(textures.size()), TEX, TEX, rgba16); }
bool BlockRegistry::removeTexture(const std::string& name) {
    int i = textureIndex(name); if (i <= 0) return false;
    textures.erase(textures.begin() + i); resolve(); return true;
}
int BlockRegistry::textureIndex(const std::string& name) const {
    if (name.empty()) return -1;
    for (size_t i = 1; i < textures.size(); i++) if (textures[i].name == name) return (int)i;
    return -1;
}
int BlockRegistry::layerSize() const {
    int m = TEX;
    for (size_t i = 1; i < textures.size(); i++) m = std::max(m, textures[i].w);
    int s = TEX; while (s < m && s < 512) s *= 2;   // power of two, 16 .. 512
    return s;
}
std::vector<uint8_t> BlockRegistry::layer(int li, int S) const {
    const TexImage& t = textures[li];
    // a tall strip of square frames (Minecraft animation style): use the first frame
    int sw = t.w, sh = (t.h > t.w && t.h % t.w == 0) ? t.w : t.h;
    std::vector<uint8_t> out((size_t)S * S * 4);
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        // source area covered by this pixel; nearest when enlarging, average when shrinking
        int x0 = x * sw / S, x1 = std::max(x0 + 1, (x + 1) * sw / S), y0 = y * sh / S, y1 = std::max(y0 + 1, (y + 1) * sh / S);
        uint32_t acc[4] = {}, n = 0;
        for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) { const uint8_t* p = &t.rgba[((size_t)yy * t.w + xx) * 4]; for (int k = 0; k < 4; k++) acc[k] += p[k]; n++; }
        for (int k = 0; k < 4; k++) out[((size_t)y * S + x) * 4 + k] = (uint8_t)(acc[k] / n);
    }
    return out;
}

BlockId BlockRegistry::add(const BlockDef& d) {
    nextId = std::max<int>(nextId, (int)defs.size());
    if (nextId > MAX_BLOCK_TYPES) return AIR;
    BlockId id = (BlockId)nextId; put(id, d); return id;
}
void BlockRegistry::put(BlockId id, const BlockDef& d) {
    id = typeOf(id); if (id == AIR) return;
    if (defs.size() <= id) { BlockDef gap; gap.exists = false; defs.resize(id + 1, gap); }
    defs[id] = d; defs[id].exists = true; nextId = std::max<int>(nextId, id + 1);
    for (int f = 0; f < 6; f++) { int t = textureIndex(d.faces[f]); defs[id].tex[f] = t < 0 ? 0 : t; }
}
void BlockRegistry::remove(BlockId id) {
    id = typeOf(id); if (id == AIR || id >= defs.size()) return;
    defs[id] = BlockDef{}; defs[id].exists = false;   // the id stays used: worlds may still contain it
}
BlockId BlockRegistry::liquidBlock(int kind) const {
    for (size_t i = 1; i < defs.size(); i++) if (defs[i].exists && defs[i].liquid == kind && !defs[i].spring) return (BlockId)i;
    return AIR;
}
BlockId BlockRegistry::find(const std::string& name) const {
    for (size_t i = 1; i < defs.size(); i++) if (defs[i].exists && defs[i].name == name) return (BlockId)i;
    return AIR;
}
void BlockRegistry::resolve() {
    for (auto& d : defs) for (int f = 0; f < 6; f++) { int t = textureIndex(d.faces[f]); d.tex[f] = t < 0 ? 0 : t; }
}
std::string BlockRegistry::uniqueName(const std::string& want, BlockId except) const {
    std::string base = want.empty() ? "block" : want, n = base; int k = 2;
    for (;;) { BlockId f = find(n); if (f == AIR || f == typeOf(except)) return n; n = base + " " + std::to_string(k++); }
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
Img water() {
    Img im;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) { float k = 1 + (rnd(x / 2, y, 30) - 0.5f) * 0.12f + 0.06f * std::sin((x + y * 0.5f) * 0.8f); im.put(x, y, 0.16f * k, 0.38f * k, 0.62f * k, 0.8f); }
    return im;
}
Img lava() {
    Img im;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float c = rnd(x / 3, y / 3, 31), f = rnd(x, y, 32);
        if (c > 0.55f) im.put(x, y, 1.0f, 0.55f + 0.3f * f, 0.1f); else im.put(x, y, 0.55f + 0.2f * f, 0.12f, 0.03f);
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

void addBuiltinLiquids(BlockRegistry& r) {
    if (r.textureIndex("water") < 0) r.setTexture("water", N, N, water().px);
    if (r.textureIndex("lava") < 0) r.setTexture("lava", N, N, lava().px);
    auto liq = [&](const char* name, const char* tex, int kind, bool spring) {
        if (r.find(name)) return;
        BlockDef d; d.name = name; d.render = Render::Liquid; d.solid = false; d.liquid = kind; d.spring = spring; d.category = "Liquids";
        for (auto& f : d.faces) f = tex;
        if (kind == 2) { d.light = 15; d.lightColor = 0xFF7020; } else d.shine = 0.9f;
        r.add(d);
    };
    liq("water", "water", 1, false); liq("lava", "lava", 2, false); liq("water spring", "water", 1, true); liq("lava spring", "lava", 2, true);
}

void registerBuiltinBlocks(BlockRegistry& r) {
    // ids 1..12 in this order (the first worlds were saved with these numbers)
    r.setTexture("stone", N, N, stone().px); r.setTexture("grass_top", N, N, grassTop().px); r.setTexture("grass_side", N, N, grassSide().px);
    r.setTexture("dirt", N, N, dirt().px); r.setTexture("cobblestone", N, N, cobble().px); r.setTexture("planks", N, N, planks().px);
    r.setTexture("log_side", N, N, logSide().px); r.setTexture("log_top", N, N, logTop().px); r.setTexture("sand", N, N, sand().px);
    r.setTexture("bricks", N, N, bricks().px); r.setTexture("metal", N, N, metal().px); r.setTexture("lamp", N, N, lamp().px);
    r.setTexture("glass", N, N, glass().px); r.setTexture("leaves", N, N, leaves().px);
    auto block = [&](const char* name, const char* side, const char* top = nullptr, const char* bottom = nullptr, Render rr = Render::Opaque, bool cullSame = false) {
        BlockDef d; d.name = name; d.render = rr; d.cullSame = cullSame;
        for (auto& f : d.faces) f = side;
        if (top) d.faces[F_PY] = top;
        if (bottom) d.faces[F_NY] = bottom; else if (top) d.faces[F_NY] = top;
        return r.add(d);
    };
    block("stone", "stone");
    block("grass", "grass_side", "grass_top", "dirt");
    block("dirt", "dirt");
    block("cobblestone", "cobblestone");
    block("planks", "planks");
    block("log", "log_side", "log_top");
    block("sand", "sand");
    block("bricks", "bricks");
    { BlockId m = block("metal", "metal"); BlockDef d = r.get(m); d.shine = 0.6f; r.put(m, d); }
    { BlockId l = block("lamp", "lamp"); BlockDef d = r.get(l); d.light = 15; r.put(l, d); }
    { BlockId g = block("glass", "glass", nullptr, nullptr, Render::Cutout, true); BlockDef d = r.get(g); d.shine = 0.5f; r.put(g, d); }
    { BlockId l = block("leaves", "leaves", nullptr, nullptr, Render::Cutout, false); BlockDef d = r.get(l); d.waves = true; r.put(l, d); }
    for (const char* n : { "grass", "dirt", "sand", "leaves", "log" }) { BlockId b = r.find(n); BlockDef d = r.get(b); d.category = "Nature"; r.put(b, d); }
    { BlockId b = r.find("lamp"); BlockDef d = r.get(b); d.category = "Lights"; r.put(b, d); }
    { BlockId b = r.find("glass"); BlockDef d = r.get(b); d.category = "Glass & metal"; r.put(b, d); b = r.find("metal"); d = r.get(b); d.category = "Glass & metal"; r.put(b, d); }
    addBuiltinLiquids(r);
}

} // namespace mc
