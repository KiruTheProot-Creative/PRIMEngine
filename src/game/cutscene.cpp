#include "cutscene.h"
#include "objects.h"
#include "particles.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace mc {

const char* cutViewName(CutView v) { static const char* n[] = { "Camera (a film camera)", "Visor (first person)" }; return n[std::clamp((int)v, 0, (int)CutView::COUNT - 1)]; }
const char* cutTextStyleName(CutTextStyle s) { static const char* n[] = { "Subtitle", "Comms (a speaker's box)", "Visor readout", "Title (big, in the middle)" }; return n[std::clamp((int)s, 0, (int)CutTextStyle::COUNT - 1)]; }
const char* cutFadeName(CutFadeKind k) { static const char* n[] = { "In from black", "Out to black", "White flash", "In from white", "Out to white" }; return n[std::clamp((int)k, 0, (int)CutFadeKind::COUNT - 1)]; }

static double wrap180(double a) { a = std::fmod(a + 180.0, 360.0); if (a < 0) a += 360.0; return a - 180.0; }
static double smooth01(double x) { x = std::clamp(x, 0.0, 1.0); return x * x * (3 - 2 * x); }

// ---- a spline through keyed values, in time: Hermite pieces; a key that eases (and the first and last) is passed at rest,
// the others at the speed of the keys around them; a cut starts a new piece of track (no speed taken across it)
namespace {
struct Spline {
    std::vector<double> t;                 // key times
    std::vector<std::vector<double>> v;    // per key: the values
    std::vector<char> ease, cut;
    int n() const { return (int)t.size(); }
    std::vector<double> vel(int j) const {
        size_t m = v[j].size(); std::vector<double> out(m, 0.0);
        if (ease[j] || j == 0 || j == n() - 1) return out;
        bool shotStart = cut[j] != 0, shotEnd = j + 1 < n() && cut[j + 1];
        int a = shotStart ? j : j - 1, b = shotEnd ? j : j + 1;
        double dt = t[b] - t[a]; if (dt <= 1e-6) return out;
        for (size_t k = 0; k < m; k++) out[k] = (v[b][k] - v[a][k]) / dt;
        return out;
    }
    std::vector<double> at(double time) const {
        if (n() == 0) return {};
        if (n() == 1 || time <= t[0]) return v[0];
        if (time >= t[n() - 1]) return v[n() - 1];
        int i = 0; while (i + 1 < n() && t[i + 1] <= time) i++;
        if (cut[i + 1]) return v[i];       // holds until the cut
        double h = t[i + 1] - t[i]; if (h <= 1e-6) return v[i + 1];
        double u = (time - t[i]) / h, u2 = u * u, u3 = u2 * u;
        double h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
        std::vector<double> m0 = vel(i), m1 = vel(i + 1), out(v[i].size());
        for (size_t k = 0; k < out.size(); k++) out[k] = h00 * v[i][k] + h10 * h * m0[k] + h01 * v[i + 1][k] + h11 * h * m1[k];
        return out;
    }
};
template <class K> std::vector<int> order(const std::vector<K>& keys) {
    std::vector<int> o(keys.size()); for (size_t i = 0; i < o.size(); i++) o[i] = (int)i;
    std::stable_sort(o.begin(), o.end(), [&](int a, int b) { return keys[a].t < keys[b].t; });
    return o;
}
}

CamPose cameraAt(const Cutscene& c, float t) {
    CamPose p; if (c.cam.empty()) return p;
    Spline s; double prevYaw = 0; bool first = true;
    for (int i : order(c.cam)) {
        const CamKey& k = c.cam[i];
        double yaw = first ? k.yaw : prevYaw + wrap180(k.yaw - prevYaw); prevYaw = yaw; first = false;   // the short way round
        s.t.push_back(k.t); s.v.push_back({ k.pos.x, k.pos.y, k.pos.z, yaw, k.pitch, k.roll, k.fov }); s.ease.push_back(k.ease); s.cut.push_back(k.cut);
    }
    std::vector<double> v = s.at(t);
    p.pos = { v[0], v[1], v[2] }; p.yaw = wrap180(v[3]); p.pitch = std::clamp(v[4], -89.5, 89.5); p.roll = v[5]; p.fov = std::clamp(v[6], 5.0, 150.0); p.valid = true;
    return p;
}

