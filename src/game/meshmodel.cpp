#include "meshmodel.h"
#include "../core/json.h"
#include "../core/png.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

// from stb_image (built in png.cpp): unpacks the zlib-packed lists in binary FBX files
extern "C" char* stbi_zlib_decode_malloc_guesssize(const char* buffer, int len, int initial_size, int* outlen);
extern "C" void stbi_image_free(void* retval_from_stbi_load);

namespace fs = std::filesystem;
namespace mc {

// ------------------------------------------------------------------ the model
void MeshModel::update() {
    lo = { 1e30, 1e30, 1e30 }; hi = { -1e30, -1e30, -1e30 };
    for (const MeshVert& v : raw) {
        lo = { std::min(lo.x, (double)v.p[0]), std::min(lo.y, (double)v.p[1]), std::min(lo.z, (double)v.p[2]) };
        hi = { std::max(hi.x, (double)v.p[0]), std::max(hi.y, (double)v.p[1]), std::max(hi.z, (double)v.p[2]) };
    }
    if (raw.empty()) lo = hi = { 0, 0, 0 };
    sx = (hi.x - lo.x) * unitScale; sy = (hi.y - lo.y) * unitScale; sz = (hi.z - lo.z) * unitScale;
    pivot = { sx * 0.5, 0, sz * 0.5 };
    transparent = false;
    for (size_t i = 3; i < tex.size(); i += 4) if (tex[i] < 128) { transparent = true; break; }
}

template <class T> static void put(std::ofstream& f, const T& v) { f.write((const char*)&v, sizeof v); }
template <class T> static bool get(std::ifstream& f, T& v) { return (bool)f.read((char*)&v, sizeof v); }
static void putStr(std::ofstream& f, const std::string& s) { uint32_t n = (uint32_t)s.size(); put(f, n); f.write(s.data(), n); }
static bool getStr(std::ifstream& f, std::string& s) { uint32_t n; if (!get(f, n) || n > (1u << 20)) return false; s.resize(n); return (bool)f.read(&s[0], n); }

bool MeshModel::save(const std::string& path) const {
    std::ofstream f(fs::u8path(path), std::ios::binary); if (!f) return false;
    f.write("PMSH", 4); put(f, (uint32_t)3); putStr(f, name); putStr(f, source); put(f, unitScale);
    put(f, (uint32_t)raw.size()); f.write((const char*)raw.data(), raw.size() * sizeof(MeshVert));
    put(f, (uint32_t)idx.size()); f.write((const char*)idx.data(), idx.size() * 4);
    put(f, (int32_t)tw); put(f, (int32_t)th); f.write((const char*)tex.data(), tex.size());
    put(f, (uint32_t)parts.size()); for (const MeshPart& p : parts) { putStr(f, p.name); f.write((const char*)p.parent, sizeof p.parent); for (const double* a : { p.t, p.r, p.s, p.pre, p.pivot }) f.write((const char*)a, 24); }
    put(f, (uint32_t)vpart.size()); f.write((const char*)vpart.data(), vpart.size() * 2);
    put(f, (uint32_t)clips.size());
    for (const AnimClip& c : clips) {
        putStr(f, c.name); put(f, c.length); put(f, (uint32_t)c.tracks.size());
        for (const AnimTrack& t : c.tracks) { put(f, (int32_t)t.part); put(f, (int32_t)t.prop); put(f, (int32_t)t.axis); put(f, (uint32_t)t.time.size()); f.write((const char*)t.time.data(), t.time.size() * 4); f.write((const char*)t.value.data(), t.value.size() * 4); }
    }
    for (const MeshPart& p : parts) { put(f, (int32_t)p.parentPart); put(f, (int32_t)p.order); }   // v3: parts inside moving parts
    return (bool)f;
}
bool MeshModel::load(const std::string& path) {
    std::ifstream f(fs::u8path(path), std::ios::binary); if (!f) return false;
    char m[4]; uint32_t ver = 0, nv = 0, ni = 0; int32_t w = 0, h = 0;
    if (!f.read(m, 4) || std::memcmp(m, "PMSH", 4) || !get(f, ver) || ver < 1 || ver > 3) return false;
    if (!getStr(f, name) || !getStr(f, source) || !get(f, unitScale) || !get(f, nv) || nv > (1u << 24)) return false;
    raw.resize(nv); if (!f.read((char*)raw.data(), nv * sizeof(MeshVert))) return false;
    if (!get(f, ni) || ni > (1u << 26)) return false;
    idx.resize(ni); if (!f.read((char*)idx.data(), ni * 4)) return false;
    if (!get(f, w) || !get(f, h) || w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;
    tw = w; th = h; tex.resize((size_t)w * h * 4); if (!f.read((char*)tex.data(), tex.size())) return false;
    for (uint32_t i : idx) if (i >= nv) return false;
    parts.clear(); vpart.clear(); clips.clear();
    if (ver >= 2) {
        uint32_t np = 0; if (!get(f, np) || np > 65535) return false;
        parts.resize(np);
        for (MeshPart& p : parts) { if (!getStr(f, p.name) || !f.read((char*)p.parent, sizeof p.parent)) return false; for (double* a : { p.t, p.r, p.s, p.pre, p.pivot }) if (!f.read((char*)a, 24)) return false; }
        uint32_t nvp = 0; if (!get(f, nvp) || (nvp && nvp != nv)) return false; vpart.resize(nvp); if (nvp && !f.read((char*)vpart.data(), nvp * 2)) return false;
        uint32_t nc = 0; if (!get(f, nc) || nc > 4096) return false; clips.resize(nc);
        for (AnimClip& c : clips) {
            uint32_t nt = 0; if (!getStr(f, c.name) || !get(f, c.length) || !get(f, nt) || nt > 65535) return false; c.tracks.resize(nt);
            for (AnimTrack& t : c.tracks) {
                int32_t a, b, d; uint32_t n; if (!get(f, a) || !get(f, b) || !get(f, d) || !get(f, n) || n > (1u << 20)) return false;
                t.part = a; t.prop = b; t.axis = d; t.time.resize(n); t.value.resize(n);
                if (!f.read((char*)t.time.data(), n * 4) || !f.read((char*)t.value.data(), n * 4)) return false;
            }
        }
        if (ver >= 3) for (MeshPart& p : parts) { int32_t a = -1, b = 0; if (!get(f, a) || !get(f, b)) return false; p.parentPart = a >= 0 && a < (int)parts.size() ? a : -1; p.order = std::clamp((int)b, 0, 5); }
    }
    update(); return true;
}

bool isModelFile(const std::string& path) {
    std::string e = fs::u8path(path).extension().string(); for (auto& c : e) c = (char)std::tolower((unsigned char)c);
    return e == ".bbmodel" || e == ".obj" || e == ".gltf" || e == ".glb" || e == ".fbx";
}

// ------------------------------------------------------------------ building: triangles + their pictures, then one texture
namespace {
struct Img { int w = 1, h = 1; std::vector<uint8_t> px{ 255, 255, 255, 255 }; };
Img solid(float r, float g, float b, float a) { Img i; i.px = { (uint8_t)std::lround(std::clamp(r, 0.f, 1.f) * 255), (uint8_t)std::lround(std::clamp(g, 0.f, 1.f) * 255), (uint8_t)std::lround(std::clamp(b, 0.f, 1.f) * 255), (uint8_t)std::lround(std::clamp(a, 0.f, 1.f) * 255) }; return i; }

struct Builder {
    std::vector<MeshVert> v; std::vector<uint32_t> i; std::vector<int> img;   // img: per vertex
    std::vector<Img> imgs;
    // ref: the direction the triangle faces (null: use the vertex normals); vertex normals of zero get the face's
    void tri(MeshVert a, MeshVert b, MeshVert c, int image, const float* ref = nullptr) {
        float e1[3] = { b.p[0] - a.p[0], b.p[1] - a.p[1], b.p[2] - a.p[2] }, e2[3] = { c.p[0] - a.p[0], c.p[1] - a.p[1], c.p[2] - a.p[2] };
        float cr[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
        float len = std::sqrt(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]); if (len < 1e-12f) return;   // no area
        float r[3] = { 0, 0, 0 };
        if (ref) { r[0] = ref[0]; r[1] = ref[1]; r[2] = ref[2]; }
        else for (int k = 0; k < 3; k++) r[k] = a.n[k] + b.n[k] + c.n[k];
        if (r[0] * r[0] + r[1] * r[1] + r[2] * r[2] < 1e-12f) { r[0] = cr[0]; r[1] = cr[1]; r[2] = cr[2]; }
        bool flip = cr[0] * r[0] + cr[1] * r[1] + cr[2] * r[2] < 0;
        if (flip) { std::swap(b, c); for (float& x : cr) x = -x; }
        for (MeshVert* q : { &a, &b, &c }) if (q->n[0] * q->n[0] + q->n[1] * q->n[1] + q->n[2] * q->n[2] < 1e-12f) for (int k = 0; k < 3; k++) q->n[k] = cr[k] / len;
        for (const MeshVert& q : { a, b, c }) { i.push_back((uint32_t)v.size()); v.push_back(q); img.push_back(image); }
    }
    void finish(MeshModel& m) {
        m.raw = v; m.idx = i;
        if (imgs.empty()) imgs.push_back(Img{});
        if (imgs.size() == 1) { m.tw = imgs[0].w; m.th = imgs[0].h; m.tex = imgs[0].px; m.update(); return; }
        // several pictures: stack them into one (texture coordinates are moved to match)
        int W = 1, H = 0; std::vector<int> y0;
        for (const Img& im : imgs) { y0.push_back(H); W = std::max(W, im.w); H += im.h; }
        m.tw = W; m.th = H; m.tex.assign((size_t)W * H * 4, 0);
        for (size_t k = 0; k < imgs.size(); k++)
            for (int y = 0; y < imgs[k].h; y++) std::memcpy(&m.tex[((size_t)(y0[k] + y) * W) * 4], &imgs[k].px[(size_t)y * imgs[k].w * 4], (size_t)imgs[k].w * 4);
        for (size_t q = 0; q < m.raw.size(); q++) {
            const Img& im = imgs[std::clamp(img[q], 0, (int)imgs.size() - 1)]; int k = std::clamp(img[q], 0, (int)imgs.size() - 1);
            float u = std::clamp(m.raw[q].uv[0], 0.f, 1.f), vv = std::clamp(m.raw[q].uv[1], 0.f, 1.f);
            // keep half a pixel away from the edge of each picture so neighbours do not bleed in
            u = (0.5f + u * (im.w - 1)) / W; vv = (y0[k] + 0.5f + vv * (im.h - 1)) / H;
            m.raw[q].uv[0] = u; m.raw[q].uv[1] = vv;
        }
        m.update();
    }
};

std::string readFile(const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
std::vector<uint8_t> dataUri(const std::string& uri) {   // data:...;base64,....
    size_t c = uri.find(','); if (c == std::string::npos) return {};
    return base64Decode(uri.substr(c + 1));
}
bool imageFrom(const std::vector<uint8_t>& bytes, Img& out) { return readImageMemory(bytes.data(), bytes.size(), out.w, out.h, out.px); }

// a 3 x 3 rotation (row major) from degrees around x, y, z in Blockbench's order (z, then y, then x)
void eulerZYX(const double deg[3], double m[9]) {
    double a = deg[0] * 3.14159265358979 / 180, b = deg[1] * 3.14159265358979 / 180, c = deg[2] * 3.14159265358979 / 180;
    double ca = std::cos(a), sa = std::sin(a), cb = std::cos(b), sb = std::sin(b), cc = std::cos(c), sc = std::sin(c);
    double X[9] = { 1, 0, 0, 0, ca, -sa, 0, sa, ca }, Y[9] = { cb, 0, sb, 0, 1, 0, -sb, 0, cb }, Z[9] = { cc, -sc, 0, sc, cc, 0, 0, 0, 1 };
    auto mul = [](const double* A, const double* B, double* C) { for (int r = 0; r < 3; r++) for (int k = 0; k < 3; k++) { C[r * 3 + k] = 0; for (int j = 0; j < 3; j++) C[r * 3 + k] += A[r * 3 + j] * B[j * 3 + k]; } };
    double ZY[9]; mul(Z, Y, ZY); mul(ZY, X, m);   // Three.js order "ZYX": matrix = Rz * Ry * Rx
}
} // namespace

// ------------------------------------------------------------------ Blockbench (.bbmodel): cubes with six textured faces
static bool importBbmodel(const fs::path& path, MeshModel& m, std::string& err) {
    Json j; if (!parseJson(readFile(path), j, &err)) { err = "This Blockbench file could not be read (" + err + ")."; return false; }
    Builder B;
    double resW = j["resolution"]["width"].num(16), resH = j["resolution"]["height"].num(16);
    std::vector<std::pair<double, double>> uvSize;   // per texture: the size its face coordinates are counted in
    for (size_t t = 0; t < j["textures"].size(); t++) {
        const Json& tx = j["textures"][t]; Img im;
        if (tx["source"].str().rfind("data:", 0) == 0) imageFrom(dataUri(tx["source"].str()), im);
        else if (!tx["path"].str().empty()) { std::string bytes = readFile(fs::u8path(tx["path"].str())); imageFrom(std::vector<uint8_t>(bytes.begin(), bytes.end()), im); }
        B.imgs.push_back(im);
        uvSize.push_back({ tx["uv_width"].num(resW), tx["uv_height"].num(resH) });
    }
    int cubes = 0, skipped = 0;
    for (size_t e = 0; e < j["elements"].size(); e++) {
        const Json& el = j["elements"][e];
        if (el.has("type") && el["type"].str() != "cube") { skipped++; continue; }
        if (el.has("visibility") && el["visibility"].type == Json::Bool && !el["visibility"].b) continue;
        double inf = el["inflate"].num(0);
        double f[3], t[3], o[3], rot[3] = { 0, 0, 0 };
        for (int k = 0; k < 3; k++) { f[k] = el["from"][k].num() - inf; t[k] = el["to"][k].num() + inf; o[k] = el["origin"][k].num(); }
        if (el["rotation"].type == Json::Array) for (int k = 0; k < 3; k++) rot[k] = el["rotation"][k].num();
        else if (el["rotation"].type == Json::Object) {   // older files: one angle around one axis
            std::string ax = el["rotation"]["axis"].str(); int k = ax == "x" ? 0 : ax == "z" ? 2 : 1; rot[k] = el["rotation"]["angle"].num();
            for (int q = 0; q < 3; q++) o[q] = el["rotation"]["origin"][q].num(o[q]);
        }
        double R[9]; eulerZYX(rot, R);
        auto P = [&](double x, double y, double z) {
            double d[3] = { x - o[0], y - o[1], z - o[2] }, r[3];
            for (int a = 0; a < 3; a++) r[a] = R[a * 3] * d[0] + R[a * 3 + 1] * d[1] + R[a * 3 + 2] * d[2];
            return std::array<float, 3>{ (float)(r[0] + o[0]), (float)(r[1] + o[1]), (float)(r[2] + o[2]) };
        };
        auto N = [&](double x, double y, double z) { return std::array<float, 3>{ (float)(R[0] * x + R[1] * y + R[2] * z), (float)(R[3] * x + R[4] * y + R[5] * z), (float)(R[6] * x + R[7] * y + R[8] * z) }; };
        // corners of each side seen from outside: top-left, top-right, bottom-right, bottom-left
        struct Side { const char* name; double c[4][3]; double n[3]; };
        const Side sides[6] = {
            { "north", { { t[0], t[1], f[2] }, { f[0], t[1], f[2] }, { f[0], f[1], f[2] }, { t[0], f[1], f[2] } }, { 0, 0, -1 } },
            { "south", { { f[0], t[1], t[2] }, { t[0], t[1], t[2] }, { t[0], f[1], t[2] }, { f[0], f[1], t[2] } }, { 0, 0, 1 } },
            { "east", { { t[0], t[1], t[2] }, { t[0], t[1], f[2] }, { t[0], f[1], f[2] }, { t[0], f[1], t[2] } }, { 1, 0, 0 } },
            { "west", { { f[0], t[1], f[2] }, { f[0], t[1], t[2] }, { f[0], f[1], t[2] }, { f[0], f[1], f[2] } }, { -1, 0, 0 } },
            { "up", { { f[0], t[1], f[2] }, { t[0], t[1], f[2] }, { t[0], t[1], t[2] }, { f[0], t[1], t[2] } }, { 0, 1, 0 } },
            { "down", { { f[0], f[1], t[2] }, { t[0], f[1], t[2] }, { t[0], f[1], f[2] }, { f[0], f[1], f[2] } }, { 0, -1, 0 } },
        };
        for (const Side& sd : sides) {
            const Json& face = el["faces"][sd.name];
            if (face.isNull() || face["texture"].isNull() || face["texture"].type == Json::Bool) continue;
            int ti = (int)face["texture"].num(0);
            std::pair<double, double> us = ti >= 0 && ti < (int)uvSize.size() ? uvSize[ti] : std::make_pair(resW, resH);
            double u1 = face["uv"][0].num() / us.first, v1 = face["uv"][1].num() / us.second, u2 = face["uv"][2].num() / us.first, v2 = face["uv"][3].num() / us.second;
            double uv[4][2] = { { u1, v1 }, { u2, v1 }, { u2, v2 }, { u1, v2 } };
            int k = ((int)std::lround(face["rotation"].num(0) / 90) % 4 + 4) % 4;
            auto nn = N(sd.n[0], sd.n[1], sd.n[2]);
            MeshVert q[4];
            for (int c = 0; c < 4; c++) {
                auto p = P(sd.c[c][0], sd.c[c][1], sd.c[c][2]); const double* w = uv[(c - k + 4) % 4];
                q[c] = { { p[0], p[1], p[2] }, { (float)w[0], (float)w[1] }, { nn[0], nn[1], nn[2] } };
            }
            float ref[3] = { nn[0], nn[1], nn[2] };
            B.tri(q[0], q[3], q[2], ti, ref); B.tri(q[0], q[2], q[1], ti, ref);
        }
        cubes++;
    }
    if (!cubes) { err = skipped ? "This Blockbench model only has \"mesh\" parts. Only cube models can be imported so far." : "This Blockbench file has no cubes in it."; return false; }
    B.finish(m); m.unitScale = 1.0 / 16; m.update();
    return true;
}

// ------------------------------------------------------------------ Wavefront .obj (+ .mtl)
static bool importObj(const fs::path& path, MeshModel& m, std::string& err) {
    std::ifstream f(path); if (!f) { err = "The file could not be opened."; return false; }
    Builder B; std::vector<std::array<float, 3>> P, N; std::vector<std::array<float, 2>> T;
    std::map<std::string, int> matImg; int cur = -1;
    auto loadMtl = [&](const fs::path& mp) {
        std::ifstream mf(mp); std::string line, name; float kd[3] = { 1, 1, 1 }; bool hasMap = false;
        auto close = [&]() { if (!name.empty() && !matImg.count(name)) { matImg[name] = (int)B.imgs.size(); B.imgs.push_back(solid(kd[0], kd[1], kd[2], 1)); } };
        while (std::getline(mf, line)) {
            std::istringstream l(line); std::string k; l >> k;
            if (k == "newmtl") { close(); l >> std::ws; std::getline(l, name); kd[0] = kd[1] = kd[2] = 1; hasMap = false; }
            else if (k == "Kd") l >> kd[0] >> kd[1] >> kd[2];
            else if (k == "map_Kd" && !name.empty()) {
                std::string rest; std::getline(l, rest); std::string file = rest.substr(rest.find_first_not_of(' '));
                if (file.find(' ') != std::string::npos && file[0] == '-') file = file.substr(file.rfind(' ') + 1);   // options before the name
                Img im; int w, h; std::vector<uint8_t> px;
                if (readImage((mp.parent_path() / fs::u8path(file)).string(), w, h, px)) { im.w = w; im.h = h; im.px = px; matImg[name] = (int)B.imgs.size(); B.imgs.push_back(im); hasMap = true; }
            }
        }
        (void)hasMap; close();
    };
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream l(line); std::string k; l >> k;
        if (k == "v") { std::array<float, 3> p{}; l >> p[0] >> p[1] >> p[2]; P.push_back(p); }
        else if (k == "vt") { std::array<float, 2> t{}; l >> t[0] >> t[1]; T.push_back({ t[0], 1 - t[1] }); }   // .obj counts from the bottom of the picture
        else if (k == "vn") { std::array<float, 3> n{}; l >> n[0] >> n[1] >> n[2]; N.push_back(n); }
        else if (k == "mtllib") { std::string mf; l >> std::ws; std::getline(l, mf); loadMtl(path.parent_path() / fs::u8path(mf)); }
        else if (k == "usemtl") { std::string mn; l >> std::ws; std::getline(l, mn); auto it = matImg.find(mn); cur = it == matImg.end() ? -1 : it->second; }
        else if (k == "f") {
            std::vector<MeshVert> poly; std::string tok;
            while (l >> tok) {
                int vi = 0, ti = 0, ni = 0; size_t a = tok.find('/'), b = a == std::string::npos ? std::string::npos : tok.find('/', a + 1);
                vi = std::atoi(tok.substr(0, a).c_str());
                if (a != std::string::npos) { std::string ts = tok.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1); if (!ts.empty()) ti = std::atoi(ts.c_str()); }
                if (b != std::string::npos) ni = std::atoi(tok.substr(b + 1).c_str());
                auto fix = [](int x, size_t n) { return x < 0 ? (int)n + x : x - 1; };
                vi = fix(vi, P.size()); ti = ti ? fix(ti, T.size()) : -1; ni = ni ? fix(ni, N.size()) : -1;
                if (vi < 0 || vi >= (int)P.size()) continue;
                MeshVert mv{ { P[vi][0], P[vi][1], P[vi][2] }, { 0, 0 }, { 0, 0, 0 } };
                if (ti >= 0 && ti < (int)T.size()) { mv.uv[0] = T[ti][0]; mv.uv[1] = T[ti][1]; }
                if (ni >= 0 && ni < (int)N.size()) { mv.n[0] = N[ni][0]; mv.n[1] = N[ni][1]; mv.n[2] = N[ni][2]; }
                poly.push_back(mv);
            }
            for (size_t q = 1; q + 1 < poly.size(); q++) B.tri(poly[0], poly[q], poly[q + 1], cur < 0 ? 0 : cur);
        }
    }
    if (B.i.empty()) { err = "There are no faces in this .obj file."; return false; }
    if (B.imgs.empty()) B.imgs.push_back(Img{});
    B.finish(m); m.unitScale = 1; m.update();
    return true;
}

// ------------------------------------------------------------------ glTF 2.0 (.gltf / .glb)
static bool importGltf(const fs::path& path, MeshModel& m, std::string& err) {
    std::string file = readFile(path); if (file.empty()) { err = "The file could not be opened."; return false; }
    std::string jsonText; std::vector<uint8_t> bin;
    if (file.size() >= 12 && !std::memcmp(file.data(), "glTF", 4)) {   // .glb: header, a JSON chunk, a binary chunk
        size_t pos = 12;
        while (pos + 8 <= file.size()) {
            uint32_t len, type; std::memcpy(&len, &file[pos], 4); std::memcpy(&type, &file[pos + 4], 4); pos += 8;
            if (pos + len > file.size()) break;
            if (type == 0x4E4F534A) jsonText.assign(file, pos, len); else if (type == 0x004E4942) bin.assign(file.begin() + pos, file.begin() + pos + len);
            pos += (len + 3) & ~3u;
        }
    } else jsonText = file;
    Json j; if (!parseJson(jsonText, j, &err)) { err = "This glTF file could not be read (" + err + ")."; return false; }
    std::vector<std::vector<uint8_t>> buffers;
    for (size_t b = 0; b < j["buffers"].size(); b++) {
        const std::string& uri = j["buffers"][b]["uri"].str();
        if (uri.empty()) buffers.push_back(bin);
        else if (uri.rfind("data:", 0) == 0) buffers.push_back(dataUri(uri));
        else { std::string d = readFile(path.parent_path() / fs::u8path(uri)); buffers.emplace_back(d.begin(), d.end()); }
    }
    auto viewBytes = [&](int vi, size_t& size) -> const uint8_t* {
        const Json& bv = j["bufferViews"][vi]; int b = (int)bv["buffer"].num(-1);
        if (b < 0 || b >= (int)buffers.size()) return nullptr;
        size_t off = (size_t)bv["byteOffset"].num(0); size = (size_t)bv["byteLength"].num(0);
        if (off + size > buffers[b].size()) return nullptr;
        return buffers[b].data() + off;
    };
    // an accessor as floats (count x components); integers become floats (normalized ones 0..1)
    auto accessor = [&](int ai, int& comps) -> std::vector<float> {
        std::vector<float> out; const Json& a = j["accessors"][ai]; if (a.isNull()) return out;
        const std::string& ty = a["type"].str(); comps = ty == "SCALAR" ? 1 : ty == "VEC2" ? 2 : ty == "VEC3" ? 3 : ty == "VEC4" ? 4 : 0;
        int ct = (int)a["componentType"].num(); size_t cnt = (size_t)a["count"].num(); bool norm = a["normalized"].type == Json::Bool && a["normalized"].b;
        int cs = ct == 5126 || ct == 5125 ? 4 : ct == 5123 || ct == 5122 ? 2 : 1;
        size_t vsize = 0; const uint8_t* base = a.has("bufferView") ? viewBytes((int)a["bufferView"].num(), vsize) : nullptr;
        if (!base || !comps) return out;
        size_t stride = (size_t)j["bufferViews"][(size_t)a["bufferView"].num()]["byteStride"].num(0); if (!stride) stride = (size_t)cs * comps;
        size_t off = (size_t)a["byteOffset"].num(0);
        if (cnt && off + (cnt - 1) * stride + (size_t)cs * comps > vsize) return out;
        out.resize(cnt * comps);
        for (size_t i = 0; i < cnt; i++) for (int c = 0; c < comps; c++) {
            const uint8_t* p = base + off + i * stride + (size_t)c * cs; float v = 0;
            switch (ct) {
            case 5126: std::memcpy(&v, p, 4); break;
            case 5125: { uint32_t x; std::memcpy(&x, p, 4); v = (float)x; break; }
            case 5123: { uint16_t x; std::memcpy(&x, p, 2); v = norm ? x / 65535.f : x; break; }
            case 5122: { int16_t x; std::memcpy(&x, p, 2); v = norm ? std::max(x / 32767.f, -1.f) : x; break; }
            case 5121: v = norm ? *p / 255.f : *p; break;
            case 5120: v = norm ? std::max(*(const int8_t*)p / 127.f, -1.f) : *(const int8_t*)p; break;
            }
            out[i * comps + c] = v;
        }
        return out;
    };
    Builder B; std::map<int, int> matImg;
    auto materialImage = [&](int mi) -> int {
        auto it = matImg.find(mi); if (it != matImg.end()) return it->second;
        const Json& pbr = j["materials"][mi]["pbrMetallicRoughness"]; Img im; bool got = false;
        const Json& bt = pbr["baseColorTexture"];
        if (!bt.isNull()) {
            const Json& img = j["images"][(size_t)j["textures"][(size_t)bt["index"].num()]["source"].num(-1)];
            std::vector<uint8_t> bytes;
            if (img.has("bufferView")) { size_t n = 0; const uint8_t* p = viewBytes((int)img["bufferView"].num(), n); if (p) bytes.assign(p, p + n); }
            else if (img["uri"].str().rfind("data:", 0) == 0) bytes = dataUri(img["uri"].str());
            else if (!img["uri"].str().empty()) { std::string d = readFile(path.parent_path() / fs::u8path(img["uri"].str())); bytes.assign(d.begin(), d.end()); }
            got = imageFrom(bytes, im);
        }
        if (!got) { const Json& f = pbr["baseColorFactor"]; im = solid((float)f[0].num(1), (float)f[1].num(1), (float)f[2].num(1), (float)f[3].num(1)); }
        int id = (int)B.imgs.size(); B.imgs.push_back(im); matImg[mi] = id; return id;
    };
    using M4 = std::array<double, 16>;   // column-major, like glTF
    auto mul = [](const M4& a, const M4& b) { M4 c{}; for (int r = 0; r < 4; r++) for (int k = 0; k < 4; k++) { double s = 0; for (int q = 0; q < 4; q++) s += a[q * 4 + r] * b[k * 4 + q]; c[k * 4 + r] = s; } return c; };
    std::function<void(int, const M4&, int)> node = [&](int ni, const M4& parent, int depth) {
        if (depth > 64) return;
        const Json& nd = j["nodes"][ni]; M4 local{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        if (nd["matrix"].size() == 16) for (int k = 0; k < 16; k++) local[k] = nd["matrix"][k].num();
        else {
            double tx = nd["translation"][0].num(0), ty = nd["translation"][1].num(0), tz = nd["translation"][2].num(0);
            double qx = nd["rotation"][0].num(0), qy = nd["rotation"][1].num(0), qz = nd["rotation"][2].num(0), qw = nd["rotation"][3].num(1);
            double sx = nd["scale"][0].num(1), sy = nd["scale"][1].num(1), sz = nd["scale"][2].num(1);
            double r[9] = { 1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy + qz * qw), 2 * (qx * qz - qy * qw),
                            2 * (qx * qy - qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz + qx * qw),
                            2 * (qx * qz + qy * qw), 2 * (qy * qz - qx * qw), 1 - 2 * (qx * qx + qy * qy) };   // columns
            local = { r[0] * sx, r[1] * sx, r[2] * sx, 0, r[3] * sy, r[4] * sy, r[5] * sy, 0, r[6] * sz, r[7] * sz, r[8] * sz, 0, tx, ty, tz, 1 };
        }
        M4 w = mul(parent, local);
        if (nd.has("mesh")) {
            const Json& mesh = j["meshes"][(size_t)nd["mesh"].num()];
            for (size_t pi = 0; pi < mesh["primitives"].size(); pi++) {
                const Json& pr = mesh["primitives"][pi];
                if ((int)pr["mode"].num(4) != 4) continue;   // triangles only
                int pc = 0, nc = 0, tc = 0, ic = 0;
                std::vector<float> pos = accessor((int)pr["attributes"]["POSITION"].num(-1), pc);
                if (pc != 3 || pos.empty()) continue;
                std::vector<float> nor = pr["attributes"].has("NORMAL") ? accessor((int)pr["attributes"]["NORMAL"].num(), nc) : std::vector<float>{};
                std::vector<float> uv = pr["attributes"].has("TEXCOORD_0") ? accessor((int)pr["attributes"]["TEXCOORD_0"].num(), tc) : std::vector<float>{};
                size_t nv = pos.size() / 3;
                std::vector<uint32_t> ind;
                if (pr.has("indices")) { std::vector<float> f = accessor((int)pr["indices"].num(), ic); for (float x : f) ind.push_back((uint32_t)x); }
                else for (uint32_t q = 0; q < nv; q++) ind.push_back(q);
                int image = pr.has("material") ? materialImage((int)pr["material"].num()) : -1;
                if (image < 0) { image = (int)B.imgs.size(); B.imgs.push_back(Img{}); }
                auto vert = [&](uint32_t q) {
                    MeshVert v{};
                    double x = pos[q * 3], y = pos[q * 3 + 1], z = pos[q * 3 + 2];
                    for (int a = 0; a < 3; a++) v.p[a] = (float)(w[a] * x + w[4 + a] * y + w[8 + a] * z + w[12 + a]);
                    if (nc == 3 && q * 3 + 2 < nor.size()) { double nx = nor[q * 3], ny = nor[q * 3 + 1], nz = nor[q * 3 + 2]; double t[3]; for (int a = 0; a < 3; a++) t[a] = w[a] * nx + w[4 + a] * ny + w[8 + a] * nz; double l = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]); if (l > 0) for (int a = 0; a < 3; a++) v.n[a] = (float)(t[a] / l); }
                    if (tc == 2 && q * 2 + 1 < uv.size()) { v.uv[0] = uv[q * 2]; v.uv[1] = uv[q * 2 + 1]; }
                    return v;
                };
                for (size_t t = 0; t + 2 < ind.size(); t += 3) if (ind[t] < nv && ind[t + 1] < nv && ind[t + 2] < nv) B.tri(vert(ind[t]), vert(ind[t + 1]), vert(ind[t + 2]), image);
            }
        }
        for (size_t c = 0; c < nd["children"].size(); c++) node((int)nd["children"][c].num(), w, depth + 1);
    };
    M4 id{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    const Json& scene = j["scenes"][(size_t)j["scene"].num(0)];
    if (!scene.isNull()) for (size_t n = 0; n < scene["nodes"].size(); n++) node((int)scene["nodes"][n].num(), id, 0);
    else for (size_t n = 0; n < j["nodes"].size(); n++) node((int)n, id, 0);
    if (B.i.empty()) { err = "There are no triangles in this glTF file."; return false; }
    B.finish(m); m.unitScale = 1; m.update();   // glTF counts in metres: 1 metre = 1 block
    return true;
}

