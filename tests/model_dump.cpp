// Dumps posed, world-space triangles of an FBX as a JS file for web/model_check.html (visual import check).
#include "../src/model.h"
#include <cstdio>
using namespace px;
static const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    Model m; std::string err; if (!m.loadFBX(argv[1], err)) { printf("%s\n", err.c_str()); return 1; }
    FILE* f = fopen(argv[2], "w");
    fprintf(f, "window.MODEL={bmin:[%f,%f,%f],bmax:[%f,%f,%f],", m.bmin.x, m.bmin.y, m.bmin.z, m.bmax.x, m.bmax.y, m.bmax.z);
    if (!m.images.empty()) {
        auto& im = m.images[0]; fprintf(f, "img:{w:%d,h:%d,b64:\"", im.w, im.h);
        const auto& d = im.rgba; for (size_t i = 0; i < d.size(); i += 3) {
            unsigned v = d[i] << 16 | (i + 1 < d.size() ? d[i + 1] : 0) << 8 | (i + 2 < d.size() ? d[i + 2] : 0);
            fputc(B64[v >> 18 & 63], f); fputc(B64[v >> 12 & 63], f); fputc(i + 1 < d.size() ? B64[v >> 6 & 63] : '=', f); fputc(i + 2 < d.size() ? B64[v & 63] : '=', f);
        }
        fprintf(f, "\"},");
    }
    fprintf(f, "shots:[");
    struct S { int clip; float frac; } shots[] = { { -1, 0 }, { m.findClip("IDLE"), 0.5f }, { m.findClip("charge"), 1.f }, { m.findClip("Charge-loop"), 0.5f }, { m.findClip("shot"), 0.4f }, { m.findClip("CHarged Shot"), 0.3f } };
    for (auto& s : shots) {
        Pose p = m.restPose(); if (s.clip >= 0) m.sample(s.clip, m.clips[s.clip].duration * s.frac, p);
        std::vector<Mat4> W; m.worldMatrices(p, W);
        fprintf(f, "{label:\"%s\",v:[", s.clip < 0 ? "rest pose" : (m.clips[s.clip].name + " @" + std::to_string((int)(s.frac * 100)) + "%").c_str());
        for (auto& x : m.meshes) for (auto idx : x.i) {
            auto& v = x.v[idx]; V3 q = transformPoint(W[x.node], { v.p[0], v.p[1], v.p[2] });
            V3 n = transformPoint(W[x.node], { v.n[0], v.n[1], v.n[2] }) - transformPoint(W[x.node], { 0, 0, 0 });
            fprintf(f, "%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%.4f,%.4f,", q.x, q.y, q.z, n.x, n.y, n.z, v.uv[0], v.uv[1]);
        }
        fprintf(f, "]},");
    }
    fprintf(f, "]};\n"); fclose(f); return 0;
}
