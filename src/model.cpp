// model.cpp : FBX import (ufbx) + animation sampling and smooth transitions.
#include "model.h"

#include "../third_party/ufbx/ufbx.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#include "../third_party/stb/stb_image.h"

#include <cstring>
#include <functional>
#include <map>

namespace px {

// ------------------------------------------------------------------ maths
Quat qslerp(Quat a, Quat b, float t) {
    float d = qdot(a, b);
    if (d < 0) { b = { -b.x, -b.y, -b.z, -b.w }; d = -d; }
    if (d > 0.9995f) return qnorm({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t });
    float th = std::acos(d), s = std::sin(th), wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return { a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb };
}
V3 qlog(Quat q) {
    if (q.w < 0) q = { -q.x, -q.y, -q.z, -q.w };
    float s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (s < 1e-8f) return { q.x * 2, q.y * 2, q.z * 2 };
    float ang = 2.f * std::atan2(s, q.w);
    return { q.x / s * ang, q.y / s * ang, q.z / s * ang };
}
Quat qexp(V3 v) {
    float ang = length(v);
    if (ang < 1e-8f) return qnorm({ v.x * 0.5f, v.y * 0.5f, v.z * 0.5f, 1 });
    float s = std::sin(ang * 0.5f) / ang;
    return { v.x * s, v.y * s, v.z * s, std::cos(ang * 0.5f) };
}
V3 qrotate(Quat q, V3 v) {
    V3 u{ q.x, q.y, q.z }; float s = q.w;
    return u * (2.f * dot(u, v)) + v * (s * s - dot(u, u)) + cross(u, v) * (2.f * s);
}
Mat4 trsMatrix(const TRS& x) {
    Mat4 m{};
    V3 cx = qrotate(x.r, { 1, 0, 0 }) * x.s.x, cy = qrotate(x.r, { 0, 1, 0 }) * x.s.y, cz = qrotate(x.r, { 0, 0, 1 }) * x.s.z;
    m.m[0][0] = cx.x; m.m[0][1] = cx.y; m.m[0][2] = cx.z;
    m.m[1][0] = cy.x; m.m[1][1] = cy.y; m.m[1][2] = cy.z;
    m.m[2][0] = cz.x; m.m[2][1] = cz.y; m.m[2][2] = cz.z;
    m.m[3][0] = x.t.x; m.m[3][1] = x.t.y; m.m[3][2] = x.t.z; m.m[3][3] = 1;
    return m;
}
Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 o{};
    for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) {
        float s = 0; for (int k = 0; k < 4; k++) s += a.m[k][r] * b.m[c][k]; o.m[c][r] = s;
    }
    return o;
}
V3 transformPoint(const Mat4& m, V3 p) {
    return { m.m[0][0] * p.x + m.m[1][0] * p.y + m.m[2][0] * p.z + m.m[3][0],
             m.m[0][1] * p.x + m.m[1][1] * p.y + m.m[2][1] * p.z + m.m[3][1],
             m.m[0][2] * p.x + m.m[1][2] * p.y + m.m[2][2] * p.z + m.m[3][2] };
}

// ------------------------------------------------------------------ import
static V3 v3(ufbx_vec3 v) { return { (float)v.x, (float)v.y, (float)v.z }; }
static Quat q4(ufbx_quat q) { return { (float)q.x, (float)q.y, (float)q.z, (float)q.w }; }
static std::string str(ufbx_string s) { return std::string(s.data, s.length); }

static bool decodeImage(const unsigned char* data, size_t size, Image& out) {
    int w, h, n; unsigned char* px = stbi_load_from_memory(data, (int)size, &w, &h, &n, 4);
    if (!px) return false;
    out.w = w; out.h = h; out.rgba.assign(px, px + (size_t)w * h * 4); stbi_image_free(px); return true;
}
static bool loadImageFile(const std::string& path, Image& out) {
    int w, h, n; unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!px) return false;
    out.w = w; out.h = h; out.rgba.assign(px, px + (size_t)w * h * 4); stbi_image_free(px); return true;
}

