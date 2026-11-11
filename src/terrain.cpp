#include "terrain.h"

namespace px {

const char* const kTerrainNames[TK_COUNT] = { "Hills", "Cavern", "Canyon", "Crater", "Ice field", "Lava cave", "Chozo ruins" };

void terrainPreset(int kind, TerrainParams& p) {
    TerrainParams d; d.seed = p.seed; d.wholeRoom = p.wholeRoom; d.x0 = p.x0; d.z0 = p.z0; d.x1 = p.x1; d.z1 = p.z1; d.kind = kind;
    switch (kind) {
    case TK_HILLS: d.height = 10; break;
    case TK_CAVERN:
        d.base = 3; d.height = 4; d.scale = 14; d.rough = 0.55f; d.overhang = 0.3f; d.caves = 6; d.caveSize = 2.6f; d.tunnels = 3;
        d.ceiling = true; d.ceilHeight = 15; d.top = MAT_ROCK; d.mid = MAT_ROCK; d.deep = MAT_ROCK; break;
    case TK_CANYON:
        d.base = 15; d.height = 3; d.scale = 28; d.rough = 0.45f; d.cliffs = 0.5f; d.caves = 1; d.tunnels = 1;
        d.top = MAT_SAND; d.mid = MAT_ROCK; d.midDepth = 4; d.deep = MAT_ROCK; d.featDepth = 11; d.featSize = 7; break;
    case TK_CRATER:
        d.base = 6; d.height = 2; d.scale = 24; d.caves = 1; d.tunnels = 1; d.top = MAT_ROCK; d.mid = MAT_ROCK; d.deep = MAT_BASALT;
        d.featDepth = 7; d.featSize = 17; break;
    case TK_ICE:
        d.base = 3; d.height = 5; d.scale = 26; d.rough = 0.4f; d.caves = 1; d.tunnels = 1; d.top = MAT_SNOW; d.mid = MAT_ICE; d.midDepth = 2;
        d.deep = MAT_ROCK; d.spikes = 10; break;
    case TK_LAVA:
        d.base = 2; d.height = 5; d.scale = 16; d.rough = 0.6f; d.overhang = 0.25f; d.ceiling = true; d.ceilHeight = 14; d.caves = 4;
        d.caveSize = 2.5f; d.tunnels = 2; d.fluidLevel = 3; d.fluidMat = MAT_LAVA; d.top = MAT_BASALT; d.mid = MAT_BASALT; d.deep = MAT_BASALT; break;
    case TK_RUINS:
        d.base = 3; d.height = 5; d.scale = 20; d.rough = 0.45f; d.caves = 1; d.tunnels = 1; d.top = MAT_SAND; d.mid = MAT_SAND; d.midDepth = 2;
        d.deep = MAT_ROCK; d.ruins = 8; break;
    default: break;
    }
    p = d;
}

// ------------------------------------------------------------------ noise (seeded, not tiled)
static uint32_t hashi(int x, int y, int z, uint32_t s) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16);
}
static float h01(int x, int y, int z, uint32_t s) { return (hashi(x, y, z, s) & 0xFFFFFF) / 16777216.f; }
static float sm(float t) { return t * t * (3 - 2 * t); }
static float vn2(float x, float z, uint32_t s) {
    int xi = (int)std::floor(x), zi = (int)std::floor(z); float fx = sm(x - xi), fz = sm(z - zi);
    float a = h01(xi, 0, zi, s), b = h01(xi + 1, 0, zi, s), c = h01(xi, 0, zi + 1, s), d = h01(xi + 1, 0, zi + 1, s);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fz;
}
static float vn3(float x, float y, float z, uint32_t s) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z); float fx = sm(x - xi), fy = sm(y - yi), fz = sm(z - zi);
    auto L = [](float a, float b, float t) { return a + (b - a) * t; };
    float c00 = L(h01(xi, yi, zi, s), h01(xi + 1, yi, zi, s), fx), c10 = L(h01(xi, yi + 1, zi, s), h01(xi + 1, yi + 1, zi, s), fx);
    float c01 = L(h01(xi, yi, zi + 1, s), h01(xi + 1, yi, zi + 1, s), fx), c11 = L(h01(xi, yi + 1, zi + 1, s), h01(xi + 1, yi + 1, zi + 1, s), fx);
    return L(L(c00, c10, fy), L(c01, c11, fy), fz);
}
static float fbm2(float x, float z, float rough, uint32_t s) {   // 0..1
    float sum = 0, amp = 1, norm = 0, f = 1;
    for (int o = 0; o < 5; o++) { sum += vn2(x * f, z * f, s + o * 101) * amp; norm += amp; amp *= rough; f *= 2.03f; }
    return sum / norm;
}
struct Rng { uint32_t s; float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 16777216.f; } float range(float a, float b) { return a + next() * (b - a); } };