#include "fbx_import.inl"

// ------------------------------------------------------------------ animations: move the parts
int MeshModel::findClip(const std::string& n) const {
    auto low = [](std::string t) { for (auto& c : t) c = (char)std::tolower((unsigned char)c); return t; };
    for (size_t i = 0; i < clips.size(); i++) if (low(clips[i].name) == low(n)) return (int)i;
    return -1;
}
static fbx::M4 inverseAffine(const fbx::M4& m) {
    // 3 x 3 inverse (cofactors), then the move
    double a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g, det = a * A + b * B + c * C;
    fbx::M4 r = fbx::ident(); if (std::fabs(det) < 1e-12) return r;
    double inv[9] = { A / det, -(b * i - c * h) / det, (b * f - c * e) / det, B / det, (a * i - c * g) / det, -(a * f - c * d) / det, C / det, -(a * h - b * g) / det, (a * e - b * d) / det };
    for (int row = 0; row < 3; row++) for (int col = 0; col < 3; col++) r[col * 4 + row] = inv[row * 3 + col];
    for (int row = 0; row < 3; row++) r[12 + row] = -(r[row] * m[12] + r[4 + row] * m[13] + r[8 + row] * m[14]);
    return r;
}
static float sampleTrack(const AnimTrack& t, float time) {
    if (t.time.empty()) return 0;
    if (time <= t.time.front()) return t.value.front();
    if (time >= t.time.back()) return t.value.back();
    size_t k = std::upper_bound(t.time.begin(), t.time.end(), time) - t.time.begin();
    float a = t.time[k - 1], b = t.time[k], f = b > a ? (time - a) / (b - a) : 0;
    return t.value[k - 1] + (t.value[k] - t.value[k - 1]) * f;
}
std::vector<MeshVert> MeshModel::posed(int clip, float time) const {
    std::vector<MeshVert> out = raw;
    if (clip < 0 || clip >= (int)clips.size() || parts.empty() || vpart.size() != raw.size()) return out;
    std::vector<fbx::M4> moveBy(parts.size()); std::vector<char> done(parts.size(), 0);
    std::function<void(size_t, int)> solve = [&](size_t p, int depth) {   // a part moves by its own animation, on top of the part it hangs from
        if (done[p]) return;
        const MeshPart& mp = parts[p];
        int pp = mp.parentPart >= 0 && mp.parentPart < (int)parts.size() && mp.parentPart != (int)p && depth < 64 ? mp.parentPart : -1;
        if (pp >= 0) solve((size_t)pp, depth + 1);
        double now[3][3] = { { mp.t[0], mp.t[1], mp.t[2] }, { mp.r[0], mp.r[1], mp.r[2] }, { mp.s[0], mp.s[1], mp.s[2] } };
        for (const AnimTrack& t : clips[clip].tracks) if (t.part == (int)p && t.prop >= 0 && t.prop < 3 && t.axis >= 0 && t.axis < 3) now[t.prop][t.axis] = sampleTrack(t, time);
        auto local = [&](const double T[3], const double R[3], const double S[3]) {
            V3 piv{ mp.pivot[0], mp.pivot[1], mp.pivot[2] };
            return fbx::mul(fbx::trans({ T[0], T[1], T[2] }), fbx::mul(fbx::trans(piv), fbx::mul(fbx::euler({ mp.pre[0], mp.pre[1], mp.pre[2] }, 0),
                   fbx::mul(fbx::euler({ R[0], R[1], R[2] }, mp.order), fbx::mul(fbx::trans(piv * -1), fbx::scal({ S[0], S[1], S[2] }))))));
        };
        fbx::M4 parent; std::copy(mp.parent, mp.parent + 16, parent.begin());
        fbx::M4 rest = fbx::mul(parent, local(mp.t, mp.r, mp.s)), at = fbx::mul(parent, local(now[0], now[1], now[2]));
        if (pp >= 0) at = fbx::mul(moveBy[pp], at);   // the parent's motion carries it
        moveBy[p] = fbx::mul(at, inverseAffine(rest)); done[p] = 1;
    };
    for (size_t p = 0; p < parts.size(); p++) solve(p, 0);
    for (size_t v = 0; v < out.size(); v++) {
        uint16_t p = vpart[v]; if (p >= parts.size()) continue;
        V3 q = fbx::point(moveBy[p], { out[v].p[0], out[v].p[1], out[v].p[2] }); V3 n = fbx::dir(moveBy[p], { out[v].n[0], out[v].n[1], out[v].n[2] });
        out[v].p[0] = (float)q.x; out[v].p[1] = (float)q.y; out[v].p[2] = (float)q.z; out[v].n[0] = (float)n.x; out[v].n[1] = (float)n.y; out[v].n[2] = (float)n.z;
    }
    return out;
}

