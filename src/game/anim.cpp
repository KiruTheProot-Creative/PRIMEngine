#include "anim.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace mc {

float applyCurve(BlendCurve c, float x) {
    x = std::clamp(x, 0.f, 1.f);
    switch (c) {
    case BlendCurve::EaseIn: return x * x;
    case BlendCurve::EaseOut: return 1 - (1 - x) * (1 - x);
    case BlendCurve::Smooth: return x * x * (3 - 2 * x);
    default: return x;
    }
}
const char* blendModeName(BlendMode m) { return m == BlendMode::Crossfade ? "crossfade" : m == BlendMode::Cut ? "cut" : "inertialize"; }
const char* blendCurveName(BlendCurve c) { return c == BlendCurve::Linear ? "linear" : c == BlendCurve::EaseIn ? "ease_in" : c == BlendCurve::EaseOut ? "ease_out" : "smooth"; }

static bool isTurn(size_t i) { size_t k = i % 9; return k >= 3 && k < 6; }
static double wrap180(double d) { d = std::fmod(d + 180.0, 360.0); if (d < 0) d += 360.0; return d - 180.0; }   // the short way round
static double diff(const PartPose& a, const PartPose& b, size_t i) { double d = a[i] - b[i]; return isTurn(i) ? wrap180(d) : d; }   // a - b

// an offset (what is on screen minus the new clip) that goes to 0 along a quintic, starting with the speed it had
// and ending with no speed and no acceleration
static PoseBlender::Off makeOff(double x0, double v0, double T) {
    PoseBlender::Off o; o.T = T;
    if (std::fabs(x0) < 1e-9) { o.x = 0; return o; }
    o.sign = x0 > 0 ? 1 : -1; o.x = std::fabs(x0); o.v = v0 * o.sign;
    if (o.v > 0) o.v = 0;                                   // moving away from the new clip: ignore that speed
    if (o.v < 0) o.T = std::min(T, -5.0 * o.x / o.v);
    o.T = std::max(o.T, 1e-3);
    return o;
}
static double evalOff(const PoseBlender::Off& o, double t) {
    if (o.x == 0 || t >= o.T) return 0;
    double T = o.T, x0 = o.x, v0 = o.v;
    double a0 = (-8 * v0 * T - 20 * x0) / (T * T);
    double A = -(a0 * T * T + 6 * v0 * T + 12 * x0) / (2 * T * T * T * T * T);
    double B = (3 * a0 * T * T + 16 * v0 * T + 30 * x0) / (2 * T * T * T * T);
    double C = -(3 * a0 * T * T + 12 * v0 * T + 20 * x0) / (2 * T * T * T);
    double x = (((A * t + B) * t + C) * t + a0 * 0.5) * t * t + v0 * t + x0;
    return x * o.sign;
}

const PartPose& PoseBlender::update(const PartPose& target, int k, float dt, const BlendSettings& s) {
    if (!have || target.size() != out.size()) { out = prev = target; have = true; key = k; fadeT = fadeDur = inT = inDur = 0; return out; }
    if (k != key) {   // a new clip (or back to rest): start the change-over from what is on screen now
        key = k; fadeT = fadeDur = inT = inDur = 0;
        if (s.time > 0 && s.mode == BlendMode::Crossfade) { from = out; fadeDur = s.time; curve = s.curve; }
        else if (s.time > 0 && s.mode == BlendMode::Inertialize) {
            off.resize(target.size()); double idt = 1.0 / std::max(lastDt, 1e-4f);
            for (size_t i = 0; i < target.size(); i++) off[i] = makeOff(diff(out, target, i), diff(out, prev, i) * idt, s.time);
            inDur = s.time;
        }
    }
    lastDt = std::max(dt, 1e-4f);
    PartPose res = target;
    if (fadeT < fadeDur) {
        fadeT += dt; double w = applyCurve(curve, fadeT / fadeDur);
        for (size_t i = 0; i < res.size(); i++) res[i] = from[i] + diff(target, from, i) * w;
    } else if (inT < inDur) {
        inT += dt;
        for (size_t i = 0; i < res.size(); i++) res[i] = target[i] + evalOff(off[i], inT);
    }
    prev = out; out = res;
    return out;
}

static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '%') o += "%25"; else if (c == ' ') o += "%20"; else o += c; } return o.empty() ? "-" : o; }
static std::string unesc(const std::string& s) {
    if (s == "-") return ""; std::string o;
    for (size_t i = 0; i < s.size(); i++) { if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16); i += 2; } else o += s[i]; }
    return o;
}
static void put(std::ostream& f, const char* k, const std::string& name, const BlendSettings& b) { f << k << " " << esc(name) << " " << blendModeName(b.mode) << " " << blendCurveName(b.curve) << " " << b.time << "\n"; }
static bool get(std::istringstream& l, BlendSettings& b) {
    std::string m, c; float t; if (!(l >> m >> c >> t)) return false;
    b.mode = m == "crossfade" ? BlendMode::Crossfade : m == "cut" ? BlendMode::Cut : BlendMode::Inertialize;
    b.curve = c == "linear" ? BlendCurve::Linear : c == "ease_in" ? BlendCurve::EaseIn : c == "ease_out" ? BlendCurve::EaseOut : BlendCurve::Smooth;
    b.time = std::max(0.f, t); return true;
}
std::string AnimBlends::clipKey(const std::string& model, const std::string& clip) {
    std::string c = clip; for (char& ch : c) ch = (char)std::tolower((unsigned char)ch); return model + "/" + c;
}
bool saveAnimBlends(const std::string& path, const AnimBlends& a) {
    std::ofstream f(path); if (!f) return false;
    f << "anim_blend 1\n"; put(f, "default", "-", a.def);
    for (auto& kv : a.byModel) put(f, "model", kv.first, kv.second);
    for (auto& kv : a.clipSpeed) f << "speed " << esc(kv.first) << " " << kv.second << "\n";
    return (bool)f;
}
bool loadAnimBlends(const std::string& path, AnimBlends& a) {
    std::ifstream f(path); if (!f) return false;
    a = AnimBlends{}; std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k, n; l >> k >> n; BlendSettings b;
        if (k == "default" && get(l, b)) a.def = b;
        else if (k == "model" && get(l, b)) a.byModel[unesc(n)] = b;
        else if (k == "speed") { float v; if (l >> v) a.clipSpeed[unesc(n)] = std::clamp(v, 0.01f, 20.f); }
    }
    return true;
}

} // namespace mc
