#include "crosshair.h"
#include "visors.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace mc {

const char* xhShapeName(XhShape s) {
    static const char* n[] = { "Dot", "Ring", "Arcs", "Corner brackets", "Ticks", "Cross", "Triangles", "Picture" };
    return n[std::clamp((int)s, 0, (int)XhShape::COUNT - 1)];
}
const char* xhStateName(XhState s) {
    static const char* n[] = { "Aiming", "On a target", "Charging (full)", "Firing", "Out of reach", "Locking", "Locked", "Losing it" };
    return n[std::clamp((int)s, 0, XH_STATES - 1)];
}
const Crosshair* CrosshairSet::find(const std::string& name) const { for (const Crosshair& c : list) if (c.name == name) return &c; return nullptr; }
const Crosshair* CrosshairSet::forVisor(int v) const {
    if (v >= 0 && v < VISORS && !visor[v].empty()) if (const Crosshair* c = find(visor[v])) return c;
    for (const Crosshair& c : list) if (!c.lock) return &c;
    return nullptr;
}

static XhLayer layer(const char* name, XhShape s, float size, float thick, uint32_t col, float alpha) {
    XhLayer L; L.name = name; L.shape = s; L.size = size; L.thick = thick; L.color = col; L.alpha = alpha; return L;
}
CrosshairSet defaultCrosshairs() {
    CrosshairSet set;
    {   // aiming: a dot, four arcs that close in and turn while charging, ticks that spread when firing
        Crosshair c; c.name = "Simple";
        XhLayer dot = layer("Dot", XhShape::Dot, 2.2f, 0, 0xE8F4FF, 0.95f);
        XhLayer arcs = layer("Arcs", XhShape::Arcs, 17, 1.6f, 0xBFE6FF, 0.75f); arcs.count = 4; arcs.gap = 40; arcs.turn = 45;
        arcs.look[(int)XhState::OnTarget].ownColor = true; arcs.look[(int)XhState::OnTarget].color = 0xFFB050; arcs.look[(int)XhState::OnTarget].scale = 0.85f;
        arcs.look[(int)XhState::Charging].scale = 0.55f; arcs.look[(int)XhState::Charging].spin = 220; arcs.look[(int)XhState::Charging].alpha = 1.3f;
        arcs.look[(int)XhState::Blocked].alpha = 0.35f;
        XhLayer ticks = layer("Ticks", XhShape::Ticks, 27, 1.6f, 0xBFE6FF, 0.55f); ticks.count = 4;
        ticks.look[(int)XhState::Firing].scale = 1.35f; ticks.look[(int)XhState::Firing].alpha = 1.4f;
        ticks.look[(int)XhState::Blocked].alpha = 0.2f; ticks.look[(int)XhState::Charging].alpha = 0.2f;
        c.layers = { dot, arcs, ticks }; set.list.push_back(c);
    }
    {   // scanning: corner brackets that close in on what can be scanned
        Crosshair c; c.name = "Scan";
        XhLayer br = layer("Brackets", XhShape::Brackets, 22, 1.8f, 0xFFB050, 0.8f); br.count = 4;
        br.look[(int)XhState::OnTarget].scale = 0.7f; br.look[(int)XhState::OnTarget].alpha = 1.2f;
        XhLayer dot = layer("Dot", XhShape::Dot, 1.8f, 0, 0xFFD8A0, 0.9f);
        c.layers = { br, dot }; set.list.push_back(c);
    }
    {   // locked on: brackets spinning in onto the target, then turning slowly
        Crosshair c; c.name = "Lock-on"; c.lock = true; c.blend = 9;
        XhLayer br = layer("Brackets", XhShape::Brackets, 30, 2.2f, 0xFF6A3A, 0.95f); br.count = 4; br.spin = 25;
        br.look[(int)XhState::Locking].scale = 2.2f; br.look[(int)XhState::Locking].alpha = 0.2f; br.look[(int)XhState::Locking].turn = -90;
        br.look[(int)XhState::Lost].scale = 1.5f; br.look[(int)XhState::Lost].alpha = 0.25f;
        XhLayer ring = layer("Ring", XhShape::Ring, 36, 1.2f, 0xFF6A3A, 0.4f);
        ring.look[(int)XhState::Locking].alpha = 0; ring.look[(int)XhState::Lost].alpha = 0;
        XhLayer dot = layer("Dot", XhShape::Dot, 2.6f, 0, 0xFFC0A0, 0.95f);
        c.layers = { br, ring, dot }; set.list.push_back(c);
    }
    {   // what a press would lock onto
        Crosshair c; c.name = "Lock hint"; c.lock = true;
        XhLayer br = layer("Corners", XhShape::Brackets, 20, 1.8f, 0xFFD070, 0.8f); br.count = 4;
        c.layers = { br }; set.list.push_back(c);
    }
    set.visor[(int)VisorKind::Scan] = "Scan"; set.lockOn = "Lock-on"; set.lockHint = "Lock hint";
    return set;
}

