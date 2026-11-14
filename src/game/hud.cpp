#include "hud.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace mc {

const char* hudKindName(HudKind k) { static const char* n[] = { "Panel", "Bar", "Text", "Image", "Circle" }; return n[std::clamp((int)k, 0, (int)HudKind::COUNT - 1)]; }
const char* hudValueName(HudValue v) { static const char* n[] = { "Nothing", "Health", "A counter", "Has a key", "Beam charge" }; return n[std::clamp((int)v, 0, (int)HudValue::COUNT - 1)]; }

static const double DEG = 3.14159265358979 / 180;

V3 rotateDeg(V3 p, float yaw, float pitch, float roll) {
    double r = roll * DEG, cr = std::cos(r), sr = std::sin(r);
    p = { p.x * cr - p.y * sr, p.x * sr + p.y * cr, p.z };
    double t = pitch * DEG, ct = std::cos(t), st = std::sin(t);
    p = { p.x, p.y * ct - p.z * st, p.y * st + p.z * ct };
    double y = yaw * DEG, cy = std::cos(y), sy = std::sin(y);
    return { p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy };
}
V3 planePoint(const HudPlane& p, float u, float v) {
    double lx = (u - 0.5) * p.width, ly = (0.5 - v) * p.height, x = lx, y = ly, z = 0;
    double a = std::clamp(p.bendX, 0.f, 1.f) * 1.5707963;
    if (a > 1e-4) { double R = p.width / a, th = lx / R; x = R * std::sin(th); z -= R * (1 - std::cos(th)); }
    double b = std::clamp(p.bendY, 0.f, 1.f) * 1.5707963;
    if (b > 1e-4) { double R = p.height / b, ph = ly / R; y = R * std::sin(ph); z -= R * (1 - std::cos(ph)); }
    return rotateDeg({ x, y, z }, p.yaw, p.pitch, p.roll) + p.pos;
}
void partAxes(const HudPart& p, V3& ax, V3& ay, V3& az) {
    ax = rotateDeg({ 1, 0, 0 }, p.yaw, p.pitch, p.roll) * (p.scale * (p.mirror ? -1 : 1));
    ay = rotateDeg({ 0, 1, 0 }, p.yaw, p.pitch, p.roll) * p.scale;
    az = rotateDeg({ 0, 0, 1 }, p.yaw, p.pitch, p.roll) * p.scale;
}
void mirrorPlane(HudPlane& p) { p.pos.x = -p.pos.x; p.yaw = -p.yaw; p.roll = -p.roll; }
void mirrorPart(HudPart& p) { p.pos.x = -p.pos.x; p.yaw = -p.yaw; p.roll = -p.roll; p.mirror = !p.mirror; }
void mirrorElement(HudElement& e) { e.x = 1 - e.x - e.w; if (e.align != 1) e.align = 2 - e.align; }

float hudValue(const HudElement& e, const HudValues& v) {
    switch (e.value) {
    case HudValue::Health: return v.health;
    case HudValue::Counter: { auto it = v.counts.find(e.valueName); return it == v.counts.end() ? 0.f : (float)it->second; }
    case HudValue::Key: return v.keys.count(e.valueName) ? 1.f : 0.f;
    case HudValue::Charge: return v.charge * 100;
    default: return hudMax(e, v);
    }
}
float hudMax(const HudElement& e, const HudValues& v) { return e.value == HudValue::Health ? v.maxHealth : e.value == HudValue::Key ? 1.f : e.value == HudValue::Charge ? 100.f : std::max(1e-3f, e.max); }
std::string hudText(const HudElement& e, const HudValues& v) {
    std::string s = e.text;
    auto put = [&](const std::string& key, float x) { char b[32]; snprintf(b, sizeof b, "%d", (int)std::lround(x)); for (size_t at; (at = s.find(key)) != std::string::npos;) s.replace(at, key.size(), b); };
    put("{value}", hudValue(e, v)); put("{max}", hudMax(e, v));
    return s;
}