bool Model::loadFBX(const std::string& path, std::string& err, float scaleOverride) {
    *this = Model{};
    ufbx_load_opts o{};
    o.target_axes = ufbx_axes_left_handed_y_up;
    o.handedness_conversion_axis = UFBX_MIRROR_AXIS_Z;
    o.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    o.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
    o.generate_missing_normals = true;
    ufbx_error e{};
    ufbx_scene* sc = ufbx_load_file(path.c_str(), &o, &e);
    if (!sc) { char buf[1024]; ufbx_format_error(buf, sizeof buf, &e); err = buf; return false; }

    source = str(sc->metadata.creator);
    bool blockbench = source.find("Blockbench") != std::string::npos;
    if (scaleOverride > 0) importScale = scaleOverride;
    else importScale = blockbench ? 1.f / 16.f : (float)(sc->settings.unit_meters > 0 ? sc->settings.unit_meters : 0.01);
    const float k = importScale;

    // nodes (ufbx typed ids are our indices)
    nodes.resize(sc->nodes.count);
    for (size_t n = 0; n < sc->nodes.count; n++) {
        const ufbx_node* un = sc->nodes.data[n];
        ModelNode& mn = nodes[n];
        mn.name = str(un->name);
        mn.parent = un->parent ? (int)un->parent->typed_id : -1;
        mn.rest.t = v3(un->local_transform.translation) * k;
        mn.rest.r = q4(un->local_transform.rotation);
        mn.rest.s = v3(un->local_transform.scale);
    }

    // textures: embedded content first (works on any PC), then the file next to the FBX
    std::string dir = path.substr(0, path.find_last_of("\\/") + 1);
    std::map<const ufbx_texture*, int> texIndex;
    auto imageFor = [&](const ufbx_material* mat) -> int {
        if (!mat) return -1;
        const ufbx_texture* tex = mat->fbx.diffuse_color.texture;
        if (!tex) tex = mat->pbr.base_color.texture;
        if (!tex && mat->textures.count) tex = mat->textures.data[0].texture;
        if (!tex) return -1;
        auto it = texIndex.find(tex); if (it != texIndex.end()) return it->second;
        Image img; bool ok = false;
        if (tex->content.size) ok = decodeImage((const unsigned char*)tex->content.data, tex->content.size, img);
        if (!ok && tex->video && tex->video->content.size) ok = decodeImage((const unsigned char*)tex->video->content.data, tex->video->content.size, img);
        if (!ok && tex->filename.length) ok = loadImageFile(str(tex->filename), img);
        if (!ok && tex->relative_filename.length) ok = loadImageFile(dir + str(tex->relative_filename), img);
        int idx = -1; if (ok) { idx = (int)images.size(); images.push_back(std::move(img)); }
        texIndex[tex] = idx; return idx;
    };

    // meshes: one ModelMesh per (node, material part), vertices in node space
    std::vector<uint32_t> tri;
    for (size_t n = 0; n < sc->nodes.count; n++) {
        const ufbx_node* un = sc->nodes.data[n];
        const ufbx_mesh* mesh = un->mesh;
        if (!mesh) continue;
        tri.resize(mesh->max_face_triangles * 3);
        size_t parts = mesh->material_parts.count ? mesh->material_parts.count : 1;
        for (size_t p = 0; p < parts; p++) {
            ModelMesh mm; mm.node = (int)n;
            const ufbx_material* mat = nullptr;
            if (p < un->materials.count) mat = un->materials.data[p];
            else if (p < mesh->materials.count) mat = mesh->materials.data[p];
            mm.image = imageFor(mat);
            auto emitFace = [&](ufbx_face face) {
                uint32_t nt = ufbx_triangulate_face(tri.data(), tri.size(), mesh, face);
                for (uint32_t t = 0; t < nt; t++) {
                    Vertex vtx[3];
                    for (int c = 0; c < 3; c++) {
                        uint32_t ix = tri[t * 3 + c];
                        V3 pos = v3(ufbx_get_vertex_vec3(&mesh->vertex_position, ix)) * k;
                        V3 nrm = mesh->vertex_normal.exists ? normalize(v3(ufbx_get_vertex_vec3(&mesh->vertex_normal, ix))) : V3{ 0, 1, 0 };
                        float u = 0, v = 0;
                        if (mesh->vertex_uv.exists) { ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ix); u = (float)uv.x; v = 1.f - (float)uv.y; }
                        vtx[c] = { { pos.x, pos.y, pos.z }, { nrm.x, nrm.y, nrm.z }, { u, v }, { 0.55f, 0.55f, 0.55f, 1.f } };
                    }
                    // enforce the engine convention (LH, clockwise front): numeric cross() agrees with the normal
                    V3 a{ vtx[0].p[0], vtx[0].p[1], vtx[0].p[2] }, b{ vtx[1].p[0], vtx[1].p[1], vtx[1].p[2] }, c{ vtx[2].p[0], vtx[2].p[1], vtx[2].p[2] };
                    V3 nsum{ vtx[0].n[0] + vtx[1].n[0] + vtx[2].n[0], vtx[0].n[1] + vtx[1].n[1] + vtx[2].n[1], vtx[0].n[2] + vtx[1].n[2] + vtx[2].n[2] };
                    if (dot(cross(b - a, c - a), nsum) < 0) std::swap(vtx[1], vtx[2]);
                    for (auto& q : vtx) { mm.i.push_back((uint32_t)mm.v.size()); mm.v.push_back(q); }
                }
            };
            if (mesh->material_parts.count) { const ufbx_mesh_part& part = mesh->material_parts.data[p]; for (size_t f = 0; f < part.face_indices.count; f++) emitFace(mesh->faces.data[part.face_indices.data[f]]); }
            else for (size_t f = 0; f < mesh->faces.count; f++) emitFace(mesh->faces.data[f]);
            if (!mm.v.empty()) meshes.push_back(std::move(mm));
        }
    }

    // animation clips: each animation stack becomes one clip, baked to linear keys
    for (size_t s = 0; s < sc->anim_stacks.count; s++) {
        const ufbx_anim_stack* st = sc->anim_stacks.data[s];
        ufbx_bake_opts bo{}; bo.trim_start_time = true;
        ufbx_error be{};
        ufbx_baked_anim* bake = ufbx_bake_anim(sc, st->anim, &bo, &be);
        if (!bake) continue;
        Clip clip; clip.name = str(st->name);
        double t0 = bake->playback_time_begin;
        clip.duration = (float)std::max(0.0, bake->playback_duration);
        for (size_t b = 0; b < bake->nodes.count; b++) {
            const ufbx_baked_node& bn = bake->nodes.data[b];
            NodeTrack tr; tr.node = (int)bn.typed_id;
            for (size_t i = 0; i < bn.translation_keys.count; i++) tr.t.push_back({ (float)std::max(0.0, bn.translation_keys.data[i].time - t0), v3(bn.translation_keys.data[i].value) * k });
            for (size_t i = 0; i < bn.rotation_keys.count; i++) tr.r.push_back({ (float)std::max(0.0, bn.rotation_keys.data[i].time - t0), q4(bn.rotation_keys.data[i].value) });
            for (size_t i = 0; i < bn.scale_keys.count; i++) tr.s.push_back({ (float)std::max(0.0, bn.scale_keys.data[i].time - t0), v3(bn.scale_keys.data[i].value) });
            clip.tracks.push_back(std::move(tr));
        }
        ufbx_free_baked_anim(bake);
        clips.push_back(std::move(clip));
    }
    ufbx_free_scene(sc);

    // rest-pose bounds
    std::vector<Mat4> W; worldMatrices(restPose(), W);
    bool first = true;
    for (auto& m : meshes) for (auto& v : m.v) {
        V3 p = transformPoint(W[m.node], { v.p[0], v.p[1], v.p[2] });
        if (first) { bmin = bmax = p; first = false; }
        bmin = { std::min(bmin.x, p.x), std::min(bmin.y, p.y), std::min(bmin.z, p.z) };
        bmax = { std::max(bmax.x, p.x), std::max(bmax.y, p.y), std::max(bmax.z, p.z) };
    }
    return true;
}