CamPose blendPose(const CamPose& a, const CamPose& b, double f) {
    f = std::clamp(f, 0.0, 1.0); CamPose p;
    p.pos = a.pos + (b.pos - a.pos) * f;
    p.yaw = wrap180(a.yaw + wrap180(b.yaw - a.yaw) * f);
    p.pitch = a.pitch + (b.pitch - a.pitch) * f; p.roll = a.roll + wrap180(b.roll - a.roll) * f; p.fov = a.fov + (b.fov - a.fov) * f;
    p.valid = a.valid || b.valid;
    return p;
}

V3 poseForward(const CamPose& p) {
    double y = p.yaw * 3.14159265358979 / 180, x = p.pitch * 3.14159265358979 / 180;
    return { std::sin(y) * std::cos(x), std::sin(x), std::cos(y) * std::cos(x) };
}

ActorPose actorAt(const CutActor& a, float t, float length) {
    ActorPose p;
    float to = a.showTo < 0 ? std::max(length, t) + 1 : a.showTo;
    p.shown = t >= a.showFrom && t < to;
    if (!a.keys.empty()) {
        Spline s; double prevYaw = 0; bool first = true;
        for (int i : order(a.keys)) {
            const ActorKey& k = a.keys[i];
            double yaw = first ? k.yaw : prevYaw + wrap180(k.yaw - prevYaw); prevYaw = yaw; first = false;
            s.t.push_back(k.t); s.v.push_back({ k.pos.x, k.pos.y, k.pos.z, yaw, k.pitch, k.roll }); s.ease.push_back(k.ease); s.cut.push_back(k.cut);   // (roll is not wrapped: 0 -> 360 is a whole barrel roll)
        }
        std::vector<double> v = s.at(t);
        p.pos = { v[0], v[1], v[2] }; p.yaw = wrap180(v[3]); p.pitch = v[4]; p.roll = v[5]; p.keyed = true;
    }
    int held = -1;
    for (int i : order(a.clips)) {   // the latest bar that is playing
        const ActorClip& c = a.clips[i];
        if (t >= c.t && t < c.t + std::max(0.01f, c.length)) { p.clip = i; p.clipName = c.clip; p.clipTime = (t - c.t) * c.speed; p.clipSpeed = c.speed; p.hold = c.once; }
        else if (t >= c.t) held = c.once ? i : -1;   // (a bar that is over: a "once" one keeps its last pose until the next bar starts)
    }
    if (p.clip < 0 && held >= 0) { const ActorClip& c = a.clips[held]; p.clip = held; p.clipName = c.clip; p.clipTime = std::max(0.01f, c.length) * c.speed; p.clipSpeed = c.speed; p.hold = true; }
    return p;
}

FadeNow fadeAt(const Cutscene& c, float t) {
    FadeNow f;
    for (int i : order(c.fades)) {
        const CutFade& d = c.fades[i]; if (t < d.t) continue;
        double len = std::max(0.01f, d.length), p = std::clamp((t - d.t) / len, 0.0, 1.0);
        switch (d.kind) {
        case CutFadeKind::FromBlack: f = { (float)(1 - smooth01(p)), false }; break;
        case CutFadeKind::ToBlack: f = { (float)smooth01(p), false }; break;
        case CutFadeKind::Flash: f = { (float)(1 - p) * (float)(1 - p), true }; break;
        case CutFadeKind::FromWhite: f = { (float)(1 - smooth01(p)), true }; break;
        case CutFadeKind::ToWhite: f = { (float)smooth01(p), true }; break;
        default: break;
        }
    }
    return f;
}

float letterboxAt(const Cutscene& c, float t, float) {
    if (!c.letterbox || c.view != CutView::Camera) return 0;
    double in = t / 0.6, out = (c.length - t) / 0.6;
    return (float)smooth01(std::min({ in, out, 1.0 }));
}

