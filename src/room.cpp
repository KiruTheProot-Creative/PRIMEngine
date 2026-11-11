#include "room.h"
#include <fstream>
#include <sstream>

namespace px {

void Room::resize(int x, int y, int z, int oX, int oY, int oZ) {
    sx = x; sy = y; sz = z; ox = oX; oy = oY; oz = oZ;
    cells.assign((size_t)sx * sy * sz, B_AIR);
}
void Room::newFlat(int half) {
    resize(64, 32, 64, -32, -1, -32);
    for (int z = -half; z < half; z++) for (int x = -half; x < half; x++) set(x, -1, z, B_CUBE);
}
size_t Room::count() const { size_t n = 0; for (auto c : cells) n += c != B_AIR; return n; }

// ------------------------------------------------------------------ file
static std::string quoted(const std::string& s) { std::string o = "\""; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o + "\""; }

bool Room::save(const std::string& path) const {
    std::ofstream f(path, std::ios::trunc); if (!f) return false;
    f << "PRIMROOM 1\n" << "name " << quoted(name) << "\n";
    f << "size " << sx << " " << sy << " " << sz << "\norigin " << ox << " " << oy << " " << oz << "\ndata\n";
    size_t i = 0, pairs = 0;
    while (i < cells.size()) {
        size_t j = i; while (j < cells.size() && cells[j] == cells[i]) j++;
        f << (int)cells[i] << " " << (j - i) << ((++pairs % 32) ? " " : "\n");
        i = j;
    }
    f << "\nend\n";
    return (bool)f;
}

bool Room::load(const std::string& path, std::string& err) {
    std::ifstream f(path); if (!f) { err = "cannot open file"; return false; }
    std::string magic; int ver = 0; f >> magic >> ver;
    if (magic != "PRIMROOM") { err = "not a room file"; return false; }
    Room r; std::string key;
    while (f >> key) {
        if (key == "name") {
            f >> std::ws; std::string s; char c;
            if (f.peek() == '"') { f.get(); while (f.get(c) && c != '"') { if (c == '\\') f.get(c); s += c; } }
            else f >> s;
            r.name = s;
        }
        else if (key == "size") f >> r.sx >> r.sy >> r.sz;
        else if (key == "origin") f >> r.ox >> r.oy >> r.oz;
        else if (key == "data") {
            if (r.sx <= 0 || r.sy <= 0 || r.sz <= 0 || (size_t)r.sx * r.sy * r.sz > 64u * 1024 * 1024) { err = "bad room size"; return false; }
            r.cells.assign((size_t)r.sx * r.sy * r.sz, B_AIR);
            size_t pos = 0; std::string a;
            while (f >> a && a != "end") {
                size_t n = 0; f >> n; int v = std::atoi(a.c_str());
                if (pos + n > r.cells.size()) { err = "room data is too long"; return false; }
                std::fill(r.cells.begin() + pos, r.cells.begin() + pos + n, (uint8_t)v); pos += n;
            }
            if (pos != r.cells.size()) { err = "room data is incomplete"; return false; }
            *this = std::move(r); return true;
        }
    }
    err = "no data in room file"; return false;
}

// ------------------------------------------------------------------ mesh
void Room::buildMesh(MeshData& out) const {
    out.v.clear(); out.i.clear();
    static const float faceShade[6] = { 0.80f, 0.80f, 1.00f, 0.55f, 0.70f, 0.70f };   // +X -X +Y -Y +Z -Z
    for (int y = oy; y < oy + sy; y++) for (int z = oz; z < oz + sz; z++) for (int x = ox; x < ox + sx; x++) {
        if (!solid(x, y, z)) continue;
        int c[3] = { x, y, z };
        for (int f = 0; f < 6; f++) {
            int a = f / 2, sgn = (f % 2 == 0) ? 1 : -1;
            int n[3] = { 0, 0, 0 }; n[a] = sgn;
            if (solid(x + n[0], y + n[1], z + n[2])) continue;
            int ua = (a + 1) % 3, va = (a + 2) % 3;
            int L[3] = { x + n[0], y + n[1], z + n[2] };
            Vertex q[4]; int ao[4];
            const int du[4] = { 0, 1, 1, 0 }, dv[4] = { 0, 0, 1, 1 };
            for (int k = 0; k < 4; k++) {
                float p[3] = { (float)c[0], (float)c[1], (float)c[2] };
                p[a] += sgn > 0 ? 1.f : 0.f; p[ua] += (float)du[k]; p[va] += (float)dv[k];
                int s1c[3] = { L[0], L[1], L[2] }, s2c[3] = { L[0], L[1], L[2] }, cc[3] = { L[0], L[1], L[2] };
                int su = du[k] ? 1 : -1, sv = dv[k] ? 1 : -1;
                s1c[ua] += su; s2c[va] += sv; cc[ua] += su; cc[va] += sv;
                int s1 = solid(s1c[0], s1c[1], s1c[2]), s2 = solid(s2c[0], s2c[1], s2c[2]), co = solid(cc[0], cc[1], cc[2]);
                ao[k] = (s1 && s2) ? 0 : 3 - (s1 + s2 + co);
                float shade = faceShade[f] * (0.38f + 0.62f * ao[k] / 3.f) * 0.85f;
                // texture: side faces keep "up" pointing up
                float u, v;
                if (a == 1) { u = p[0] - std::floor((float)x); v = p[2] - std::floor((float)z); }
                else { u = (a == 0) ? p[2] - z : p[0] - x; v = 1.f - (p[1] - y); }
                q[k] = { { p[0], p[1], p[2] }, { (float)n[0], (float)n[1], (float)n[2] }, { u, v }, { shade, shade, shade, 1.f } };
            }
            uint32_t b = (uint32_t)out.v.size();
            for (auto& vv : q) out.v.push_back(vv);
            // split along the brighter diagonal (avoids the classic AO "seam"), then fix winding to the engine convention
            uint32_t t[6];
            if (ao[0] + ao[2] > ao[1] + ao[3]) { uint32_t s[6] = { 0, 1, 2, 0, 2, 3 }; std::copy(s, s + 6, t); }
            else { uint32_t s[6] = { 0, 1, 3, 1, 2, 3 }; std::copy(s, s + 6, t); }
            for (int tri = 0; tri < 2; tri++) {
                uint32_t i0 = t[tri * 3], i1 = t[tri * 3 + 1], i2 = t[tri * 3 + 2];
                V3 p0{ q[i0].p[0], q[i0].p[1], q[i0].p[2] }, p1{ q[i1].p[0], q[i1].p[1], q[i1].p[2] }, p2{ q[i2].p[0], q[i2].p[1], q[i2].p[2] };
                if (dot(cross(p1 - p0, p2 - p0), V3{ (float)n[0], (float)n[1], (float)n[2] }) < 0) std::swap(i1, i2);
                out.i.insert(out.i.end(), { b + i0, b + i1, b + i2 });
            }
        }
    }
}

// ------------------------------------------------------------------ ray cast (grid walk)
bool Room::raycast(V3 o, V3 d, float maxT, RayHit& hit) const {
    d = normalize(d);
    int x = (int)std::floor(o.x), y = (int)std::floor(o.y), z = (int)std::floor(o.z);
    int stx = d.x > 0 ? 1 : -1, sty = d.y > 0 ? 1 : -1, stz = d.z > 0 ? 1 : -1;
    auto tDelta = [](float dd) { return std::fabs(dd) > 1e-9f ? std::fabs(1.f / dd) : 1e30f; };
    auto tFirst = [](float oo, float dd, int cell) {
        if (std::fabs(dd) < 1e-9f) return 1e30f;
        float b = dd > 0 ? (float)(cell + 1) : (float)cell; return (b - oo) / dd;
    };
    float tdx = tDelta(d.x), tdy = tDelta(d.y), tdz = tDelta(d.z);
    float tmx = tFirst(o.x, d.x, x), tmy = tFirst(o.y, d.y, y), tmz = tFirst(o.z, d.z, z);
    int nx = 0, ny = 0, nz = 0; float t = 0;
    for (int it = 0; it < 4096 && t <= maxT; it++) {
        if (solid(x, y, z)) { hit = { x, y, z, nx, ny, nz, t, false }; return true; }
        if (tmx < tmy && tmx < tmz) { x += stx; t = tmx; tmx += tdx; nx = -stx; ny = 0; nz = 0; }
        else if (tmy < tmz) { y += sty; t = tmy; tmy += tdy; nx = 0; ny = -sty; nz = 0; }
        else { z += stz; t = tmz; tmz += tdz; nx = 0; ny = 0; nz = -stz; }
    }
    // nothing hit: use the bottom of the room as a building surface
    if (d.y < -1e-6f) {
        float tp = ((float)oy - o.y) / d.y;
        if (tp > 0 && tp <= maxT) {
            V3 p = o + d * tp; int gx = (int)std::floor(p.x), gz = (int)std::floor(p.z);
            if (inBounds(gx, oy, gz)) { hit = { gx, oy - 1, gz, 0, 1, 0, tp, true }; return true; }
        }
    }
    return false;
}

// ------------------------------------------------------------------ collision
bool Room::boxFree(V3 f, float h, float H) const {
    const float e = 1e-4f;
    for (int y = (int)std::floor(f.y + e); y <= (int)std::floor(f.y + H - e); y++)
        for (int z = (int)std::floor(f.z - h + e); z <= (int)std::floor(f.z + h - e); z++)
            for (int x = (int)std::floor(f.x - h + e); x <= (int)std::floor(f.x + h - e); x++)
                if (solid(x, y, z)) return false;
    return true;
}

V3 Room::moveBox(V3 feet, V3& vel, float h, float H, float dt, bool& onGround) const {
    onGround = false;
    V3 total = vel * dt;
    int steps = std::max(1, (int)std::ceil(std::max(std::fabs(total.x), std::max(std::fabs(total.y), std::fabs(total.z))) / 0.4f));
    V3 step = total * (1.f / steps);
    const float e = 1e-3f;
    for (int s = 0; s < steps; s++) {
        // y first (gravity / landing), then x, then z
        for (int axis : { 1, 0, 2 }) {
            float d = axis == 0 ? step.x : axis == 1 ? step.y : step.z;
            if (d == 0) continue;
            V3 np = feet; (axis == 0 ? np.x : axis == 1 ? np.y : np.z) += d;
            if (boxFree(np, h, H)) { feet = np; continue; }
            // blocked: slide up to the nearest block boundary
            if (axis == 1) {
                if (d < 0) { feet.y = std::floor(np.y) + 1.f + e; if (!boxFree(feet, h, H)) feet.y = np.y - d; onGround = true; }
                else { feet.y = std::floor(np.y + H) - H - e; if (!boxFree(feet, h, H)) feet.y = np.y - d; }
                vel.y = 0; step.y = 0;
            } else {
                float& c = axis == 0 ? feet.x : feet.z; float nc = axis == 0 ? np.x : np.z;
                float old = c;
                c = d > 0 ? std::floor(nc + h) - h - e : std::floor(nc - h) + 1.f + h + e;
                if (!boxFree(feet, h, H)) c = old;
                if (axis == 0) { vel.x = 0; step.x = 0; } else { vel.z = 0; step.z = 0; }
            }
        }
    }
    if (!onGround) { V3 below = feet; below.y -= 0.02f; onGround = !boxFree(below, h, H); }
    return feet;
}

} // namespace px
