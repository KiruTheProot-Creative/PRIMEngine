#include "brush.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace mc {

const BrushInfo& shapeInfo(BrushShape s) {
    static const BrushInfo v[] = {
        { "Sphere", "A ball. The all-rounder." },
        { "Cube", "A box. Hard edges, walls and floors." },
        { "Cylinder", "A round column. Set its height." },
        { "Dome", "The top half of a ball. Hills and caves." },
        { "Diamond", "Pointy in every direction. Rocky shapes." },
        { "Pyramid", "Square bottom, pointy top. Steep peaks." },
    };
    return v[std::clamp((int)s, 0, (int)BrushShape::COUNT - 1)];
}
const BrushInfo& opInfo(BrushOp o) {
    static const BrushInfo v[] = {
        { "Add", "Fills the shape with the material." },
        { "Remove", "Digs the shape out." },
        { "Paint", "Recolours only the surface (grass on hills, sand on beaches)." },
        { "Replace", "Swaps one block for the material inside the shape." },
        { "Smooth", "Rounds off bumps and fills dents." },
        { "Raise", "Pushes the ground up, most in the middle." },
        { "Lower", "Pushes the ground down, most in the middle." },
        { "Flatten", "Cuts and fills to the height where the stroke started." },
        { "Fill", "Fills holes up to the height you click (pits, lake beds)." },
        { "Roughen", "Adds rocky noise to the surface." },
    };
    return v[std::clamp((int)o, 0, (int)BrushOp::COUNT - 1)];
}
BrushOp opposite(BrushOp o) {
    switch (o) {
    case BrushOp::Add: return BrushOp::Remove;
    case BrushOp::Remove: return BrushOp::Add;
    case BrushOp::Raise: return BrushOp::Lower;
    case BrushOp::Lower: return BrushOp::Raise;
    case BrushOp::Fill: return BrushOp::Remove;
    default: return o;
    }
}

static int halfH(const BrushSettings& b) { return b.shape == BrushShape::Cylinder || b.shape == BrushShape::Cube ? std::max(0, b.height) : b.radius; }

bool insideBrush(const BrushSettings& b, int dx, int dy, int dz) {
    double r = b.radius + 0.5, x = dx, y = dy, z = dz;
    switch (b.shape) {
    case BrushShape::Sphere: return x * x + y * y + z * z <= r * r;
    case BrushShape::Cube: return std::abs(dx) <= b.radius && std::abs(dy) <= halfH(b) && std::abs(dz) <= b.radius;
    case BrushShape::Cylinder: return x * x + z * z <= r * r && std::abs(dy) <= halfH(b);
    case BrushShape::Dome: return dy >= 0 && x * x + y * y + z * z <= r * r;
    case BrushShape::Diamond: return std::abs(dx) + std::abs(dy) + std::abs(dz) <= b.radius;
    case BrushShape::Pyramid: { if (dy < 0 || dy > b.radius) return false; int w = b.radius - dy; return std::abs(dx) <= w && std::abs(dz) <= w; }
    default: return false;
    }
}

static uint32_t hash3(int x, int y, int z, uint32_t s) {
    uint32_t h = s * 0x9E3779B1u ^ (uint32_t)x * 0x85EBCA77u ^ (uint32_t)y * 0xC2B2AE3Du ^ (uint32_t)z * 0x27D4EB2Fu;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15; return h;
}
static double valueNoise(double x, double y, double z, uint32_t s) {   // smooth noise, 0 .. 1
    int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
    double fx = x - xi, fy = y - yi, fz = z - zi;
    auto sm = [](double t) { return t * t * (3 - 2 * t); }; fx = sm(fx); fy = sm(fy); fz = sm(fz);
    auto v = [&](int a, int b, int c) { return (hash3(xi + a, yi + b, zi + c, s) & 0xFFFF) / 65535.0; };
    auto l = [](double a, double b, double t) { return a + (b - a) * t; };
    return l(l(l(v(0, 0, 0), v(1, 0, 0), fx), l(v(0, 1, 0), v(1, 1, 0), fx), fy), l(l(v(0, 0, 1), v(1, 0, 1), fx), l(v(0, 1, 1), v(1, 1, 1), fx), fy), fz);
}