static size_t letters(const std::string& s) { size_t n = 0; for (unsigned char ch : s) if ((ch & 0xC0) != 0x80) n++; return n; }
size_t cutTypedBytes(const CutText& x, float t) {
    if (t < x.t) return 0;
    if (x.typeSpeed <= 0) return x.text.size();
    size_t want = (size_t)std::floor((t - x.t) * x.typeSpeed), seen = 0;
    for (size_t i = 0; i < x.text.size(); i++) {
        if (((unsigned char)x.text[i] & 0xC0) != 0x80) { if (seen == want) return i; seen++; }
    }
    return x.text.size();
}
float cutTextAlpha(const CutText& x, float t) {
    if (t < x.t || t > x.t + x.length) return 0;
    double in = (t - x.t) / 0.2, out = (x.t + x.length - t) / 0.3;
    return (float)std::clamp(std::min(in, out), 0.0, 1.0);
}

bool isLasting(Do d) {
    switch (d) {
    case Do::Message: case Do::Particles: case Do::Wait: case Do::HudFlash: case Do::Shake: case Do::VisorEffect: case Do::Hurt: case Do::Kill: case Do::PlayCutscene: return false;
    default: return true;
    }
}
std::vector<EventDo> skippedActions(const Cutscene& c, float from) {
    std::vector<EventDo> out;
    for (int i : order(c.events)) if (c.events[i].t > from && isLasting(c.events[i].action.kind)) out.push_back(c.events[i].action);
    return out;
}
template <class K> static std::vector<int> between(const std::vector<K>& v, float from, float to) {
    std::vector<int> out; for (int i : order(v)) if (v[i].t > from && v[i].t <= to) out.push_back(i); return out;
}
std::vector<int> eventsBetween(const Cutscene& c, float from, float to) { return between(c.events, from, to); }
std::vector<int> soundsBetween(const Cutscene& c, float from, float to) { return between(c.sounds, from, to); }

void sortTracks(Cutscene& c) {
    auto byT = [](const auto& a, const auto& b) { return a.t < b.t; };
    std::stable_sort(c.cam.begin(), c.cam.end(), byT);
    for (CutActor& a : c.actors) { std::stable_sort(a.keys.begin(), a.keys.end(), byT); std::stable_sort(a.clips.begin(), a.clips.end(), byT); }
    std::stable_sort(c.texts.begin(), c.texts.end(), byT); std::stable_sort(c.sounds.begin(), c.sounds.end(), byT);
    std::stable_sort(c.fades.begin(), c.fades.end(), byT); std::stable_sort(c.events.begin(), c.events.end(), byT);
    std::stable_sort(c.space.begin(), c.space.end(), byT); std::stable_sort(c.effects.begin(), c.effects.end(), byT);
}
const CutSpace* spaceAt(const Cutscene& c, float t) {
    const CutSpace* s = nullptr;
    for (int i : order(c.space)) if (t >= c.space[i].t && t < c.space[i].t + std::max(0.01f, c.space[i].length)) s = &c.space[i];
    return s;
}
bool effectOn(const CutEffect& e, float t) { return t >= e.t && t < e.t + std::max(0.01f, e.length); }
V3 effectSpot(const V3& pos, double yawDeg, double pitchDeg, double rollDeg, const V3& o) {
    V3 ax, ay, az; turnAxes(yawDeg, pitchDeg, rollDeg, ax, ay, az); return pos + ax * o.x + ay * o.y + az * o.z;
}
V3 effectSpot(const V3& pos, double yawDeg, const V3& o) {   // (the same turn as an object's: its x (cos, 0, -sin), its z (sin, 0, cos))
    double a = yawDeg * 3.14159265358979 / 180, c = std::cos(a), s = std::sin(a);
    return { pos.x + c * o.x + s * o.z, pos.y + o.y, pos.z - s * o.x + c * o.z };
}
std::vector<std::string> cutEffectPresets() {
    std::vector<std::string> v = { "Re-entry fire", "Landing dust", "Thrusters" };
    for (const std::string& p : particlePresets()) v.push_back(p);
    return v;
}
float lastMoment(const Cutscene& c) {
    float m = 0;
    for (auto& k : c.cam) m = std::max(m, k.t);
    for (auto& a : c.actors) { for (auto& k : a.keys) m = std::max(m, k.t); for (auto& k : a.clips) m = std::max(m, k.t + k.length); if (a.showTo > 0) m = std::max(m, a.showTo); }
    for (auto& k : c.texts) m = std::max(m, k.t + k.length);
    for (auto& k : c.sounds) m = std::max(m, k.t);
    for (auto& k : c.fades) m = std::max(m, k.t + k.length);
    for (auto& k : c.events) m = std::max(m, k.t);
    for (auto& k : c.space) m = std::max(m, k.t + k.length);
    for (auto& k : c.effects) m = std::max(m, k.t + k.length);
    return m;
}
int findCutscene(const std::vector<Cutscene>& list, const std::string& name) {
    for (size_t i = 0; i < list.size(); i++) if (list[i].name == name) return (int)i;
    return -1;
}

