#include "shapes.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace mc {

// ================================================================== rotations
namespace {
const int DIR[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
int faceOfDir(const int v[3]) { for (int f = 0; f < 6; f++) if (DIR[f][0] == v[0] && DIR[f][1] == v[1] && DIR[f][2] == v[2]) return f; return -1; }

std::vector<Rot> makeRots() {
    // every signed permutation matrix with determinant +1; identity first. The order must never change (worlds store it).
    std::vector<Rot> out; const int perms[6][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } };
    for (auto& p : perms) for (int s = 0; s < 8; s++) {
        Rot r{}; int sg[3] = { (s & 1) ? -1 : 1, (s & 2) ? -1 : 1, (s & 4) ? -1 : 1 };
        for (int c = 0; c < 3; c++) r.m[p[c]][c] = sg[c];   // column c (where axis c goes) has its 1 in row p[c]
        int det = r.m[0][0] * (r.m[1][1] * r.m[2][2] - r.m[1][2] * r.m[2][1]) - r.m[0][1] * (r.m[1][0] * r.m[2][2] - r.m[1][2] * r.m[2][0]) + r.m[0][2] * (r.m[1][0] * r.m[2][1] - r.m[1][1] * r.m[2][0]);
        if (det == 1) out.push_back(r);
    }
    return out;
}
const std::vector<Rot>& rots() { static const std::vector<Rot> r = makeRots(); return r; }
void apply(const Rot& r, const int v[3], int o[3]) { for (int i = 0; i < 3; i++) o[i] = r.m[i][0] * v[0] + r.m[i][1] * v[1] + r.m[i][2] * v[2]; }
int indexOf(const Rot& q) {
    for (size_t i = 0; i < rots().size(); i++) { bool same = true; for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) same &= rots()[i].m[a][b] == q.m[a][b]; if (same) return (int)i; }
    return 0;
}
} // namespace

const Rot& Shapes::rot(int r) { return rots()[r >= 0 && r < 24 ? r : 0]; }
int Shapes::sideAfter(int r, int side) { int o[3]; apply(rot(r), DIR[side], o); return faceOfDir(o); }
int Shapes::rotFor(int fa, int ta, int fb, int tb) {
    for (int i = 0; i < 24; i++) if (sideAfter(i, fa) == ta && sideAfter(i, fb) == tb) return i;
    return -1;
}
int Shapes::turn(int r, int axis) {
    Rot q{}; // quarter turn around the axis (right-handed)
    int a = axis, b = (axis + 1) % 3, c = (axis + 2) % 3;
    q.m[a][a] = 1; q.m[c][b] = 1; q.m[b][c] = -1;
    const Rot& m = rot(r); Rot out{};
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) for (int k = 0; k < 3; k++) out.m[i][j] += q.m[i][k] * m.m[k][j];
    return indexOf(out);
}
int Shapes::placeRot(Place p, int n, int look, bool upperHalf) {
    bool vertical = n == F_PY || n == F_NY;
    int r = -1;
    switch (p) {
    case Place::Fixed: return 0;
    case Place::Yaw: r = rotFor(F_PZ, look, F_PY, F_PY); break;
    case Place::Facing: r = rotFor(F_PZ, look, F_PY, (n == F_NY || (!vertical && upperHalf)) ? F_NY : F_PY); break;
    case Place::Surface: r = vertical ? rotFor(F_PX, n, F_PY, look) : rotFor(F_PX, n, F_PY, F_PY); break;
    case Place::Axis: r = vertical ? rotFor(F_PY, n, F_PZ, look) : rotFor(F_PY, n, F_PZ, F_PY); break;
    }
    return r < 0 ? 0 : r;
}
int Shapes::legacyPlaneRot(int f) {
    if (f < 0 || f > 5) return 0;
    int r = (f / 2 == 1) ? rotFor(F_PX, f, F_PY, F_PZ) : rotFor(F_PX, f, F_PY, F_PY);
    return r < 0 ? 0 : r;
}

