// math.h : small vector types. World positions use double so the world can be huge without jitter.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mc {

struct V3 {
    double x = 0, y = 0, z = 0;
    V3() = default;
    V3(double X, double Y, double Z) : x(X), y(Y), z(Z) {}
    V3 operator+(V3 o) const { return { x + o.x, y + o.y, z + o.z }; }
    V3 operator-(V3 o) const { return { x - o.x, y - o.y, z - o.z }; }
    V3 operator*(double s) const { return { x * s, y * s, z * s }; }
    V3& operator+=(V3 o) { x += o.x; y += o.y; z += o.z; return *this; }
};
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline double length(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 normalize(V3 a) { double l = length(a); return l > 1e-12 ? a * (1.0 / l) : V3{ 0, 0, 0 }; }

struct I3 {
    int x = 0, y = 0, z = 0;
    bool operator==(const I3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const I3& o) const { return !(*this == o); }
    I3 operator+(I3 o) const { return { x + o.x, y + o.y, z + o.z }; }
};
struct I3Hash {
    size_t operator()(const I3& p) const {
        uint64_t h = (uint64_t)(uint32_t)p.x * 0x9E3779B185EBCA87ull ^ (uint64_t)(uint32_t)p.y * 0xC2B2AE3D27D4EB4Full ^ (uint64_t)(uint32_t)p.z * 0x165667B19E3779F9ull;
        return (size_t)(h ^ (h >> 31));
    }
};

// floor division / modulo that work for negative numbers (block -1 is in chunk -1, slot 15)
inline int floorDiv(int a, int b) { int q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }
inline int floorMod(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }
inline int ifloor(double v) { return (int)std::floor(v); }

} // namespace mc