// ------------------------------------------------------------------ saving (one line per thing, names escaped)
static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '%') o += "%25"; else if (c == ' ') o += "%20"; else if (c == '\n') o += "%0A"; else if (c == '\t') o += "%09"; else o += c; } return o.empty() ? "-" : o; }
static std::string unesc(const std::string& s) {
    if (s == "-") return ""; std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16); i += 2; } else o += s[i];
    }
    return o;
}
bool saveCutscenes(const std::string& path, const std::vector<Cutscene>& list) {
    std::ofstream f(path); if (!f) return false;
    f.precision(10);
    f << "cutscenes 1\n";
    for (const Cutscene& c : list) {
        f << "cut " << esc(c.name) << " " << (int)c.view << " " << c.length << " " << c.letterbox << " " << c.skippable << " " << c.onNewGame << " " << c.fromPlayer << " "
          << c.lookOnly << " " << c.handOff << " " << c.blendOut << " " << c.lookAround << "\n";
        for (const CamKey& k : c.cam) f << "cam " << k.t << " " << k.pos.x << " " << k.pos.y << " " << k.pos.z << " " << k.yaw << " " << k.pitch << " " << k.roll << " " << k.fov << " " << k.ease << " " << k.cut << "\n";
        for (const CutActor& a : c.actors) {
            f << "actor " << esc(a.name) << " " << esc(a.object) << " " << esc(a.model) << " " << a.scale << " " << a.showFrom << " " << a.showTo << "\n";
            for (const ActorKey& k : a.keys) f << "akey " << k.t << " " << k.pos.x << " " << k.pos.y << " " << k.pos.z << " " << k.yaw << " " << k.ease << " " << k.cut << " " << k.pitch << " " << k.roll << "\n";
            for (const ActorClip& k : a.clips) f << "aclip " << k.t << " " << k.length << " " << esc(k.clip) << " " << k.speed << " " << (k.once ? 1 : 0) << "\n";
        }
        for (const CutText& x : c.texts) f << "text " << x.t << " " << x.length << " " << (int)x.style << " " << x.typeSpeed << " " << esc(x.speaker) << " " << esc(x.text) << "\n";
        for (const CutSound& s : c.sounds) f << "sound " << s.t << " " << esc(s.sound.file) << " " << s.sound.volume << " " << s.sound.pitch << " " << s.sound.pitchVar << "\n";
        for (const CutFade& d : c.fades) f << "fade " << d.t << " " << d.length << " " << (int)d.kind << "\n";
        for (const CutSpace& s : c.space) f << "space " << s.t << " " << s.length << " " << esc(s.planet) << " " << s.yaw << " " << s.pitch << " " << s.size << " " << s.spin << " " << s.axisTilt << " "
                                             << s.stars << " " << s.nebula << " " << s.nebulaColor << " " << s.bright << " " << s.sunYaw << " " << s.sunPitch << "\n";
        for (const CutEffect& e : c.effects) f << "effect " << e.t << " " << e.length << " " << esc(e.actor) << " " << esc(e.preset) << " " << e.offset.x << " " << e.offset.y << " " << e.offset.z << " " << e.size << "\n";
        for (const CutEvent& e : c.events) f << "event " << e.t << " " << (int)e.action.kind << " " << esc(e.action.target) << " " << esc(e.action.text) << " " << e.action.number << "\n";
    }
    return (bool)f;
}
bool loadCutscenes(const std::string& path, std::vector<Cutscene>& list) {
    list.clear(); std::ifstream f(path); if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k;
        if (k == "cut") {
            Cutscene c; std::string n; int v = 0;
            l >> n >> v >> c.length >> c.letterbox >> c.skippable >> c.onNewGame >> c.fromPlayer >> c.lookOnly >> c.handOff >> c.blendOut >> c.lookAround;
            c.name = unesc(n); c.view = (CutView)std::clamp(v, 0, (int)CutView::COUNT - 1); list.push_back(c);
        }
        else if (list.empty()) continue;
        else if (k == "cam") { CamKey q; l >> q.t >> q.pos.x >> q.pos.y >> q.pos.z >> q.yaw >> q.pitch >> q.roll >> q.fov >> q.ease >> q.cut; list.back().cam.push_back(q); }
        else if (k == "actor") { CutActor a; std::string n, o, m; l >> n >> o >> m >> a.scale >> a.showFrom >> a.showTo; a.name = unesc(n); a.object = unesc(o); a.model = unesc(m); list.back().actors.push_back(a); }
        else if (k == "akey" && !list.back().actors.empty()) { ActorKey q; l >> q.t >> q.pos.x >> q.pos.y >> q.pos.z >> q.yaw >> q.ease >> q.cut; if (!(l >> q.pitch >> q.roll)) q.pitch = q.roll = 0; list.back().actors.back().keys.push_back(q); }
        else if (k == "aclip" && !list.back().actors.empty()) { ActorClip q; std::string n; l >> q.t >> q.length >> n >> q.speed; int once = 0; if (l >> once) q.once = once != 0; q.clip = unesc(n); list.back().actors.back().clips.push_back(q); }
        else if (k == "text") { CutText x; int s = 0; std::string sp, tx; l >> x.t >> x.length >> s >> x.typeSpeed >> sp >> tx; x.style = (CutTextStyle)std::clamp(s, 0, (int)CutTextStyle::COUNT - 1); x.speaker = unesc(sp); x.text = unesc(tx); list.back().texts.push_back(x); }
        else if (k == "sound") { CutSound s; std::string fl; l >> s.t >> fl >> s.sound.volume >> s.sound.pitch; float pv; if (l >> pv) s.sound.pitchVar = pv; s.sound.file = unesc(fl); list.back().sounds.push_back(s); }
        else if (k == "fade") { CutFade d; int kd = 0; l >> d.t >> d.length >> kd; d.kind = (CutFadeKind)std::clamp(kd, 0, (int)CutFadeKind::COUNT - 1); list.back().fades.push_back(d); }
        else if (k == "space") { CutSpace s; std::string n; l >> s.t >> s.length >> n >> s.yaw >> s.pitch >> s.size >> s.spin >> s.axisTilt >> s.stars >> s.nebula >> s.nebulaColor >> s.bright; float sy, sp; if (l >> sy >> sp) { s.sunYaw = sy; s.sunPitch = sp; } s.planet = unesc(n); list.back().space.push_back(s); }
        else if (k == "effect") { CutEffect e; std::string a, p; l >> e.t >> e.length >> a >> p >> e.offset.x >> e.offset.y >> e.offset.z >> e.size; e.actor = unesc(a); e.preset = unesc(p); list.back().effects.push_back(e); }
        else if (k == "event") { CutEvent e; int kd = 0; std::string a, b; l >> e.t >> kd >> a >> b >> e.action.number; e.action.kind = (Do)std::clamp(kd, 0, (int)Do::COUNT - 1); e.action.target = unesc(a); e.action.text = unesc(b); list.back().events.push_back(e); }
    }
    return true;
}

} // namespace mc