Pose Model::restPose() const { Pose p(nodes.size()); for (size_t i = 0; i < nodes.size(); i++) p[i] = nodes[i].rest; return p; }

template <class T, class F> static T sampleKeys(const std::vector<Key<T>>& k, float t, F lerp) {
    if (k.size() == 1 || t <= k.front().time) return k.front().value;
    if (t >= k.back().time) return k.back().value;
    size_t lo = 0, hi = k.size() - 1;
    while (hi - lo > 1) { size_t mid = (lo + hi) / 2; if (k[mid].time <= t) lo = mid; else hi = mid; }
    float span = k[hi].time - k[lo].time, a = span > 1e-9f ? (t - k[lo].time) / span : 0.f;
    return lerp(k[lo].value, k[hi].value, a);
}
void Model::sample(int c, float t, Pose& out) const {
    if (c < 0 || c >= (int)clips.size()) return;
    auto lv = [](V3 a, V3 b, float x) { return a + (b - a) * x; };
    for (const NodeTrack& tr : clips[c].tracks) {
        if (tr.node < 0 || tr.node >= (int)out.size()) continue;
        if (!tr.t.empty()) out[tr.node].t = sampleKeys(tr.t, t, lv);
        if (!tr.r.empty()) out[tr.node].r = sampleKeys(tr.r, t, qslerp);
        if (!tr.s.empty()) out[tr.node].s = sampleKeys(tr.s, t, lv);
    }
}
void Model::worldMatrices(const Pose& p, std::vector<Mat4>& out) const {
    out.assign(nodes.size(), Mat4{});
    std::vector<char> done(nodes.size(), 0);
    std::function<void(int)> solve = [&](int i) {
        if (done[i]) return;
        Mat4 local = trsMatrix(p[i]); int par = nodes[i].parent;
        if (par >= 0) { solve(par); out[i] = mul(out[par], local); } else out[i] = local;
        done[i] = 1;
    };
    for (int i = 0; i < (int)nodes.size(); i++) solve(i);
}
int Model::findClip(const std::string& name) const {
    for (size_t i = 0; i < clips.size(); i++) if (clips[i].name == name) return (int)i;
    return -1;
}