// ------------------------------------------------------------------ generator
void generateTerrain(Room& r, const TerrainParams& p) {
    int ax0 = p.wholeRoom ? r.ox : std::max(r.ox, std::min(p.x0, p.x1)), ax1 = p.wholeRoom ? r.ox + r.sx - 1 : std::min(r.ox + r.sx - 1, std::max(p.x0, p.x1));
    int az0 = p.wholeRoom ? r.oz : std::max(r.oz, std::min(p.z0, p.z1)), az1 = p.wholeRoom ? r.oz + r.sz - 1 : std::min(r.oz + r.sz - 1, std::max(p.z0, p.z1));
    if (ax0 > ax1 || az0 > az1) return;
    int W = ax1 - ax0 + 1, D = az1 - az0 + 1, Hn = r.sy;
    auto inArea = [&](int x, int z) { return x >= ax0 && x <= ax1 && z >= az0 && z <= az1; };
    float scale = std::max(2.f, p.scale);
    uint32_t seed = p.seed * 7919u + 17u;

    // ---- 1) height per column (and ceiling for caverns)
    std::vector<float> H((size_t)W * D), C((size_t)W * D, 1e9f);
    float cxm = (ax0 + ax1) * 0.5f, czm = (az0 + az1) * 0.5f;
    for (int z = az0; z <= az1; z++) for (int x = ax0; x <= ax1; x++) {
        float n = fbm2(x / scale, z / scale, p.rough, seed);
        n = std::max(0.f, std::min(1.f, (n - 0.5f) * 2.2f + 0.5f));   // fbm clusters around 0.5: stretch to use the full range
        float h = p.base + p.height * n;
        if (p.kind == TK_CANYON) {   // a winding valley along Z
            float cx = cxm + (vn2(0.5f, z / 18.f, seed + 9) - 0.5f) * W * 0.35f;
            float dist = std::fabs(x - cx) + (vn2(x / 6.f, z / 6.f, seed + 10) - 0.5f) * 2.f;
            float k = 1.f - std::max(0.f, std::min(1.f, (dist - p.featSize * 0.5f) / (p.featSize * 0.5f)));
            h -= p.featDepth * sm(k);
        } else if (p.kind == TK_CRATER) {   // a bowl with a raised rim
            float rr = std::sqrt((x - cxm) * (x - cxm) + (z - czm) * (z - czm)) + (vn2(x / 5.f, z / 5.f, seed + 11) - 0.5f) * 3.f;
            float R = p.featSize, inside = 1.f - std::max(0.f, std::min(1.f, rr / R));
            h += -p.featDepth * sm(inside) + 4.f * std::exp(-((rr - R) * (rr - R)) / 12.f);
        }
        if (p.cliffs > 0) { float st = 3.f, t = std::floor(h / st) * st, fr = (h - t) / st; h = h + (t + st * sm(std::max(0.f, std::min(1.f, (fr - 0.55f) / 0.45f))) - h) * p.cliffs; }
        h = std::max(1.f, std::min((float)Hn - 2, h));
        H[(size_t)(z - az0) * W + (x - ax0)] = h;
        if (p.ceiling) C[(size_t)(z - az0) * W + (x - ax0)] = std::max(h + 3.f, p.ceilHeight + (fbm2(x / scale, z / scale, p.rough, seed + 5) - 0.5f) * 6.f);
    }
    auto Hat = [&](int x, int z) { return H[(size_t)(z - az0) * W + (x - ax0)]; };

    // ---- 2) fill: clear the area, then solid below the surface (overhangs bend it with 3D noise)
    Block air; Block rock; rock.type = B_CUBE; rock.mat = p.deep;
    for (int z = az0; z <= az1; z++) for (int x = ax0; x <= ax1; x++) {
        float h = Hat(x, z), c = C[(size_t)(z - az0) * W + (x - ax0)];
        for (int ly = 0; ly < Hn; ly++) {
            int y = r.oy + ly; float bend = p.overhang > 0 ? (vn3(x / 5.f, ly / 4.f, z / 5.f, seed + 3) * 2 - 1) * 4.f * p.overhang : 0.f;
            bool s = ly == 0 || ly + 0.5f < h + bend || (p.ceiling && ly + 0.5f > c - bend);
            r.set(x, y, z, s ? rock : air);
        }
    }
    // ---- 3) caves: wandering tunnels that carve spheres
    Rng rng{ seed * 2654435761u + 1 };
    auto carve = [&](float cx, float cy, float cz, float rad) {
        int R = (int)std::ceil(rad);
        for (int dz = -R; dz <= R; dz++) for (int dy = -R; dy <= R; dy++) for (int dx = -R; dx <= R; dx++) {
            int x = (int)std::floor(cx) + dx, y = (int)std::floor(cy) + dy, z = (int)std::floor(cz) + dz;
            if (!inArea(x, z) || y <= r.oy) continue;
            float ddx = x + 0.5f - cx, ddy = y + 0.5f - cy, ddz = z + 0.5f - cz;
            if (ddx * ddx + ddy * ddy + ddz * ddz <= rad * rad) r.set(x, y, z, air);
        }
    };
    for (int i = 0; i < p.caves; i++) {
        float x = rng.range((float)ax0, (float)ax1), z = rng.range((float)az0, (float)az1);
        float y = r.oy + rng.range(2.f, std::max(2.5f, Hat((int)x, (int)z) - 2.f));
        float yaw = rng.range(0, 6.283f), pitch = 0; int len = (int)rng.range(30, 90);
        for (int s = 0; s < len; s++) {
            carve(x, y, z, p.caveSize * (0.8f + 0.4f * vn2(s * 0.2f, (float)i, seed + 12)));
            yaw += rng.range(-0.35f, 0.35f); pitch = std::max(-0.35f, std::min(0.35f, pitch + rng.range(-0.15f, 0.15f)));
            x += std::cos(yaw) * std::cos(pitch); z += std::sin(yaw) * std::cos(pitch); y += std::sin(pitch) * 0.6f;
            y = std::max((float)r.oy + 1.5f, y);
            if (!inArea((int)std::floor(x), (int)std::floor(z))) break;
        }
    }
    // morph ball tunnels: exactly 1 block wide and tall, with right-angle turns
    for (int i = 0; i < p.tunnels; i++) {
        int x = (int)rng.range((float)ax0 + 2, (float)ax1 - 2), z = (int)rng.range((float)az0 + 2, (float)az1 - 2);
        int y = r.oy + std::max(1, (int)Hat(x, z) - 3);
        int dir = (int)rng.range(0, 4) & 3, len = (int)rng.range(18, 45), turnIn = (int)rng.range(5, 12);
        for (int s = 0; s < len && inArea(x, z); s++) {
            if (y > r.oy) r.set(x, y, z, air);
            if (--turnIn <= 0) { dir = (dir + (rng.next() < 0.5f ? 1 : 3)) & 3; turnIn = (int)rng.range(5, 12); }
            x += dir == 0 ? 1 : dir == 2 ? -1 : 0; z += dir == 1 ? 1 : dir == 3 ? -1 : 0;
        }
    }
    // ---- 4) fluid fills low air
    if (p.fluidLevel >= 0) {
        Block fl; fl.type = B_CUBE; fl.mat = p.fluidMat;
        for (int z = az0; z <= az1; z++) for (int x = ax0; x <= ax1; x++)
            for (int ly = 1; ly <= (int)p.fluidLevel && ly < Hn; ly++) if (r.get(x, r.oy + ly, z) == B_AIR) r.set(x, r.oy + ly, z, fl);
    }
    // ---- 5) layers: top / middle / deep by depth below the nearest air above (fluid counts as air)
    for (int z = az0; z <= az1; z++) for (int x = ax0; x <= ax1; x++) {
        int run = 0;
        for (int ly = Hn - 1; ly >= 0; ly--) {
            int y = r.oy + ly; Block b = r.block(x, y, z);
            if (b.type == B_AIR || (p.fluidLevel >= 0 && b.mat == p.fluidMat && ly <= (int)p.fluidLevel)) { run = 0; continue; }
            b.mat = run == 0 ? p.top : run <= p.midDepth ? p.mid : p.deep;
            r.set(x, y, z, b); run++;
        }
    }
    // ---- 6) smooth: slopes and corners where the ground steps up by one block
    if (p.smooth) {
        const int DX[4] = { 1, 0, -1, 0 }, DZ[4] = { 0, 1, 0, -1 };
        std::vector<std::pair<std::pair<int, int>, std::pair<int, Block>>> place;
        for (int z = az0; z <= az1; z++) for (int x = ax0; x <= ax1; x++) for (int ly = 1; ly < Hn - 1; ly++) {
            int y = r.oy + ly;
            if (r.get(x, y, z) != B_AIR) continue;
            Block below = r.block(x, y - 1, z);
            if (below.type != B_CUBE || (below.mat == p.fluidMat && p.fluidLevel >= 0)) continue;
            if (r.get(x, y + 1, z) != B_AIR) continue;   // keep low ceilings walkable
            bool hi[4]; int n = 0;
            for (int d = 0; d < 4; d++) { hi[d] = r.get(x + DX[d], y, z + DZ[d]) == B_CUBE && r.get(x + DX[d], y + 1, z + DZ[d]) == B_AIR; n += hi[d]; }
            Block b; b.mat = below.mat; b.size = 8;
            auto fwd = [&](int d) { return V3{ (float)DX[d], 0, (float)DZ[d] }; };
            if (n == 1) { int d = hi[0] ? 0 : hi[1] ? 1 : hi[2] ? 2 : 3; if (r.get(x - DX[d], y, z - DZ[d]) != B_AIR) continue;
                b.type = B_SLOPE; b.rot = chooseRot(B_SLOPE, 0, 1, 0, fwd(d), 0, false); }
            else if (n == 2) {
                int a = -1, c = -1; for (int d = 0; d < 4; d++) if (hi[d]) { if (a < 0) a = d; else c = d; }
                if ((c - a) == 2) continue;   // opposite sides: a groove, leave it
                // inner corner: high on its front (+Z) and right (+X) sides; right = cross(up, front)
                int front = ((a + 3) & 3) == c ? a : c;
                b.type = B_INNER; b.rot = chooseRot(B_INNER, 0, 1, 0, fwd(front), 0, false);
            } else if (n == 0) {
                for (int d = 0; d < 4 && b.type == B_AIR; d++) {
                    int e = (d + 1) & 3, gx = x + DX[d] + DX[e], gz = z + DZ[d] + DZ[e];
                    if (r.get(gx, y, gz) == B_CUBE && r.get(gx, y + 1, gz) == B_AIR) { b.type = B_CORNER; b.rot = chooseRot(B_CORNER, 0, 1, 0, fwd(e), 0, false); }
                }
                if (b.type == B_AIR) continue;
            } else continue;
            place.push_back({ { x, z }, { y, b } });
        }
        for (auto& q : place) r.set(q.first.first, q.second.first, q.first.second, q.second.second);
    }
    // ---- 7) ice spikes
    for (int i = 0; i < p.spikes; i++) {
        int x = (int)rng.range((float)ax0 + 1, (float)ax1), z = (int)rng.range((float)az0 + 1, (float)az1);
        int y = r.oy + (int)std::ceil(Hat(x, z)); int hgt = (int)rng.range(3, 9);
        while (y > r.oy + 1 && r.get(x, y - 1, z) == B_AIR) y--;
        Block ice; ice.type = B_CUBE; ice.mat = MAT_ICE;
        for (int k = 0; k < hgt; k++) {
            if (k == hgt - 1) { ice.type = B_PILLAR; ice.size = 5; ice.rot = makeRot(2, 0); }
            r.set(x, y + k, z, ice);
            if (k < hgt / 2 && hgt > 5) for (int d = 0; d < 4; d++) if (rng.next() < 0.35f) { Block s2 = ice; s2.type = B_CUBE; r.set(x + (d == 0) - (d == 2), y + k, z + (d == 1) - (d == 3), s2); }
        }
    }
    // ---- 8) Chozo ruins: broken walls and pillars standing on the ground
    auto groundY = [&](int x, int z) { int y = r.oy + Hn - 2; while (y > r.oy && r.get(x, y - 1, z) == B_AIR) y--; return y; };
    Block chozo; chozo.type = B_CUBE; chozo.mat = MAT_CHOZO;
    for (int i = 0; i < p.ruins; i++) {
        int x = (int)rng.range((float)ax0 + 2, (float)ax1 - 2), z = (int)rng.range((float)az0 + 2, (float)az1 - 2);
        bool alongX = rng.next() < 0.5f; int len = (int)rng.range(4, 11), hgt = (int)rng.range(2, 6);
        for (int k = 0; k < len; k++) {
            int wx = alongX ? x + k : x, wz = alongX ? z : z + k; if (!inArea(wx, wz)) break;
            int gy = groundY(wx, wz);
            int colH = (int)std::round(hgt * (0.35f + 0.75f * vn2(k * 0.5f, (float)i * 3.1f, seed + 20)));
            for (int ly = 0; ly < colH; ly++) {
                bool top = ly == colH - 1;
                if (top && rng.next() < 0.4f) { Block s2 = chozo; s2.type = B_SLAB; s2.size = 4; s2.rot = makeRot(2, 0); r.set(wx, gy + ly, wz, s2); }
                else r.set(wx, gy + ly, wz, chozo);
            }
        }
        if (i % 2 == 0) {   // a free-standing pillar nearby
            int px_ = x + (alongX ? (int)rng.range(0, (float)len) : 3), pz_ = z + (alongX ? 3 : (int)rng.range(0, (float)len));
            if (inArea(px_, pz_)) { int gy = groundY(px_, pz_), ph = (int)rng.range(2, 5); Block pl = chozo; pl.type = B_PILLAR; pl.size = 7; pl.rot = makeRot(2, 0);
                for (int k = 0; k < ph; k++) r.set(px_, gy + k, pz_, pl); }
        }
    }
}

} // namespace px