std::vector<Edit> brushEdits(const World& w, const BrushSettings& b, I3 c, BlockId material, int flattenY, uint32_t seed) {
    std::vector<Edit> out;
    const BlockRegistry& reg = w.reg;
    auto isLiquid = [&](BlockId v) { return v != AIR && reg.has(v) && reg.get(v).liquid != 0; };
    auto filled = [&](BlockId v) { return v != AIR && !isLiquid(v); };   // ground
    auto emptyish = [&](BlockId v) { return v == AIR || isLiquid(v); };  // may be filled in
    auto put = [&](int x, int y, int z, BlockId before, BlockId after) { if (before != after && w.loaded(x, y, z)) out.push_back({ x, y, z, before, after }); };
    const int R = std::max(0, b.radius), H = halfH(b);
    material = typeOf(material);
    auto each = [&](auto fn) {
        for (int dy = -H; dy <= H; dy++) for (int dz = -R; dz <= R; dz++) for (int dx = -R; dx <= R; dx++)
            if (insideBrush(b, dx, dy, dz)) fn(c.x + dx, c.y + dy, c.z + dz, dx, dy, dz);
    };
    switch (b.op) {
    case BrushOp::Add:
        if (!material) break;
        each([&](int x, int y, int z, int, int, int) { BlockId v = w.get(x, y, z); if (emptyish(v)) put(x, y, z, v, material); });
        break;
    case BrushOp::Remove:
        each([&](int x, int y, int z, int, int, int) { BlockId v = w.get(x, y, z); if (filled(v) || (b.liquidsToo && isLiquid(v))) put(x, y, z, v, AIR); });
        break;
    case BrushOp::Paint:
        if (!material) break;
        each([&](int x, int y, int z, int, int, int) {
            BlockId v = w.get(x, y, z); if (!filled(v) || typeOf(v) == material) return;
            bool surface = false;   // open air within paintDepth above, or right next to it
            for (int k = 1; k <= std::max(1, b.paintDepth) && !surface; k++) surface = emptyish(w.get(x, y + k, z));
            if (!surface) { const int n[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }; for (auto& d : n) surface |= emptyish(w.get(x + d[0], y, z + d[1])); surface |= emptyish(w.get(x, y - 1, z)) && b.paintDepth <= 1; }
            if (surface) put(x, y, z, v, makeBlock(material));
        });
        break;
    case BrushOp::Replace:
        if (!material) break;
        each([&](int x, int y, int z, int, int, int) {
            BlockId v = w.get(x, y, z); if (!filled(v)) return;
            if (b.replaceFrom ? typeOf(v) == typeOf(b.replaceFrom) : typeOf(v) != material) put(x, y, z, v, makeBlock(material));
        });
        break;
    case BrushOp::Smooth: {
        // each block looks at its 26 neighbours (as they were before this dab): lonely blocks go, dents fill
        int k = (int)std::lround((1 - std::clamp(b.strength, 0.f, 1.f)) * 5);
        each([&](int x, int y, int z, int, int, int) {
            BlockId v = w.get(x, y, z); int n = 0; std::map<BlockId, int> types;
            for (int a = -1; a <= 1; a++) for (int bb = -1; bb <= 1; bb++) for (int cc = -1; cc <= 1; cc++) {
                if (!a && !bb && !cc) continue;
                BlockId u = w.get(x + a, y + bb, z + cc); if (filled(u)) { n++; types[typeOf(u)] += bb <= 0 ? 2 : 1; }
            }
            if (filled(v) && n <= 12 - k) put(x, y, z, v, AIR);
            else if (emptyish(v) && n >= 14 + k && !types.empty()) {
                BlockId best = 0; int bc = -1; for (auto& t : types) if (t.second > bc) { bc = t.second; best = t.first; }
                if (!isLiquid(v) || b.liquidsToo) put(x, y, z, v, makeBlock(best));
            }
        });
        break; }
    case BrushOp::Raise: case BrushOp::Lower: {
        // works on columns: find the ground's top, push it up or down, most in the middle
        bool up = b.op == BrushOp::Raise; int reach = R + 24;
        for (int dz = -R; dz <= R; dz++) for (int dx = -R; dx <= R; dx++) {
            double d = std::sqrt((double)dx * dx + (double)dz * dz);
            bool in = b.shape == BrushShape::Cube || b.shape == BrushShape::Pyramid ? true : b.shape == BrushShape::Diamond ? std::abs(dx) + std::abs(dz) <= R : d <= R + 0.5;
            if (!in) continue;
            double fall = R > 0 ? std::max(0.0, 1 - d / (R + 1.0)) : 1;
            if (b.shape == BrushShape::Cube || b.shape == BrushShape::Cylinder) fall = 1;
            int amount = (int)std::lround(fall * (0.4 + std::clamp(b.strength, 0.f, 1.f) * 2.6));
            if (amount <= 0) continue;
            int x = c.x + dx, z = c.z + dz, top = INT32_MIN;
            for (int y = c.y + reach; y >= c.y - reach; y--) if (filled(w.get(x, y, z))) { top = y; break; }
            if (top == INT32_MIN) continue;
            BlockId topT = w.get(x, top, z), sub = filled(w.get(x, top - 1, z)) ? w.get(x, top - 1, z) : topT;
            if (up) {
                for (int k = 1; k <= amount; k++) { BlockId v = w.get(x, top + k, z); if (!emptyish(v)) break; put(x, top + k, z, v, k == amount ? topT : sub); }
                if (amount >= 1 && typeOf(sub) != typeOf(topT)) put(x, top, z, topT, sub);   // the old top is buried now
            } else {
                for (int k = 0; k < amount; k++) put(x, top - k, z, w.get(x, top - k, z), AIR);
                BlockId nt = w.get(x, top - amount, z); if (filled(nt)) put(x, top - amount, z, nt, topT);   // the top layer (grass) moves down
            }
        }
        break; }
    case BrushOp::Flatten: case BrushOp::Fill: {
        bool cut = b.op == BrushOp::Flatten;
        for (int dz = -R; dz <= R; dz++) for (int dx = -R; dx <= R; dx++) {
            bool in = false; for (int dy = -H; dy <= H && !in; dy++) in = insideBrush(b, dx, dy, dz);
            if (!in) continue;
            int x = c.x + dx, z = c.z + dz, lo = c.y - H - (cut ? 0 : 8), hi = c.y + H + (cut ? 16 : 0);
            BlockId fillT = material ? makeBlock(material) : AIR;
            for (int y = lo; y <= hi; y++) {
                BlockId v = w.get(x, y, z);
                if (y > flattenY) { if (cut && filled(v)) put(x, y, z, v, AIR); }
                else if (emptyish(v) && fillT) put(x, y, z, v, fillT);
            }
        }
        break; }
    case BrushOp::Roughen: {
        double s = std::clamp(b.noiseScale, 0.03f, 1.f); double t = 0.5 + (1 - std::clamp(b.strength, 0.f, 1.f)) * 0.25;
        each([&](int x, int y, int z, int, int, int) {
            BlockId v = w.get(x, y, z); double n = valueNoise(x * s, y * s, z * s, seed);
            bool nearAir = emptyish(w.get(x, y + 1, z)) || emptyish(w.get(x + 1, y, z)) || emptyish(w.get(x - 1, y, z)) || emptyish(w.get(x, y, z + 1)) || emptyish(w.get(x, y, z - 1));
            bool nearGround = filled(w.get(x, y - 1, z)) || filled(w.get(x + 1, y, z)) || filled(w.get(x - 1, y, z)) || filled(w.get(x, y, z + 1)) || filled(w.get(x, y, z - 1));
            if (filled(v) && nearAir && n > t) put(x, y, z, v, AIR);
            else if (emptyish(v) && nearGround && n < 1 - t) { BlockId g = filled(w.get(x, y - 1, z)) ? w.get(x, y - 1, z) : material ? makeBlock(material) : AIR; if (g) put(x, y, z, v, g); }
        });
        break; }
    default: break;
    }
    return out;
}