// ------------------------------------------------------------------ playback
float applyCurve(BlendCurve c, float x) {
    x = std::max(0.f, std::min(1.f, x));
    switch (c) {
    case BlendCurve::EaseIn: return x * x;
    case BlendCurve::EaseOut: return 1 - (1 - x) * (1 - x);
    case BlendCurve::Smooth: return x * x * (3 - 2 * x);
    default: return x;
    }
}

// Inertialization (Bollo, GDC 2018): a source-minus-target offset that decays to zero along a quintic
// with zero velocity and acceleration at the end, starting with the source's real velocity.
static Animator::Off makeOff(V3 x0v, V3 v0v, float T) {
    Animator::Off o; float x = length(x0v);
    if (x < 1e-6f) { o.dir = { 0, 0, 0 }; o.x = 0; o.v = 0; o.T = T; return o; }
    o.dir = x0v * (1.f / x); o.x = x; o.v = dot(v0v, o.dir);
    if (o.v > 0) o.v = 0;                                  // moving away from the target: ignore
    if (o.v < 0) o.T = std::min(T, -5.f * o.x / o.v); else o.T = T;
    o.T = std::max(o.T, 1e-3f);
    return o;
}
static V3 evalOff(const Animator::Off& o, float t) {
    if (o.x == 0 || t >= o.T) return { 0, 0, 0 };
    float T = o.T, x0 = o.x, v0 = o.v;
    float a0 = (-8.f * v0 * T - 20.f * x0) / (T * T);
    float A = -(a0 * T * T + 6.f * v0 * T + 12.f * x0) / (2.f * T * T * T * T * T);
    float B = (3.f * a0 * T * T + 16.f * v0 * T + 30.f * x0) / (2.f * T * T * T * T);
    float C = -(3.f * a0 * T * T + 12.f * v0 * T + 20.f * x0) / (2.f * T * T * T);
    float x = (((A * t + B) * t + C) * t + a0 * 0.5f) * t * t + v0 * t + x0;
    return o.dir * x;
}

void Animator::bind(const Model* m, std::vector<ClipSettings>* s) {
    model = m; settings = s; cur = -1; time = 0; fadeT = fadeDur = 0; inT = inDur = 0; layerClip = -1;
    if (settings && settings->size() < m->clips.size()) settings->resize(m->clips.size());
    out = prevOut = m->restPose();
}
float Animator::clipLength() const { return (model && cur >= 0) ? model->clips[cur].duration : 0.f; }
bool Animator::finished() const {
    if (!model || cur < 0) return false;
    bool loop = settings && cur < (int)settings->size() && (*settings)[cur].loop;
    return !loop && time >= model->clips[cur].duration;
}