bool importModel(const std::string& path, MeshModel& out, std::string& error) {
    fs::path p = fs::u8path(path);
    std::string e = p.extension().string(); for (auto& c : e) c = (char)std::tolower((unsigned char)c);
    out = MeshModel{}; out.name = p.stem().string(); out.source = p.filename().string();
    bool ok = e == ".bbmodel" ? importBbmodel(p, out, error) : e == ".obj" ? importObj(p, out, error) : (e == ".gltf" || e == ".glb") ? importGltf(p, out, error)
            : e == ".fbx" ? importFbx(p, out, error) : (error = "Models can be .bbmodel (Blockbench), .obj, .gltf, .glb or .fbx files.", false);
    if (ok && out.triangles() == 0) { error = "The model has no triangles."; return false; }
    return ok;
}

// ------------------------------------------------------------------ collision: fill the shape with small cubes, merge them into boxes
Box3 doorOpening(const MeshModel& m) {
    Box3 b{ { 1e30, 1e30, 1e30 }, { -1e30, -1e30, -1e30 } }; bool any = false;
    if (m.vpart.size() == m.raw.size())
        for (size_t i = 0; i < m.raw.size(); i++) if (m.vpart[i] != 0xFFFF) {
            V3 p = m.blockPos(m.raw[i]); any = true;
            b.lo = { std::min(b.lo.x, p.x), std::min(b.lo.y, p.y), std::min(b.lo.z, p.z) }; b.hi = { std::max(b.hi.x, p.x), std::max(b.hi.y, p.y), std::max(b.hi.z, p.z) };
        }
    if (!any) b = { { m.sx * 0.15, 0, 0 }, { m.sx * 0.85, m.sy * 0.85, m.sz } };
    return b;
}
std::vector<Box3> meshBoxes(const MeshModel& m, int maxCells, bool stillOnly) {
    std::vector<Box3> out;
    double ext = std::max({ m.sx, m.sy, m.sz }); if (ext <= 0 || m.idx.empty()) return out;
    double cell = std::max(1.0 / 8, ext / maxCells);
    int nx = std::max(1, (int)std::ceil(m.sx / cell - 1e-6)), ny = std::max(1, (int)std::ceil(m.sy / cell - 1e-6)), nz = std::max(1, (int)std::ceil(m.sz / cell - 1e-6));
    int X = nx + 2, Y = ny + 2, Z = nz + 2;   // one empty cell around it, so "outside" is connected
    std::vector<uint8_t> g((size_t)X * Y * Z, 0);   // 0 unknown, 1 surface, 2 outside
    auto at = [&](int x, int y, int z) -> uint8_t& { return g[((size_t)y * Z + z) * X + x]; };
    auto mark = [&](V3 p) {
        int x = std::clamp((int)std::floor(p.x / cell), 0, nx - 1) + 1, y = std::clamp((int)std::floor(p.y / cell), 0, ny - 1) + 1, z = std::clamp((int)std::floor(p.z / cell), 0, nz - 1) + 1;
        at(x, y, z) = 1;
    };
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {   // dense points on every triangle
        if (stillOnly && m.vpart.size() == m.raw.size() && m.vpart[m.idx[t]] != 0xFFFF) continue;
        V3 a = m.blockPos(m.raw[m.idx[t]]), b = m.blockPos(m.raw[m.idx[t + 1]]), c = m.blockPos(m.raw[m.idx[t + 2]]);
        auto len = [](V3 d) { return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z); };
        int steps = std::max(1, (int)std::ceil(std::max({ len(b - a), len(c - a), len(c - b) }) / (cell * 0.45)));
        steps = std::min(steps, 2000);
        for (int i = 0; i <= steps; i++) for (int k = 0; k <= steps - i; k++) {
            double u = (double)i / steps, v = (double)k / steps;
            mark(a + (b - a) * u + (c - a) * v);
        }
    }
    // everything the outside can reach without crossing the surface is outside; the rest is the solid inside
    std::vector<int> stack; stack.push_back(0); at(0, 0, 0) = 2;
    while (!stack.empty()) {
        int i = stack.back(); stack.pop_back(); int x = i % X, z = (i / X) % Z, y = i / (X * Z);
        const int d[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
        for (auto& o : d) {
            int a = x + o[0], b = y + o[1], c = z + o[2]; if (a < 0 || b < 0 || c < 0 || a >= X || b >= Y || c >= Z) continue;
            if (at(a, b, c) == 0) { at(a, b, c) = 2; stack.push_back((b * Z + c) * X + a); }
        }
    }
    auto solid = [&](int x, int y, int z) { return at(x + 1, y + 1, z + 1) != 2; };
    std::vector<uint8_t> used((size_t)nx * ny * nz, 0);
    auto u = [&](int x, int y, int z) -> uint8_t& { return used[((size_t)y * nz + z) * nx + x]; };
    for (int y = 0; y < ny; y++) for (int z = 0; z < nz; z++) for (int x = 0; x < nx; x++) {
        if (u(x, y, z) || !solid(x, y, z)) continue;
        int x2 = x; while (x2 + 1 < nx && !u(x2 + 1, y, z) && solid(x2 + 1, y, z)) x2++;
        auto rowOk = [&](int yy, int zz) { for (int k = x; k <= x2; k++) if (u(k, yy, zz) || !solid(k, yy, zz)) return false; return true; };
        int z2 = z; while (z2 + 1 < nz && rowOk(y, z2 + 1)) z2++;
        auto layerOk = [&](int yy) { for (int zz = z; zz <= z2; zz++) if (!rowOk(yy, zz)) return false; return true; };
        int y2 = y; while (y2 + 1 < ny && layerOk(y2 + 1)) y2++;
        for (int yy = y; yy <= y2; yy++) for (int zz = z; zz <= z2; zz++) for (int k = x; k <= x2; k++) u(k, yy, zz) = 1;
        out.push_back({ { x * cell, y * cell, z * cell }, { std::min(m.sx, (x2 + 1) * cell), std::min(m.sy, (y2 + 1) * cell), std::min(m.sz, (z2 + 1) * cell) } });
    }
    return out;
}

