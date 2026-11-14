#pragma once
// The player's HUD (Player editor, part 1): flat 2D pictures on planes in front of the camera (bent like a visor if you like)
// and 3D helmet parts, all in camera space, swaying behind the camera like in Metroid Prime. Plain C++, tested on Linux.
// Camera space: x right, y up, z forward (blocks); the camera is at (0, 0, 0).
#include "game.h"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mc {

enum class HudKind { Panel, Bar, Text, Image, Circle, COUNT };
enum class HudValue { None, Health, Counter, Key, Charge, COUNT };   // what a bar / text shows
const char* hudKindName(HudKind k);
const char* hudValueName(HudValue v);

struct HudElement {
    std::string name = "Element";
    HudKind kind = HudKind::Panel;
    float x = 0.4f, y = 0.4f, w = 0.2f, h = 0.08f;     // on the plane: 0..1 across and down
    uint32_t color = 0x60E0FF; float alpha = 0.85f;
    std::string text = "{value}";                       // Text: {value} becomes the value, {max} the most it can be
    std::string image;                                  // Image: a picture in the project's hud folder
    HudValue value = HudValue::None; std::string valueName;   // Counter / Key: which one
    float max = 100;                                    // a bar is full at this (health: the player's most health)
    int segments = 1;                                   // a bar cut into pieces (energy tanks)
    float textSize = 0.5f;                              // Text: how tall, as part of the element's height ... of its box: 1 fills it
    int align = 0;                                      // Text: 0 left, 1 middle, 2 right
    bool filled = true; float thickness = 2;            // Panel / Circle: filled or just the outline (pixels on a 1024-wide plane)
    bool visible = true;
};

struct HudPlane {
    std::string name = "Visor";
    V3 pos{ 0, 0, 1.0 };                                // its middle, in camera space
    float yaw = 0, pitch = 0, roll = 0;                 // degrees
    float width = 1.6f, height = 0.9f;                  // blocks
    float bendX = 0.3f, bendY = 0.1f;                   // 0 flat .. 1 a quarter circle across (side to side / top to bottom)
    float sway = 1;                                     // how much of the sway it gets (0: fixed to the camera)
    float opacity = 1; bool glow = true;                // glow: added on top (light), else painted over
    float bloom = 0.6f;                                 // a soft glow around what is drawn on it (0: none)
    float lightUp = 0.35f;                              // how much its light falls on the helmet's 3D parts (0: none)
    bool visible = true;
    std::vector<HudElement> elements;
};

V3 planeLight(const HudPlane& p);   // the light it gives off (linear colour, more for bigger and brighter elements)

struct HudPart {   // a 3D model fixed to the camera (helmet frame, visor struts...)
    std::string name = "Helmet part", model;
    V3 pos{ 0, -0.25, 0.5 };
    float yaw = 0, pitch = 0, roll = 0;                 // degrees
    double scale = 0.3;
    bool mirror = false;                                // flipped left-right
    float sway = 0.6f;
    bool visible = true;
};

struct HudSwaySettings {
    bool enabled = true;
    float strength = 1;                                 // how far the HUD lags behind when you turn
    float smoothness = 0.5f;                            // 0 snappy .. 1 slow and floaty
    float bob = 0.5f;                                   // up and down while walking, a dip when landing
    float tilt = 0.5f;                                  // leans when turning
    float land = 1;                                     // landing: dips and shakes, more the faster you fall
};

struct HudLayout {
    std::vector<HudPlane> planes;
    std::vector<HudPart> parts;
    HudSwaySettings sway;
    float fov = 70;                                     // the view angle the HUD is laid out for: it keeps its place on screen whatever the player's field of view
    bool empty() const { return planes.empty() && parts.empty(); }
};
HudLayout defaultHud();                                 // a starter visor: energy, missiles, a radar ring, two struts
bool saveHud(const std::string& path, const HudLayout& h);
bool loadHud(const std::string& path, HudLayout& h);

// the plane's surface: u, v 0..1 (left to right, top to bottom) -> camera space (bent toward the camera at the edges)
V3 planePoint(const HudPlane& p, float u, float v);
// turning by degrees: yaw around y, pitch around x, roll around z (applied roll, pitch, then yaw)
V3 rotateDeg(V3 p, float yaw, float pitch, float roll);
// the part's model axes in camera space (mirror flips x)
void partAxes(const HudPart& p, V3& ax, V3& ay, V3& az);

// mirror copies: the other side of the screen
void mirrorPlane(HudPlane& p);
void mirrorPart(HudPart& p);
void mirrorElement(HudElement& e);

// what a value is right now (bars, texts)
struct HudValues { float charge = 0; float health = 100, maxHealth = 100; std::map<std::string, int> counts; std::set<std::string> keys; };
float hudValue(const HudElement& e, const HudValues& v);
float hudMax(const HudElement& e, const HudValues& v);
std::string hudText(const HudElement& e, const HudValues& v);

// the sway: the HUD stays behind a little when the camera turns, then springs back; bobs when walking, dips when landing
struct HudSway {
    float yaw = 0, pitch = 0, roll = 0;                 // degrees the HUD is turned away from the camera right now
    float vy = 0, vp = 0, vr = 0;                       // how fast those change
    V3 offset{ 0, 0, 0 }; float dropVel = 0, drop = 0;  // walk bob / landing dip (blocks)
    float shake = 0, shakeT = 0;                        // landing shake (degrees), and its clock
    double walk = 0;
    // turnYaw / turnPitch: degrees the camera turned this step; speed: walking speed (blocks/s); landed: hit the ground this step at fallSpeed
    void update(const HudSwaySettings& s, double dt, float turnYaw, float turnPitch, float speed, bool landed, float fallSpeed);
    void reset() { *this = HudSway{}; }
    // a point on the HUD, swayed (amount: the plane's / part's sway)
    V3 apply(V3 p, float amount) const;
};

} // namespace mc
