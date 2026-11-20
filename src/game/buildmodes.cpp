#include "buildmodes.h"
#include <cstdlib>
#include <functional>

namespace mc {

const char* buildModeName(BuildMode m) {
    switch (m) {
    case BuildMode::Normal: return "Normal";
    case BuildMode::Line: return "Line";
    case BuildMode::Wall: return "Wall";
    case BuildMode::Floor: return "Floor";
    case BuildMode::Cube: return "Cube";
    case BuildMode::DiagLine: return "Diagonal line";
    case BuildMode::DiagWall: return "Diagonal wall";
    case BuildMode::Slope: return "Slope";
    case BuildMode::Circle: return "Circle";
    case BuildMode::Cylinder: return "Cylinder";
    case BuildMode::Sphere: return "Sphere";
    case BuildMode::Pyramid: return "Pyramid";
    case BuildMode::Cone: return "Cone";
    case BuildMode::Dome: return "Dome";
    default: return "?";
    }
}

int buildSteps(BuildMode m) {
    switch (m) {
    case BuildMode::Normal: return 1;
    case BuildMode::Line: case BuildMode::Floor: case BuildMode::Circle: return 2;
    default: return 3;
    }
}

std::vector<int> buildFills(BuildMode m) {
    switch (m) {
    case BuildMode::Normal: case BuildMode::Line: case BuildMode::DiagLine: return {};
    case BuildMode::Cube: return { FILL_SOLID, FILL_HOLLOW, FILL_WALLS, FILL_EDGES };
    case BuildMode::Cylinder: case BuildMode::Pyramid: case BuildMode::Cone: case BuildMode::Dome: return { FILL_SOLID, FILL_HOLLOW, FILL_WALLS };
    default: return { FILL_SOLID, FILL_HOLLOW };
    }
}

const char* buildFillName(BuildMode m, int fill) {
    bool prism = m == BuildMode::Cube || m == BuildMode::Cylinder;
    switch (fill) {
    case FILL_SOLID: return "Filled";
    case FILL_HOLLOW: return m == BuildMode::Slope ? "Surface" : (m == BuildMode::Wall || m == BuildMode::Floor || m == BuildMode::DiagWall || m == BuildMode::Circle) ? "Outline" : "Hollow";
    case FILL_WALLS: return prism ? "Walls only" : "No floor";
    case FILL_EDGES: return "Edges only";
    default: return "?";
    }
}

bool buildHasCentre(BuildMode m) {
    switch (m) {
    case BuildMode::Circle: case BuildMode::Cylinder: case BuildMode::Sphere: case BuildMode::Pyramid: case BuildMode::Cone: case BuildMode::Dome: return true;
    default: return false;
    }
}

I3 buildSnap(BuildMode m, I3 a, I3 b) {
    if (m == BuildMode::Normal) return a;
    if (m == BuildMode::Line) {   // straight along one axis: the biggest difference
        int dx = std::abs(b.x - a.x), dy = std::abs(b.y - a.y), dz = std::abs(b.z - a.z);
        if (dx >= dy && dx >= dz) return { b.x, a.y, a.z };
        if (dy >= dz) return { a.x, b.y, a.z };
        return { a.x, a.y, b.z };
    }
    b.y = a.y;   // the rest: the second point is on the start's level
    if (m == BuildMode::Wall) { if (std::abs(b.x - a.x) >= std::abs(b.z - a.z)) b.z = a.z; else b.x = a.x; }
    return b;
}

static std::vector<I3> lineCells(I3 a, I3 b) {
    std::vector<I3> out;
    int n = std::max({ std::abs(b.x - a.x), std::abs(b.y - a.y), std::abs(b.z - a.z) });
    if (n == 0) return { a };
    for (int i = 0; i <= n; i++) {
        double t = (double)i / n;
        out.push_back({ a.x + (int)std::lround((b.x - a.x) * t), a.y + (int)std::lround((b.y - a.y) * t), a.z + (int)std::lround((b.z - a.z) * t) });
    }
    return out;
}

std::vector<I3> buildShape(BuildMode m, int fill, bool fromCentre, I3 a, I3 b, int h, size_t limit, bool* capped) {
    if (capped) *capped = false;
    const int R = 255;
    b.x = std::clamp(b.x, a.x - R, a.x + R); b.y = std::clamp(b.y, a.y - R, a.y + R); b.z = std::clamp(b.z, a.z - R, a.z + R); h = std::clamp(h, a.y - R, a.y + R);
    b = buildSnap(m, a, b);
    if (buildSteps(m) < 3) h = a.y;
    auto cap = [&](std::vector<I3> v) { if (v.size() > limit) { v.resize(limit); if (capped) *capped = true; } return v; };
    if (m == BuildMode::Normal) return { a };
    if (m == BuildMode::Line) return cap(lineCells(a, b));
    if (m == BuildMode::DiagLine) return cap(lineCells(a, { b.x, h, b.z }));

    // the rest: a box of cells, which of them are in the shape, then filled / hollow / ...
    I3 lo{ std::min(a.x, b.x), std::min(a.y, h), std::min(a.z, b.z) }, hi{ std::max(a.x, b.x), std::max(a.y, h), std::max(a.z, b.z) };
    if (fromCentre && buildHasCentre(m)) {
        int r = std::min(127, (int)std::lround(std::hypot((double)(b.x - a.x), (double)(b.z - a.z)))), rx = r, rz = r;   // round: the distance dragged is the radius
        lo.x = a.x - rx; hi.x = a.x + rx; lo.z = a.z - rz; hi.z = a.z + rz;
        if (m == BuildMode::Sphere) { int ry = std::min(127, std::abs(h - a.y)); lo.y = a.y - ry; hi.y = a.y + ry; }
    }
    hi.x = std::min(hi.x, lo.x + R); hi.y = std::min(hi.y, lo.y + R); hi.z = std::min(hi.z, lo.z + R);
    int sx = hi.x - lo.x + 1, sy = hi.y - lo.y + 1, sz = hi.z - lo.z + 1;
    double cx = (lo.x + hi.x + 1) * 0.5, cz = (lo.z + hi.z + 1) * 0.5, cy = (lo.y + hi.y + 1) * 0.5, rx = sx * 0.5, rz = sz * 0.5, ry = sy * 0.5;
    bool upward = h >= a.y; int n = std::abs(h - a.y) + 1;   // tapering shapes: from their base (the start's level) toward the height
    std::vector<I3> diag; if (m == BuildMode::DiagWall) diag = lineCells({ a.x, 0, a.z }, { b.x, 0, b.z });
    int rampAxis = std::abs(b.x - a.x) >= std::abs(b.z - a.z) ? 0 : 2;
    auto rampTop = [&](int x, int z) {
        int c = rampAxis == 0 ? x : z, ca = rampAxis == 0 ? a.x : a.z, cb = rampAxis == 0 ? b.x : b.z;
        double u = cb == ca ? 1.0 : (double)(c - ca) / (cb - ca);
        return a.y + (int)std::lround(u * (h - a.y));
    };
    const double E = 1e-9;
    std::vector<uint8_t> diagMap; if (m == BuildMode::DiagWall) { diagMap.assign((size_t)sx * sz, 0); for (const I3& d : diag) if (d.x >= lo.x && d.x <= hi.x && d.z >= lo.z && d.z <= hi.z) diagMap[(size_t)(d.z - lo.z) * sx + (d.x - lo.x)] = 1; }
    auto inside = [&](int x, int y, int z) -> bool {
        double dx = x + 0.5 - cx, dz = z + 0.5 - cz, dy = y + 0.5 - cy; int k = std::abs(y - a.y);
        switch (m) {
        case BuildMode::Wall: case BuildMode::Floor: case BuildMode::Cube: return true;
        case BuildMode::DiagWall: return diagMap[(size_t)(z - lo.z) * sx + (x - lo.x)] != 0;
        case BuildMode::Slope: { int t = rampTop(x, z); return y >= std::min(a.y, t) && y <= std::max(a.y, t); }
        case BuildMode::Circle: case BuildMode::Cylinder: return (dx * dx) / (rx * rx) + (dz * dz) / (rz * rz) <= 1 + E;
        case BuildMode::Sphere: return (dx * dx) / (rx * rx) + (dz * dz) / (rz * rz) + (dy * dy) / (ry * ry) <= 1 + E;
        case BuildMode::Pyramid: { double s = 1 - (double)k / n; return std::fabs(dx) <= std::max(0.5, rx * s) + E && std::fabs(dz) <= std::max(0.5, rz * s) + E; }
        case BuildMode::Cone: { double s = 1 - (double)k / n, ax = std::max(0.5, rx * s), az = std::max(0.5, rz * s); return (dx * dx) / (ax * ax) + (dz * dz) / (az * az) <= 1 + E; }
        case BuildMode::Dome: { double t = (k + 0.5) / n; return (dx * dx) / (rx * rx) + (dz * dz) / (rz * rz) + t * t <= 1 + E; }
        default: return false;
        }
    };
    std::vector<uint8_t> in((size_t)sx * sy * sz);
    auto at = [&](int x, int y, int z) -> size_t { return ((size_t)(y - lo.y) * sz + (z - lo.z)) * sx + (x - lo.x); };
    for (int y = lo.y; y <= hi.y; y++) for (int z = lo.z; z <= hi.z; z++) for (int x = lo.x; x <= hi.x; x++) in[at(x, y, z)] = inside(x, y, z) ? 1 : 0;
    auto isIn = [&](int x, int y, int z) { return x >= lo.x && x <= hi.x && y >= lo.y && y <= hi.y && z >= lo.z && z <= hi.z && in[at(x, y, z)]; };

    std::vector<int> fills = buildFills(m); bool ok = false; for (int f : fills) ok |= f == fill; if (!ok) fill = FILL_SOLID;
    bool act[3] = { sx > 1, sy > 1, sz > 1 }; int nAct = act[0] + act[1] + act[2];
    bool prism = m == BuildMode::Cube || m == BuildMode::Cylinder;
    auto keep = [&](int x, int y, int z) -> bool {
        if (fill == FILL_SOLID) return true;
        if (m == BuildMode::DiagWall) return y == lo.y || y == hi.y || (x == diag.front().x && z == diag.front().z) || (x == diag.back().x && z == diag.back().z);
        if (m == BuildMode::Slope) {   // the ramp's surface: open above (or below, going down) or toward the low end
            int up = upward ? 1 : -1, dn = (rampAxis == 0 ? (a.x < b.x ? -1 : 1) : (a.z < b.z ? -1 : 1));
            if (!isIn(x, y + up, z)) return true;
            return rampAxis == 0 ? !isIn(x + dn, y, z) : !isIn(x, y, z + dn);
        }
        if (fill == FILL_EDGES) {
            int on = (act[0] && (x == lo.x || x == hi.x)) + (act[1] && (y == lo.y || y == hi.y)) + (act[2] && (z == lo.z || z == hi.z));
            return on >= std::min(2, std::max(1, nAct - 1));
        }
        static const int D[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
        for (auto& d : D) {
            int axis = d[0] ? 0 : d[1] ? 1 : 2; if (!act[axis]) continue;
            int nx = x + d[0], ny = y + d[1], nz = z + d[2];
            if (fill == FILL_WALLS && axis == 1 && (ny < lo.y || ny > hi.y)) {   // open: the base (and the top of prisms)
                if (prism || (upward ? ny < lo.y : ny > hi.y)) continue;
            }
            if (!isIn(nx, ny, nz)) return true;
        }
        return false;
    };
    std::vector<I3> out;
    for (int y = lo.y; y <= hi.y; y++) for (int z = lo.z; z <= hi.z; z++) for (int x = lo.x; x <= hi.x; x++) {
        if (!in[at(x, y, z)] || !keep(x, y, z)) continue;
        if (out.size() >= limit) { if (capped) *capped = true; return out; }
        out.push_back({ x, y, z });
    }
    return out;
}

I3 buildBoxSize(const std::vector<I3>& c) {
    if (c.empty()) return { 0, 0, 0 };
    I3 lo = c[0], hi = c[0];
    for (const I3& p : c) { lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z); hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z); }
    return { hi.x - lo.x + 1, hi.y - lo.y + 1, hi.z - lo.z + 1 };
}

}
