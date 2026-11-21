#pragma once
// Screens (the Screens editor): the game's own flat 2D screens outside of play - the main menu, options, the death screen,
// credits and any of your own. Each has a background (a colour, a picture or GIF, a video, a world seen through a camera,
// or the game itself behind), items laid out on a 2D canvas (panels, text, pictures, buttons, the options list, a credits
// roll) and a compositor: post-processing as a node graph, like Blender's, ending in an Output. Plain C++, tested.
#include "sound.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mc {

enum class ScreenRole { Custom, MainMenu, Options, Death, Credits, Files, Intro, NewGameIntro, ItemGet, GameSaved, COUNT };
// GameSaved: the pop-up after a Save Station saved the game ({savetitle} {savetext}); the game stops behind it
// ItemGet: the item pop-up while playing (the game stops behind it); {itemtitle} {itemtext} {itemname} and an Item model show what was found
// Files: picking a save file; Intro: the splash, shown first (before the main menu); NewGameIntro: the story, after picking an empty file (then the game)
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
    float tilt = -8, height = 1.0f;                      // World: looking up (+) / down (-), degrees; the eye above the camera object (blocks)
};

enum class ScreenItemKind { Panel, Text, Image, Button, Options, Credits, SaveSlots, Sound, ItemModel, COUNT };   // ItemModel: the found item's 3D model, turning (Item get screen)   // SaveSlots: the save files, one card each; Sound: plays at its time (not seen)
const char* screenItemKindName(ScreenItemKind k);
enum class ScreenAct { None, NewGame, Continue, Options, Credits, Quit, GoTo, Back, Retry, MainMenu, Files, COUNT };
const char* screenActName(ScreenAct a);

struct ScreenColors {
    uint32_t text = 0xD8EEF8, accent = 0x6CF0FF, picked = 0xFFFFFF, dim = 0x587888, fill = 0x07121C;
    float fillAlpha = 0.6f;
};
struct ItemKey { float t = 0, x = 0, y = 0, scale = 1, alpha = 1; bool smooth = true; };   // a keyframe: at t seconds the item is here (its top left), this big, this see-through
struct ScreenItem {
    std::string name = "Item";
    ScreenItemKind kind = ScreenItemKind::Text;
    float x = 0.1f, y = 0.1f, w = 0.3f, h = 0.08f;       // on the screen, 0..1 (left to right, top to bottom)
    std::string text;                                    // Text / Button: what it says ({game} {world}); Credits: the whole roll, line by line ("# " a heading)
    std::string image;                                   // Image: a picture or GIF in the "screens" folder
    float textSize = 0.045f;                             // as a part of the screen's height
    int align = 1;                                       // 0 left, 1 middle, 2 right
    int valign = 1;                                      // Text: 0 top, 1 middle, 2 bottom of its box
    float weight = 0;                                    // texts: thicker letters (0 normal .. 1 heavy)
    float outline = 0; uint32_t outlineColor = 0x000000; // texts: a line all around the outside of the letters (0 none .. 1 thick); the letters stay whole
    ScreenColors col; float alpha = 1;
    bool filled = true, frame = true; float rounding = 0.25f, border = 2;   // Panel / Button: a fill, a frame line, round corners (part of the height)
    ScreenAct act = ScreenAct::None; std::string target; // Button: what it does (GoTo: a screen's name)
    std::string group;                                   // Options: "" all three as tabs, or "Sound" / "Graphics" / "Controls"
    float speed = 0.06f;                                 // Credits: screen heights a second
    int slots = 3;                                       // SaveSlots: how many files
    int cards = 0;                                       // SaveSlots: 0 one row each (top to bottom), 1 cards side by side (energy tanks, missiles)
    float pickFill = 0.25f;                              // Button / SaveSlots: how strongly the picked one is filled with the accent (1: a solid bar, its text in the Picked colour)
    bool corners = false;                                // Panel / Button / SaveSlots: bracket corners, like a visor's frame
    std::string tip;                                     // Button: a line about what it does ({tip} in a text shows the picked button's)
    // timing (splash screens, intros): seconds after the screen appears
    float showAt = 0, hideAt = 0, fade = 0.4f;           // appears at, goes at (0: stays), fading in over this long
    float fadeOut = -1;                                  // fading out over this long (< 0: as long as it fades in)
    float typeSpeed = 0; SoundUse typeSound;             // Text: letters a second as if typed (0: all at once), a sound per letter
    float driftX = 0, driftY = 0, grow = 0;              // moves (screen widths / heights a second), grows (a second), from when it appears
    std::vector<ItemKey> keys;                           // keyframes (sorted by time): where it is, how big, how see-through over time
    SoundUse sound;                                      // Sound: what plays at showAt
    float fadeOutTime() const { return fadeOut < 0 ? fade : fadeOut; }
};
// where an item is t seconds into its screen: its keyframes (between two: blended), else where it was put
struct ItemPose { float x, y, scale, alpha; };
ItemPose itemPose(const ScreenItem& it, double t);
void setKey(ScreenItem& it, const ItemKey& k);            // puts a keyframe at k.t (replacing one within 0.02 s), keeps them sorted
float itemEnd(const ScreenItem& it, float screenLength); // when its bar on the timeline ends (staying ones: the screen's length)
// a text broken into lines that fit a width (at spaces; new lines kept): byte ranges [start, end) of the text
struct TextLine { size_t start, end; };
std::vector<TextLine> wrapText(const std::string& s, float width, const std::function<float(const char*, const char*)>& measure);
// how much of an item shows t seconds into its screen (0..1), from its timing
float itemShown(const ScreenItem& it, double t);
// how many letters of a typed text show t seconds in (whole UTF-8 letters: returns bytes)
size_t typedBytes(const std::string& text, const ScreenItem& it, double t);
struct UiSounds { SoundUse hover, click, back; };       // picking something, using it, going back (empty: none / the game's own)

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

