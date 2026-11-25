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

enum class ScreenRole { Custom, MainMenu, Options, Death, Credits, Files, Intro, NewGameIntro, ItemGet, GameSaved, FileMenu, Codex, COUNT };
// FileMenu: after picking a file on the Files screen (if the game has one): play it (continue / start), its codex, options; {file} {fileinfo} {fileaction}
// Codex: the picked file's scans (a Scan list item)
// GameSaved: the pop-up after a Save Station saved the game ({savetitle} {savetext}); the game stops behind it
// ItemGet: the item pop-up while playing (the game stops behind it); {itemtitle} {itemtext} {itemname} and an Item model show what was found
// Files: picking a save file; Intro: the splash, shown first (before the main menu); NewGameIntro: the story, after picking an empty file (then the game)
const char* screenRoleName(ScreenRole r);

enum class BgKind { Color, Image, Video, World, Game, Space, COUNT };   // Image: png / jpg / gif (moves); Game: the game as it is, behind; Space: stars and a planet (Planets editor)
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
    // Space: a planet from the Planets editor ("": the ready-made Earth-like one), where it sits, how it turns, the sun, the stars
    std::string planet; float planetSize = 0.42f, planetX = 0.7f, planetY = 0.56f;   // its radius (part of the screen's height), its middle (0..1)
    float spin = 5, axisTilt = 15;                       // degrees a second; the axis leaning (degrees)
    float sunYaw = -55, sunPitch = 20;                   // where the light comes from (degrees: left / right, up / down)
    float stars = 0.5f, nebula = 0.35f; uint32_t nebulaColor = 0x2A1A5A;   // how many stars, a coloured haze behind
};

enum class ScreenItemKind { Panel, Text, Image, Button, Options, Credits, SaveSlots, Sound, ItemModel, ScanList, COUNT };   // ScanList: the picked file's scans - a list, and the picked one's text beside it   // ItemModel: the found item's 3D model, turning (Item get screen)   // SaveSlots: the save files, one card each; Sound: plays at its time (not seen)
const char* screenItemKindName(ScreenItemKind k);
enum class ScreenAct { None, NewGame, Continue, Options, Credits, Quit, GoTo, Back, Retry, MainMenu, Files, PlayFile, Codex, COUNT };   // PlayFile: the picked file (continue it, or start it if empty); Codex: its scans
const char* screenActName(ScreenAct a);

// ---- transitions: how one screen gives way to the next (the old picture and the new one put together for a moment)
enum class TransKind { None, Fade, Crossfade, Slide, Push, ZoomIn, ZoomOut, Wipe, Iris, Diagonal, VisorScan, Static, Pixelate, Blur, Flash, Dissolve, COUNT };   // Dissolve: the old picture burns away (a glowing edge)
const char* transKindName(TransKind k);
const char* transEaseName(int e);                        // 0 smooth, 1 snappy, 2 bouncy, 3 even
float transEase(int e, float t);                          // t 0..1 -> how far along (bouncy goes a little past 1 before settling)
bool transUsesDir(TransKind k); bool transUsesColor(TransKind k);
struct Transition {
    TransKind kind = TransKind::None;                    // None: a cut (the next screen at once)
    int dir = 1;                                          // Slide / Push / Wipe: where the new screen comes from (0 left, 1 right, 2 top, 3 bottom)
    float time = 0.4f; int ease = 0;                      // seconds; how it moves
    uint32_t color = 0x000000;                            // Fade: through this colour; Flash, Wipe, Iris, Visor scan: their light
};
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
    int font = 0;                                        // 0 the engine's, 1 display (Chakra Petch: titles, buttons), 2 text (Barlow: sentences)
    bool ownTrans = false; Transition trans;             // Button: its own transition to where it leads (instead of the screens')
    // timing (splash screens, intros): seconds after the screen appears
    float showAt = 0, hideAt = 0, fade = 0.4f;           // appears at, goes at (0: stays), fading in over this long
    float fadeOut = -1;                                  // fading out over this long (< 0: as long as it fades in)
    float typeSpeed = 0; SoundUse typeSound;             // Text: letters a second as if typed (0: all at once), a sound per letter
    float driftX = 0, driftY = 0, grow = 0;              // moves (screen widths / heights a second), grows (a second), from when it appears
    std::vector<ItemKey> keys;                           // keyframes (sorted by time): where it is, how big, how see-through over time
    SoundUse sound;                                      // Sound: what plays at showAt
    int inFx = 0; float inFxTime = 0.9f;                 // how it comes in (inFxName): 0 a fade, 1 a scan line with glitches, 2 glitching, 3 a scan line; over this long
    bool cursor = false;                                 // Text: a blinking bar after the typed letters
    bool code = false;                                   // Text: made-up scanner code around it (a tag before it, little lines above and under it), typed with it
    float pulse = 0;                                     // breathes (brighter / dimmer) over this many seconds (0: steady) - "press start"
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
const char* inFxName(int fx);                            // "Fade", "Scan + glitch", "Glitch", "Scan"
constexpr int IN_FX_COUNT = 4;
float inFxProgress(const ScreenItem& it, double t);      // how far its coming in is (0..1; 1: done, or no effect)
struct CodeBits { std::string tag, top, bottom; };        // the made-up scanner code around a text ("nP_0", "(n2H 94cF)", "1v -90 nus-4cr -2304 / ...")
CodeBits codeBits(const std::string& text);               // always the same for the same text
// how many letters of a typed text show t seconds in (whole UTF-8 letters: returns bytes)
size_t typedBytes(const std::string& text, const ScreenItem& it, double t);
struct UiSounds { SoundUse hover, click, back; };       // picking something, using it, going back (empty: none / the game's own)