std::vector<std::pair<V3, V3>> brushOutline(const BrushSettings& b) {
    std::vector<std::pair<V3, V3>> L; double r = b.radius + 0.5, h = halfH(b) + 0.5;
    auto circle = [&](int axis, double y0, double rad, int seg, double a0 = 0, double a1 = 6.2831853) {
        for (int i = 0; i < seg; i++) {
            double t0 = a0 + (a1 - a0) * i / seg, t1 = a0 + (a1 - a0) * (i + 1) / seg;
            auto p = [&](double t) -> V3 { double u = std::cos(t) * rad, v = std::sin(t) * rad;
                return axis == 1 ? V3{ u, y0, v } : axis == 0 ? V3{ y0, u, v } : V3{ u, v, y0 }; };
            L.push_back({ p(t0), p(t1) });
        }
    };
    auto box = [&](V3 lo, V3 hi) {
        V3 c[8]; for (int i = 0; i < 8; i++) c[i] = { (i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z };
        const int e[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        for (auto& k : e) L.push_back({ c[k[0]], c[k[1]] });
    };
    switch (b.shape) {
    case BrushShape::Sphere: circle(1, 0, r, 40); circle(0, 0, r, 40); circle(2, 0, r, 40); break;
    case BrushShape::Cube: box({ -r, -h, -r }, { r, h, r }); break;
    case BrushShape::Cylinder:
        circle(1, -h, r, 40); circle(1, h, r, 40);
        for (int i = 0; i < 4; i++) { double a = i * 1.5707963; L.push_back({ { std::cos(a) * r, -h, std::sin(a) * r }, { std::cos(a) * r, h, std::sin(a) * r } }); }
        break;
    case BrushShape::Dome: circle(1, -0.5, r, 40); circle(0, 0, r, 20, -1.5707963, 1.5707963); circle(2, 0, r, 20, 0, 3.1415926); break;
    case BrushShape::Diamond: {
        V3 p[6] = { { r, 0, 0 }, { -r, 0, 0 }, { 0, r, 0 }, { 0, -r, 0 }, { 0, 0, r }, { 0, 0, -r } };
        const int e[12][2] = { { 0, 2 }, { 0, 3 }, { 0, 4 }, { 0, 5 }, { 1, 2 }, { 1, 3 }, { 1, 4 }, { 1, 5 }, { 2, 4 }, { 4, 3 }, { 3, 5 }, { 5, 2 } };
        for (auto& k : e) L.push_back({ p[k[0]], p[k[1]] });
        break; }
    case BrushShape::Pyramid: {
        V3 a{ 0, r, 0 }, c[4] = { { -r, -0.5, -r }, { r, -0.5, -r }, { r, -0.5, r }, { -r, -0.5, r } };
        for (int i = 0; i < 4; i++) { L.push_back({ c[i], c[(i + 1) % 4] }); L.push_back({ c[i], a }); }
        break; }
    default: break;
    }
    return L;
}

void BrushStroke::apply(World& w, const std::vector<Edit>& dab) {
    for (const Edit& e : dab) {
        I3 p{ e.x, e.y, e.z };
        auto it = index.find(p);
        if (it == index.end()) { index.emplace(p, edits.size()); edits.push_back(e); }
        else edits[it->second].after = e.after;   // keeps the very first "before"
        w.set(e.x, e.y, e.z, e.after);
    }
}
std::vector<Edit> BrushStroke::finish() {
    std::vector<Edit> out; for (auto& e : edits) if (e.before != e.after) out.push_back(e);
    edits.clear(); index.clear(); return out;
}

} // namespace mc
