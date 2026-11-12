#include "noise.h"
#include <algorithm>
#include <cmath>

namespace mc {

static uint32_t hash3(int x, int y, int z, uint32_t seed) {
    uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ (uint32_t)z * 0xcb1ab31fu ^ seed * 0x9e3779b9u;
    h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15; return h;
}
static float rnd(int x, int y, int z, uint32_t s) { return (hash3(x, y, z, s) & 0xFFFFFF) / 16777215.f; }
static int wrap(int a, int p) { return ((a % p) + p) % p; }

// value noise with `cells` lattice cells across the tile (wraps)
static float valueNoise(float x, float y, float z, int cells, uint32_t seed) {
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y), z0 = (int)std::floor(z);
    float fx = x - x0, fy = y - y0, fz = z - z0;
    auto s = [](float t) { return t * t * (3 - 2 * t); };
    fx = s(fx); fy = s(fy); fz = s(fz);
    float c[2][2][2];
    for (int k = 0; k < 2; k++) for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) c[k][j][i] = rnd(wrap(x0 + i, cells), wrap(y0 + j, cells), wrap(z0 + k, cells), seed);
    auto l = [](float a, float b, float t) { return a + (b - a) * t; };
    return l(l(l(c[0][0][0], c[0][0][1], fx), l(c[0][1][0], c[0][1][1], fx), fy), l(l(c[1][0][0], c[1][0][1], fx), l(c[1][1][0], c[1][1][1], fx), fy), fz);
}
// distance to the nearest random point, one point per cell (wraps); 0 at a point
static float worley(float x, float y, float z, int cells, uint32_t seed) {
    int cx = (int)std::floor(x), cy = (int)std::floor(y), cz = (int)std::floor(z); float best = 9;
    for (int k = -1; k <= 1; k++) for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
        int gx = cx + i, gy = cy + j, gz = cz + k; int wx = wrap(gx, cells), wy = wrap(gy, cells), wz = wrap(gz, cells);
        float px = gx + rnd(wx, wy, wz, seed), py = gy + rnd(wx, wy, wz, seed + 1), pz = gz + rnd(wx, wy, wz, seed + 2);
        float d = (px - x) * (px - x) + (py - y) * (py - y) + (pz - z) * (pz - z); best = std::min(best, d);
    }
    return std::sqrt(best);
}

std::vector<uint8_t> makeCloudNoise(int n) {
    std::vector<uint8_t> out((size_t)n * n * n * 2);
    for (int z = 0; z < n; z++) for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
        float u = (float)x / n, v = (float)y / n, w = (float)z / n;
        float soft = 0, amp = 0.5f, norm = 0;
        for (int o = 0; o < 5; o++) { int cells = 4 << o; soft += amp * valueNoise(u * cells, v * cells, w * cells, cells, 11 + o); norm += amp; amp *= 0.5f; }
        soft /= norm;
        float cells = 0, a2 = 0.625f, n2 = 0;
        for (int o = 0; o < 3; o++) { int c = 4 << o; cells += a2 * (1 - std::min(1.f, worley(u * c, v * c, w * c, c, 101 + o * 7) * 1.1f)); n2 += a2; a2 *= 0.5f; }
        cells /= n2;
        // contrast: spread the values over the whole range
        soft = std::min(1.f, std::max(0.f, (soft - 0.25f) / 0.5f));
        size_t i = (((size_t)z * n + y) * n + x) * 2;
        out[i] = (uint8_t)std::lround(soft * 255); out[i + 1] = (uint8_t)std::lround(std::min(1.f, std::max(0.f, cells)) * 255);
    }
    return out;
}

} // namespace mc
