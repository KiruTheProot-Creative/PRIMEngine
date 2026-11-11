// Room core test: flat room, mesh, ray cast, save/load, collision (walking, falling, jumping, walls).
#include "../src/room.h"
#include <cstdio>
using namespace px;
int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)
int main() {
    Room r; r.newFlat(16);
    CHECK(r.count() == 32 * 32, "flat room has %zu blocks", r.count());
    MeshData m; r.buildMesh(m);
    CHECK(m.i.size() / 6 == 32 * 32 + 4 * 32 + 32 * 32, "flat room: %zu faces (top + rim + bottom)", m.i.size() / 6);
    size_t bad = 0; for (size_t i = 0; i < m.i.size(); i += 3) { auto& a = m.v[m.i[i]]; auto& b = m.v[m.i[i + 1]]; auto& c = m.v[m.i[i + 2]];
        V3 pa{ a.p[0], a.p[1], a.p[2] }, pb{ b.p[0], b.p[1], b.p[2] }, pc{ c.p[0], c.p[1], c.p[2] }; if (dot(cross(pb - pa, pc - pa), V3{ a.n[0], a.n[1], a.n[2] }) <= 0) bad++; }
    CHECK(bad == 0, "winding: %zu wrong", bad);
    // corner shading: a block placed on the floor darkens the floor next to it
    r.set(0, 0, 0, B_CUBE); r.buildMesh(m);
    float darkest = 1; for (auto& v : m.v) if (v.n[1] > 0.5f && v.p[1] == 0.f && v.p[0] >= 1 && v.p[0] <= 1 && v.p[2] >= 0 && v.p[2] <= 1) darkest = std::min(darkest, v.c[0]);
    CHECK(darkest < 0.8f, "floor next to a block is shaded (%.2f)", darkest);
    // ray cast from above
    RayHit h; bool hit = r.raycast({ 0.5f, 5.f, 0.5f }, { 0, -1, 0 }, 50, h);
    CHECK(hit && h.x == 0 && h.y == 0 && h.z == 0 && h.ny == 1, "ray down hits the placed block's top (%d %d %d n %d %d %d)", h.x, h.y, h.z, h.nx, h.ny, h.nz);
    hit = r.raycast({ 3.5f, 0.5f, 0.5f }, { -1, 0, 0 }, 50, h);
    CHECK(hit && h.x == 0 && h.nx == 1, "ray sideways hits its +X face");
    Room e; e.resize(16, 8, 16, -8, -1, -8);
    hit = e.raycast({ 0.5f, 3.f, 0.5f }, { 0.1f, -1, 0 }, 50, h);
    CHECK(hit && h.onGround && h.y + h.ny == -1, "empty room: ray lands on the floor plane (place at y=%d)", h.y + h.ny);
    // save / load
    r.name = "Test \"room\""; CHECK(r.save("/tmp/t.room"), "saved");
    Room r2; std::string err; CHECK(r2.load("/tmp/t.room", err) && r2.cells == r.cells && r2.name == r.name, "loaded identical (%s)", err.c_str());
    // collision: standing, walking into a wall, falling off the edge, jumping onto a block
    V3 feet{ 5.5f, 0.5f, 5.5f }, vel{ 0, 0, 0 }; bool g = false;
    for (int i = 0; i < 60; i++) { vel.y -= 22.f / 60; feet = r.moveBox(feet, vel, 0.35f, 1.9f, 1 / 60.f, g); }
    CHECK(g && std::fabs(feet.y) < 0.01f, "falls and lands on the floor (y=%.3f)", feet.y);
    feet = { 3.5f, 0.f, 0.5f }; vel = { -4, 0, 0 };
    for (int i = 0; i < 60; i++) { vel.x = -4; vel.y -= 22.f / 60; feet = r.moveBox(feet, vel, 0.35f, 1.9f, 1 / 60.f, g); }
    CHECK(std::fabs(feet.x - 1.35f) < 0.01f, "walking into the block stops at its face (x=%.3f)", feet.x);
    vel = { 0, 7.5f, 0 }; float maxY = 0;
    for (int i = 0; i < 90; i++) { vel.x = -4; vel.y -= 22.f / 60; feet = r.moveBox(feet, vel, 0.35f, 1.9f, 1 / 60.f, g); maxY = std::max(maxY, feet.y); }
    CHECK(maxY > 1.f && std::fabs(feet.y) < 0.02f && feet.x < 0, "jumps over a 1-block step (peak %.2f m) and lands past it (x=%.2f)", maxY, feet.x);
    feet = { 15.5f, 0.f, 0.5f }; vel = { 4, 0, 0 };
    for (int i = 0; i < 60; i++) { vel.x = 4; vel.y -= 22.f / 60; feet = r.moveBox(feet, vel, 0.35f, 1.9f, 1 / 60.f, g); }
    CHECK(feet.y < -2.f, "walks off the edge and falls (y=%.2f)", feet.y);
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