void Animator::play(int clip, float blendTime, bool restart) {
    if (!model || clip < 0 || clip >= (int)model->clips.size()) return;
    if (!restart && clip == cur) return;
    if (cur < 0 || blendTime <= 0 || mode == BlendMode::Cut) {
        cur = clip; time = 0; fadeT = fadeDur = 0; inT = inDur = 0; return;
    }
    if (mode == BlendMode::Crossfade) {
        from = out; fadeT = 0; fadeDur = blendTime; inT = inDur = 0;
        cur = clip; time = 0; return;
    }
    // inertialize: offset between what is on screen now and the new clip's first frame
    Pose dst = model->restPose(); model->sample(clip, 0.f, dst);
    Pose dst2 = model->restPose(); model->sample(clip, lastDt, dst2);
    size_t n = model->nodes.size(); offT.resize(n); offR.resize(n); offS.resize(n);
    float idt = 1.f / std::max(lastDt, 1e-4f);
    for (size_t i = 0; i < n; i++) {
        V3 xt = out[i].t - dst[i].t, vt = (out[i].t - prevOut[i].t) * idt - (dst2[i].t - dst[i].t) * idt;
        Quat dq = qmul(out[i].r, qconj(dst[i].r));
        V3 xr = qlog(dq);
        V3 vr = qlog(qmul(out[i].r, qconj(prevOut[i].r))) * idt - qlog(qmul(dst2[i].r, qconj(dst[i].r))) * idt;
        V3 xs = out[i].s - dst[i].s, vs = (out[i].s - prevOut[i].s) * idt - (dst2[i].s - dst[i].s) * idt;
        offT[i] = makeOff(xt, vt, blendTime); offR[i] = makeOff(xr, vr, blendTime); offS[i] = makeOff(xs, vs, blendTime);
    }
    inT = 0; inDur = blendTime; fadeT = fadeDur = 0;
    cur = clip; time = 0;
}

void Animator::update(float dt) {
    if (!model) return;
    lastDt = std::max(dt, 1e-4f);
    Pose base = model->restPose();
    if (cur >= 0) {
        const Clip& c = model->clips[cur];
        ClipSettings cs = (settings && cur < (int)settings->size()) ? (*settings)[cur] : ClipSettings{};
        if (!paused) time += dt * cs.speed;
        if (c.duration > 0) {
            if (cs.loop) { time = std::fmod(time, c.duration); if (time < 0) time += c.duration; }
            else time = std::max(0.f, std::min(time, c.duration));
        }
        model->sample(cur, time, base);
    }
    // additive layer: the layer clip's change from rest is added on top
    if (layerClip >= 0) {
        if (!paused) layerTime += dt;
        const Clip& lc = model->clips[layerClip];
        Pose L = model->restPose(); model->sample(layerClip, std::min(layerTime, lc.duration), L);
        for (size_t i = 0; i < base.size(); i++) {
            const TRS& r = model->nodes[i].rest;
            base[i].t = base[i].t + (L[i].t - r.t) * layerWeight;
            Quat d = qmul(qconj(r.r), L[i].r);
            base[i].r = qnorm(qmul(base[i].r, qslerp(Quat{}, d, layerWeight)));
        }
        if (layerTime >= lc.duration) layerClip = -1;
    }
    Pose res = base;
    if (fadeT < fadeDur) {
        if (!paused) fadeT += dt;
        float w = applyCurve(curve, fadeT / fadeDur);
        for (size_t i = 0; i < res.size(); i++) {
            res[i].t = from[i].t + (base[i].t - from[i].t) * w;
            res[i].r = qslerp(from[i].r, base[i].r, w);
            res[i].s = from[i].s + (base[i].s - from[i].s) * w;
        }
    } else if (inT < inDur && !offT.empty()) {
        if (!paused) inT += dt;
        for (size_t i = 0; i < res.size(); i++) {
            res[i].t = base[i].t + evalOff(offT[i], inT);
            res[i].r = qnorm(qmul(qexp(evalOff(offR[i], inT)), base[i].r));
            res[i].s = base[i].s + evalOff(offS[i], inT);
        }
    }
    prevOut = out; out = res;
}

} // namespace px