// ------------------------------------------------------------------ built-in looks for helper objects
std::vector<std::string> builtinShapeNames() { return { "@health", "@key", "@gem", "@checkpoint", "@teleporter", "@zone", "@light", "@emitter" }; }
bool builtinShape(const std::string& name, MeshModel& m) {
    Builder B;
    auto colour = [&](uint32_t c) { Img im = solid(((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, 1); int id = (int)B.imgs.size(); B.imgs.push_back(im); return id; };
    auto quadTri = [&](V3 a, V3 b, V3 c, int img) { MeshVert x{ { (float)a.x, (float)a.y, (float)a.z }, { 0.5f, 0.5f }, { 0, 0, 0 } }, y = x, z = x;
        y.p[0] = (float)b.x; y.p[1] = (float)b.y; y.p[2] = (float)b.z; z.p[0] = (float)c.x; z.p[1] = (float)c.y; z.p[2] = (float)c.z;
        V3 mid = (a + b + c) * (1.0 / 3); float ref[3] = { (float)mid.x, (float)(mid.y - 0.5), (float)mid.z }; B.tri(x, y, z, img, ref); };
    auto prism = [&](int sides, double r, double y0, double y1, int img, double twist = 0) {   // a flat n-sided pad
        for (int k = 0; k < sides; k++) {
            double a0 = twist + k * 6.2831853 / sides, a1 = twist + (k + 1) * 6.2831853 / sides;
            V3 p0{ std::cos(a0) * r, y0, std::sin(a0) * r }, p1{ std::cos(a1) * r, y0, std::sin(a1) * r }, q0{ p0.x, y1, p0.z }, q1{ p1.x, y1, p1.z };
            MeshVert v[4]; V3 c[4] = { p0, p1, q1, q0 }; V3 n{ std::cos((a0 + a1) / 2), 0, std::sin((a0 + a1) / 2) };
            for (int i = 0; i < 4; i++) v[i] = { { (float)c[i].x, (float)c[i].y, (float)c[i].z }, { 0.5f, 0.5f }, { (float)n.x, 0, (float)n.z } };
            float rf[3] = { (float)n.x, 0, (float)n.z }; B.tri(v[0], v[1], v[2], img, rf); B.tri(v[0], v[2], v[3], img, rf);
            float up[3] = { 0, 1, 0 }, dn[3] = { 0, -1, 0 };
            MeshVert t0{ { 0, (float)y1, 0 }, { 0.5f, 0.5f }, { 0, 1, 0 } }, t1 = v[3], t2 = v[2]; t1.n[0] = t1.n[2] = 0; t1.n[1] = 1; t2.n[0] = t2.n[2] = 0; t2.n[1] = 1; B.tri(t0, t1, t2, img, up);
            MeshVert b0{ { 0, (float)y0, 0 }, { 0.5f, 0.5f }, { 0, -1, 0 } }, b1 = v[0], b2 = v[1]; b1.n[0] = b1.n[2] = 0; b1.n[1] = -1; b2.n[0] = b2.n[2] = 0; b2.n[1] = -1; B.tri(b0, b1, b2, img, dn);
        }
    };
    auto diamond = [&](double r, double y0, double ym, double y1, int img) {
        V3 top{ 0, y1, 0 }, bot{ 0, y0, 0 };
        for (int k = 0; k < 4; k++) { double a0 = k * 1.5707963, a1 = (k + 1) * 1.5707963; V3 p0{ std::cos(a0) * r, ym, std::sin(a0) * r }, p1{ std::cos(a1) * r, ym, std::sin(a1) * r };
            float o1[3] = { (float)((p0.x + p1.x) / 2), 0.4f, (float)((p0.z + p1.z) / 2) }, o2[3] = { o1[0], -0.4f, o1[2] };
            MeshVert a{ { (float)top.x, (float)top.y, (float)top.z }, { 0.5f, 0.5f }, { 0, 0, 0 } }, b{ { (float)p0.x, (float)p0.y, (float)p0.z }, { 0.5f, 0.5f }, { 0, 0, 0 } }, c{ { (float)p1.x, (float)p1.y, (float)p1.z }, { 0.5f, 0.5f }, { 0, 0, 0 } }, d{ { (float)bot.x, (float)bot.y, (float)bot.z }, { 0.5f, 0.5f }, { 0, 0, 0 } };
            B.tri(a, b, c, img, o1); B.tri(d, b, c, img, o2); }
    };
    auto slab = [&](V3 lo, V3 hi, int img) {   // a box, each face its own normal
        const int f[6][4] = { { 0, 2, 6, 4 }, { 1, 5, 7, 3 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 1, 3, 2 }, { 4, 6, 7, 5 } };
        const float nn[6][3] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
        for (int k = 0; k < 6; k++) {
            MeshVert v[4];
            for (int i = 0; i < 4; i++) { int c = f[k][i]; v[i] = { { (float)((c & 1) ? hi.x : lo.x), (float)((c & 2) ? hi.y : lo.y), (float)((c & 4) ? hi.z : lo.z) }, { 0.5f, 0.5f }, { nn[k][0], nn[k][1], nn[k][2] } }; }
            float rf[3] = { nn[k][0], nn[k][1], nn[k][2] }; B.tri(v[0], v[1], v[2], img, rf); B.tri(v[0], v[2], v[3], img, rf);
        }
    };
    if (name == "@visor-strut") {   // a helmet strut for the HUD (not in the object list)
        int d = colour(0x3A4352), m = colour(0x5A6578), c = colour(0x6CF0FF);
        slab({ -0.12, 0, -0.08 }, { 0.12, 0.08, 0.08 }, m);
        slab({ -0.06, 0.08, -0.05 }, { 0.06, 0.55, 0.05 }, d);
        slab({ -0.04, 0.55, -0.04 }, { 0.04, 0.8, 0.04 }, m);
        slab({ 0.06, 0.1, -0.02 }, { 0.075, 0.5, 0.02 }, c);
    }
    else if (name == "@health") { int r = colour(0xFF4A5A), w = colour(0xFFFFFF); diamond(0.28, 0.1, 0.4, 0.7, r); prism(8, 0.12, 0.36, 0.44, w); }
    else if (name == "@key") { int y = colour(0xFFD23A); prism(6, 0.16, 0.1, 0.2, y); prism(4, 0.05, 0.2, 0.75, y, 0.785); prism(4, 0.09, 0.62, 0.7, y, 0.785); }
    else if (name == "@gem") { int c = colour(0x3AE0FF); diamond(0.25, 0.1, 0.45, 0.75, c); }
    else if (name == "@checkpoint") { int b = colour(0x8FA8C0), g = colour(0x6CF0FF); prism(6, 0.8, 0, 0.12, b); prism(6, 0.55, 0.12, 0.16, g); }
    else if (name == "@teleporter") { int d = colour(0x4A4060), v = colour(0xC070FF), w = colour(0xF0E0FF); prism(8, 1.0, 0, 0.18, d); prism(8, 0.78, 0.18, 0.22, v); prism(8, 0.35, 0.22, 0.25, w); }
    else if (name == "@zone") { int g = colour(0xFF9A3A); prism(4, 0.7071, 0, 1, g, 0.785398); }
    else if (name == "@light") { int y = colour(0xFFF0B0), d = colour(0x404048); prism(8, 0.08, 0, 0.15, d); diamond(0.16, 0.12, 0.3, 0.48, y); }   // a little bulb (seen in the editor only)
    else if (name == "@emitter") { int o = colour(0xFF8040), d = colour(0x404048); prism(6, 0.22, 0, 0.08, d); prism(6, 0.08, 0.08, 0.3, o); }
    else return false;
    B.finish(m); m.name = name; m.source = "built in"; m.unitScale = 1; m.update();
    return true;
}

// ------------------------------------------------------------------ example models
static std::vector<uint8_t> picture(int w, int h, const std::function<void(int, int, uint8_t*)>& f) {
    std::vector<uint8_t> px((size_t)w * h * 4); for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) f(x, y, &px[((size_t)y * w + x) * 4]); return px;
}
std::vector<std::string> writeDemoModels(const std::string& dir) {
    std::error_code ec; fs::create_directories(fs::u8path(dir), ec);
    std::vector<std::string> files;
    {   // Blockbench lamp post: a base, a pole, a tilted arm and a glowing head; top half of the picture metal, bottom half warm light
        auto px = picture(16, 16, [](int x, int y, uint8_t* p) {
            if (y < 8) { int v = 70 + ((x * 7 + y * 13) % 5) * 6 + (x == 0 || x == 15 ? -20 : 0); p[0] = (uint8_t)v; p[1] = (uint8_t)(v + 4); p[2] = (uint8_t)(v + 10); }
            else { p[0] = 255; p[1] = (uint8_t)(200 + (x % 3) * 10); p[2] = (uint8_t)(110 + (y % 2) * 30); }
            p[3] = 255; });
        std::string src = "data:image/png;base64," + base64Encode(pngBytes(16, 16, px));
        auto cube = [](const char* name, double f0, double f1, double f2, double t0, double t1, double t2, const char* rot, const char* uv) {
            std::string faces;
            for (const char* s : { "north", "south", "east", "west", "up", "down" }) faces += std::string(faces.empty() ? "" : ",") + "\"" + s + "\":{\"uv\":" + uv + ",\"texture\":0}";
            char b[1024]; snprintf(b, sizeof b, "{\"name\":\"%s\",\"from\":[%g,%g,%g],\"to\":[%g,%g,%g],\"origin\":[8,27,8],\"rotation\":%s,\"faces\":{%s}}", name, f0, f1, f2, t0, t1, t2, rot, faces.c_str());
            return std::string(b);
        };
        std::string els = cube("base", 4, 0, 4, 12, 2, 12, "[0,0,0]", "[0,0,16,8]") + "," + cube("pole", 7, 2, 7, 9, 30, 9, "[0,0,0]", "[0,0,2,8]") + ","
                        + cube("arm", 8, 26, 7, 18, 28, 9, "[0,0,-15]", "[0,0,16,2]") + "," + cube("head", 14, 20, 5, 20, 25, 11, "[0,0,-15]", "[0,8,16,16]");
        std::string j = "{\"meta\":{\"format_version\":\"4.5\",\"model_format\":\"free\"},\"name\":\"Lamp post\",\"resolution\":{\"width\":16,\"height\":16},\"elements\":[" + els
                      + "],\"textures\":[{\"name\":\"lamp\",\"source\":\"" + src + "\"}]}";
        std::string p = dir + "/Lamp post.bbmodel"; std::ofstream(fs::u8path(p)) << j; files.push_back(p);
    }
    {   // .obj rock: a lumpy ball (an icosahedron split once), grey speckled picture
        double t = (1 + std::sqrt(5.0)) / 2;
        std::vector<V3> v = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t }, { 0, -1, -t }, { 0, 1, -t }, { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
        std::vector<std::array<int, 3>> f = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
                                              { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 }, { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
        std::map<std::pair<int, int>, int> mid; std::vector<std::array<int, 3>> f2;
        auto midp = [&](int a, int b) { auto k = std::minmax(a, b); auto it = mid.find(k); if (it != mid.end()) return it->second; v.push_back((v[a] + v[b]) * 0.5); return mid[k] = (int)v.size() - 1; };
        for (auto& tr : f) { int a = midp(tr[0], tr[1]), b = midp(tr[1], tr[2]), c = midp(tr[2], tr[0]); f2.push_back({ tr[0], a, c }); f2.push_back({ tr[1], b, a }); f2.push_back({ tr[2], c, b }); f2.push_back({ a, b, c }); }
        std::string o = "# rock\nmtllib rock.mtl\nusemtl rock\n";
        for (size_t i = 0; i < v.size(); i++) {
            double l = std::sqrt(v[i].x * v[i].x + v[i].y * v[i].y + v[i].z * v[i].z), r = 1.0 + 0.12 * std::sin(i * 2.3) * std::cos(i * 1.7);
            V3 p = v[i] * (r / l); p.y *= 0.7;
            char b[160]; snprintf(b, sizeof b, "v %.4f %.4f %.4f\nvt %.4f %.4f\n", p.x, p.y + 0.7, p.z, 0.5 + std::atan2(p.z, p.x) / 6.2832, 0.5 + p.y / 2); o += b;
        }
        for (auto& tr : f2) { char b[96]; snprintf(b, sizeof b, "f %d/%d %d/%d %d/%d\n", tr[0] + 1, tr[0] + 1, tr[1] + 1, tr[1] + 1, tr[2] + 1, tr[2] + 1); o += b; }
        std::ofstream(fs::u8path(dir + "/rock.obj")) << o;
        std::ofstream(fs::u8path(dir + "/rock.mtl")) << "newmtl rock\nKd 1 1 1\nmap_Kd rock.png\n";
        writePng(dir + "/rock.png", 32, 32, picture(32, 32, [](int x, int y, uint8_t* p) { int h = (x * 73856093 ^ y * 19349663) & 255; int v = 95 + (h % 40) - ((x / 4 + y / 4) % 2) * 8; p[0] = (uint8_t)v; p[1] = (uint8_t)(v - 3); p[2] = (uint8_t)(v - 8); p[3] = 255; }));
        files.push_back(dir + "/rock.obj");
    }
    {   // glTF crystal: a tall double pyramid, flat sides, purple, turned 45 degrees by its node
        std::vector<V3> P = { { 0, 2, 0 }, { 0.5, 0.8, 0 }, { 0, 0.8, 0.5 }, { -0.5, 0.8, 0 }, { 0, 0.8, -0.5 }, { 0, 0, 0 } };
        int F[8][3] = { { 0, 2, 1 }, { 0, 3, 2 }, { 0, 4, 3 }, { 0, 1, 4 }, { 5, 1, 2 }, { 5, 2, 3 }, { 5, 3, 4 }, { 5, 4, 1 } };
        std::vector<float> pos, nor; std::vector<uint16_t> ind;
        for (auto& f : F) {
            V3 a = P[f[0]], b = P[f[1]], c = P[f[2]], e1 = b - a, e2 = c - a;
            V3 n{ e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x }; double l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z); n = n * (1 / l);
            for (V3 p : { a, b, c }) { pos.insert(pos.end(), { (float)p.x, (float)p.y, (float)p.z }); nor.insert(nor.end(), { (float)n.x, (float)n.y, (float)n.z }); ind.push_back((uint16_t)ind.size()); }
        }
        std::vector<uint8_t> buf(pos.size() * 4 + nor.size() * 4 + ind.size() * 2);
        std::memcpy(buf.data(), pos.data(), pos.size() * 4); std::memcpy(buf.data() + pos.size() * 4, nor.data(), nor.size() * 4); std::memcpy(buf.data() + pos.size() * 8, ind.data(), ind.size() * 2);
        size_t pb = pos.size() * 4, ib = ind.size() * 2; size_t n = ind.size();
        char j[4096];
        snprintf(j, sizeof j,
            "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0,\"rotation\":[0,0.3826834,0,0.9238795],\"scale\":[1,1.25,1]}],"
            "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1},\"indices\":2,\"material\":0}]}],"
            "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.62,0.32,0.95,1]}}],"
            "\"buffers\":[{\"byteLength\":%zu,\"uri\":\"data:application/octet-stream;base64,%s\"}],"
            "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":%zu},{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu},{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}],"
            "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC3\",\"min\":[-0.5,0,-0.5],\"max\":[0.5,2,0.5]},{\"bufferView\":1,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC3\"},{\"bufferView\":2,\"componentType\":5123,\"count\":%zu,\"type\":\"SCALAR\"}]}",
            buf.size(), base64Encode(buf).c_str(), pb, pb, pb, pb * 2, ib, n, n, n);
        std::ofstream(fs::u8path(dir + "/crystal.gltf")) << j; files.push_back(dir + "/crystal.gltf");
    }
    return files;
}

} // namespace mc
