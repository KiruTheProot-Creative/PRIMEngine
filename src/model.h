// model.h : imported models (FBX via ufbx) and animation playback. Platform-independent, no D3D.
//
//   Model      - nodes (groups/cubes), meshes attached to nodes, embedded images, animation clips
//   Pose       - one local transform per node
//   Animator   - plays clips with smooth transitions: crossfade or inertialization, + additive layers
//
// Engine space: left-handed, Y up, metres, clockwise front faces (same as scene.h).
#pragma once
#include "scene.h"
#include <string>
#include <vector>

namespace px {

// ------------------------------------------------------------------ quaternion maths
struct Quat { float x = 0, y = 0, z = 0, w = 1; };
inline Quat qmul(Quat a, Quat b) {
    return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
             a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
             a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
             a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
inline Quat qconj(Quat q) { return { -q.x, -q.y, -q.z, q.w }; }
inline float qdot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Quat qnorm(Quat q) { float l = std::sqrt(qdot(q, q)); if (l < 1e-12f) return {}; return { q.x / l, q.y / l, q.z / l, q.w / l }; }
Quat qslerp(Quat a, Quat b, float t);       // shortest path
V3 qlog(Quat q);                            // rotation vector (axis * angle)
Quat qexp(V3 v);
V3 qrotate(Quat q, V3 v);

struct TRS { V3 t{ 0, 0, 0 }; Quat r{}; V3 s{ 1, 1, 1 }; };
using Pose = std::vector<TRS>;

// 4x4, column-vector convention (m[col][row]); converted to row-major for D3D in the renderer
struct Mat4 { float m[4][4]; };
Mat4 trsMatrix(const TRS& x);
Mat4 mul(const Mat4& a, const Mat4& b);     // a * b  (apply b first)
V3 transformPoint(const Mat4& m, V3 p);

// ------------------------------------------------------------------ model data
struct ModelNode { std::string name; int parent = -1; TRS rest; };
struct ModelMesh { int node = -1; int image = -1; std::vector<Vertex> v; std::vector<uint32_t> i; };
struct Image { int w = 0, h = 0; std::vector<uint8_t> rgba; };

template <class T> struct Key { float time; T value; };
struct NodeTrack { int node = -1; std::vector<Key<V3>> t; std::vector<Key<Quat>> r; std::vector<Key<V3>> s; };
struct Clip { std::string name; float duration = 0; std::vector<NodeTrack> tracks; };

struct Model {
    std::string source;          // e.g. "Blockbench 5.2.0-beta.2"
    float importScale = 1.f;     // file units -> metres (Blockbench: 16 units = 1 block = 1 m)
    std::vector<ModelNode> nodes;
    std::vector<ModelMesh> meshes;
    std::vector<Image> images;
    std::vector<Clip> clips;
    V3 bmin{ 0, 0, 0 }, bmax{ 0, 0, 0 };   // rest-pose bounds, metres

    // Loads an FBX (binary or ASCII). Returns false and fills `err` on failure.
    // scaleOverride <= 0 means "auto": 1/16 for Blockbench files, the file's own unit otherwise.
    bool loadFBX(const std::string& path, std::string& err, float scaleOverride = 0.f);

    Pose restPose() const;
    void sample(int clip, float time, Pose& out) const;          // writes only animated channels
    void worldMatrices(const Pose& p, std::vector<Mat4>& out) const;
    int findClip(const std::string& name) const;
};

// ------------------------------------------------------------------ playback
enum class BlendMode { Inertialize, Crossfade, Cut };
enum class BlendCurve { Linear, EaseIn, EaseOut, Smooth };
float applyCurve(BlendCurve c, float x);

struct ClipSettings { bool loop = false; float speed = 1.f; };

struct Animator {
    const Model* model = nullptr;
    std::vector<ClipSettings>* settings = nullptr;   // one per clip, owned by the asset's meta data

    int cur = -1; float time = 0;
    bool paused = false;

    BlendMode mode = BlendMode::Inertialize;
    BlendCurve curve = BlendCurve::Smooth;

    // crossfade state
    Pose from; float fadeT = 0, fadeDur = 0;
    // inertialization state: per-node offsets (position, rotation vector, scale) decaying to zero
    struct Off { V3 dir{ 0, 0, 0 }; float x = 0, v = 0, T = 0; };
    std::vector<Off> offT, offR, offS; float inT = 0, inDur = 0;

    // additive layer (e.g. a recoil kick on top of the idle sway)
    int layerClip = -1; float layerTime = 0, layerWeight = 1;

    Pose out, prevOut; float lastDt = 1.f / 60.f;

    void bind(const Model* m, std::vector<ClipSettings>* s);
    void play(int clip, float blendTime, bool restart = true);
    void playLayer(int clip, float weight = 1.f) { layerClip = clip; layerTime = 0; layerWeight = weight; }
    void update(float dt);
    bool finished() const;                                     // a non-looping clip reached its end
    float clipLength() const;
};

} // namespace px