// ---- the stack (intros, like Super Metroid's): steps played one after another, the timing worked out by itself.
// A picture starts a new page (the page before fades away); a text replaces the text before it; Clear fades all away.
enum class StepKind { Picture, Text, Wait, Sound, Clear, COUNT };
const char* stepKindName(StepKind k);
struct StackStep {
    StepKind kind = StepKind::Text;
    std::string file;                                    // Picture: a picture / GIF in the "screens" folder
    bool zoom = false; float zoomTo = 1.2f, zoomTime = 4;   // Picture: zooms in slowly (to this size, over this long; then the next step), or stays (the next step right after it faded in)
    int place = 0;                                       // Picture: 0 the whole screen, 1 the top part (text under it), 2 the middle (a logo); Text: 0 bottom, 1 middle, 2 top, 3 under the middle (a line under a title)
    std::string text;                                    // Text: what it says ({game} {world})
    float typeSpeed = 22; SoundUse sound;                // Text: letters a second (0: all at once), a sound per letter; Sound: what plays
    float hold = 2;                                      // Text: seconds it stays once typed; Wait: seconds; Sound: seconds to wait after it starts
    float fade = 0.8f;                                   // Picture: fading in; Clear: fading out
    float textSize = 0.036f; uint32_t color = 0xD8EEF8; int align = 1;
    float weight = 0, outline = 0; uint32_t outlineColor = 0x000000;   // Text: thicker letters, an outline around them
    float textFade = 0.15f;                              // Text: fading in over this long
    bool keep = false;                                   // Text: comes with the text before (instead of replacing it: a title and a line under it)
};
struct StackBuild { std::vector<ScreenItem> items; std::vector<float> starts; float end = 0, length = 0; };   // its items, when each step starts, when the last is done, how long it all takes
StackBuild buildStack(const std::vector<StackStep>& steps);
// a timeline's texts, pictures and sounds as steps (taken out of `items`; what can't be a step stays there), timed about the same
std::vector<StackStep> stepsFromItems(std::vector<ScreenItem>& items);

struct Screen {
    std::string name = "Screen";
    ScreenRole role = ScreenRole::Custom;
    ScreenBg bg;
    std::vector<ScreenItem> items;
    Compositor comp;
    float fadeIn = 0.35f;                                 // seconds it takes to appear
    UiSounds sounds;                                      // its own interface sounds (empty: the game's)
    int musicMode = 0; SoundUse music; float musicFade = 1;   // 0 keeps what plays, 1 plays this (loops), 2 silence; fading over musicFade seconds
    float duration = 0; bool untilVideoEnds = false;      // goes on by itself after this long (0: stays) / when its video ends
    std::string next; bool skippable = true;              // the screen after it (empty: the main menu); a key or click goes on early
    bool useStack = false; std::vector<StackStep> stack;  // made as a stack (its items come from the steps; it lasts as long as they take)
    float holdMin = 0.8f;                                 // Item get / Game saved: seconds before a button closes it (so a held jump doesn't skip it)
};
float screenLength(const Screen& s);                     // its duration, or (staying) a little past its last item's times, at least 10 s
Screen presetScreen(ScreenRole r);                        // ready-made: main menu, options, death, credits, files, splash (and an empty one)
Screen presetStory();                                     // ready-made: an intro of typed text (like Super Metroid's)
struct ScreenSet {
    std::vector<Screen> screens;
    int find(const std::string& name) const;              // -1: none
    int byRole(ScreenRole r) const;                       // the first screen with that role (-1: none)
    UiSounds sounds;                                      // the game's interface sounds (every screen and the pause menu)
    SoundUse startSound; float startFade = 1.5f; bool fadeWithSound = true;   // picking a file: this sound, and the screen fades to black (as long as the sound, or startFade s)
};
ScreenSet defaultScreens();                               // Main menu, Files, Options, Death, Credits
bool saveScreens(const std::string& path, const ScreenSet& s);
bool loadScreens(const std::string& path, ScreenSet& s);

} // namespace mc