// ------------------------------------------------------------------ sway
void HudSway::update(const HudSwaySettings& s, double dt, float turnYaw, float turnPitch, float speed, bool landed, float fallSpeed) {
    if (!s.enabled) { reset(); return; }
    dt = std::clamp(dt, 0.0, 0.1);
    float st = std::max(0.f, s.strength), lim = 8 * st;
    yaw = std::clamp(yaw - turnYaw * 0.6f * st, -lim, lim);           // the camera turned right: the HUD is left behind
    pitch = std::clamp(pitch + turnPitch * 0.6f * st, -lim, lim);
    float rollTarget = dt > 0 ? std::clamp(-turnYaw / (float)dt * 0.02f * s.tilt * st, -4.f, 4.f) : 0.f;
    float sm = std::clamp(s.smoothness, 0.f, 1.f), k = 220 + (18 - 220) * sm, c = 2 * std::sqrt(k) * 0.85f;
    if (landed) {   // a dip, and a shake that grows with how fast you came down
        float v = std::max(0.f, fallSpeed), impact = std::clamp((v - 4) / 22, 0.f, 1.f);
        dropVel -= std::min(v, 40.f) * 0.007f * s.land;
        shake = std::max(shake, impact * 2.5f * s.land); shakeT = 0;
    }
    shake *= (float)std::exp(-dt * 7); shakeT += (float)dt; if (shake < 1e-3f) shake = 0;
    for (double left = dt; left > 1e-9; left -= 1.0 / 240) {   // small steps: steady for any frame rate
        float h = (float)std::min(left, 1.0 / 240);
        vy += (-k * yaw - c * vy) * h; yaw += vy * h;
        vp += (-k * pitch - c * vp) * h; pitch += vp * h;
        vr += (-k * (roll - rollTarget) - c * vr) * h; roll += vr * h;
        dropVel += (-150 * drop - 2 * std::sqrt(150.f) * 0.7f * dropVel) * h; drop += dropVel * h;
    }
    float walking = std::min(speed / 4.3f, 1.f);
    walk += dt * speed * 1.7;
    offset = { std::sin(walk) * 0.004 * s.bob * walking, std::sin(walk * 2) * 0.006 * s.bob * walking + drop, 0 };
}
V3 HudSway::apply(V3 p, float amount) const {
    float sp = shake * std::sin(shakeT * 47.f), sr = shake * 0.6f * std::sin(shakeT * 31.f + 1.f);
    return rotateDeg(p, yaw * amount, (pitch + sp) * amount, (roll + sr) * amount) + offset * amount;
}

// ------------------------------------------------------------------ the starter visor
HudLayout defaultHud() {
    HudLayout h;
    HudPlane v; v.name = "Visor"; v.pos = { 0, 0, 1 }; v.width = 1.75f; v.height = 1.15f; v.bendX = 0.35f; v.bendY = 0.12f;
    auto el = [&](const char* name, HudKind k, float x, float y, float w, float hh) { HudElement e; e.name = name; e.kind = k; e.x = x; e.y = y; e.w = w; e.h = hh; e.color = 0x8FE6FF; return e; };
    HudElement frame = el("Frame", HudKind::Panel, 0.03f, 0.04f, 0.94f, 0.92f); frame.filled = false; frame.alpha = 0.22f; frame.thickness = 2; v.elements.push_back(frame);
    HudElement et = el("Energy", HudKind::Text, 0.07f, 0.07f, 0.3f, 0.05f); et.text = "ENERGY {value}"; et.value = HudValue::Health; et.textSize = 1; v.elements.push_back(et);
    HudElement eb = el("Energy bar", HudKind::Bar, 0.07f, 0.125f, 0.3f, 0.022f); eb.value = HudValue::Health; eb.segments = 10; v.elements.push_back(eb);
    HudElement mt = el("Missiles", HudKind::Text, 0.73f, 0.07f, 0.2f, 0.06f); mt.text = "{value}"; mt.value = HudValue::Counter; mt.valueName = "Missile"; mt.align = 2; mt.textSize = 1; v.elements.push_back(mt);
    HudElement ml = el("Missile label", HudKind::Text, 0.73f, 0.13f, 0.2f, 0.03f); ml.text = "MISSILES"; ml.align = 2; ml.textSize = 1; ml.alpha = 0.6f; v.elements.push_back(ml);
    HudElement radar = el("Radar", HudKind::Circle, 0.06f, 0.7f, 0.09f, 0.16f); radar.filled = false; radar.thickness = 3; radar.alpha = 0.6f; v.elements.push_back(radar);
    HudElement aim = el("Aim", HudKind::Circle, 0.49f, 0.482f, 0.02f, 0.036f); aim.filled = false; aim.alpha = 0.5f; v.elements.push_back(aim);
    h.planes.push_back(v);
    HudPart s; s.name = "Left strut"; s.model = "@visor-strut"; s.pos = { -0.62, -0.42, 0.55 }; s.roll = 18; s.yaw = -20; s.scale = 0.8;
    h.parts.push_back(s); mirrorPart(s); s.name = "Right strut"; h.parts.push_back(s);
    return h;
}

