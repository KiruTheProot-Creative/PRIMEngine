// Content-code test: builds the scene on any platform and prints a fingerprint that is compared against the JS harness.
#include "../src/scene.h"
#include <cstdio>
int main() {
    px::Scene sc;
    const char* names[] = { "floor","wall","trim","strip","core","door","cannon","cannonglow" };
    for (int m = 0; m < px::M_COUNT; m++) {
        double s = 0; for (auto& v : sc.mats[m].mesh.v) s += v.p[0] + v.p[1] * 2 + v.p[2] * 3 + v.c[0] + v.uv[0];
        printf("mesh %s verts=%zu idx=%zu sum=%.2f\n", names[m], sc.mats[m].mesh.v.size(), sc.mats[m].mesh.i.size(), s);
    }
    const char* tn[] = { "floor","wall","trim","cannon","core","strip","door","env","sprite","block" };
    for (int k = 0; k < px::T_COUNT; k++) { auto t = px::makeTex((px::TexKind)k); unsigned long long s = 0; for (auto b : t) s += b; printf("tex %s sum=%llu\n", tn[k], s); }
    // winding sanity: every triangle's numeric cross() must agree with its stored normal (LH, CW-front)
    size_t bad = 0, tot = 0;
    for (auto& m : sc.mats) for (size_t i = 0; i + 2 < m.mesh.i.size(); i += 3) {
        auto& a = m.mesh.v[m.mesh.i[i]]; auto& b = m.mesh.v[m.mesh.i[i + 1]]; auto& c = m.mesh.v[m.mesh.i[i + 2]];
        px::V3 pa{ a.p[0], a.p[1], a.p[2] }, pb{ b.p[0], b.p[1], b.p[2] }, pc{ c.p[0], c.p[1], c.p[2] };
        px::V3 cr = px::cross(pb - pa, pc - pa); if (px::length(cr) < 1e-9f) continue; tot++;
        px::V3 n{ a.n[0] + b.n[0] + c.n[0], a.n[1] + b.n[1] + c.n[1], a.n[2] + b.n[2] + c.n[2] };
        if (px::dot(cr, n) <= 0) { bad++; if (bad < 9) printf("  bad tri area=%g mat=%d\n", px::length(cr), (int)(&m - sc.mats)); }
    }
    printf("winding: %zu/%zu triangles wrong\n", bad, tot);
    px::Sim sim; sim.fire({ 0, 1.5f, 2 }, { 0, 0, 1 }); for (int i = 0; i < 200; i++) sim.update(1 / 60.f);
    printf("sim ok: %zu particles\n", sim.parts.size());
}
