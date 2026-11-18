#pragma once
// Culling helpers: is a box (any 8 corners, e.g. a turned model part) inside a view? Plain C++, tested.
// The matrix is row-major and points are row vectors (p * M), like DirectXMath's XMVector4Transform.
#include "../core/math.h"

namespace mc {

// false when all 8 corners are outside the same side of the view (then nothing of the box can show)
inline bool cornersVisible(const float m[16], const V3* c) {
    int out[6] = {};
    for (int i = 0; i < 8; i++) {
        float x = (float)c[i].x, y = (float)c[i].y, z = (float)c[i].z;
        float cx = x * m[0] + y * m[4] + z * m[8] + m[12], cy = x * m[1] + y * m[5] + z * m[9] + m[13];
        float cz = x * m[2] + y * m[6] + z * m[10] + m[14], cw = x * m[3] + y * m[7] + z * m[11] + m[15];
        out[0] += cx < -cw; out[1] += cx > cw; out[2] += cy < -cw; out[3] += cy > cw; out[4] += cz < 0; out[5] += cz > cw;
    }
    for (int k = 0; k < 6; k++) if (out[k] == 8) return false;
    return true;
}
// the 8 corners of a box at org with edges ex, ey, ez (they may be turned and stretched)
inline void boxCorners(V3 org, V3 ex, V3 ey, V3 ez, V3* c) {
    for (int i = 0; i < 8; i++) c[i] = org + ex * (double)(i & 1) + ey * (double)((i >> 1) & 1) + ez * (double)((i >> 2) & 1);
}

} // namespace mc