// ------------------------------------------------------------------ saving (one line each, texts escaped)
static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '%') o += "%25"; else if (c == ' ') o += "%20"; else if (c == '\n') o += "%0A"; else o += c; } return o.empty() ? "-" : o; }
static std::string unesc(const std::string& s) {
    if (s == "-") return ""; std::string o;
    for (size_t i = 0; i < s.size(); i++) { if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16); i += 2; } else o += s[i]; }
    return o;
}
V3 planeLight(const HudPlane& p) {
    V3 sum{ 0, 0, 0 }; if (!p.visible) return sum;
    for (const HudElement& e : p.elements) {
        if (!e.visible) continue;
        double cover = e.kind == HudKind::Text ? 0.3 : (e.kind == HudKind::Panel || e.kind == HudKind::Circle) && !e.filled ? 0.15 : e.kind == HudKind::Circle ? 0.785 : 1.0;   // how much of its box is lit
        double k = std::clamp((double)e.w, 0.0, 1.0) * std::clamp((double)e.h, 0.0, 1.0) * cover * std::clamp((double)e.alpha, 0.0, 1.0);
        auto lin = [](uint32_t c, int sh) { return std::pow(((c >> sh) & 255) / 255.0, 2.2); };
        sum = sum + V3{ lin(e.color, 16), lin(e.color, 8), lin(e.color, 0) } * k;
    }
    return sum * (double)std::clamp(p.opacity, 0.f, 1.f);
}

bool saveHud(const std::string& path, const HudLayout& h) {
    std::ofstream f(path); if (!f) return false;
    f << "hud 1\n";
    f << "sway " << h.sway.enabled << " " << h.sway.strength << " " << h.sway.smoothness << " " << h.sway.bob << " " << h.sway.tilt << " " << h.sway.land << "\n";
    f << "view " << h.fov << "\n";
    for (const HudPlane& p : h.planes) {
        f << "plane " << esc(p.name) << " " << p.pos.x << " " << p.pos.y << " " << p.pos.z << " " << p.yaw << " " << p.pitch << " " << p.roll << " " << p.width << " " << p.height
          << " " << p.bendX << " " << p.bendY << " " << p.sway << " " << p.opacity << " " << p.glow << " " << p.visible << " " << p.bloom << " " << p.lightUp << "\n";
        for (const HudElement& e : p.elements)
            f << "el " << esc(e.name) << " " << (int)e.kind << " " << e.x << " " << e.y << " " << e.w << " " << e.h << " " << e.color << " " << e.alpha << " " << esc(e.text) << " " << esc(e.image)
              << " " << (int)e.value << " " << esc(e.valueName) << " " << e.max << " " << e.segments << " " << e.textSize << " " << e.align << " " << e.filled << " " << e.thickness << " " << e.visible << "\n";
    }
    for (const HudPart& p : h.parts)
        f << "part " << esc(p.name) << " " << esc(p.model) << " " << p.pos.x << " " << p.pos.y << " " << p.pos.z << " " << p.yaw << " " << p.pitch << " " << p.roll << " " << p.scale
          << " " << p.mirror << " " << p.sway << " " << p.visible << "\n";
    return (bool)f;
}
bool loadHud(const std::string& path, HudLayout& h) {
    std::ifstream f(path); if (!f) return false;
    h = HudLayout{}; std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k;
        if (k == "sway") { l >> h.sway.enabled >> h.sway.strength >> h.sway.smoothness >> h.sway.bob >> h.sway.tilt; float v; if (l >> v) h.sway.land = v; }
        else if (k == "view") { float v; if (l >> v) h.fov = std::clamp(v, 20.f, 150.f); }
        else if (k == "plane") { HudPlane p; std::string n; l >> n >> p.pos.x >> p.pos.y >> p.pos.z >> p.yaw >> p.pitch >> p.roll >> p.width >> p.height >> p.bendX >> p.bendY >> p.sway >> p.opacity >> p.glow >> p.visible; { float b, lu; if (l >> b >> lu) { p.bloom = b; p.lightUp = lu; } } p.name = unesc(n); h.planes.push_back(p); }
        else if (k == "el" && !h.planes.empty()) {
            HudElement e; std::string n, t, im, vn; int kind = 0, val = 0;
            l >> n >> kind >> e.x >> e.y >> e.w >> e.h >> e.color >> e.alpha >> t >> im >> val >> vn >> e.max >> e.segments >> e.textSize >> e.align >> e.filled >> e.thickness >> e.visible;
            e.name = unesc(n); e.text = unesc(t); e.image = unesc(im); e.valueName = unesc(vn);
            e.kind = (HudKind)std::clamp(kind, 0, (int)HudKind::COUNT - 1); e.value = (HudValue)std::clamp(val, 0, (int)HudValue::COUNT - 1);
            h.planes.back().elements.push_back(e);
        } else if (k == "part") { HudPart p; std::string n, m; l >> n >> m >> p.pos.x >> p.pos.y >> p.pos.z >> p.yaw >> p.pitch >> p.roll >> p.scale >> p.mirror >> p.sway >> p.visible; p.name = unesc(n); p.model = unesc(m); h.parts.push_back(p); }
    }
    return true;
}

} // namespace mc
