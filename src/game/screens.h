#pragma once
// Screens (the Screens editor): the game's own flat 2D screens outside of play - the main menu, options, the death screen,
// credits and any of your own. Each has a background (a colour, a picture or GIF, a video, a world seen through a camera,
// or the game itself behind), items laid out on a 2D canvas (panels, text, pictures, buttons, the options list, a credits
// roll) and a compositor: post-processing as a node graph, like Blender's, ending in an Output. Plain C++, tested.
#include <cstdint>
#include <string>
#include <vector>

namespace mc {

enum class ScreenRole { Custom, MainMenu, Options, Death, Credits, COUNT };
const char* screenRoleName(ScreenRole r);

enum class BgKind { Color, Image, Video, World, Game, COUNT };   // Image: png / jpg / gif (moves); Game: the game as it is, behind
const char* bgKindName(BgKind k);
struct ScreenBg {
    BgKind kind = BgKind::Color;
    uint32_t color = 0x050A12, color2 = 0x12304A;        // Color: top and bottom (the same: one colour)
    std::string file;                                    // Image / Video: a file in the game's "screens" folder
    int fit = 0;                                         // 0 fill (cut off what sticks out), 1 all of it (bars), 2 stretch
    float speed = 1; bool loop = true;                   // Video / GIF
    std::string world, camera;                           // World: which world, and the object to look through (a Map Camera...; empty: its spawn point)
    float turn = 0;                                      // World: the view turns slowly (degrees a second)
    float dim = 0;                                       // darker (0..1)
};

enum class ScreenItemKind { Panel, Text, Image, Button, Options, Credits, COUNT };
const char* screenItemKindName(ScreenItemKind k);
enum class ScreenAct { None, NewGame, Continue, Options, Credits, Quit, GoTo, Back, Retry, MainMenu, COUNT };
const char* screenActName(ScreenAct a);

struct ScreenColors {
    uint32_t text = 0xD8EEF8, accent = 0x6CF0FF, picked = 0xFFFFFF, dim = 0x587888, fill = 0x07121C;
    float fillAlpha = 0.6f;
};
struct ScreenItem {
    std::string name = "Item";
    ScreenItemKind kind = ScreenItemKind::Text;
    float x = 0.1f, y = 0.1f, w = 0.3f, h = 0.08f;       // on the screen, 0..1 (left to right, top to bottom)
    std::string text;                                    // Text / Button: what it says ({game} {world}); Credits: the whole roll, line by line ("# " a heading)
    std::string image;                                   // Image: a picture or GIF in the "screens" folder
    float textSize = 0.045f;                             // as a part of the screen's height
    int align = 1;                                       // 0 left, 1 middle, 2 right
    ScreenColors col; float alpha = 1;
    bool filled = true, frame = true; float rounding = 0.25f, border = 2;   // Panel / Button: a fill, a frame line, round corners (part of the height)
    ScreenAct act = ScreenAct::None; std::string target; // Button: what it does (GoTo: a screen's name)
    std::string group;                                   // Options: "" all three as tabs, or "Sound" / "Graphics" / "Controls"
    float speed = 0.06f;                                 // Credits: screen heights a second
};

// ---- the compositor: nodes wired together. Background and UI give pictures; the rest change them; Output is what shows.
enum class NodeType { Background, UI, Output, AlphaOver, Mix, Blur, Glare, ColorBalance, HueSat, BrightContrast, Invert,
                      Vignette, Chromatic, LensDistort, Pixelate, Grain, Scanlines, Sharpen, Gradient, COUNT };
constexpr int NODE_PARAMS = 6, NODE_COLORS = 3;
struct NodeInfo {
    const char* name; const char* group;                 // group: "Input", "Output", "Color", "Filter", "Distort", "Stylize"
    int inputs; const char* inName[2];
    int params; const char* pName[NODE_PARAMS]; float pDef[NODE_PARAMS], pMin[NODE_PARAMS], pMax[NODE_PARAMS];
    int colors; const char* cName[NODE_COLORS]; uint32_t cDef[NODE_COLORS];
    int modes; const char* modeName[8];                  // Mix: how the two are put together
    const char* tip;
};
const NodeInfo& nodeInfo(NodeType t);
struct CompNode {
    int id = 0; NodeType type = NodeType::Output;
    float x = 0, y = 0;                                  // where it sits in the node editor
    float p[NODE_PARAMS] = {}; uint32_t c[NODE_COLORS] = { 0xFFFFFF, 0xFFFFFF, 0xFFFFFF }; int mode = 0;
    int in[2] = { 0, 0 };                                // the node feeding each input (0: nothing)
    bool mute = false;                                   // passes its first input through untouched
};
struct Compositor {
    std::vector<CompNode> nodes; int nextId = 1;
    CompNode* find(int id); const CompNode* find(int id) const;
    int add(NodeType t, float x, float y);               // with its starting values; returns its id
    void remove(int id);                                  // and any wires to it
    bool connect(int from, int to, int slot);            // false (and nothing changes) if that would make a loop
    void disconnect(int to, int slot);
    int output() const;                                   // the Output node (0: none)
    std::vector<int> order() const;                       // the nodes the Output needs, each after what feeds it (empty: no Output)
};
Compositor defaultCompositor();                           // Background + UI -> Alpha Over -> Glare -> Vignette -> Output

struct Screen {
    std::string name = "Screen";
    ScreenRole role = ScreenRole::Custom;
    ScreenBg bg;
    std::vector<ScreenItem> items;
    Compositor comp;
    float fadeIn = 0.35f;                                 // seconds it takes to appear
};
Screen presetScreen(ScreenRole r);                        // ready-made: main menu, options, death, credits (and an empty one)
struct ScreenSet {
    std::vector<Screen> screens;
    int find(const std::string& name) const;              // -1: none
    int byRole(ScreenRole r) const;                       // the first screen with that role (-1: none)
};
ScreenSet defaultScreens();                               // Main menu, Options, Death, Credits
bool saveScreens(const std::string& path, const ScreenSet& s);
bool loadScreens(const std::string& path, ScreenSet& s);

} // namespace mc
