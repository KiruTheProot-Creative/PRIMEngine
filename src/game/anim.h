#pragma once
// Smooth changes between animations (from the first prototype, phase1-fbx-animation): when one clip takes over from
// another (or the model goes back to rest), the moving parts do not jump. Three ways:
//  - Inertialization (default): the parts keep moving the way they were and settle onto the new clip along a smooth
//    curve (Bollo, GDC 2018). Stays smooth even when a new clip starts in the middle of a change.
//  - Crossfade: a mix from what was on screen to the new clip, along a curve (linear, ease in, ease out, smooth).
//  - Cut: no blending.
// Works on the parts' numbers (MeshModel::pose: 9 per part); turns go the short way round. Plain C++, tested on Linux.
#include <climits>
#include <map>
#include <string>
#include <vector>

namespace mc {

using PartPose = std::vector<double>;

enum class BlendMode { Inertialize, Crossfade, Cut };
enum class BlendCurve { Linear, EaseIn, EaseOut, Smooth };
float applyCurve(BlendCurve c, float x);
const char* blendModeName(BlendMode m);
const char* blendCurveName(BlendCurve c);

struct BlendSettings {
    BlendMode mode = BlendMode::Inertialize;
    BlendCurve curve = BlendCurve::Smooth;             // crossfade only
    float time = 0.2f;                                  // seconds
    bool operator==(const BlendSettings& o) const { return mode == o.mode && curve == o.curve && time == o.time; }
};

struct PoseBlender {
    // target: where the playing clip has the parts now; key: which clip is playing (any change starts a change-over)
    const PartPose& update(const PartPose& target, int key, float dt, const BlendSettings& s);
    bool busy() const { return fadeT < fadeDur || inT < inDur; }
    void reset() { *this = PoseBlender{}; }
    const PartPose& now() const { return out; }

    PartPose out, prev; bool have = false; int key = INT_MIN; float lastDt = 1.f / 60.f;
    PartPose from; float fadeT = 0, fadeDur = 0; BlendCurve curve = BlendCurve::Smooth;   // crossfade
    struct Off { double x = 0, v = 0, T = 0, sign = 1; };
    std::vector<Off> off; float inT = 0, inDur = 0;                                         // inertialization
};

// per model (by name); models without their own use the default
struct AnimBlends {
    std::map<std::string, BlendSettings> byModel;
    BlendSettings def;
    BlendSettings of(const std::string& model) const { auto it = byModel.find(model); return it != byModel.end() ? it->second : def; }
};
bool saveAnimBlends(const std::string& path, const AnimBlends& a);
bool loadAnimBlends(const std::string& path, AnimBlends& a);

} // namespace mc
