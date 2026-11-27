#pragma once
// Crosshairs: made of layers - ready-made shapes (a dot, a ring, arcs, corner brackets, ticks, a cross, little triangles) and your
// own pictures. Each layer looks a little different in each state (aiming at nothing, at a target, charging, firing, out of reach;
// a lock-on reticle: locking, locked, losing it) and glides from one look to the next.
// Each visor can have its own crosshair; lock-on has its own reticle that sits on the target. Saved in the project's crosshairs.txt.
// Plain C++, tested on Linux; the app draws them.
#include "visors.h"
#include <string>
#include <vector>

namespace mc {

enum class XhShape { Dot, Ring, Arcs, Brackets, Ticks, Cross, Triangles, Picture, COUNT };
const char* xhShapeName(XhShape s);
// the states: the first five for a crosshair, the last three for a lock-on reticle
enum class XhState { Idle, OnTarget, Charging, Firing, Blocked, Locking, Locked, Lost, COUNT };
const char* xhStateName(XhState s);
constexpr int XH_STATES = (int)XhState::COUNT;

struct XhLook {                // a layer in one state
    bool shown = true;
    float scale = 1, alpha = 1;   // its size and how visible (times the layer's own)
    float turn = 0, spin = 0;     // turned more (degrees), and turning (degrees a second) on top of the layer's own
    bool ownColor = false; uint32_t color = 0xFFFFFF;   // a colour of its own in this state
};
struct XhLayer {
    std::string name = "Layer";
    XhShape shape = XhShape::Ring;
    std::string picture;          // Picture: a picture of the project's (textures), drawn size x size
    uint32_t color = 0xFFFFFF; float alpha = 0.9f;
    float size = 18;              // its radius (pixels on a 1080-line screen)
    float thick = 2;              // line thickness (pixels)
    float glow = 0.35f;           // a soft glow round it (0: none)
    int count = 4;                // arcs / brackets / ticks / triangles round it
    float gap = 30;               // arcs: degrees left open between them; cross: the hole in the middle (pixels)
    float turn = 0, spin = 0;     // turned (degrees), turning (degrees a second)
    float x = 0, y = 0;           // moved from the middle (pixels)
    XhLook look[XH_STATES];
};
struct Crosshair {
    std::string name = "Crosshair";
    bool lock = false;            // a lock-on reticle (its states: locking, locked, losing it)
    float blend = 12;             // how fast it glides from one look to the next (bigger: quicker)
    std::vector<XhLayer> layers;
};
struct CrosshairSet {
    std::vector<Crosshair> list;
    std::string visor[VISORS];    // each visor's crosshair ("": the first one that is not a lock-on reticle)
    std::string lockOn;           // the reticle on what you are locked onto ("": the built-in one)
    std::string lockHint;         // on what a press would lock onto ("": the built-in corners)
    bool hideWhileLocked = false; // the crosshair hides while locked on (else both show)
    const Crosshair* find(const std::string& name) const;
    const Crosshair* forVisor(int v) const;
};
CrosshairSet defaultCrosshairs();   // a simple crosshair, one for the Scan Visor, a lock-on reticle and a hint
bool saveCrosshairs(const std::string& path, const CrosshairSet& s);
bool loadCrosshairs(const std::string& path, CrosshairSet& s);

// ---- a layer's look now: gliding toward its state's look (the app keeps one per layer)
struct XhNow { float scale = 1, alpha = 1, turn = 0, spin = 0, shown = 1; float r = 1, g = 1, b = 1; bool init = false; };
// the look a layer wants: its state's, and while charging between the resting look and the charging one by how full it is (0..1);
// firing: a short kick on top (kick 1 -> 0)
XhNow xhTarget(const XhLayer& L, XhState base, float charge, float kick);
void xhGlide(XhNow& now, const XhNow& to, float dt, float speed);
// all the layers of a crosshair gliding (resized when the layers change)
struct XhRun {
    std::vector<XhNow> now; std::vector<float> angle;   // angle: each layer's turn so far from spinning (degrees)
    void update(const Crosshair& c, XhState s, float charge, float kick, float dt);
    float turnOf(size_t i) const { return i < now.size() ? now[i].turn + (i < angle.size() ? angle[i] : 0.f) : 0.f; }
};

} // namespace mc
