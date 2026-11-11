// Loads an FBX and checks the importer + animator. Usage: model_test file.fbx
#include "../src/model.h"
#include <cstdio>
using namespace px;
int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: model_test file.fbx\n"); return 2; }
    Model m; std::string err;
    if (!m.loadFBX(argv[1], err)) { printf("LOAD FAILED: %s\n", err.c_str()); return 1; }
    size_t tris = 0; for (auto& x : m.meshes) tris += x.i.size() / 3;
    printf("source: %s | scale %.4f | nodes %zu | meshes %zu | triangles %zu | images %zu\n", m.source.c_str(), m.importScale, m.nodes.size(), m.meshes.size(), tris, m.images.size());
    for (auto& im : m.images) printf("  image %dx%d\n", im.w, im.h);
    printf("bounds (m): min %.3f %.3f %.3f  max %.3f %.3f %.3f\n", m.bmin.x, m.bmin.y, m.bmin.z, m.bmax.x, m.bmax.y, m.bmax.z);
    for (auto& c : m.clips) printf("  clip %-14s %.3f s, %zu tracks\n", ("\"" + c.name + "\"").c_str(), c.duration, c.tracks.size());
    // winding check (engine convention)
    size_t bad = 0, tot = 0;
    for (auto& x : m.meshes) for (size_t i = 0; i + 2 < x.i.size(); i += 3) {
        auto& a = x.v[x.i[i]]; auto& b = x.v[x.i[i + 1]]; auto& c = x.v[x.i[i + 2]];
        V3 pa{ a.p[0], a.p[1], a.p[2] }, pb{ b.p[0], b.p[1], b.p[2] }, pc{ c.p[0], c.p[1], c.p[2] }; V3 cr = cross(pb - pa, pc - pa);
        if (length(cr) < 1e-12f) continue;
        tot++;
        if (dot(cr, V3{ a.n[0] + b.n[0] + c.n[0], a.n[1] + b.n[1] + c.n[1], a.n[2] + b.n[2] + c.n[2] }) <= 0) bad++;
    }
    printf("winding: %zu/%zu wrong\n", bad, tot);
    // bounds across all clips: parts must stay together
    float worst = 0;
    for (size_t c = 0; c < m.clips.size(); c++) for (int f = 0; f <= 20; f++) {
        Pose p = m.restPose(); m.sample((int)c, m.clips[c].duration * f / 20.f, p); std::vector<Mat4> W; m.worldMatrices(p, W);
        for (auto& x : m.meshes) for (auto& v : x.v) { V3 q = transformPoint(W[x.node], { v.p[0], v.p[1], v.p[2] }); V3 ctr = (m.bmin + m.bmax) * 0.5f; worst = std::max(worst, length(q - ctr)); }
    }
    printf("max distance of any vertex from rest centre over all clips: %.3f m (rest half-diagonal %.3f m)\n", worst, length(m.bmax - m.bmin) * 0.5f);
    // transitions: smoothness = biggest change in velocity between frames (second difference), versus the clips alone
    std::vector<ClipSettings> cs(m.clips.size()); for (auto& s : cs) s.loop = true;
    auto acc = [](const Pose& a, const Pose& b, const Pose& c) {
        float mx = 0;
        for (size_t i = 0; i < a.size(); i++) {
            mx = std::max(mx, length(a[i].t - b[i].t * 2.f + c[i].t));
            V3 w1 = qlog(qmul(b[i].r, qconj(a[i].r))), w2 = qlog(qmul(c[i].r, qconj(b[i].r)));
            mx = std::max(mx, length(w2 - w1));
        }
        return mx;
    };
    float base = 0;
    for (size_t a = 0; a < m.clips.size(); a++) {
        Animator an; an.bind(&m, &cs); an.play((int)a, 0); Pose p0 = an.out, p1 = an.out;
        for (int i = 0; i < 120; i++) { an.update(1 / 60.f); if (i > 2) base = std::max(base, acc(p0, p1, an.out)); p0 = p1; p1 = an.out; }
    }
    printf("clips alone      : worst velocity change per frame %.4f\n", base);
    for (BlendMode mode : { BlendMode::Cut, BlendMode::Crossfade, BlendMode::Inertialize }) {
        float worst = 0, worstMid = 0;
        for (size_t a = 0; a < m.clips.size(); a++) for (size_t b = 0; b < m.clips.size(); b++) for (int when : { 7, 23 }) {
            if (a == b) continue;
            Animator an; an.mode = mode; an.bind(&m, &cs); an.play((int)a, 0);
            Pose p0, p1; for (int i = 0; i < when; i++) { an.update(1 / 60.f); p0 = p1; p1 = an.out; }
            an.play((int)b, 0.25f);
            for (int i = 0; i < 30; i++) {
                an.update(1 / 60.f); worst = std::max(worst, acc(p0, p1, an.out)); p0 = p1; p1 = an.out;
                if (i == 5) { an.play((int)a, 0.25f); }   // interrupt the transition half way
            }
        }
        printf("transition %-6s: worst velocity change per frame %.4f (incl. interrupted transitions)\n", mode == BlendMode::Cut ? "cut" : mode == BlendMode::Crossfade ? "fade" : "inert", worst);
        (void)worstMid;
    }
    return 0;
}