// ---- saving: one line per crosshair, one per layer, one per layer's look in a state
bool saveCrosshairs(const std::string& path, const CrosshairSet& s) {
    std::ofstream f(path); if (!f) return false;
    f << "crosshairs 1\n";
    for (int v = 0; v < VISORS; v++) f << "visor " << v << " " << escText(s.visor[v]) << "\n";
    f << "lock " << escText(s.lockOn) << " " << escText(s.lockHint) << " " << (s.hideWhileLocked ? 1 : 0) << "\n";
    for (const Crosshair& c : s.list) {
        f << "crosshair " << escText(c.name) << " " << (c.lock ? 1 : 0) << " " << c.blend << "\n";
        for (const XhLayer& L : c.layers) {
            f << "layer " << escText(L.name) << " " << (int)L.shape << " " << escText(L.picture) << " " << L.color << " " << L.alpha << " " << L.size << " " << L.thick << " " << L.glow
              << " " << L.count << " " << L.gap << " " << L.turn << " " << L.spin << " " << L.x << " " << L.y << "\n";
            for (int k = 0; k < XH_STATES; k++) {
                const XhLook& o = L.look[k]; XhLook d;
                if (o.shown == d.shown && o.scale == d.scale && o.alpha == d.alpha && o.turn == d.turn && o.spin == d.spin && o.ownColor == d.ownColor) continue;   // (as it is: not written)
                f << "look " << k << " " << (o.shown ? 1 : 0) << " " << o.scale << " " << o.alpha << " " << o.turn << " " << o.spin << " " << (o.ownColor ? 1 : 0) << " " << o.color << "\n";
            }
        }
    }
    return (bool)f;
}
bool loadCrosshairs(const std::string& path, CrosshairSet& s) {
    std::ifstream f(path); if (!f) return false;
    CrosshairSet out; std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k;
        if (k == "visor") { int v = -1; std::string n; if (l >> v >> n && v >= 0 && v < VISORS) out.visor[v] = unescText(n); }
        else if (k == "lock") { std::string a, b; int h = 0; if (l >> a >> b >> h) { out.lockOn = unescText(a); out.lockHint = unescText(b); out.hideWhileLocked = h != 0; } }
        else if (k == "crosshair") { Crosshair c; std::string n; int lk = 0; if (l >> n >> lk >> c.blend) { c.name = unescText(n); c.lock = lk != 0; c.blend = std::clamp(c.blend, 0.5f, 60.f); out.list.push_back(c); } }
        else if (k == "layer" && !out.list.empty()) {
            XhLayer L; std::string n, pic; int sh = 1;
            if (l >> n >> sh >> pic >> L.color >> L.alpha >> L.size >> L.thick >> L.glow >> L.count >> L.gap >> L.turn >> L.spin >> L.x >> L.y) {
                L.name = unescText(n); L.shape = (XhShape)std::clamp(sh, 0, (int)XhShape::COUNT - 1); L.picture = unescText(pic); L.count = std::clamp(L.count, 1, 16);
                out.list.back().layers.push_back(L);
            }
        }
        else if (k == "look" && !out.list.empty() && !out.list.back().layers.empty()) {
            int st = -1, sh = 1, oc = 0; XhLook o;
            if (l >> st >> sh >> o.scale >> o.alpha >> o.turn >> o.spin >> oc >> o.color && st >= 0 && st < XH_STATES) { o.shown = sh != 0; o.ownColor = oc != 0; out.list.back().layers.back().look[st] = o; }
        }
    }
    s = out; return true;
}

// ---- the looks
static XhNow lookOf(const XhLayer& L, const XhLook& o) {
    XhNow n; n.scale = o.scale; n.alpha = o.alpha * L.alpha; n.turn = L.turn + o.turn; n.spin = L.spin + o.spin; n.shown = o.shown ? 1.f : 0.f;
    uint32_t c = o.ownColor ? o.color : L.color; n.r = ((c >> 16) & 255) / 255.f; n.g = ((c >> 8) & 255) / 255.f; n.b = (c & 255) / 255.f; n.init = true;
    return n;
}
static XhNow mix(const XhNow& a, const XhNow& b, float k) {
    XhNow n; auto m = [&](float x, float y) { return x + (y - x) * k; };
    n.scale = m(a.scale, b.scale); n.alpha = m(a.alpha, b.alpha); n.turn = m(a.turn, b.turn); n.spin = m(a.spin, b.spin); n.shown = m(a.shown, b.shown);
    n.r = m(a.r, b.r); n.g = m(a.g, b.g); n.b = m(a.b, b.b); n.init = true; return n;
}
XhNow xhTarget(const XhLayer& L, XhState base, float charge, float kick) {
    XhNow n = lookOf(L, L.look[std::clamp((int)base, 0, XH_STATES - 1)]);
    if (charge > 0 && base != XhState::Charging) n = mix(n, lookOf(L, L.look[(int)XhState::Charging]), std::clamp(charge, 0.f, 1.f));   // closing in as it fills
    if (kick > 0) n = mix(n, lookOf(L, L.look[(int)XhState::Firing]), std::clamp(kick, 0.f, 1.f));
    return n;
}
void xhGlide(XhNow& now, const XhNow& to, float dt, float speed) {
    if (!now.init) { now = to; return; }
    float k = 1 - std::exp(-std::max(0.f, dt) * std::max(0.1f, speed));
    now = mix(now, to, k);
}
void XhRun::update(const Crosshair& c, XhState s, float charge, float kick, float dt) {
    if (now.size() != c.layers.size()) { now.assign(c.layers.size(), XhNow{}); angle.assign(c.layers.size(), 0.f); }
    for (size_t i = 0; i < c.layers.size(); i++) { xhGlide(now[i], xhTarget(c.layers[i], s, charge, kick), dt, c.blend); angle[i] = std::fmod(angle[i] + now[i].spin * std::max(0.f, dt), 360.f); }
}

} // namespace mc