// ---- the compositor: nodes wired together. Background and UI give pictures; the rest change them; Output is what shows.
enum class NodeType { Background, UI, Output, AlphaOver, Mix, Blur, Glare, ColorBalance, HueSat, BrightContrast, Invert,
                      Vignette, Chromatic, LensDistort, Pixelate, Grain, Scanlines, Sharpen, Gradient,
                      LensGlow, Glitch, InkSpread, BurnIn, COUNT };   // (the last four are timed: seconds into the screen)
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
    int font = 0;                                        // Text: 0 the engine's, 1 display, 2 text (as screen items)
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
    Transition in, out;                                   // how it comes in (from the screen or the game before), how it goes (when the next one has none of its own)
    float shakeAt = 0, shakeTime = 0, shakeAmount = 0.012f;   // the whole picture shakes from shakeAt for shakeTime seconds (amount: part of the height); 0 long: never
    bool pressStart = false;                              // waits at its end for a key ("press start"); a key before that skips to the end
};
void shakeOffset(const Screen& s, double t, float out[2]);   // how far the picture is pushed t seconds in (parts of the height; 0 0 when still)
float screenLength(const Screen& s);                     // its duration, or (staying) a little past its last item's times, at least 10 s
Screen presetScreen(ScreenRole r);                        // ready-made: main menu, options, death, credits, files, splash (and an empty one)
Screen presetStory();                                     // ready-made: an intro of typed text (like Super Metroid's)
Screen presetPhazonIntro();                               // ready-made: the Phazon splash - code typing in over a glow, ink swallowing it, a corrupted planet burning in, the title, "press start"
struct ScreenSet {
    std::vector<Screen> screens;
    int find(const std::string& name) const;              // -1: none
    int byRole(ScreenRole r) const;                       // the first screen with that role (-1: none)
    UiSounds sounds;                                      // the game's interface sounds (every screen and the pause menu)
    SoundUse startSound; float startFade = 1.5f; bool fadeWithSound = true;
    // getting into the game (after loading): the helmet fades in, the world shows as cyan block outlines sweeping out, colour fades in, the HUD glitches in (seconds each; 0: that step is left out)
    bool bootOn = true; float bootHelmet = 1.0f, bootWire = 2.0f, bootColor = 1.2f, bootHud = 1.2f;   // picking a file: this sound, and the screen fades to black (as long as the sound, or startFade s)
};
ScreenSet defaultScreens();                               // Main menu, Files, Options, Death, Credits
bool saveScreens(const std::string& path, const ScreenSet& s);
bool loadScreens(const std::string& path, ScreenSet& s);

} // namespace mc