// ================================================================== geometry builder
namespace {
struct P3 { float x, y, z; };
P3 operator+(P3 a, P3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
P3 operator-(P3 a, P3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
P3 operator*(P3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
float dot(P3 a, P3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
P3 cross(P3 a, P3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
P3 nrm(P3 a) { float l = std::sqrt(dot(a, a)); return l > 1e-9f ? a * (1 / l) : P3{ 0, 1, 0 }; }
struct P2 { float u, v; };
const float PI = 3.14159265358979f;

// profile coordinates (u, v) and depth w along an axis -> block coordinates
P3 axisMap(int axis, float u, float v, float w) {
    if (axis == 0) return { w, v, u };   // along X: u = z, v = y
    if (axis == 1) return { u, w, v };   // along Y: u = x, v = z
    return { u, v, w };                  // along Z: u = x, v = y
}
float area2(const std::vector<P2>& p) { float a = 0; for (size_t i = 0; i < p.size(); i++) { const P2 &q = p[i], &r = p[(i + 1) % p.size()]; a += q.u * r.v - r.u * q.v; } return a; }
bool inPoly(const std::vector<P2>& p, float u, float v) {
    bool in = false;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
        if (((p[i].v > v) != (p[j].v > v)) && u < (p[j].u - p[i].u) * (v - p[i].v) / (p[j].v - p[i].v) + p[i].u) in = !in;
    return in;
}
// ear clipping for a simple counter-clockwise polygon
std::vector<int> triangulate(const std::vector<P2>& p) {
    std::vector<int> idx(p.size()), out; for (size_t i = 0; i < p.size(); i++) idx[i] = (int)i;
    auto cr = [&](int a, int b, int c) { return (p[b].u - p[a].u) * (p[c].v - p[a].v) - (p[b].v - p[a].v) * (p[c].u - p[a].u); };
    int guard = 0;
    while (idx.size() > 3 && guard++ < 10000) {
        bool found = false;
        for (size_t i = 0; i < idx.size(); i++) {
            int a = idx[(i + idx.size() - 1) % idx.size()], b = idx[i], c = idx[(i + 1) % idx.size()];
            float k = cr(a, b, c);
            if (k <= 1e-9f) { if (std::fabs(k) <= 1e-9f) { idx.erase(idx.begin() + i); found = true; break; } continue; }   // drop straight-line points
            bool inside = false;
            for (int o : idx) {
                if (o == a || o == b || o == c) continue;
                if (cr(a, b, o) >= -1e-9f && cr(b, c, o) >= -1e-9f && cr(c, a, o) >= -1e-9f) { inside = true; break; }
            }
            if (inside) continue;
            out.insert(out.end(), { a, b, c }); idx.erase(idx.begin() + i); found = true; break;
        }
        if (!found) break;
    }
    if (idx.size() == 3) out.insert(out.end(), { idx[0], idx[1], idx[2] });
    return out;
}
std::vector<P2> arc(float cu, float cv, float r, float a0deg, float a1deg, int n, bool ends) {
    std::vector<P2> o;
    for (int i = ends ? 0 : 1; i <= (ends ? n : n - 1); i++) { float a = (a0deg + (a1deg - a0deg) * i / n) * PI / 180; o.push_back({ cu + r * std::cos(a), cv + r * std::sin(a) }); }
    return o;
}

struct Builder {
    std::vector<STri> tris;
    std::vector<std::function<bool(P3)>> solids;
    bool twoSided = false;

    void tri(P3 a, P3 b, P3 c, P3 na, P3 nb, P3 nc, P3 fn) {
        P3 cr = cross(b - a, c - a); if (dot(cr, cr) < 1e-12f) return;   // degenerate
        if (dot(cr, fn) < 0) { std::swap(b, c); std::swap(nb, nc); }      // clockwise seen from outside (cross points out)
        STri t; P3 ps[3] = { a, b, c }, ns[3] = { na, nb, nc };
        for (int k = 0; k < 3; k++) { t.p[k][0] = ps[k].x; t.p[k][1] = ps[k].y; t.p[k][2] = ps[k].z; t.n[k][0] = ns[k].x; t.n[k][1] = ns[k].y; t.n[k][2] = ns[k].z; }
        t.fn[0] = fn.x; t.fn[1] = fn.y; t.fn[2] = fn.z; tris.push_back(t);
    }
    void flat(P3 a, P3 b, P3 c, P3 n) { tri(a, b, c, n, n, n, n); }
    void quad(P3 a, P3 b, P3 c, P3 d, P3 n) { flat(a, b, c, n); flat(a, c, d, n); }

    void box(float x0, float y0, float z0, float x1, float y1, float z1) {
        P3 v[8]; for (int i = 0; i < 8; i++) v[i] = { (i & 1) ? x1 : x0, (i & 2) ? y1 : y0, (i & 4) ? z1 : z0 };
        quad(v[1], v[3], v[7], v[5], { 1, 0, 0 }); quad(v[0], v[4], v[6], v[2], { -1, 0, 0 });
        quad(v[2], v[6], v[7], v[3], { 0, 1, 0 }); quad(v[0], v[1], v[5], v[4], { 0, -1, 0 });
        quad(v[4], v[5], v[7], v[6], { 0, 0, 1 }); quad(v[0], v[2], v[3], v[1], { 0, 0, -1 });
        solids.push_back([=](P3 p) { return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1 && p.z >= z0 && p.z <= z1; });
    }
    // a 2D outline pushed along an axis from w0 to w1 (any simple outline, also with holes cut in from the edge)
    void extrude(std::vector<P2> pr, int axis, float w0, float w1, bool smooth = true) {
        if (area2(pr) < 0) std::reverse(pr.begin(), pr.end());
        size_t n = pr.size();
        auto M = [&](P2 p, float w) { return axisMap(axis, p.u, p.v, w); };
        P3 n0 = axisMap(axis, 0, 0, -1), n1 = axisMap(axis, 0, 0, 1);
        std::vector<int> t = triangulate(pr);
        for (size_t k = 0; k + 2 < t.size(); k += 3) { flat(M(pr[t[k]], w0), M(pr[t[k + 1]], w0), M(pr[t[k + 2]], w0), n0); flat(M(pr[t[k]], w1), M(pr[t[k + 1]], w1), M(pr[t[k + 2]], w1), n1); }
        std::vector<P3> en(n);   // outward normal of each edge
        for (size_t i = 0; i < n; i++) { P2 a = pr[i], b = pr[(i + 1) % n]; float du = b.u - a.u, dv = b.v - a.v, l = std::sqrt(du * du + dv * dv); en[i] = l > 1e-9f ? axisMap(axis, dv / l, -du / l, 0) : P3{ 0, 1, 0 }; }
        for (size_t i = 0; i < n; i++) {
            size_t j = (i + 1) % n, ip = (i + n - 1) % n, jn = (i + 1) % n;
            P3 na = en[i], nb = en[i];
            if (smooth && dot(en[i], en[ip]) > 0.64f) na = nrm(en[i] + en[ip]);   // round outlines (arches) get soft shading
            if (smooth && dot(en[i], en[jn]) > 0.64f) nb = nrm(en[i] + en[jn]);
            P3 a0 = M(pr[i], w0), b0 = M(pr[j], w0), b1 = M(pr[j], w1), a1 = M(pr[i], w1);
            tri(a0, b0, b1, na, nb, nb, en[i]); tri(a0, b1, a1, na, nb, na, en[i]);
        }
        solids.push_back([=](P3 p) {
            float c[3] = { p.x, p.y, p.z }, w = c[axis], u = axis == 0 ? p.z : p.x, v = axis == 1 ? p.z : p.y;
            return w >= w0 && w <= w1 && inPoly(pr, u, v);
        });
    }
    // under a sloped top: corner heights h00 (x0 z0), h10 (x1 z0), h01 (x0 z1), h11 (x1 z1)
    void heightfield(float h00, float h10, float h01, float h11, bool anti = false) {
        P3 c00{ 0, h00, 0 }, c10{ 1, h10, 0 }, c01{ 0, h01, 1 }, c11{ 1, h11, 1 };
        auto top = [&](P3 a, P3 b, P3 c) { P3 n = nrm(cross(b - a, c - a)); if (n.y < 0) n = n * -1; flat(a, b, c, n); };
        auto top2 = [&](P3 a, P3 b, P3 c) { if (a.y > 0 || b.y > 0 || c.y > 0) top(a, b, c); };
        if (!anti) { top2(c00, c10, c11); top2(c00, c11, c01); } else { top2(c00, c10, c01); top2(c10, c11, c01); }
        quad({ 0, 0, 0 }, { 1, 0, 0 }, c10, c00, { 0, 0, -1 });
        quad({ 1, 0, 0 }, { 1, 0, 1 }, c11, c10, { 1, 0, 0 });
        quad({ 1, 0, 1 }, { 0, 0, 1 }, c01, c11, { 0, 0, 1 });
        quad({ 0, 0, 1 }, { 0, 0, 0 }, c00, c01, { -1, 0, 0 });
        // bottom: only under the part that has any height (a corner with nothing on it leaves the block below visible)
        P3 b00{ 0, 0, 0 }, b10{ 1, 0, 0 }, b01{ 0, 0, 1 }, b11{ 1, 0, 1 }, dn{ 0, -1, 0 };
        auto bot = [&](float ha, float hb, float hc, P3 a, P3 b, P3 c) { if (ha > 0 || hb > 0 || hc > 0) flat(a, b, c, dn); };
        if (!anti) { bot(h00, h10, h11, b00, b10, b11); bot(h00, h11, h01, b00, b11, b01); } else { bot(h00, h10, h01, b00, b10, b01); bot(h10, h11, h01, b10, b11, b01); }
        solids.push_back([=](P3 p) {
            float h;
            if (!anti) h = p.x >= p.z ? h00 + (h10 - h00) * p.x + (h11 - h10) * p.z : h00 + (h01 - h00) * p.z + (h11 - h01) * p.x;
            else h = p.x + p.z <= 1 ? h00 + (h10 - h00) * p.x + (h01 - h00) * p.z : h11 + (h01 - h11) * (1 - p.x) + (h10 - h11) * (1 - p.z);
            return p.y >= 0 && p.y <= h && p.x >= 0 && p.x <= 1 && p.z >= 0 && p.z <= 1;
        });
    }
    // an n-sided prism / cylinder / cone along an axis (r1 = 0 makes a cone)
    void prism(int axis, float cu, float cv, float r0, float r1, float w0, float w1, int n, float phaseDeg = 0) {
        bool smooth = n >= 8;
        std::vector<P2> c0, c1;
        for (int k = 0; k < n; k++) { float a = (phaseDeg + 360.f * k / n) * PI / 180; c0.push_back({ cu + r0 * std::cos(a), cv + r0 * std::sin(a) }); c1.push_back({ cu + r1 * std::cos(a), cv + r1 * std::sin(a) }); }
        P3 n0 = axisMap(axis, 0, 0, -1), n1 = axisMap(axis, 0, 0, 1);
        P3 cen0 = axisMap(axis, cu, cv, w0), cen1 = axisMap(axis, cu, cv, w1);
        for (int k = 0; k < n; k++) {
            int j = (k + 1) % n;
            P3 a0 = axisMap(axis, c0[k].u, c0[k].v, w0), b0 = axisMap(axis, c0[j].u, c0[j].v, w0), a1 = axisMap(axis, c1[k].u, c1[k].v, w1), b1 = axisMap(axis, c1[j].u, c1[j].v, w1);
            if (r0 > 0) flat(cen0, a0, b0, n0);
            if (r1 > 0) flat(cen1, a1, b1, n1);
            float slope = (r0 - r1) / std::max(1e-6f, w1 - w0);
            auto radial = [&](float ang) { return nrm(axisMap(axis, std::cos(ang), std::sin(ang), slope)); };
            float ak = (phaseDeg + 360.f * k / n) * PI / 180, aj = (phaseDeg + 360.f * (k + 1) / n) * PI / 180, am = (ak + aj) / 2;   // k + 1, not j: no wrap at 360
            P3 fn = radial(am), nk = smooth ? radial(ak) : fn, nj = smooth ? radial(aj) : fn;
            tri(a0, b0, b1, nk, nj, nj, fn); tri(a0, b1, a1, nk, nj, nk, fn);
        }
        solids.push_back([=](P3 p) {
            float c[3] = { p.x, p.y, p.z }, w = c[axis], u = axis == 0 ? p.z : p.x, v = axis == 1 ? p.z : p.y;
            if (w < w0 || w > w1) return false;
            float r = r0 + (r1 - r0) * (w - w0) / std::max(1e-6f, w1 - w0);
            std::vector<P2> poly; for (int k = 0; k < n; k++) { float a = (phaseDeg + 360.f * k / n) * PI / 180; poly.push_back({ cu + r * std::cos(a), cv + r * std::sin(a) }); }
            return inPoly(poly, u, v);
        });
    }
    void sphere(P3 c, float r, int nu, int nv, bool dome) {
        int v0 = dome ? nv / 2 : 0;
        auto pt = [&](int i, int j) { float th = 2 * PI * i / nu, ph = PI * j / nv - PI / 2; return P3{ std::cos(ph) * std::cos(th), std::sin(ph), std::cos(ph) * std::sin(th) }; };
        for (int j = v0; j < nv; j++) for (int i = 0; i < nu; i++) {
            P3 a = pt(i, j), b = pt(i + 1, j), cc = pt(i + 1, j + 1), d = pt(i, j + 1);
            P3 fn = nrm(a + b + cc + d);
            tri(c + a * r, c + b * r, c + cc * r, a, b, cc, fn); tri(c + a * r, c + cc * r, c + d * r, a, cc, d, fn);
        }
        if (dome) for (int i = 0; i < nu; i++) flat(c, c + pt(i, v0) * r, c + pt(i + 1, v0) * r, { 0, -1, 0 });
        solids.push_back([=](P3 p) { P3 d = p - c; return dot(d, d) <= r * r && (!dome || p.y >= c.y); });
    }
    // a flat picture with nothing behind it (drawn from both sides)
    void plane(P3 a, P3 b, P3 c, P3 d, float halfThick) {
        P3 n = nrm(cross(b - a, c - a)); quad(a, b, c, d, n); twoSided = true;
        solids.push_back([=](P3 p) {
            float dist = dot(p - a, n); if (std::fabs(dist) > halfThick) return false;
            P3 q = p - n * dist, e1 = b - a, e2 = d - a; float s = dot(q - a, e1) / dot(e1, e1), t = dot(q - a, e2) / dot(e2, e2);
            return s >= 0 && s <= 1 && t >= 0 && t <= 1;
        });
    }
    bool inside(P3 p) const { for (auto& s : solids) if (s(p)) return true; return false; }
};

// ------------------------------------------------------------------ boxes for collision: 8 x 8 x 8 cells, merged
std::vector<SBox> voxelBoxes(const Builder& b) {
    const int N = 8; bool g[N][N][N], used[N][N][N] = {};
    for (int y = 0; y < N; y++) for (int z = 0; z < N; z++) for (int x = 0; x < N; x++) {
        bool any = false;   // a cell counts if any of 8 sample points inside it is solid (thin plates still collide)
        for (int s = 0; s < 8 && !any; s++) any = b.inside({ (x + ((s & 1) ? 0.75f : 0.25f)) / N, (y + ((s & 2) ? 0.75f : 0.25f)) / N, (z + ((s & 4) ? 0.75f : 0.25f)) / N });
        g[x][y][z] = any;
    }
    std::vector<SBox> out;
    for (int y = 0; y < N; y++) for (int z = 0; z < N; z++) for (int x = 0; x < N; x++) {
        if (!g[x][y][z] || used[x][y][z]) continue;
        int x1 = x; while (x1 + 1 < N && g[x1 + 1][y][z] && !used[x1 + 1][y][z]) x1++;
        int z1 = z; for (bool ok = true; ok && z1 + 1 < N;) { for (int i = x; i <= x1; i++) ok &= g[i][y][z1 + 1] && !used[i][y][z1 + 1]; if (ok) z1++; }
        int y1 = y; for (bool ok = true; ok && y1 + 1 < N;) { for (int k = z; k <= z1; k++) for (int i = x; i <= x1; i++) ok &= g[i][y1 + 1][k] && !used[i][y1 + 1][k]; if (ok) y1++; }
        for (int j = y; j <= y1; j++) for (int k = z; k <= z1; k++) for (int i = x; i <= x1; i++) used[i][j][k] = true;
        out.push_back({ { (float)x / N, (float)y / N, (float)z / N }, { (float)(x1 + 1) / N, (float)(y1 + 1) / N, (float)(z1 + 1) / N } });
    }
    return out;
}

// ================================================================== the shape collection
struct Def { std::string key, name, cat; Place place; std::function<void(Builder&)> make; };

std::vector<Def> catalog() {
    const float E = 1.f / 16;   // one pixel
    std::vector<Def> d;
    auto add = [&](std::string k, std::string n, std::string c, Place p, std::function<void(Builder&)> f) { d.push_back({ k, n, c, p, std::move(f) }); };
    // ---- NEVER reorder or remove entries: the position is the id stored in worlds. Only add at the end. ----
    add("full", "Full block", "Basic", Place::Fixed, [](Builder& b) { b.box(0, 0, 0, 1, 1, 1); });
    add("cross", "Cross plane", "Panels", Place::Fixed, [](Builder& b) {
        b.plane({ 0, 0, 0 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 0 }, 0.06f); b.plane({ 0, 0, 1 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 1 }, 0.06f); });
    add("plane", "Flat plane", "Panels", Place::Surface, [=](Builder& b) { float x = E / 2; b.plane({ x, 0, 1 }, { x, 0, 0 }, { x, 1, 0 }, { x, 1, 1 }, x); });
    // basic
    add("half", "Half block", "Basic", Place::Facing, [](Builder& b) { b.box(0, 0, 0, 1, 0.5f, 1); });
    add("quarter_slab", "Quarter slab", "Basic", Place::Facing, [](Builder& b) { b.box(0, 0, 0, 1, 0.25f, 1); });
    add("three_quarter", "Three-quarter block", "Basic", Place::Facing, [](Builder& b) { b.box(0, 0, 0, 1, 0.75f, 1); });
    add("plate", "Plate", "Basic", Place::Facing, [=](Builder& b) { b.box(0, 0, 0, 1, 2 * E, 1); });
    add("plate_thin", "Thin plate", "Basic", Place::Facing, [=](Builder& b) { b.box(0, 0, 0, 1, E, 1); });
    add("half_center", "Centered half block", "Basic", Place::Facing, [](Builder& b) { b.box(0, 0.25f, 0, 1, 0.75f, 1); });
    add("plate_center", "Centered plate", "Basic", Place::Facing, [=](Builder& b) { b.box(0, 7 * E, 0, 1, 9 * E, 1); });
    add("quarter_block", "Quarter block", "Basic", Place::Facing, [](Builder& b) { b.box(0, 0, 0, 1, 0.5f, 0.5f); });
    add("eighth", "Eighth block", "Basic", Place::Facing, [](Builder& b) { b.box(0, 0, 0, 0.5f, 0.5f, 0.5f); });
    add("corner_cut", "Corner-cut block", "Basic", Place::Yaw, [](Builder& b) { b.extrude({ { 0, 0 }, { 1, 0 }, { 1, 0.5f }, { 0.5f, 0.5f }, { 0.5f, 1 }, { 0, 1 } }, 1, 0, 1, false); });
    add("seven_eighths", "Block minus a corner", "Basic", Place::Facing, [](Builder& b) { b.box(0, 0, 0, 1, 0.5f, 1); b.extrude({ { 0, 0 }, { 1, 0 }, { 1, 0.5f }, { 0.5f, 0.5f }, { 0.5f, 1 }, { 0, 1 } }, 1, 0.5f, 1, false); });
    add("three_eighths", "Half-block corner (L)", "Basic", Place::Facing, [](Builder& b) { b.extrude({ { 0, 0 }, { 1, 0 }, { 1, 0.5f }, { 0.5f, 0.5f }, { 0.5f, 1 }, { 0, 1 } }, 1, 0, 0.5f, false); });
    add("wall_center", "Centered wall", "Basic", Place::Yaw, [](Builder& b) { b.box(0, 0, 0.25f, 1, 1, 0.75f); });
    // slopes (rise toward the side away from you)
    auto ramp = [](float a, float b2) { return [=](Builder& b) { b.heightfield(a, a, b2, b2); }; };
    add("ramp", "Ramp", "Slopes", Place::Facing, ramp(0, 1));
    add("ramp2_a", "Long ramp 1/2 (bottom)", "Slopes", Place::Facing, ramp(0, 0.5f));
    add("ramp2_b", "Long ramp 2/2 (top)", "Slopes", Place::Facing, ramp(0.5f, 1));
    add("ramp3_a", "Longer ramp 1/3", "Slopes", Place::Facing, ramp(0, 1.f / 3));
    add("ramp3_b", "Longer ramp 2/3", "Slopes", Place::Facing, ramp(1.f / 3, 2.f / 3));
    add("ramp3_c", "Longer ramp 3/3", "Slopes", Place::Facing, ramp(2.f / 3, 1));
    add("ramp4_a", "Longest ramp 1/4", "Slopes", Place::Facing, ramp(0, 0.25f));
    add("ramp4_b", "Longest ramp 2/4", "Slopes", Place::Facing, ramp(0.25f, 0.5f));
    add("ramp4_c", "Longest ramp 3/4", "Slopes", Place::Facing, ramp(0.5f, 0.75f));
    add("ramp4_d", "Longest ramp 4/4", "Slopes", Place::Facing, ramp(0.75f, 1));
    add("ramp_steep", "Steep ramp", "Slopes", Place::Facing, [](Builder& b) { b.extrude({ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0.5f, 1 } }, 0, 0, 1, false); });
    add("ramp_low", "Low ramp (half height)", "Slopes", Place::Facing, [](Builder& b) { b.extrude({ { 0, 0 }, { 1, 0 }, { 1, 0.5f } }, 0, 0, 1, false); });
    auto corner = [](float lo, float hi, bool outer) { return [=](Builder& b) { if (outer) b.heightfield(lo, lo, lo, hi); else b.heightfield(lo, hi, hi, hi); }; };
    add("ramp_outer", "Ramp outer corner", "Slopes", Place::Facing, corner(0, 1, true));
    add("ramp_inner", "Ramp inner corner", "Slopes", Place::Facing, corner(0, 1, false));
    add("ramp2a_outer", "Long ramp outer corner (bottom)", "Slopes", Place::Facing, corner(0, 0.5f, true));
    add("ramp2a_inner", "Long ramp inner corner (bottom)", "Slopes", Place::Facing, corner(0, 0.5f, false));
    add("ramp2b_outer", "Long ramp outer corner (top)", "Slopes", Place::Facing, corner(0.5f, 1, true));
    add("ramp2b_inner", "Long ramp inner corner (top)", "Slopes", Place::Facing, corner(0.5f, 1, false));
    add("ramp_tip", "Ramp tip (pyramid corner)", "Slopes", Place::Facing, [](Builder& b) { b.heightfield(0, 0, 0, 1, true); });
    add("ramp_valley", "Ramp valley", "Slopes", Place::Facing, [](Builder& b) { b.heightfield(0, 1, 1, 1, true); });
    add("wedge_vertical", "Vertical wedge", "Slopes", Place::Yaw, [](Builder& b) { b.extrude({ { 0, 0 }, { 1, 0 }, { 1, 1 } }, 1, 0, 1, false); });
    add("wedge_vertical_half", "Low vertical wedge", "Slopes", Place::Facing, [](Builder& b) { b.extrude({ { 0, 0 }, { 1, 0 }, { 1, 1 } }, 1, 0, 0.5f, false); });
    // stairs
    add("stairs", "Stairs", "Stairs", Place::Facing, [](Builder& b) { b.extrude({ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0.5f, 1 }, { 0.5f, 0.5f }, { 0, 0.5f } }, 0, 0, 1, false); });
    add("stairs3", "Stairs (3 steps)", "Stairs", Place::Facing, [](Builder& b) { float t = 1.f / 3; b.extrude({ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 2 * t, 1 }, { 2 * t, 2 * t }, { t, 2 * t }, { t, t }, { 0, t } }, 0, 0, 1, false); });
    add("stairs4", "Stairs (4 steps)", "Stairs", Place::Facing, [](Builder& b) { b.extrude({ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0.75f, 1 }, { 0.75f, 0.75f }, { 0.5f, 0.75f }, { 0.5f, 0.5f }, { 0.25f, 0.5f }, { 0.25f, 0.25f }, { 0, 0.25f } }, 0, 0, 1, false); });
    add("stairs_outer", "Stairs outer corner", "Stairs", Place::Facing, [](Builder& b) { b.box(0, 0, 0, 1, 0.5f, 1); b.box(0.5f, 0.5f, 0.5f, 1, 1, 1); });
    add("stairs_inner", "Stairs inner corner", "Stairs", Place::Facing, [](Builder& b) { b.box(0, 0, 0, 1, 0.5f, 1); b.extrude({ { 0, 0.5f }, { 0.5f, 0.5f }, { 0.5f, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }, 1, 0.5f, 1, false); });
    add("stairs4_outer", "Stairs (4 steps) outer corner", "Stairs", Place::Facing, [](Builder& b) { for (int k = 0; k < 4; k++) b.box(k * 0.25f, k * 0.25f, k * 0.25f, 1, (k + 1) * 0.25f, 1); });
    add("stairs4_inner", "Stairs (4 steps) inner corner", "Stairs", Place::Facing, [](Builder& b) {
        for (int k = 0; k < 4; k++) { float s = k * 0.25f; if (k == 0) b.box(0, 0, 0, 1, 0.25f, 1); else b.extrude({ { 0, s }, { s, s }, { s, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }, 1, s, s + 0.25f, false); } });
    add("stairs_half", "Half-width stairs", "Stairs", Place::Facing, [](Builder& b) { b.extrude({ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0.5f, 1 }, { 0.5f, 0.5f }, { 0, 0.5f } }, 0, 0, 0.5f, false); });
    add("step_thin", "Floating step", "Stairs", Place::Facing, [](Builder& b) { b.box(0, 0.375f, 0.5f, 1, 0.5f, 1); });
    // pillars and poles
    for (int px : { 2, 4, 8, 12 }) {
        float h = px / 32.f; add("pillar" + std::to_string(px), "Square pillar " + std::to_string(px) + " px", "Pillars", Place::Axis, [=](Builder& b) { b.box(0.5f - h, 0, 0.5f - h, 0.5f + h, 1, 0.5f + h); });
    }
    add("pillar_corner4", "Corner pillar 4 px", "Pillars", Place::Yaw, [](Builder& b) { b.box(0, 0, 0, 0.25f, 1, 0.25f); });
    add("pillar_corner8", "Corner pillar 8 px", "Pillars", Place::Yaw, [](Builder& b) { b.box(0, 0, 0, 0.5f, 1, 0.5f); });
    add("pillar_edge4", "Edge pillar 4 px", "Pillars", Place::Yaw, [](Builder& b) { b.box(0.375f, 0, 0.75f, 0.625f, 1, 1); });
    for (int px : { 2, 4, 8, 12, 16 }) {
        float r = px / 32.f; int n = px <= 4 ? 8 : 16;
        add("pole" + std::to_string(px), px == 16 ? "Cylinder" : "Round pole " + std::to_string(px) + " px", px == 16 ? "Round" : "Pillars", Place::Axis, [=](Builder& b) { b.prism(1, 0.5f, 0.5f, r, r, 0, 1, n, 360.f / n / 2); });
    }
    add("octagon", "Octagonal pillar", "Pillars", Place::Axis, [](Builder& b) { b.prism(1, 0.5f, 0.5f, 0.5f / std::cos(PI / 8), 0.5f / std::cos(PI / 8), 0, 1, 8, 22.5f); });
    add("hexagon", "Hexagonal pillar", "Pillars", Place::Axis, [](Builder& b) { b.prism(1, 0.5f, 0.5f, 0.5f, 0.5f, 0, 1, 6, 0); });
    add("post", "Short post", "Pillars", Place::Axis, [](Builder& b) { b.box(0.375f, 0, 0.375f, 0.625f, 0.5f, 0.625f); });
    // round
    add("half_cylinder", "Half cylinder (lying)", "Round", Place::Facing, [](Builder& b) { b.extrude(arc(0.5f, 0, 0.5f, 0, 180, 16, true), 0, 0, 1); });
    add("rounded_edge", "Rounded edge", "Round", Place::Facing, [](Builder& b) { auto p = arc(0, 0, 1, 0, 90, 16, true); p.insert(p.begin(), P2{ 0, 0 }); b.extrude(p, 0, 0, 1); });
    add("cove", "Cove (rounded inner edge)", "Round", Place::Facing, [](Builder& b) { std::vector<P2> p = { { 0, 0 } }; auto a = arc(1, 1, 1, 270, 180, 16, true); p.insert(p.end(), a.begin(), a.end()); b.extrude(p, 0, 0, 1); });
    add("arch", "Arch", "Round", Place::Yaw, [](Builder& b) { std::vector<P2> p = { { 1, 0 }, { 1, 1 }, { 0, 1 }, { 0, 0 } }; auto a = arc(0.5f, 0, 0.5f, 180, 0, 16, false); p.insert(p.end(), a.begin(), a.end()); b.extrude(p, 2, 0, 1); });
    add("arch_wide", "Wide arch (half)", "Round", Place::Yaw, [](Builder& b) { std::vector<P2> p = { { 0, 1 }, { 1, 1 } }; auto a = arc(1, 0, 1, 90, 180, 16, false); p.insert(p.end(), a.begin(), a.end()); p.push_back({ 0, 0 }); b.extrude(p, 2, 0, 1); });
    add("arch_thin", "Thin arch", "Round", Place::Yaw, [](Builder& b) { std::vector<P2> p = { { 1, 0 }, { 1, 1 }, { 0, 1 }, { 0, 0 } }; auto a = arc(0.5f, 0, 0.5f, 180, 0, 16, false); p.insert(p.end(), a.begin(), a.end()); b.extrude(p, 2, 0.375f, 0.625f); });
    add("ball", "Ball", "Round", Place::Fixed, [](Builder& b) { b.sphere({ 0.5f, 0.5f, 0.5f }, 0.5f, 16, 10, false); });
    add("dome", "Dome", "Round", Place::Facing, [](Builder& b) { b.sphere({ 0.5f, 0, 0.5f }, 0.5f, 16, 10, true); });
    add("cone", "Cone", "Round", Place::Facing, [](Builder& b) { b.prism(1, 0.5f, 0.5f, 0.5f, 0, 0, 1, 16); });
    add("half_cylinder_upright", "Half cylinder (standing)", "Round", Place::Yaw, [](Builder& b) { b.extrude(arc(0.5f, 0, 0.5f, 0, 180, 16, true), 1, 0, 1); });
    add("quarter_cylinder", "Rounded corner", "Round", Place::Yaw, [](Builder& b) { auto p = arc(0, 0, 1, 0, 90, 16, true); p.insert(p.begin(), P2{ 0, 0 }); b.extrude(p, 1, 0, 1); });
    // beams and bars
    add("beam4", "Beam 4 px", "Beams & bars", Place::Yaw, [](Builder& b) { b.box(0, 0.375f, 0.375f, 1, 0.625f, 0.625f); });
    add("beam8", "Beam 8 px", "Beams & bars", Place::Yaw, [](Builder& b) { b.box(0, 0.25f, 0.25f, 1, 0.75f, 0.75f); });
    add("beam_edge", "Edge beam", "Beams & bars", Place::Facing, [](Builder& b) { b.box(0, 0, 0.75f, 1, 0.25f, 1); });
    add("ibeam", "I-beam", "Beams & bars", Place::Yaw, [=](Builder& b) {
        b.extrude({ { 0.25f, 0.25f }, { 0.75f, 0.25f }, { 0.75f, 0.25f + E }, { 0.5f + E, 0.25f + E }, { 0.5f + E, 0.75f - E }, { 0.75f, 0.75f - E }, { 0.75f, 0.75f }, { 0.25f, 0.75f }, { 0.25f, 0.75f - E }, { 0.5f - E, 0.75f - E }, { 0.5f - E, 0.25f + E }, { 0.25f, 0.25f + E } }, 0, 0, 1, false); });
    add("bar2", "Round bar 2 px", "Beams & bars", Place::Yaw, [](Builder& b) { b.prism(0, 0.5f, 0.5f, 1.f / 16, 1.f / 16, 0, 1, 8, 22.5f); });
    add("bar4", "Round bar 4 px", "Beams & bars", Place::Yaw, [](Builder& b) { b.prism(0, 0.5f, 0.5f, 0.125f, 0.125f, 0, 1, 12, 15); });
    add("window_bars", "Window bars", "Beams & bars", Place::Yaw, [=](Builder& b) { for (int i = 0; i < 4; i++) { float x = (i * 2 + 1) / 8.f; b.box(x - E, 0, 0.5f - E, x + E, 1, 0.5f + E); } });
    add("grate", "Floor grate", "Beams & bars", Place::Facing, [=](Builder& b) { for (int i = 0; i < 4; i++) { float z = (i * 2 + 1) / 8.f; b.box(0, 0, z - E, 1, E, z + E); } b.box(0.125f - E, E, 0, 0.125f + E, 2 * E, 1); b.box(0.875f - E, E, 0, 0.875f + E, 2 * E, 1); });
    add("railing", "Railing", "Beams & bars", Place::Yaw, [=](Builder& b) {
        b.box(0, 1 - 3 * E, 0.5f - 1.5f * E, 1, 1 - E, 0.5f + 1.5f * E); b.box(0, E, 0.5f - E, 1, 3 * E, 0.5f + E);
        for (float x : { 0.25f, 0.75f }) b.box(x - E, 3 * E, 0.5f - E, x + E, 1 - 3 * E, 0.5f + E); });
    add("railing_edge", "Railing on the edge", "Beams & bars", Place::Yaw, [=](Builder& b) {
        b.box(0, 1 - 3 * E, 1 - 3 * E, 1, 1 - E, 1); b.box(0, E, 1 - 2 * E, 1, 3 * E, 1);
        for (float x : { 0.25f, 0.75f }) b.box(x - E, 3 * E, 1 - 2 * E, x + E, 1 - 3 * E, 1); });
    add("fence", "Fence", "Beams & bars", Place::Yaw, [=](Builder& b) { b.box(0.375f, 0, 0.375f, 0.625f, 1, 0.625f); b.box(0, 0.75f, 0.5f - E, 1, 0.875f, 0.5f + E); b.box(0, 0.375f, 0.5f - E, 1, 0.5f, 0.5f + E); });
    add("gusset", "Gusset (corner brace)", "Beams & bars", Place::Facing, [](Builder& b) { b.extrude({ { 0.5f, 1 }, { 1, 1 }, { 1, 0.5f } }, 0, 0.4375f, 0.5625f, false); });
    add("ladder", "Ladder", "Beams & bars", Place::Surface, [=](Builder& b) {
        for (float z : { 0.1875f, 0.8125f }) b.box(0, 0, z - E, 2 * E, 1, z + E);
        for (int i = 0; i < 4; i++) { float y = (i * 2 + 1) / 8.f; b.box(0, y - E / 2, 0.1875f + E, 1.5f * E, y + E / 2, 0.8125f - E); } });
    // walls, frames and panels
    add("window_frame", "Window frame", "Walls & frames", Place::Yaw, [=](Builder& b) { float t = 2 * E, z0 = 0.375f, z1 = 0.625f; b.box(0, 0, z0, 1, t, z1); b.box(0, 1 - t, z0, 1, 1, z1); b.box(0, t, z0, t, 1 - t, z1); b.box(1 - t, t, z0, 1, 1 - t, z1); });
    add("door_frame", "Door frame", "Walls & frames", Place::Yaw, [=](Builder& b) { float t = 2 * E, z0 = 0.375f, z1 = 0.625f; b.box(0, 1 - t, z0, 1, 1, z1); b.box(0, 0, z0, t, 1 - t, z1); b.box(1 - t, 0, z0, 1, 1 - t, z1); });
    add("frame_edge", "Frame edge", "Walls & frames", Place::Yaw, [=](Builder& b) { b.box(0, 0, 0.375f, 2 * E, 1, 0.625f); });
    add("wall_diagonal", "Diagonal wall", "Walls & frames", Place::Yaw, [=](Builder& b) { float t = 2 * E; b.extrude({ { 0, 0 }, { t, 0 }, { 1, 1 - t }, { 1, 1 }, { 1 - t, 1 }, { 0, t } }, 1, 0, 1, false); });
    add("wall_corner", "Thin corner wall", "Walls & frames", Place::Yaw, [=](Builder& b) { float t = 2 * E; b.extrude({ { 0, 0 }, { 1, 0 }, { 1, t }, { t, t }, { t, 1 }, { 0, 1 } }, 1, 0, 1, false); });
    add("plane_center", "Centered plane", "Panels", Place::Yaw, [](Builder& b) { b.plane({ 0, 0, 0.5f }, { 1, 0, 0.5f }, { 1, 1, 0.5f }, { 0, 1, 0.5f }, 0.06f); });
    add("plane_diagonal", "Diagonal plane", "Panels", Place::Yaw, [](Builder& b) { b.plane({ 0, 0, 0 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 0 }, 0.06f); });
    add("plane_floor", "Floor plane", "Panels", Place::Fixed, [=](Builder& b) { float y = E / 2; b.plane({ 0, y, 0 }, { 1, y, 0 }, { 1, y, 1 }, { 0, y, 1 }, y); });
    // roofs
    auto thinRamp = [=](float a, float c) { return [=](Builder& b) { float t = 2 * E; b.extrude({ { 0, a - t }, { 0, a }, { 1, c }, { 1, c - t } }, 0, 0, 1, false); }; };
    add("roof", "Roof (thin)", "Roofs", Place::Facing, [=](Builder& b) { float t = 2 * E; b.extrude({ { 0, 0 }, { t, 0 }, { 1, 1 - t }, { 1, 1 } }, 0, 0, 1, false); });
    add("roof2_a", "Low roof 1/2 (thin)", "Roofs", Place::Facing, [=](Builder& b) { float t = 2 * E; b.extrude({ { 0, 0 }, { 2 * t, 0 }, { 1, 0.5f - t }, { 1, 0.5f } }, 0, 0, 1, false); });
    add("roof2_b", "Low roof 2/2 (thin)", "Roofs", Place::Facing, thinRamp(0.5f, 1));
    add("ridge", "Roof ridge", "Roofs", Place::Yaw, [](Builder& b) { b.extrude({ { 0, 0 }, { 1, 0 }, { 0.5f, 0.5f } }, 0, 0, 1, false); });
    add("ridge_thin", "Roof ridge (thin)", "Roofs", Place::Yaw, [=](Builder& b) { float t = 2 * E; b.extrude({ { 0, 0 }, { t, 0 }, { 0.5f, 0.5f - t }, { 1 - t, 0 }, { 1, 0 }, { 0.5f, 0.5f } }, 0, 0, 1, false); });
    add("ridge_tall", "Steep roof ridge", "Roofs", Place::Yaw, [](Builder& b) { b.extrude({ { 0, 0 }, { 1, 0 }, { 0.5f, 1 } }, 0, 0, 1, false); });
    add("gable", "Gable wall", "Roofs", Place::Yaw, [](Builder& b) { b.extrude({ { 0, 0 }, { 1, 0 }, { 0.5f, 0.5f } }, 2, 0.375f, 0.625f, false); });
    add("gable_half", "Half gable wall", "Roofs", Place::Yaw, [](Builder& b) { b.extrude({ { 0, 0 }, { 1, 0 }, { 1, 1 } }, 2, 0.375f, 0.625f, false); });
    // decor
    add("pyramid", "Pyramid", "Decor", Place::Facing, [](Builder& b) { b.prism(1, 0.5f, 0.5f, 0.70710678f, 0, 0, 1, 4, 45); });
    add("pyramid_low", "Low pyramid", "Decor", Place::Facing, [](Builder& b) { b.prism(1, 0.5f, 0.5f, 0.70710678f, 0, 0, 0.5f, 4, 45); });
    add("spike", "Spike", "Decor", Place::Facing, [](Builder& b) { b.prism(1, 0.5f, 0.5f, 0.125f, 0, 0, 1, 8); });
    add("frustum", "Tapered block", "Decor", Place::Facing, [](Builder& b) { b.prism(1, 0.5f, 0.5f, 0.70710678f, 0.35355339f, 0, 1, 4, 45); });
    add("table_top", "Table", "Decor", Place::Yaw, [=](Builder& b) { b.box(0, 1 - 2 * E, 0, 1, 1, 1); for (float x : { 0.0f, 1 - 2 * E }) for (float z : { 0.0f, 1 - 2 * E }) b.box(x, 0, z, x + 2 * E, 1 - 2 * E, z + 2 * E); });
    add("shelf", "Shelf", "Decor", Place::Surface, [=](Builder& b) { b.box(0, 0.4375f, 0, 0.5f, 0.5625f, 1); });
    add("button", "Small panel", "Decor", Place::Surface, [=](Builder& b) { b.box(0, 0.375f, 0.375f, E, 0.625f, 0.625f); });
    // ---- add new shapes below this line only ----
    return d;
}

SideMask rasterize(const std::vector<STri>& tris, int side) {
    SideMask m; int a = side / 2, ua = (a + 1) % 3, va = (a + 2) % 3;
    for (auto& t : tris) {
        float A[2] = { t.p[0][ua], t.p[0][va] }, B[2] = { t.p[1][ua], t.p[1][va] }, C[2] = { t.p[2][ua], t.p[2][va] };
        float d = (B[0] - A[0]) * (C[1] - A[1]) - (B[1] - A[1]) * (C[0] - A[0]); if (std::fabs(d) < 1e-12f) continue;
        for (int j = 0; j < 16; j++) for (int i = 0; i < 16; i++) {
            float u = (i + 0.5f) / 16, v = (j + 0.5f) / 16;
            float w1 = ((B[0] - u) * (C[1] - v) - (B[1] - v) * (C[0] - u)) / d, w2 = ((C[0] - u) * (A[1] - v) - (C[1] - v) * (A[0] - u)) / d, w3 = 1 - w1 - w2;
            if (w1 >= -1e-5f && w2 >= -1e-5f && w3 >= -1e-5f) m.set(j * 16 + i);
        }
    }
    return m;
}
} // namespace

Shapes::Shapes() {
    auto defs = catalog();
    infos.resize(1); infos[0] = { "default", "Block's own shape", "Basic", false, Place::Fixed };
    vars.resize(24);   // id 0 is never used for geometry
    for (auto& d : defs) {
        Builder b; d.make(b);
        infos.push_back({ d.key, d.name, d.cat, b.twoSided, d.place });
        std::vector<SBox> boxes = voxelBoxes(b);
        for (int r = 0; r < 24; r++) {
            const Rot& R = rot(r); ShapeVariant v;
            auto tp = [&](const float p[3], float o[3], bool point) {
                float q[3] = { p[0] - (point ? 0.5f : 0), p[1] - (point ? 0.5f : 0), p[2] - (point ? 0.5f : 0) };
                for (int i = 0; i < 3; i++) o[i] = R.m[i][0] * q[0] + R.m[i][1] * q[1] + R.m[i][2] * q[2] + (point ? 0.5f : 0);
            };
            for (const STri& t : b.tris) {
                STri o; for (int k = 0; k < 3; k++) { tp(t.p[k], o.p[k], true); tp(t.n[k], o.n[k], false); }
                tp(t.fn, o.fn, false);
                int side = 6;
                if (!b.twoSided) for (int f = 0; f < 6; f++) {
                    int a = f / 2; float s = DIR[f][a], plane = s > 0 ? 1.f : 0.f;
                    if (o.fn[a] * s > 0.999f && std::fabs(o.p[0][a] - plane) < 1e-4f && std::fabs(o.p[1][a] - plane) < 1e-4f && std::fabs(o.p[2][a] - plane) < 1e-4f) { side = f; break; }
                }
                v.tris[side].push_back(o);
            }
            for (int f = 0; f < 6; f++) { v.mask[f] = rasterize(v.tris[f], f); v.full[f] = v.mask[f].all(); }
            v.bounds = { { 1, 1, 1 }, { 0, 0, 0 } };
            for (const SBox& bx : boxes) {
                float c0[3], c1[3]; tp(bx.lo, c0, true); tp(bx.hi, c1, true);
                SBox o; for (int i = 0; i < 3; i++) { o.lo[i] = std::min(c0[i], c1[i]); o.hi[i] = std::max(c0[i], c1[i]); v.bounds.lo[i] = std::min(v.bounds.lo[i], o.lo[i]); v.bounds.hi[i] = std::max(v.bounds.hi[i], o.hi[i]); }
                v.boxes.push_back(o);
            }
            if (v.boxes.empty()) v.bounds = { { 0, 0, 0 }, { 1, 1, 1 } };
            vars.push_back(std::move(v));
        }
    }
}

const Shapes& Shapes::get() { static const Shapes s; return s; }
int Shapes::find(const std::string& key) const { for (size_t i = 1; i < infos.size(); i++) if (infos[i].key == key) return (int)i; return 0; }
std::vector<std::string> Shapes::categories() const {
    std::vector<std::string> c;
    for (size_t i = 1; i < infos.size(); i++) if (std::find(c.begin(), c.end(), infos[i].category) == c.end()) c.push_back(infos[i].category);
    return c;
}

} // namespace mc
