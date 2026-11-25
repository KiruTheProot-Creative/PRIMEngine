#include "screens.h"
#include "visors.h"   // escText / unescText
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>

namespace mc {

const char* screenRoleName(ScreenRole r) {
    static const char* n[] = { "Custom", "Main menu", "Options", "Death screen", "Credits", "Files (save files)", "Splash (shown first)", "Intro of a new game (after an empty file)", "Item get (pop-up while playing)", "Game saved (pop-up after a Save Station)",
                                  "File menu (after picking a file)", "Codex (the picked file's scans)" };
    int i = (int)r; return i >= 0 && i < (int)ScreenRole::COUNT ? n[i] : "?";
}
const char* bgKindName(BgKind k) {
    static const char* n[] = { "Colour", "Picture / GIF", "Video", "A world (through a camera)", "The game behind", "Space (a planet)" };
    int i = (int)k; return i >= 0 && i < (int)BgKind::COUNT ? n[i] : "?";
}
const char* screenItemKindName(ScreenItemKind k) {
    static const char* n[] = { "Panel", "Text", "Picture", "Button", "Options", "Credits roll", "Save files", "Sound", "Item model", "Scan list (codex)", "Visor frame", "Scan target lines", "Turning rings", "Picture gallery" };
    int i = (int)k; return i >= 0 && i < (int)ScreenItemKind::COUNT ? n[i] : "?";
}
const char* screenActName(ScreenAct a) {
    static const char* n[] = { "Nothing", "New game", "Continue", "Options", "Credits", "Quit", "Go to a screen", "Back (the screen before)", "Try again", "Main menu", "Files (pick a save file)", "Play the picked file", "Codex (the picked file's scans)", "Title (the splash, waiting for start)" };
    int i = (int)a; return i >= 0 && i < (int)ScreenAct::COUNT ? n[i] : "?";
}

// ------------------------------------------------------------------ transitions
const char* transKindName(TransKind k) {
    static const char* n[] = { "Cut (none)", "Fade (through a colour)", "Crossfade", "Slide in", "Push", "Zoom in", "Zoom out", "Wipe", "Iris (a circle)", "Diagonal wipe", "Visor scan", "Static", "Pixelate", "Blur", "Flash", "Dissolve (burning away)" };
    int i = (int)k; return i >= 0 && i < (int)TransKind::COUNT ? n[i] : "?";
}
const char* transEaseName(int e) { static const char* n[] = { "Smooth", "Snappy", "Bouncy", "Even" }; return e >= 0 && e < 4 ? n[e] : "?"; }
float transEase(int e, float t) {
    t = std::clamp(t, 0.f, 1.f);
    if (e == 1) { float u = 1 - t; return 1 - u * u * u; }                                   // quick, then settles
    if (e == 2) { const float c1 = 1.70158f, c3 = c1 + 1; float u = t - 1; return 1 + c3 * u * u * u + c1 * u * u; }   // a little past, then back
    if (e == 3) return t;
    return t * t * (3 - 2 * t);
}
bool transUsesDir(TransKind k) { return k == TransKind::Slide || k == TransKind::Push || k == TransKind::Wipe; }
bool transUsesColor(TransKind k) { return k == TransKind::Dissolve || k == TransKind::Fade || k == TransKind::Flash || k == TransKind::Wipe || k == TransKind::Iris || k == TransKind::VisorScan || k == TransKind::Diagonal; }
static std::string transText(const Transition& t) { std::ostringstream o; o << (int)t.kind << " " << t.dir << " " << t.time << " " << t.ease << " " << t.color; return o.str(); }
static void transFrom(std::istringstream& l, Transition& t) {
    int k = 0, d = 1, e = 0; float tm = 0.4f; uint32_t c = 0; if (!(l >> k >> d >> tm >> e >> c)) return;
    t.kind = (TransKind)std::clamp(k, 0, (int)TransKind::COUNT - 1); t.dir = std::clamp(d, 0, 3); t.time = std::clamp(tm, 0.f, 5.f); t.ease = std::clamp(e, 0, 3); t.color = c;
}

// ------------------------------------------------------------------ nodes
const NodeInfo& nodeInfo(NodeType t) {
    static const NodeInfo info[] = {
        { "Background", "Input", 0, {}, 0, {}, {}, {}, {}, 0, {}, {}, 0, {}, "The screen's background: its colour, picture, video or world." },
        { "UI", "Input", 0, {}, 0, {}, {}, {}, {}, 0, {}, {}, 0, {}, "The screen's items: panels, text, buttons... (see-through where there are none)." },
        { "Output", "Output", 1, { "Image" }, 0, {}, {}, {}, {}, 0, {}, {}, 0, {}, "What shows on the screen." },
        { "Alpha Over", "Color", 2, { "Back", "Front" }, 1, { "Factor" }, { 1 }, { 0 }, { 1 }, 0, {}, {}, 0, {}, "Puts the front picture over the back one, where it is not see-through." },
        { "Mix", "Color", 2, { "A", "B" }, 1, { "Factor" }, { 0.5f }, { 0 }, { 1 }, 0, {}, {},
          8, { "Mix", "Add", "Multiply", "Screen", "Overlay", "Difference", "Lighten", "Darken" }, "Two pictures put together." },
        { "Blur", "Filter", 1, { "Image" }, 1, { "Size" }, { 8 }, { 0 }, { 80 }, 0, {}, {}, 0, {}, "Soft and smudged (size: pixels on a 1080 tall screen)." },
        { "Glare", "Filter", 1, { "Image" }, 3, { "Threshold", "Strength", "Size" }, { 0.75f, 0.8f, 24 }, { 0, 0, 2 }, { 1.5f, 4, 120 },
          1, { "Tint" }, { 0xFFFFFF }, 0, {}, "Bright parts glow." },
        { "Color Balance", "Color", 1, { "Image" }, 3, { "Lift", "Gamma", "Gain" }, { 0, 1, 1 }, { -0.5f, 0.2f, 0 }, { 0.5f, 3, 3 },
          3, { "Lift tint", "Gamma tint", "Gain tint" }, { 0xFFFFFF, 0xFFFFFF, 0xFFFFFF }, 0, {}, "Lift: the darks, gamma: the middle, gain: the brights (each can be tinted)." },
        { "Hue / Saturation", "Color", 1, { "Image" }, 3, { "Hue", "Saturation", "Value" }, { 0, 1, 1 }, { -0.5f, 0, 0 }, { 0.5f, 2, 2 }, 0, {}, {}, 0, {}, "Turn the colours, make them stronger or greyer, brighter or darker." },
        { "Bright / Contrast", "Color", 1, { "Image" }, 2, { "Bright", "Contrast" }, { 0, 0 }, { -1, -1 }, { 1, 1 }, 0, {}, {}, 0, {}, "" },
        { "Invert", "Color", 1, { "Image" }, 1, { "Factor" }, { 1 }, { 0 }, { 1 }, 0, {}, {}, 0, {}, "" },
        { "Vignette", "Stylize", 1, { "Image" }, 3, { "Amount", "Size", "Softness" }, { 0.5f, 0.75f, 0.45f }, { 0, 0.1f, 0.01f }, { 1, 1.5f, 1 },
          1, { "Colour" }, { 0x000000 }, 0, {}, "Darker (or coloured) edges." },
        { "Chromatic", "Distort", 1, { "Image" }, 1, { "Amount" }, { 2 }, { 0 }, { 20 }, 0, {}, {}, 0, {}, "Red and blue fringes toward the edges (pixels)." },
        { "Lens Distort", "Distort", 1, { "Image" }, 2, { "Distortion", "Dispersion" }, { 0.1f, 0 }, { -0.5f, 0 }, { 0.5f, 0.1f }, 0, {}, {}, 0, {}, "Bulges (or pinches) the picture like a lens." },
        { "Pixelate", "Stylize", 1, { "Image" }, 1, { "Size" }, { 4 }, { 1 }, { 64 }, 0, {}, {}, 0, {}, "Big pixels (size on a 1080 tall screen)." },
        { "Grain", "Stylize", 1, { "Image" }, 3, { "Amount", "Size", "Moving" }, { 0.08f, 1.5f, 1 }, { 0, 1, 0 }, { 1, 8, 1 }, 0, {}, {}, 0, {}, "Film grain." },
        { "Scanlines", "Stylize", 1, { "Image" }, 3, { "Amount", "Lines", "Speed" }, { 0.25f, 240, 0 }, { 0, 20, -100 }, { 1, 1080, 100 }, 0, {}, {}, 0, {}, "Lines across like an old screen." },
        { "Sharpen", "Filter", 1, { "Image" }, 1, { "Amount" }, { 0.5f }, { 0 }, { 3 }, 0, {}, {}, 0, {}, "" },
        { "Gradient Map", "Color", 1, { "Image" }, 1, { "Factor" }, { 1 }, { 0 }, { 1 }, 2, { "Dark", "Bright" }, { 0x000814, 0x9FF6FF }, 0, {}, "Dark to bright becomes one colour to the other." },
        { "Lens Glow", "Stylize", 1, { "Image" }, 6, { "Starts at", "Ends at", "Fade", "Width", "Height", "Dark spot" }, { 0.5f, 0, 1.5f, 1.55f, 0.85f, 0.6f }, { 0, 0, 0.01f, 0.1f, 0.1f, 0 }, { 120, 120, 10, 3, 2, 1 },
          3, { "Edge", "Middle", "Dark spot" }, { 0x1A5CFF, 0xA8D6FF, 0x0630A8 }, 0, {}, "A soft glowing oval behind everything, like looking through a visor out of focus. Timed: seconds into the screen (ends at 0: stays)." },
        { "Glitch", "Distort", 1, { "Image" }, 6, { "Amount", "Starts at", "Ends at", "Bands", "Shift", "Speed" }, { 0.6f, 0, 0, 48, 24, 14 }, { 0, 0, 0, 4, 0, 1 }, { 1, 120, 120, 200, 200, 60 },
          1, { "Streaks" }, { 0x6FC4FF }, 0, {}, "Rows of the picture jump sideways now and then, thin streaks of light run across. Timed (ends at 0: stays)." },
        { "Ink Spread", "Stylize", 1, { "Image" }, 6, { "Starts at", "Takes", "Gone at", "Size", "Seed", "Darkness from" }, { 16, 8, 25, 1.2f, 1, 0.66f }, { 0, 0.1f, 0, 0.2f, 0, 0 }, { 120, 30, 120, 5, 100, 0.95f },
          3, { "Ink", "Deep ink", "Darkness" }, { 0x2A6BFF, 0x0A1A8C, 0x000000 }, 0, {}, "Ink blots bloom over the picture, deepen, and a darkness swallows it all. Timed; gone at: the picture is back (0: stays dark)." },
        { "Burn In", "Stylize", 1, { "Image" }, 6, { "Starts at", "Takes", "Size", "Edge", "Glow", "Seed" }, { 25.5f, 4, 2.5f, 0.05f, 1.3f, 3 }, { 0, 0.1f, 0.2f, 0.01f, 0, 0 }, { 120, 30, 8, 0.5f, 4, 100 },
          2, { "Edge", "Hot" }, { 0x2E78FF, 0xA8DCFF }, 0, {}, "The picture burns into view out of the black, a glowing edge eating the dark. Before it starts: black. Timed." },
    };
    static_assert(sizeof(info) / sizeof(info[0]) == (size_t)NodeType::COUNT, "one entry per node type");
    int i = (int)t; return info[i >= 0 && i < (int)NodeType::COUNT ? i : 2];
}

CompNode* Compositor::find(int id) { for (CompNode& n : nodes) if (n.id == id) return &n; return nullptr; }
const CompNode* Compositor::find(int id) const { for (const CompNode& n : nodes) if (n.id == id) return &n; return nullptr; }
int Compositor::add(NodeType t, float x, float y) {
    CompNode n; n.id = nextId++; n.type = t; n.x = x; n.y = y;
    const NodeInfo& in = nodeInfo(t);
    for (int i = 0; i < in.params; i++) n.p[i] = in.pDef[i];
    for (int i = 0; i < in.colors; i++) n.c[i] = in.cDef[i];
    nodes.push_back(n); return n.id;
}
void Compositor::remove(int id) {
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const CompNode& n) { return n.id == id; }), nodes.end());
    for (CompNode& n : nodes) for (int& i : n.in) if (i == id) i = 0;
}
bool Compositor::connect(int from, int to, int slot) {
    CompNode* a = find(from); CompNode* b = find(to);
    if (!a || !b || from == to || a->type == NodeType::Output || slot < 0 || slot >= nodeInfo(b->type).inputs) return false;
    std::function<bool(int, int)> feeds = [&](int n, int depth) -> bool {   // does node n (or what feeds it) come from `to`?
        if (n == to) return true; if (depth > 256) return true;
        const CompNode* c = find(n); if (!c) return false;
        for (int i : c->in) if (i && feeds(i, depth + 1)) return true;
        return false;
    };
    if (feeds(from, 0)) return false;   // `to` already feeds `from`: a loop
    b->in[slot] = from; return true;
}
void Compositor::disconnect(int to, int slot) { if (CompNode* b = find(to)) if (slot >= 0 && slot < 2) b->in[slot] = 0; }
int Compositor::output() const { for (const CompNode& n : nodes) if (n.type == NodeType::Output) return n.id; return 0; }
std::vector<int> Compositor::order() const {
    std::vector<int> out; int o = output(); if (!o) return out;
    std::vector<int> state;   // ids being visited (loop guard) / done
    std::function<void(int)> visit = [&](int id) {
        if (!id || std::find(out.begin(), out.end(), id) != out.end() || std::find(state.begin(), state.end(), id) != state.end()) return;
        const CompNode* n = find(id); if (!n) return;
        state.push_back(id);
        for (int i = 0; i < nodeInfo(n->type).inputs; i++) visit(n->in[i]);
        out.push_back(id);
    };
    visit(o); return out;
}
Compositor defaultCompositor() {
    Compositor c;
    int bg = c.add(NodeType::Background, 40, 60), ui = c.add(NodeType::UI, 40, 220);
    int over = c.add(NodeType::AlphaOver, 300, 120), glare = c.add(NodeType::Glare, 520, 120), vig = c.add(NodeType::Vignette, 740, 120), out = c.add(NodeType::Output, 960, 120);
    c.find(glare)->p[0] = 0.85f; c.find(glare)->p[1] = 0.6f; c.find(vig)->p[0] = 0.35f;
    c.connect(bg, over, 0); c.connect(ui, over, 1); c.connect(over, glare, 0); c.connect(glare, vig, 0); c.connect(vig, out, 0);
    return c;
}

// ------------------------------------------------------------------ presets
static ScreenItem text(const std::string& name, const std::string& t, float x, float y, float w, float h, float size, int align = 1) {
    ScreenItem i; i.name = name; i.kind = ScreenItemKind::Text; i.text = t; i.x = x; i.y = y; i.w = w; i.h = h; i.textSize = size; i.align = align; return i;
}
static ScreenItem button(const std::string& t, ScreenAct a, float x, float y, float w = 0.3f) {
    ScreenItem i; i.name = t; i.kind = ScreenItemKind::Button; i.text = t; i.act = a; i.x = x; i.y = y; i.w = w; i.h = 0.07f; i.textSize = 0.036f; i.align = 0; return i;
}
static const uint32_t VISOR_CYAN = 0x6CF0FF, VISOR_DARK = 0x03070C, DEATH_RED = 0xFF5A4A, SAVE_AMBER = 0xFFC85A;
static ScreenItem rule(const std::string& name, float x, float y, float w, uint32_t c, float a) {   // a thin line (or a small bar)
    ScreenItem i; i.name = name; i.kind = ScreenItemKind::Panel; i.x = x; i.y = y; i.w = w; i.h = 0.0025f; i.frame = false; i.col.fill = c; i.col.fillAlpha = a; i.rounding = 0; return i;
}
static void visorButton(ScreenItem& b) {   // the visor look: no box until picked, then a solid lit bar with dark letters
    b.font = 1; b.weight = 0; b.filled = false; b.frame = false; b.rounding = 0; b.pickFill = 1; b.col.picked = VISOR_DARK; b.col.text = 0x9FC2D2; b.col.accent = VISOR_CYAN;
}
static ScreenItem visorPanel(const std::string& name, float x, float y, float w, float h) {   // a framed pane of dark glass with bracket corners
    ScreenItem p; p.name = name; p.kind = ScreenItemKind::Panel; p.x = x; p.y = y; p.w = w; p.h = h; p.col.fill = 0x08182A; p.col.fillAlpha = 0.8f; p.col.accent = VISOR_CYAN; p.col.picked = 0xB8F8FF;
    p.rounding = 0; p.border = 1; p.corners = true; return p;
}
static void hintBar(Screen& s, const std::string& left, const std::string& right) {   // a dark strip along the bottom: the keys on the left, a note on the right
    ScreenItem bar = rule("Hint bar", 0, 0.938f, 1, VISOR_DARK, 0.7f); bar.h = 0.062f; s.items.push_back(bar);
    s.items.push_back(rule("Hint bar line", 0, 0.938f, 1, VISOR_CYAN, 0.25f));
    ScreenItem l = text("Keys", left, 0.075f, 0.938f, 0.5f, 0.062f, 0.0167f, 0); l.col.text = 0x9FC2D2; l.font = 1; s.items.push_back(l);
    ScreenItem r = text("Note", right, 0.5f, 0.938f, 0.425f, 0.062f, 0.0167f, 2); r.col.text = 0x5E8193; r.font = 1; s.items.push_back(r);
}
static void visorFonts(Screen& s) {   // the ready-made screens' letters: Chakra Petch for titles, buttons and labels, Barlow for sentences
    auto pick = [](const std::string& name) { return name == "Tip" || name == "Text" || name == "Credits roll" ? 2 : 1; };
    for (ScreenItem& i : s.items) if (i.font == 0 && (i.kind == ScreenItemKind::Text || i.kind == ScreenItemKind::Button || i.kind == ScreenItemKind::Credits || i.kind == ScreenItemKind::SaveSlots)) {
        i.font = pick(i.name); if (i.font == 1) i.weight = std::min(i.weight, 0.15f);
    }
    for (StackStep& st : s.stack) if (st.kind == StepKind::Text && st.font == 0) { st.font = st.textSize >= 0.06f || st.text == "MADE WITH PRIMENGINE" ? 1 : 2; if (st.font == 1) st.weight = std::min(st.weight, 0.15f); }
}
static void visorComp(Screen& s, float scan) {   // Background + UI -> Alpha Over -> Glare -> Scanlines -> Vignette -> Output
    s.comp = Compositor{};
    int bg = s.comp.add(NodeType::Background, 40, 60), ui = s.comp.add(NodeType::UI, 40, 220), over = s.comp.add(NodeType::AlphaOver, 300, 120), gl = s.comp.add(NodeType::Glare, 520, 120);
    int sl = s.comp.add(NodeType::Scanlines, 740, 120), vig = s.comp.add(NodeType::Vignette, 960, 120), out = s.comp.add(NodeType::Output, 1180, 120);
    s.comp.find(gl)->p[0] = 0.8f; s.comp.find(gl)->p[1] = 0.7f; s.comp.find(sl)->p[0] = scan; s.comp.find(sl)->p[1] = 300; s.comp.find(vig)->p[0] = 0.45f;
    s.comp.connect(bg, over, 0); s.comp.connect(ui, over, 1); s.comp.connect(over, gl, 0); s.comp.connect(gl, sl, 0); s.comp.connect(sl, vig, 0); s.comp.connect(vig, out, 0);
}
Screen presetScreen(ScreenRole r) {
    Screen s; s.role = r; s.comp = defaultCompositor();
    switch (r) {
    case ScreenRole::MainMenu: {   // the title: the game's name top left, a list of buttons where the picked one is a lit bar, what it does under it, a hint bar at the bottom
        s.name = "Main menu"; s.bg.color = 0x0C2533; s.bg.color2 = 0x03070C; s.bg.kind = BgKind::Space;   // (a planet turning behind the title)
        ScreenItem k = text("Kicker", "A PRIMENGINE GAME", 0.075f, 0.15f, 0.5f, 0.03f, 0.0156f, 0); k.col.text = VISOR_CYAN; k.weight = 0.3f; s.items.push_back(k);
        ScreenItem t = text("Title", "{game}", 0.075f, 0.18f, 0.85f, 0.13f, 0.1f, 0); t.weight = 0.6f; t.valign = 0; s.items.push_back(t);
        s.items.push_back(rule("Line", 0.075f, 0.32f, 0.26f, VISOR_CYAN, 0.8f));
        const char* tips[] = { "Go on from your newest save: {continue}", "Pick a file and start from the beginning.", "Sound, picture, controls.", "Who made this game.", "Back to the desktop." };
        float y = 0.46f; int n = 0;
        for (auto b : { std::make_pair("Continue", ScreenAct::Continue), std::make_pair("New game", ScreenAct::NewGame), std::make_pair("Options", ScreenAct::Options),
                        std::make_pair("Credits", ScreenAct::Credits), std::make_pair("Quit", ScreenAct::Quit) }) {
            ScreenItem bt = button(b.first, b.second, 0.06f, y, 0.35f); bt.h = 0.062f; bt.textSize = 0.029f; bt.weight = 0.25f; visorButton(bt); bt.tip = tips[n++]; s.items.push_back(bt); y += 0.069f;
        }
        ScreenItem tp = text("Tip", "{tip}", 0.075f, 0.82f, 0.6f, 0.04f, 0.019f, 0); tp.col.text = 0x9FC2D2; s.items.push_back(tp);
        hintBar(s, "ENTER  select       W / S  move", "made with PRIMEngine");
        ScreenItem ro = text("Readout", "SYSTEM ONLINE", 0.6f, 0.13f, 0.325f, 0.03f, 0.0144f, 2); ro.col.text = VISOR_CYAN; ro.alpha = 0.75f; s.items.push_back(ro);
        for (int i = 0; i < 4; i++) { ScreenItem p = rule("Readout bar", 0.865f + i * 0.0165f, 0.17f, 0.0138f, VISOR_CYAN, i < 3 ? 0.75f : 0.2f); p.h = 0.0067f; s.items.push_back(p); }
        visorComp(s, 0.1f);
        break; }
    case ScreenRole::Options: {   // the options list in a framed visor panel
        s.name = "Options"; s.bg.color = 0x0C2533; s.bg.color2 = 0x03070C;
        ScreenItem t = text("Title", "OPTIONS", 0.06f, 0.05f, 0.5f, 0.08f, 0.044f, 0); t.weight = 0.4f; s.items.push_back(t);
        ScreenItem p = visorPanel("Backing", 0.0375f, 0.16f, 0.925f, 0.7f); s.items.push_back(p);
        ScreenItem o; o.name = "Options"; o.kind = ScreenItemKind::Options; o.x = 0.055f; o.y = 0.18f; o.w = 0.89f; o.h = 0.66f; o.textSize = 0.028f; s.items.push_back(o);
        ScreenItem bk = button("Back", ScreenAct::Back, 0.06f, 0.895f, 0.12f); bk.h = 0.053f; bk.textSize = 0.02f; bk.align = 1; visorButton(bk); bk.frame = true; s.items.push_back(bk);
        ScreenItem h = text("Hint", "Q / E  group       A / D  change       ESC  back", 0.3f, 0.895f, 0.64f, 0.053f, 0.0167f, 2); h.col.text = 0x9FC2D2; s.items.push_back(h);
        visorComp(s, 0.1f);
        break; }
    case ScreenRole::Death: {   // red: the game grey and blurred behind, "ENERGY DEPLETED" with its colours torn apart, three ways back
        s.name = "Death screen"; s.bg.kind = BgKind::Game; s.bg.dim = 0.45f;
        ScreenItem tanks = text("Tanks", "", 0.37f, 0.27f, 0.26f, 0.025f, 0.02f); tanks.kind = ScreenItemKind::Panel; tanks.filled = false; tanks.frame = true; tanks.col.accent = DEATH_RED; tanks.rounding = 0; tanks.border = 2; s.items.push_back(tanks);
        ScreenItem t = text("Title", "ENERGY DEPLETED", 0.05f, 0.31f, 0.9f, 0.11f, 0.089f); t.col.text = DEATH_RED; t.weight = 0.6f; s.items.push_back(t);
        ScreenItem sub = text("Subtitle", "SUIT INTEGRITY LOST", 0.1f, 0.425f, 0.8f, 0.035f, 0.0167f); sub.col.text = 0xC08880; sub.showAt = 0.6f; s.items.push_back(sub);
        const char* tips[] = { "From the last checkpoint.", "From the last Save Station you used.", "Leave this game." };
        float y = 0.56f; int n = 0;
        for (auto b : { std::make_pair("TRY AGAIN", ScreenAct::Retry), std::make_pair("LOAD LAST SAVE", ScreenAct::Continue), std::make_pair("MAIN MENU", ScreenAct::MainMenu) }) {
            ScreenItem bt = button(b.first, b.second, 0.38f, y, 0.24f); bt.h = 0.062f; bt.textSize = 0.022f; bt.align = 1; bt.weight = 0.25f; visorButton(bt);
            bt.frame = true; bt.border = 1.5f; bt.col.accent = DEATH_RED; bt.col.text = 0xE8B8B0; bt.tip = tips[n++]; bt.showAt = 0.9f + n * 0.08f; s.items.push_back(bt); y += 0.075f;
        }
        ScreenItem tp = text("Tip", "{tip}", 0.1f, 0.92f, 0.8f, 0.035f, 0.0167f); tp.col.text = 0xA87872; s.items.push_back(tp);
        s.comp = Compositor{};
        int bg = s.comp.add(NodeType::Background, 40, 60), ui = s.comp.add(NodeType::UI, 40, 240), hs = s.comp.add(NodeType::HueSat, 260, 60);
        int bl = s.comp.add(NodeType::Blur, 480, 60), over = s.comp.add(NodeType::AlphaOver, 700, 140), ch = s.comp.add(NodeType::Chromatic, 920, 140), sl = s.comp.add(NodeType::Scanlines, 1140, 140);
        int vig = s.comp.add(NodeType::Vignette, 1360, 140), out = s.comp.add(NodeType::Output, 1580, 140);
        s.comp.find(hs)->p[1] = 0.15f; s.comp.find(bl)->p[0] = 8; s.comp.find(ch)->p[0] = 3; s.comp.find(sl)->p[0] = 0.18f; s.comp.find(sl)->p[1] = 300; s.comp.find(sl)->p[2] = 12;
        s.comp.find(vig)->p[0] = 0.85f; s.comp.find(vig)->c[0] = 0x6E0000;
        s.comp.connect(bg, hs, 0); s.comp.connect(hs, bl, 0); s.comp.connect(bl, over, 0); s.comp.connect(ui, over, 1); s.comp.connect(over, ch, 0); s.comp.connect(ch, sl, 0); s.comp.connect(sl, vig, 0); s.comp.connect(vig, out, 0);
        s.fadeIn = 1.2f;
        break; }
    case ScreenRole::Credits: {   // the roll between the visor's edges, fading at the top and bottom
        s.name = "Credits"; s.bg.color = s.bg.color2 = 0x02040A;
        ScreenItem c; c.name = "Credits roll"; c.kind = ScreenItemKind::Credits; c.x = 0.25f; c.y = 0.12f; c.w = 0.5f; c.h = 0.76f; c.textSize = 0.031f;
        c.text = "# {game}\n\n# Made by\nYou\n\n# Made with\nPRIMEngine\n\n\nTHANK YOU FOR PLAYING";
        s.items.push_back(c);
        for (float x : { 0.1875f, 0.8125f }) { ScreenItem e = rule("Edge", x, 0.22f, 0.0008f, VISOR_CYAN, 0.25f); e.h = 0.56f; s.items.push_back(e); }
        ScreenItem bk = button("Back", ScreenAct::Back, 0.06f, 0.895f, 0.12f); bk.h = 0.053f; bk.textSize = 0.02f; bk.align = 1; visorButton(bk); bk.frame = true; s.items.push_back(bk);
        visorComp(s, 0.06f);
        break; }
    case ScreenRole::Files: {   // the save files as cards side by side
        s.name = "Files"; s.bg.color = 0x0C2533; s.bg.color2 = 0x03070C;
        ScreenItem t = text("Title", "SELECT FILE", 0.075f, 0.065f, 0.6f, 0.07f, 0.049f, 0); t.weight = 0.4f; s.items.push_back(t);
        ScreenItem sub = text("Subtitle", "PICK WHERE YOUR GAME IS KEPT", 0.075f, 0.135f, 0.6f, 0.03f, 0.0144f, 0); sub.col.text = 0x7FA3B5; s.items.push_back(sub);
        ScreenItem f; f.name = "Save files"; f.kind = ScreenItemKind::SaveSlots; f.x = 0.075f; f.y = 0.2f; f.w = 0.85f; f.h = 0.58f; f.textSize = 0.028f; f.slots = 3;
        f.cards = 1; f.corners = true; f.pickFill = 0.15f; f.border = 1.5f; f.col.fill = 0x08182A; f.col.fillAlpha = 0.78f; f.col.dim = 0x7FA3B5; f.col.picked = VISOR_CYAN; s.items.push_back(f);
        ScreenItem bk = button("Back", ScreenAct::Back, 0.075f, 0.87f, 0.12f); bk.h = 0.053f; bk.textSize = 0.02f; bk.align = 1; visorButton(bk); bk.frame = true; s.items.push_back(bk);
        ScreenItem hint = text("Hint", "A / D  pick a file       ENTER  play       DELETE twice  erase       ESC  back", 0.25f, 0.87f, 0.675f, 0.053f, 0.0167f, 2); hint.col.text = 0x9FC2D2; s.items.push_back(hint);
        visorComp(s, 0.1f);
        break; }
    case ScreenRole::FileMenu: {   // after picking a file: a scanner frame, bracketed choices down the left, the file beside them
        s.name = "File menu"; s.bg.color = 0x06162E; s.bg.color2 = 0x01040C;
        const uint32_t PB = 0x5AB4FF;
        ScreenItem fr = visorPanel("Frame", 0.03f, 0.05f, 0.94f, 0.88f); fr.col.accent = PB; fr.col.fill = 0x041026; fr.col.fillAlpha = 0.55f; s.items.push_back(fr);
        ScreenItem t = text("Title", "{FILE}", 0.06f, 0.075f, 0.5f, 0.06f, 0.036f, 0); t.col.text = 0xCFE8FF; t.font = 1; t.weight = 0.4f; s.items.push_back(t);
        s.items.push_back(rule("Line", 0.06f, 0.14f, 0.88f, PB, 0.6f));
        const char* lbl[] = { "[ {FILEACTION} ]", "[ CODEX ]", "[ OPTIONS ]", "[ BACK ]" }; ScreenAct acts[] = { ScreenAct::PlayFile, ScreenAct::Codex, ScreenAct::Options, ScreenAct::Back };
        const char* tips[] = { "Into the game.", "What this file has scanned.", "Sound, picture, controls.", "Pick another file." };
        float y = 0.24f;
        for (int i = 0; i < 4; i++) {
            ScreenItem b = button(lbl[i], acts[i], 0.06f, i == 3 ? 0.76f : y, 0.38f); b.name = i == 0 ? "Play" : i == 1 ? "Codex" : i == 2 ? "Options" : "Back"; b.h = 0.075f; b.textSize = 0.03f;
            visorButton(b); b.frame = true; b.border = 1.5f; b.col.accent = PB; b.col.text = 0xCFE8FF; b.pickFill = 0.25f; b.col.picked = 0xFFFFFF; b.corners = true; b.tip = tips[i];
            b.showAt = 0.1f + i * 0.08f; b.inFx = 3; b.inFxTime = 0.35f; s.items.push_back(b); y += 0.1f;
        }
        ScreenItem info = visorPanel("File", 0.5f, 0.24f, 0.42f, 0.3f); info.col.accent = PB; info.col.fill = 0x061A38; s.items.push_back(info);
        ScreenItem it = text("File info", "{fileinfo}", 0.53f, 0.27f, 0.36f, 0.24f, 0.024f, 0); it.col.text = 0xA8D8FF; it.font = 2; it.valign = 0; it.typeSpeed = 90; it.showAt = 0.3f; s.items.push_back(it);
        ScreenItem tp = text("Tip", "{tip}", 0.06f, 0.87f, 0.6f, 0.04f, 0.019f, 0); tp.col.text = 0x7FA8D0; s.items.push_back(tp);
        visorComp(s, 0.12f);
        s.in.kind = TransKind::Fade; s.in.time = 0.5f; s.in.color = 0x000000;
        break; }
    case ScreenRole::Codex: {   // the picked file's scans: a list on the left, the picked one's text on the right
        s.name = "Codex"; s.bg.color = 0x06162E; s.bg.color2 = 0x01040C;
        const uint32_t PB = 0x5AB4FF;
        ScreenItem fr = visorPanel("Frame", 0.03f, 0.05f, 0.94f, 0.88f); fr.col.accent = PB; fr.col.fill = 0x041026; fr.col.fillAlpha = 0.55f; s.items.push_back(fr);
        ScreenItem t = text("Title", "CODEX  -  {FILE}", 0.06f, 0.075f, 0.6f, 0.06f, 0.036f, 0); t.col.text = 0xCFE8FF; t.font = 1; t.weight = 0.4f; s.items.push_back(t);
        s.items.push_back(rule("Line", 0.06f, 0.14f, 0.88f, PB, 0.6f));
        ScreenItem l; l.name = "Scans"; l.kind = ScreenItemKind::ScanList; l.x = 0.06f; l.y = 0.17f; l.w = 0.88f; l.h = 0.66f; l.textSize = 0.024f; l.font = 1;
        l.col.text = 0xCFE8FF; l.col.accent = PB; l.col.dim = 0x5A7FA8; l.col.picked = 0xFFFFFF; l.col.fill = 0x061A38; l.col.fillAlpha = 0.6f; s.items.push_back(l);
        ScreenItem bk = button("[ BACK ]", ScreenAct::Back, 0.06f, 0.86f, 0.16f); bk.name = "Back"; bk.h = 0.055f; bk.textSize = 0.022f; visorButton(bk); bk.col.accent = PB; bk.frame = true; s.items.push_back(bk);
        visorComp(s, 0.12f);
        break; }
    case ScreenRole::Intro: {   // a splash: the game's name fades in and out, then on to the story
        s.name = "Splash"; s.bg.color = s.bg.color2 = 0x000000; s.comp = Compositor{}; s.fadeIn = 0.6f; s.duration = 4; s.next = "";
        { int bg = s.comp.add(NodeType::Background, 40, 60), ui = s.comp.add(NodeType::UI, 40, 220), over = s.comp.add(NodeType::AlphaOver, 300, 120), gl = s.comp.add(NodeType::Glare, 520, 120), sl = s.comp.add(NodeType::Scanlines, 740, 120), out = s.comp.add(NodeType::Output, 960, 120);
          s.comp.find(sl)->p[0] = 0.08f; s.comp.find(sl)->p[1] = 300;
          s.comp.connect(bg, over, 0); s.comp.connect(ui, over, 1); s.comp.connect(over, gl, 0); s.comp.connect(gl, sl, 0); s.comp.connect(sl, out, 0); }
        s.useStack = true;   // (a stack: the name, then a line under it, then on)
        StackStep t; t.kind = StepKind::Text; t.text = "{game}"; t.place = 1; t.textSize = 0.093f; t.weight = 0.6f; t.typeSpeed = 0; t.textFade = 0.8f; t.hold = 0.6f; s.stack.push_back(t);
        StackStep m; m.kind = StepKind::Text; m.text = "MADE WITH PRIMENGINE"; m.keep = true; m.place = 3; m.textSize = 0.018f; m.color = 0x7FA3B5; m.typeSpeed = 0; m.textFade = 0.6f; m.hold = 1.6f; s.stack.push_back(m);
        s.duration = buildStack(s.stack).length;
        break; }
    case ScreenRole::ItemGet: {   // the item pop-up: the game frozen and blurred behind a framed visor panel, the item turning on the left
        s.name = "Item get"; s.bg.kind = BgKind::Game; s.bg.dim = 0.3f; s.fadeIn = 0.25f; s.holdMin = 0.8f;
        ScreenItem p = visorPanel("Panel", 0.169f, 0.278f, 0.6625f, 0.444f); p.col.fill = 0x04101A; p.col.fillAlpha = 0.86f; p.border = 2;
        p.keys = { { 0, 0.169f, 0.31f, 0.96f, 0, true }, { 0.25f, 0.169f, 0.278f, 1, 1, true } }; s.items.push_back(p);
        ScreenItem m; m.name = "Item model"; m.kind = ScreenItemKind::ItemModel; m.x = 0.19f; m.y = 0.31f; m.w = 0.19f; m.h = 0.34f; m.showAt = 0.1f; m.fade = 0.3f; s.items.push_back(m);
        ScreenItem k = text("Kicker", "ITEM ACQUIRED", 0.41f, 0.315f, 0.38f, 0.03f, 0.0156f, 0); k.col.text = VISOR_CYAN; k.weight = 0.3f; k.showAt = 0.1f; s.items.push_back(k);
        ScreenItem t = text("Title", "{itemtitle}", 0.41f, 0.345f, 0.38f, 0.08f, 0.056f, 0); t.col.text = VISOR_CYAN; t.weight = 0.6f; t.showAt = 0.1f; t.fade = 0.25f; s.items.push_back(t);
        ScreenItem l = rule("Line", 0.41f, 0.435f, 0.36f, VISOR_CYAN, 0.6f); l.showAt = 0.15f; s.items.push_back(l);
        ScreenItem x = text("Text", "{itemtext}", 0.41f, 0.455f, 0.38f, 0.16f, 0.0245f, 0); x.col.text = 0xC8E2EE; x.valign = 0; x.showAt = 0.2f; x.typeSpeed = 70; s.items.push_back(x);
        ScreenItem h = text("Continue", "{key:jump}: continue", 0.41f, 0.645f, 0.38f, 0.04f, 0.0178f, 0); h.col.text = 0x9FC2D2; h.showAt = 0.8f; h.fade = 0.4f; s.items.push_back(h);
        s.comp = Compositor{};
        int bg = s.comp.add(NodeType::Background, 40, 60), ui = s.comp.add(NodeType::UI, 40, 240), bl = s.comp.add(NodeType::Blur, 260, 60);
        int over = s.comp.add(NodeType::AlphaOver, 480, 140), gl = s.comp.add(NodeType::Glare, 700, 140), sl = s.comp.add(NodeType::Scanlines, 920, 140), vig = s.comp.add(NodeType::Vignette, 1140, 140), out = s.comp.add(NodeType::Output, 1360, 140);
        s.comp.find(bl)->p[0] = 8; s.comp.find(sl)->p[0] = 0.1f; s.comp.find(sl)->p[1] = 300; s.comp.find(vig)->p[0] = 0.5f;
        s.comp.connect(bg, bl, 0); s.comp.connect(bl, over, 0); s.comp.connect(ui, over, 1); s.comp.connect(over, gl, 0); s.comp.connect(gl, sl, 0); s.comp.connect(sl, vig, 0); s.comp.connect(vig, out, 0);
        break; }
    case ScreenRole::GameSaved: {   // after a Save Station: "GAME SAVED!" in an amber visor panel, the game frozen and blurred behind
        s.name = "Game saved"; s.bg.kind = BgKind::Game; s.bg.dim = 0.35f; s.fadeIn = 0.25f; s.holdMin = 0.8f;
        ScreenItem p = visorPanel("Panel", 0.25f, 0.3f, 0.5f, 0.4f); p.col.fill = 0x1A1204; p.col.fillAlpha = 0.88f; p.col.accent = SAVE_AMBER; p.col.picked = 0xFFE6B0; p.border = 2;
        p.keys = { { 0, 0.25f, 0.33f, 0.96f, 0, true }, { 0.25f, 0.25f, 0.3f, 1, 1, true } }; s.items.push_back(p);
        ScreenItem k = text("Kicker", "{savefile}", 0.27f, 0.34f, 0.46f, 0.03f, 0.0144f, 1); k.col.text = 0xC9A970; k.weight = 0.3f; k.showAt = 0.1f; s.items.push_back(k);
        ScreenItem t = text("Title", "{savetitle}", 0.27f, 0.38f, 0.46f, 0.09f, 0.071f, 1); t.col.text = SAVE_AMBER; t.weight = 0.6f; t.showAt = 0.1f; t.fade = 0.25f; s.items.push_back(t);
        ScreenItem l = rule("Line", 0.37f, 0.48f, 0.26f, SAVE_AMBER, 0.7f); l.showAt = 0.15f; s.items.push_back(l);
        ScreenItem x = text("Text", "{savetext}", 0.27f, 0.5f, 0.46f, 0.05f, 0.0245f, 1); x.col.text = 0xF2DDB6; x.showAt = 0.25f; x.typeSpeed = 70; s.items.push_back(x);
        ScreenItem h = text("Continue", "{key:jump}: continue", 0.27f, 0.63f, 0.46f, 0.04f, 0.0167f, 1); h.col.text = 0xC9A970; h.showAt = 0.8f; h.fade = 0.4f; s.items.push_back(h);
        s.comp = Compositor{};
        int bg = s.comp.add(NodeType::Background, 40, 60), ui = s.comp.add(NodeType::UI, 40, 240), bl = s.comp.add(NodeType::Blur, 260, 60);
        int over = s.comp.add(NodeType::AlphaOver, 480, 140), gl = s.comp.add(NodeType::Glare, 700, 140), sl = s.comp.add(NodeType::Scanlines, 920, 140), vig = s.comp.add(NodeType::Vignette, 1140, 140), out = s.comp.add(NodeType::Output, 1360, 140);
        s.comp.find(bl)->p[0] = 8; s.comp.find(sl)->p[0] = 0.1f; s.comp.find(sl)->p[1] = 300; s.comp.find(vig)->p[0] = 0.55f; s.comp.find(vig)->c[0] = 0x140C00;
        s.comp.connect(bg, bl, 0); s.comp.connect(bl, over, 0); s.comp.connect(ui, over, 1); s.comp.connect(over, gl, 0); s.comp.connect(gl, sl, 0); s.comp.connect(sl, vig, 0); s.comp.connect(vig, out, 0);
        break; }
    default: s.name = "Screen"; break;
    }
    auto tr = [](TransKind k, float t, int dir = 1, int ease = 0, uint32_t col = 0x000000) { Transition x; x.kind = k; x.time = t; x.dir = dir; x.ease = ease; x.color = col; return x; };
    switch (r) {   // how each comes and goes
    case ScreenRole::MainMenu: s.in = tr(TransKind::Fade, 0.6f); break;
    case ScreenRole::Options: case ScreenRole::Files: s.in = tr(TransKind::Slide, 0.35f, 1, 1); s.out = tr(TransKind::Slide, 0.3f, 1, 1); break;
    case ScreenRole::Credits: s.in = tr(TransKind::Fade, 0.8f); break;
    case ScreenRole::Death: s.in = tr(TransKind::Static, 1.0f, 1, 3); break;
    case ScreenRole::ItemGet: s.in = tr(TransKind::VisorScan, 0.45f, 1, 0, VISOR_CYAN); s.out = tr(TransKind::Crossfade, 0.25f); break;
    case ScreenRole::GameSaved: s.in = tr(TransKind::Flash, 0.45f, 1, 0, 0xFFE6B0); s.out = tr(TransKind::Crossfade, 0.3f); break;
    case ScreenRole::Intro: s.in = tr(TransKind::Fade, 0.6f); s.out = tr(TransKind::Fade, 0.8f); break;
    default: break;
    }
    visorFonts(s);
    return s;
}
static Screen storyScreen() {   // an intro like Super Metroid's: a stack of steps - text typed over the dark, page after page, then the game
    Screen s; s.name = "Story"; s.role = ScreenRole::NewGameIntro; s.comp = defaultCompositor(); s.bg.color = 0x02040A; s.bg.color2 = 0x0A1B28; s.fadeIn = 1; s.useStack = true;
    for (const char* page : { "The research station on planet Zebes\nhas gone silent.", "The last message spoke of creatures\nthat drain the life from anything they touch.", "One hunter is sent to find out why." }) {
        StackStep st; st.kind = StepKind::Text; st.text = page; st.place = 1; st.hold = 2.5f; st.textSize = 0.04f; st.weight = 0.2f; s.stack.push_back(st);
    }
    s.duration = buildStack(s.stack).length;
    ScreenItem k = text("Skip", "ANY KEY: SKIP", 0.7f, 0.9f, 0.24f, 0.04f, 0.0156f, 2); k.col.text = 0x7FA3B5; k.showAt = 2; s.items.push_back(k);
    s.in.kind = TransKind::Fade; s.in.time = 0.8f; s.out.kind = TransKind::Fade; s.out.time = 1.0f;
    visorFonts(s);
    return s;
}
Screen presetStory() { return storyScreen(); }
Screen presetPhazonIntro() {
    // 1 (0-16 s): a Phazon-blue glow fades in; code types in over it (dark letters, a cursor, made-up scanner lines), glitch streaks run across
    // 2 (16-25 s): ink blots bloom, deepen, a darkness swallows it all; the picture shakes
    // 3 (25.5-29.5 s): a corrupted planet burns in out of the black
    // 4 (30.5 s on): the title fades in, the subtitle scans in with glitches, "press start" breathes; it waits there for a key
    Screen s; s.name = "Phazon intro"; s.role = ScreenRole::Intro; s.fadeIn = 0.2f; s.next = ""; s.skippable = true;
    s.bg.kind = BgKind::Space; s.bg.planet = "Corrupted world"; s.bg.planetSize = 0.62f; s.bg.planetX = 0.5f; s.bg.planetY = 0.98f;
    s.bg.spin = 2.5f; s.bg.axisTilt = 18; s.bg.sunYaw = -35; s.bg.sunPitch = 28; s.bg.stars = 0.55f; s.bg.nebula = 0.3f; s.bg.nebulaColor = 0x081A4A;
    const uint32_t INK = 0x041640, DIM = 0x0A3480;
    auto code = [&](const char* name, const char* t, float x, float y, float w, float size, float at, float speed, float weight) {
        ScreenItem i = text(name, t, x, y, w, size * 1.4f, size, 0); i.col.text = INK; i.col.dim = DIM; i.font = 1; i.weight = weight;
        i.showAt = at; i.fade = 0.05f; i.typeSpeed = speed; i.cursor = true; i.code = true; i.hideAt = 25.0f; i.fadeOut = 0.4f; s.items.push_back(i);
    };
    code("Kiru presents", "KIRU PRESENTS", 0.24f, 0.36f, 0.6f, 0.042f, 3.0f, 12, 0.5f);
    code("A fan game", "A FAN GAME", 0.24f, 0.43f, 0.6f, 0.036f, 6.2f, 12, 0.4f);
    { ScreenItem n = text("Based on", "based on NINTENDO", 0.52f, 0.475f, 0.3f, 0.026f, 0.017f, 0); n.col.text = DIM; n.font = 1; n.weight = 0.3f;
      n.showAt = 8.6f; n.fade = 0.05f; n.typeSpeed = 20; n.hideAt = 25.0f; n.fadeOut = 0.4f; s.items.push_back(n); }
    code("Metroid Prime 4", "METROID PRIME 4", 0.24f, 0.52f, 0.6f, 0.046f, 10.4f, 11, 0.6f);
    code("Leviathan", "LEVIATHAN", 0.3f, 0.595f, 0.6f, 0.058f, 13.6f, 9, 0.75f);
    // the title: three pictures in the "screens" folder, put back together where they sat in the whole logo (a picture that isn't there yet: its text instead)
    auto logo = [&](const char* name, const char* file, const char* stand, float x, float y, float w, float h, float at, int fx, float fxTime, uint32_t textCol, uint32_t accent, float size) {
        ScreenItem i; i.name = name; i.kind = ScreenItemKind::Image; i.image = file; i.text = stand; i.x = x; i.y = y; i.w = w; i.h = h;
        i.textSize = size; i.font = 1; i.weight = 0.8f; i.sharp = true; i.col.text = textCol; i.col.accent = accent; i.showAt = at; i.fade = fx ? 0.05f : 1.6f; i.inFx = fx; i.inFxTime = fxTime;
        s.items.push_back(i);
    };
    logo("Logo: Metroid Prime", "MPL_logo_metroid_prime.png", "METROID PRIME", 0.109f, 0.100f, 0.591f, 0.301f, 30.5f, 0, 0.9f, 0xE8F3FF, 0x9FD0FF, 0.075f);
    logo("Logo: 4", "MPL_logo_4.png", "4", 0.665f, 0.096f, 0.230f, 0.295f, 31.8f, 2, 0.9f, 0x5AB4FF, 0x9FD0FF, 0.2f);
    logo("Logo: Leviathan", "MPL_logo_leviathan.png", "L E V I A T H A N", 0.102f, 0.373f, 0.799f, 0.172f, 32.8f, 1, 1.4f, 0xA070FF, 0xE0C8FF, 0.05f);
    { ScreenItem t = text("Press start", "PRESS {START} TO START", 0.2f, 0.85f, 0.6f, 0.04f, 0.022f, 1); t.col.text = 0xA8D8FF; t.font = 1; t.weight = 0.3f;
      t.showAt = 35.0f; t.fade = 1.0f; t.pulse = 2.2f; s.items.push_back(t); }
    s.duration = 35.6f; s.pressStart = true; s.next = "Files";   // (then the save files)
    s.out.kind = TransKind::Dissolve; s.out.time = 1.6f; s.out.color = 0x2E78FF; s.out.ease = 3;   // (pressing start: it burns away)
    s.shakeAt = 20.5f; s.shakeTime = 4.5f; s.shakeAmount = 0.014f;
    // the pictures: background -> burn in -> the glow -> the items over it -> glitches -> the ink -> glare -> vignette -> out
    s.comp = Compositor{};
    int bg = s.comp.add(NodeType::Background, 40, 60), ui = s.comp.add(NodeType::UI, 40, 260), burn = s.comp.add(NodeType::BurnIn, 260, 60), glow = s.comp.add(NodeType::LensGlow, 480, 60);
    int over = s.comp.add(NodeType::AlphaOver, 700, 160), gl = s.comp.add(NodeType::Glitch, 920, 160), ink = s.comp.add(NodeType::InkSpread, 1140, 160);
    int glare = s.comp.add(NodeType::Glare, 1360, 160), vig = s.comp.add(NodeType::Vignette, 1580, 160), out = s.comp.add(NodeType::Output, 1800, 160);
    { CompNode& n = *s.comp.find(glow); n.p[0] = 0.5f; n.p[1] = 24.6f; n.p[2] = 2.0f; }
    { CompNode& n = *s.comp.find(gl); n.p[0] = 0.55f; n.p[1] = 2.5f; n.p[2] = 25.0f; }
    { CompNode& n = *s.comp.find(ink); n.p[0] = 16.0f; n.p[1] = 8.5f; n.p[2] = 25.2f; }
    { CompNode& n = *s.comp.find(burn); n.p[0] = 25.5f; n.p[1] = 4.0f; }
    { CompNode& n = *s.comp.find(glare); n.p[0] = 0.75f; n.p[1] = 0.8f; n.c[0] = 0x8CC8FF; }
    { CompNode& n = *s.comp.find(vig); n.p[0] = 0.6f; n.c[0] = 0x000208; }
    s.comp.connect(bg, burn, 0); s.comp.connect(burn, glow, 0); s.comp.connect(glow, over, 0); s.comp.connect(ui, over, 1); s.comp.connect(over, gl, 0);
    s.comp.connect(gl, ink, 0); s.comp.connect(ink, glare, 0); s.comp.connect(glare, vig, 0); s.comp.connect(vig, out, 0);
    return s;
}
// ------------------------------------------------------------------ the Corruption layout (like Metroid Prime 3's menus)
// The scan screens (main menu, files, a file's menu, codex): the planet seen as a scan - a negative, toned cyan to white - through a visor
// frame (double lines, notches, ticks, crosshairs), choices in [ brackets ] in boxes whose fill fades to the right, scan lines fanning out
// from the planet. The plate screens (extras, options, credits, the gallery): rings turning in the dark behind a metal frame with a title tab.
static const uint32_t CR_TEXT = 0xEAF6F8, CR_LINE = 0xD2F0F6, CR_FILL = 0x072029, CR_PICK = 0x2E9ED0, CR_DIM = 0x8CB4C0, CR_BEZEL = 0x12303A;
static const uint32_t CR_METAL = 0x2A3238, CR_MLINE = 0xC8D4DA, CR_MFILL = 0x0A1216;
static ScreenItem crFrame(const std::string& title, int style) {
    ScreenItem f; f.name = "Frame"; f.kind = ScreenItemKind::Frame; f.x = 0; f.y = 0; f.w = 1; f.h = 1; f.text = title; f.style = style; f.font = 1; f.weight = 0.1f;
    f.textSize = style ? 0.04f : 0.036f; f.col.text = CR_TEXT; f.col.accent = style ? CR_MLINE : CR_LINE; f.col.fill = style ? CR_METAL : CR_BEZEL; f.col.fillAlpha = style ? 1.f : 0.86f;
    f.col.dim = CR_DIM; f.col.picked = style ? 0x0C1418 : CR_PICK; f.rounding = 0; f.border = 2; return f;
}
static ScreenItem crButton(const std::string& label, ScreenAct a, float x, float y, float w, float h, bool plate = false) {
    ScreenItem b; b.name = label; b.kind = ScreenItemKind::Button; b.text = "[ " + label + " ]"; b.act = a; b.x = x; b.y = y; b.w = w; b.h = h;
    b.textSize = std::min(0.036f, h * 0.45f); b.align = 0; b.font = 1; b.weight = 0.1f; b.outline = 0.25f; b.outlineColor = 0x03121A;
    b.style = 1; b.filled = true; b.frame = true; b.rounding = 0; b.border = 1.5f; b.fillFade = plate ? 0.25f : 0.92f; b.pickFill = 0.5f;
    b.col.text = CR_TEXT; b.col.accent = plate ? CR_MLINE : CR_LINE; b.col.picked = CR_PICK; b.col.fill = plate ? CR_MFILL : CR_FILL; b.col.fillAlpha = plate ? 0.62f : 0.72f; b.col.dim = CR_DIM;
    return b;
}
static void crScan(Screen& s) {   // the planet as a scan: a negative, toned from deep teal to white, softened, grainy; the items over it; a little glare, faint lines, darker edges
    s.bg = ScreenBg{}; s.bg.kind = BgKind::Space; s.bg.planet = "Corrupted world"; s.bg.planetSize = 0.46f; s.bg.planetX = 0.66f; s.bg.planetY = 0.52f;
    s.bg.spin = 3; s.bg.axisTilt = 20; s.bg.sunYaw = -60; s.bg.sunPitch = 25; s.bg.stars = 0.3f; s.bg.nebula = 0.5f; s.bg.nebulaColor = 0x1A3A5A;
    s.comp = Compositor{};
    int bg = s.comp.add(NodeType::Background, 40, 60), ui = s.comp.add(NodeType::UI, 40, 260), inv = s.comp.add(NodeType::Invert, 260, 60), gm = s.comp.add(NodeType::Gradient, 480, 60);
    int bl = s.comp.add(NodeType::Blur, 700, 60), gr = s.comp.add(NodeType::Grain, 920, 60), over = s.comp.add(NodeType::AlphaOver, 1140, 160), gl = s.comp.add(NodeType::Glare, 1360, 160);
    int sl = s.comp.add(NodeType::Scanlines, 1580, 160), vig = s.comp.add(NodeType::Vignette, 1800, 160), out = s.comp.add(NodeType::Output, 2020, 160);
    { CompNode& n = *s.comp.find(gm); n.c[0] = 0x0B2633; n.c[1] = 0xE9FCFF; }
    s.comp.find(bl)->p[0] = 1.5f;
    { CompNode& n = *s.comp.find(gr); n.p[0] = 0.07f; n.p[1] = 1.5f; n.p[2] = 1; }
    { CompNode& n = *s.comp.find(gl); n.p[0] = 0.9f; n.p[1] = 0.45f; n.p[2] = 18; n.c[0] = 0xBFF4FF; }
    { CompNode& n = *s.comp.find(sl); n.p[0] = 0.05f; n.p[1] = 360; }
    { CompNode& n = *s.comp.find(vig); n.p[0] = 0.45f; n.p[1] = 0.8f; n.p[2] = 0.5f; }
    s.comp.connect(bg, inv, 0); s.comp.connect(inv, gm, 0); s.comp.connect(gm, bl, 0); s.comp.connect(bl, gr, 0); s.comp.connect(gr, over, 0); s.comp.connect(ui, over, 1);
    s.comp.connect(over, gl, 0); s.comp.connect(gl, sl, 0); s.comp.connect(sl, vig, 0); s.comp.connect(vig, out, 0);
}
static void crPlate(Screen& s, float rings = 1) {   // the dark behind a metal plate: rings turning in it (an item, under everything)
    s.bg = ScreenBg{}; s.bg.kind = BgKind::Color; s.bg.color = 0x1A2227; s.bg.color2 = 0x05080B;
    ScreenItem r; r.name = "Rings"; r.kind = ScreenItemKind::Rings; r.x = -0.1f; r.y = -0.32f; r.w = 1.2f; r.h = 1.56f; r.speed = 0.035f; r.alpha = rings;
    r.col.fill = 0x161E23; r.col.accent = 0x7E8E98; r.col.picked = 0xE6FAFF; r.col.dim = 0x2E3A42; s.items.push_back(r);
    s.comp = Compositor{};
    int bg = s.comp.add(NodeType::Background, 40, 60), ui = s.comp.add(NodeType::UI, 40, 260), over = s.comp.add(NodeType::AlphaOver, 300, 160), gl = s.comp.add(NodeType::Glare, 520, 160);
    int gr = s.comp.add(NodeType::Grain, 740, 160), vig = s.comp.add(NodeType::Vignette, 960, 160), out = s.comp.add(NodeType::Output, 1180, 160);
    { CompNode& n = *s.comp.find(gl); n.p[0] = 0.8f; n.p[1] = 0.6f; n.p[2] = 22; n.c[0] = 0xD8F6FF; }
    { CompNode& n = *s.comp.find(gr); n.p[0] = 0.05f; n.p[1] = 1.5f; n.p[2] = 1; }
    s.comp.find(vig)->p[0] = 0.5f;
    s.comp.connect(bg, over, 0); s.comp.connect(ui, over, 1); s.comp.connect(over, gl, 0); s.comp.connect(gl, gr, 0); s.comp.connect(gr, vig, 0); s.comp.connect(vig, out, 0);
}
static void crCommon(Screen& s, bool mainMenu) {   // how they come and go: a quick crossfade (the main menu takes the splash's own way out), the visor pointer
    s.pointer = true; s.fadeIn = mainMenu ? 0.15f : 0.35f;
    s.in = Transition{}; s.out = Transition{}; s.out.kind = TransKind::Crossfade; s.out.time = 0.3f;
    if (!mainMenu) { s.in.kind = TransKind::Crossfade; s.in.time = 0.3f; }
}
static Screen presetCorruption(ScreenRole r) {
    Screen s; s.role = r;
    const float L = 0.105f, W = 0.79f;   // the scan screens' boxes: inside the frame's inner lines
    switch (r) {
    case ScreenRole::MainMenu: {   // "Main Menu": play, options, extras; back to the title
        s.name = "Main menu"; crScan(s);
        s.items.push_back(crFrame("Main Menu", 0));
        ScreenItem t; t.name = "Scan lines"; t.kind = ScreenItemKind::TargetLines; t.x = 0.25f; t.y = 0.33f; t.w = 0.19f; t.h = 0.4f; t.slots = 7; t.showAt = 0.3f; t.fade = 0.2f;
        t.col.accent = 0x0E2630; t.col.picked = 0x0E2630; t.alpha = 0.85f; s.items.push_back(t);
        const char* lbl[] = { "Play Game", "Options", "Extras", "Back" }; ScreenAct act[] = { ScreenAct::Files, ScreenAct::Options, ScreenAct::GoTo, ScreenAct::Title };
        const float ys[] = { 0.215f, 0.31f, 0.405f, 0.6f };
        for (int i = 0; i < 4; i++) { ScreenItem b = crButton(lbl[i], act[i], L, ys[i], W, 0.08f); if (act[i] == ScreenAct::GoTo) b.target = "Extras"; b.showAt = 0.05f + i * 0.06f; b.fade = 0.25f; s.items.push_back(b); }
        break; }
    case ScreenRole::Files: {   // "Select File": the three files in rows like the buttons
        s.name = "Files"; crScan(s);
        s.items.push_back(crFrame("Select File", 0));
        ScreenItem f; f.name = "Save files"; f.kind = ScreenItemKind::SaveSlots; f.x = L; f.y = 0.205f; f.w = W; f.h = 0.5f; f.slots = 3; f.cards = 0; f.style = 1; f.fillFade = 0.85f; f.pickFill = 0.5f;
        f.textSize = 0.03f; f.font = 1; f.border = 1.5f; f.rounding = 0; f.outline = 0.25f; f.outlineColor = 0x03121A;
        f.col.text = CR_TEXT; f.col.accent = CR_LINE; f.col.picked = CR_PICK; f.col.fill = CR_FILL; f.col.fillAlpha = 0.72f; f.col.dim = 0xB8D6DE; s.items.push_back(f);
        s.items.push_back(crButton("Back", ScreenAct::Back, L, 0.74f, 0.24f, 0.075f));
        break; }
    case ScreenRole::FileMenu: {   // the picked file: play it, its codex, options; what is in it on the right
        s.name = "File menu"; crScan(s);
        s.items.push_back(crFrame("{file}", 0));
        const char* lbl[] = { "{fileaction}", "Codex", "Options", "Back" }; ScreenAct act[] = { ScreenAct::PlayFile, ScreenAct::Codex, ScreenAct::Options, ScreenAct::Back };
        const float ys[] = { 0.215f, 0.31f, 0.405f, 0.6f };
        for (int i = 0; i < 4; i++) { ScreenItem b = crButton(lbl[i], act[i], L, ys[i], 0.4f, 0.08f); if (i == 0) b.name = "Play"; b.showAt = 0.05f + i * 0.06f; b.fade = 0.25f; s.items.push_back(b); }
        ScreenItem p; p.name = "File"; p.kind = ScreenItemKind::Panel; p.x = 0.54f; p.y = 0.215f; p.w = 0.355f; p.h = 0.37f; p.rounding = 0; p.border = 1.5f; p.fillFade = 0.35f;
        p.col.fill = CR_FILL; p.col.fillAlpha = 0.78f; p.col.accent = CR_LINE; p.showAt = 0.2f; p.inFx = 3; p.inFxTime = 0.4f; s.items.push_back(p);
        ScreenItem it = text("File info", "{fileinfo}", 0.56f, 0.235f, 0.315f, 0.33f, 0.025f, 0); it.col.text = CR_TEXT; it.font = 2; it.valign = 0; it.typeSpeed = 90; it.showAt = 0.45f;
        it.outline = 0.2f; it.outlineColor = 0x03121A; s.items.push_back(it);
        break; }
    case ScreenRole::Codex: {   // the scans: a list on the left, the picked one's text on the right
        s.name = "Codex"; crScan(s);
        s.items.push_back(crFrame("Codex", 0));
        ScreenItem l; l.name = "Scans"; l.kind = ScreenItemKind::ScanList; l.x = L; l.y = 0.2f; l.w = W; l.h = 0.55f; l.textSize = 0.024f; l.font = 1;
        l.col.text = CR_TEXT; l.col.accent = CR_LINE; l.col.dim = CR_DIM; l.col.picked = 0xFFFFFF; l.col.fill = CR_FILL; l.col.fillAlpha = 0.8f; s.items.push_back(l);
        s.items.push_back(crButton("Back", ScreenAct::Back, L, 0.775f, 0.24f, 0.075f));
        break; }
    case ScreenRole::Options: {   // on a metal plate over the rings
        s.name = "Options"; crPlate(s);
        s.items.push_back(crFrame("Options", 1));
        ScreenItem o; o.name = "Options"; o.kind = ScreenItemKind::Options; o.x = 0.05f; o.y = 0.17f; o.w = 0.9f; o.h = 0.65f; o.textSize = 0.028f; o.font = 1;
        o.col.text = CR_TEXT; o.col.accent = CR_PICK; o.col.picked = 0xFFFFFF; o.col.dim = 0x8A9CA6; s.items.push_back(o);
        s.items.push_back(crButton("Back", ScreenAct::Back, 0.05f, 0.85f, 0.2f, 0.075f, true));
        break; }
    case ScreenRole::Credits: {   // the roll on a metal plate, the rings dim behind
        s.name = "Credits"; crPlate(s, 0.6f);
        s.items.push_back(crFrame("Credits", 1));
        ScreenItem c; c.name = "Credits roll"; c.kind = ScreenItemKind::Credits; c.x = 0.25f; c.y = 0.16f; c.w = 0.5f; c.h = 0.66f; c.textSize = 0.031f; c.font = 2;
        c.text = "# {game}\n\n# Made by\nYou\n\n# Made with\nPRIMEngine\n\n\nTHANK YOU FOR PLAYING"; c.col.text = CR_TEXT; c.col.accent = CR_PICK; s.items.push_back(c);
        s.items.push_back(crButton("Back", ScreenAct::Back, 0.05f, 0.85f, 0.2f, 0.075f, true));
        break; }
    default: return presetScreen(r);
    }
    crCommon(s, r == ScreenRole::MainMenu);
    return s;
}
static Screen crExtras() {   // "Extras": a row each, across the plate (like the galleries and the rest in Metroid Prime 3)
    Screen s; s.name = "Extras"; crPlate(s);
    s.items.push_back(crFrame("Extras", 1));
    const char* lbl[] = { "Concept Gallery", "Credits", "Back" }; ScreenAct act[] = { ScreenAct::GoTo, ScreenAct::Credits, ScreenAct::Back };
    const float ys[] = { 0.17f, 0.295f, 0.795f };
    for (int i = 0; i < 3; i++) { ScreenItem b = crButton(lbl[i], act[i], 0.035f, ys[i], 0.93f, 0.095f, true); if (i == 0) b.target = "Concept Gallery"; b.showAt = 0.05f + i * 0.06f; b.fade = 0.25f; s.items.push_back(b); }
    crCommon(s, false);
    return s;
}
static Screen crGallery() {   // "Concept Gallery": the pictures in screens/gallery, one at a time
    Screen s; s.name = "Concept Gallery"; crPlate(s, 0.5f);
    s.items.push_back(crFrame("Concept Gallery", 1));
    ScreenItem g; g.name = "Gallery"; g.kind = ScreenItemKind::Gallery; g.image = "gallery"; g.x = 0.05f; g.y = 0.15f; g.w = 0.9f; g.h = 0.67f; g.textSize = 0.026f; g.font = 1;
    g.col.text = CR_TEXT; g.col.accent = CR_MLINE; g.col.picked = CR_PICK; g.col.dim = 0x8A9CA6; g.col.fill = CR_MFILL; g.col.fillAlpha = 0.7f; s.items.push_back(g);
    s.items.push_back(crButton("Back", ScreenAct::Back, 0.05f, 0.85f, 0.2f, 0.075f, true));
    crCommon(s, false);
    return s;
}
const char* screenLayoutName(ScreenLayout l) { static const char* n[] = { "Visor (PRIMEngine's own)", "Corruption (like Metroid Prime 3)" }; int i = (int)l; return i >= 0 && i < (int)ScreenLayout::COUNT ? n[i] : "?"; }
std::vector<ScreenRole> layoutRoles(ScreenLayout) { return { ScreenRole::MainMenu, ScreenRole::Files, ScreenRole::FileMenu, ScreenRole::Codex, ScreenRole::Options, ScreenRole::Credits }; }
std::vector<Screen> layoutOwnScreens(ScreenLayout l) { if (l == ScreenLayout::Corruption) return { crExtras(), crGallery() }; return {}; }
Screen presetScreen(ScreenRole r, ScreenLayout l) { return l == ScreenLayout::Corruption ? presetCorruption(r) : presetScreen(r); }
int applyLayout(ScreenSet& set, ScreenLayout l) {
    int n = 0;
    auto adopt = [](Screen& dst, const Screen& p) {   // the layout's look; its name (buttons lead there by name), role, music and sounds stay
        std::string name = dst.name; ScreenRole role = dst.role; UiSounds snd = dst.sounds; int mm = dst.musicMode; SoundUse mu = dst.music; float mf = dst.musicFade;
        dst = p; dst.name = name; dst.role = role; dst.sounds = snd; dst.musicMode = mm; dst.music = mu; dst.musicFade = mf;
    };
    for (const Screen& p : layoutOwnScreens(l)) { int i = set.find(p.name); if (i >= 0) adopt(set.screens[i], p); else set.screens.push_back(p); n++; }
    for (ScreenRole r : layoutRoles(l)) {
        Screen p = presetScreen(r, l); int i = set.byRole(r);
        if (i >= 0) adopt(set.screens[i], p);
        else { std::string base = p.name; int k = 2; while (set.find(p.name) >= 0) p.name = base + " " + std::to_string(k++); set.screens.push_back(p); }
        n++;
    }
    int sp = set.byRole(ScreenRole::Intro), mm = set.byRole(ScreenRole::MainMenu);   // the splash goes on to the main menu (not straight to the files)
    if (sp >= 0 && mm >= 0) {
        Screen& s = set.screens[sp]; int nx = s.next.empty() ? -1 : set.find(s.next);
        if (nx < 0 || set.screens[nx].role == ScreenRole::Files || set.screens[nx].role == ScreenRole::MainMenu) s.next = set.screens[mm].name;
    }
    return n;
}
ScreenSet defaultScreens() {
    ScreenSet s;
    for (ScreenRole r : { ScreenRole::MainMenu, ScreenRole::Files, ScreenRole::Options, ScreenRole::Death, ScreenRole::Credits }) s.screens.push_back(presetScreen(r));
    s.screens.push_back(presetScreen(ScreenRole::Intro)); s.screens.push_back(storyScreen());   // a splash before the main menu; the story after picking an empty file
    s.screens.push_back(presetScreen(ScreenRole::ItemGet));   // the item pop-up
    s.screens.push_back(presetScreen(ScreenRole::GameSaved)); // after a Save Station
    return s;
}
int ScreenSet::find(const std::string& name) const { for (int i = 0; i < (int)screens.size(); i++) if (screens[i].name == name) return i; return -1; }
int ScreenSet::byRole(ScreenRole r) const { for (int i = 0; i < (int)screens.size(); i++) if (screens[i].role == r) return i; return -1; }

// ------------------------------------------------------------------ timing
const char* stepKindName(StepKind k) {
    static const char* n[] = { "Picture", "Text", "Wait", "Sound", "Clear" };
    int i = (int)k; return i >= 0 && i < (int)StepKind::COUNT ? n[i] : "?";
}
StackBuild buildStack(const std::vector<StackStep>& steps) {
    StackBuild b; float t = 0.4f; int pic = -1, txt = -1; std::vector<int> kept;   // kept: texts staying along with the newest one
    auto letters = [](const std::string& s) { size_t n = 0; for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++; return n; };
    auto fadeAway = [&](int& i, float at, float over) { if (i < 0) return; b.items[i].hideAt = at + over; b.items[i].fadeOut = over; i = -1; };
    auto textsAway = [&](float at, float over) { for (int k : kept) { int j = k; fadeAway(j, at, over); } kept.clear(); fadeAway(txt, at, over); };
    for (size_t k = 0; k < steps.size(); k++) {
        const StackStep& s = steps[k]; b.starts.push_back(t);
        switch (s.kind) {
        case StepKind::Picture: {   // a new page: the one before fades as this one comes
            float f = std::max(0.05f, s.fade);
            textsAway(t, 0.5f); fadeAway(pic, t, f);
            ScreenItem it; it.name = "Picture " + std::to_string(k + 1); it.kind = ScreenItemKind::Image; it.image = s.file; it.showAt = t; it.fade = f;
            if (s.place == 1) { it.x = 0.15f; it.y = 0.06f; it.w = 0.7f; it.h = 0.58f; } else if (s.place == 2) { it.x = 0.3f; it.y = 0.2f; it.w = 0.4f; it.h = 0.42f; } else { it.x = 0; it.y = 0; it.w = 1; it.h = 1; }
            if (s.zoom) {   // slowly closer, from when it starts to show
                ItemKey a; a.t = t; a.x = it.x; a.y = it.y; a.smooth = false; ItemKey z = a; z.t = t + f + std::max(0.1f, s.zoomTime); z.scale = std::max(0.1f, s.zoomTo); z.smooth = true;
                it.keys = { a, z };
            }
            b.items.push_back(it); pic = (int)b.items.size() - 1;
            t += f + (s.zoom ? std::max(0.1f, s.zoomTime) : 0.f);
            break; }
        case StepKind::Text: {   // the text before goes first
            if (s.keep) { if (txt >= 0) kept.push_back(txt); txt = -1; }
            else if (txt >= 0 || !kept.empty()) { textsAway(t, 0.4f); t += 0.45f; }
            ScreenItem it; it.name = "Text " + std::to_string(k + 1); it.kind = ScreenItemKind::Text; it.text = s.text; it.showAt = t; it.fade = std::max(0.02f, s.textFade);
            it.typeSpeed = s.typeSpeed; it.typeSound = s.sound; it.textSize = s.textSize; it.col.text = s.color; it.align = s.align;
            it.weight = s.weight; it.outline = s.outline; it.outlineColor = s.outlineColor; it.font = s.font;
            it.x = 0.1f; it.w = 0.8f;
            if (s.place == 2) { it.y = 0.05f; it.h = 0.22f; it.valign = 0; } else if (s.place == 1) { it.y = 0.32f; it.h = 0.36f; it.valign = 1; } else if (s.place == 3) { it.y = 0.6f; it.h = 0.1f; it.valign = 0; } else { it.y = 0.68f; it.h = 0.26f; it.valign = 0; }
            b.items.push_back(it); txt = (int)b.items.size() - 1;
            t += std::max(s.typeSpeed > 0 ? letters(s.text) / s.typeSpeed : 0.f, s.typeSpeed > 0 ? 0.f : std::max(0.02f, s.textFade)) + std::max(0.f, s.hold);   // (shown at once: once it has faded in)
            break; }
        case StepKind::Wait: t += std::max(0.f, s.hold); break;
        case StepKind::Sound: {
            ScreenItem it; it.name = "Sound " + std::to_string(k + 1); it.kind = ScreenItemKind::Sound; it.sound = s.sound; it.showAt = t; b.items.push_back(it);
            t += std::max(0.f, s.hold); break; }
        case StepKind::Clear: { float f = std::max(0.05f, s.fade); textsAway(t, f); fadeAway(pic, t, f); t += f; break; }
        default: break;
        }
    }
    float end = steps.empty() ? 0 : 1.f; b.end = t; textsAway(t, end); fadeAway(pic, t, end);   // at the end all of it fades away
    b.length = t + end + (steps.empty() ? 0 : 0.3f);
    return b;
}
std::vector<StackStep> stepsFromItems(std::vector<ScreenItem>& items) {
    std::vector<ScreenItem> moving, rest;
    for (const ScreenItem& it : items) (it.kind == ScreenItemKind::Text || it.kind == ScreenItemKind::Image || it.kind == ScreenItemKind::Sound) ? moving.push_back(it) : rest.push_back(it);
    std::stable_sort(moving.begin(), moving.end(), [](const ScreenItem& a, const ScreenItem& b) { return a.showAt < b.showAt; });
    std::vector<StackStep> st; const ScreenItem* prevText = nullptr;
    for (size_t i = 0; i < moving.size(); i++) {
        const ScreenItem& it = moving[i];
        float next = buildStack(st).end; if (st.empty()) next = 0.4f;
        if (it.showAt > next + 0.05f) { StackStep w; w.kind = StepKind::Wait; w.hold = it.showAt - next; st.push_back(w); }   // (a pause, so it comes at about its time)
        StackStep s;
        if (it.kind == ScreenItemKind::Text) {
            s.kind = StepKind::Text; s.text = it.text; s.typeSpeed = it.typeSpeed; s.sound = it.typeSound; s.textSize = it.textSize; s.color = it.col.text; s.align = it.align;
            s.weight = it.weight; s.outline = it.outline; s.outlineColor = it.outlineColor; s.font = it.font; s.textFade = std::max(0.02f, it.fade);
            float cy = it.y + it.h * 0.5f; s.place = cy < 0.3f ? 2 : cy > 0.66f ? 0 : cy >= 0.53f ? 3 : 1;
            s.keep = prevText && (prevText->hideAt <= 0 || prevText->hideAt > it.showAt);   // (still there when this one comes)
            s.hold = 0; prevText = &it;
        } else if (it.kind == ScreenItemKind::Image) {
            s.kind = StepKind::Picture; s.file = it.image; s.fade = std::max(0.05f, it.fade);
            s.place = it.w >= 0.95f && it.h >= 0.95f ? 0 : it.y < 0.12f && it.w >= 0.6f ? 1 : 2;
            if (it.grow > 0) { s.zoom = true; s.zoomTime = 3; s.zoomTo = 1 + it.grow * 3; }
        } else { s.kind = StepKind::Sound; s.sound = it.sound; s.hold = 0; }
        st.push_back(s);
        if (s.kind == StepKind::Text) {   // it stays until the next one comes (or until it goes)
            float typed = buildStack(st).end;   // (when this one is done showing, with no stay)
            float until = i + 1 < moving.size() ? moving[i + 1].showAt : it.hideAt > 0 ? it.hideAt : typed + 1.5f;
            st.back().hold = std::max(0.f, until - typed);
        }
    }
    items = rest;
    return st;
}
const char* inFxName(int fx) { static const char* n[IN_FX_COUNT] = { "Fade", "Scan + glitch", "Glitch", "Scan" }; return n[fx >= 0 && fx < IN_FX_COUNT ? fx : 0]; }
float inFxProgress(const ScreenItem& it, double t) {
    if (it.inFx <= 0 || it.inFx >= IN_FX_COUNT) return 1.f;
    return (float)std::clamp((t - it.showAt) / std::max(0.05, (double)it.inFxTime), 0.0, 1.0);
}
CodeBits codeBits(const std::string& text) {   // made-up, but always the same for the same text: a tag, a line above, a line under
    uint32_t h = 2166136261u; for (unsigned char c : text) h = (h ^ c) * 16777619u;
    auto rnd = [&](uint32_t n) { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return h % n; };
    const char* low = "nmpkxvrt", * up = "PHKXRT", * al = "abcdefhknrsuvx0123456789";
    CodeBits b;
    b.tag = std::string(1, low[rnd(8)]) + up[rnd(6)] + "_" + char('0' + rnd(10));
    b.top = "(" + std::string(1, low[rnd(8)]) + char('0' + rnd(10)) + up[rnd(6)] + " " + char('1' + rnd(9)) + char('0' + rnd(10)) + al[rnd(24)] + up[rnd(6)] + ")";
    char t[96]; std::snprintf(t, sizeof t, "%dv -%d %s%s-%d%s -%d / %c%d%d  sync -%d  s-%d", 1 + (int)rnd(9), 10 + (int)rnd(90), "nu", rnd(2) ? "s" : "x", (int)rnd(9), rnd(2) ? "cr" : "kv",
                  1000 + (int)rnd(9000), up[rnd(6)], (int)rnd(10), (int)rnd(10), (int)rnd(10), 100 + (int)rnd(900));
    b.bottom = t;
    return b;
}
void shakeOffset(const Screen& s, double t, float out[2]) {
    out[0] = out[1] = 0;
    if (s.shakeTime <= 0 || s.shakeAmount <= 0 || t < s.shakeAt || t > s.shakeAt + s.shakeTime) return;
    double u = (t - s.shakeAt) / s.shakeTime, env = std::sin(u * 3.14159265) * (0.6 + 0.4 * u);   // (swells, strongest near the end, settles)
    uint32_t k = (uint32_t)(t * 30);   // (a new push 30 times a second)
    auto hs = [](uint32_t v) { v ^= v * 0x9E3779B9u; v ^= v >> 15; v *= 0x85EBCA6Bu; v ^= v >> 13; return (v & 0xFFFF) / 65535.0 * 2 - 1; };
    out[0] = (float)(hs(k * 2 + 1) * s.shakeAmount * env); out[1] = (float)(hs(k * 2 + 7) * s.shakeAmount * env);
}
float itemShown(const ScreenItem& it, double t) {
    double f = std::max(0.01f, it.fade), fo = std::max(0.01f, it.fadeOutTime());
    double in = (t - it.showAt) / f; double k = std::clamp(in, 0.0, 1.0);
    if (it.showAt <= 0 && t >= 0) k = 1;
    if (it.hideAt > 0) k = std::min(k, std::clamp((it.hideAt - t) / fo, 0.0, 1.0));
    return (float)k;
}
ItemPose itemPose(const ScreenItem& it, double t) {
    if (it.keys.empty()) return { it.x, it.y, 1, 1 };
    const std::vector<ItemKey>& k = it.keys;
    auto pose = [](const ItemKey& a) { return ItemPose{ a.x, a.y, a.scale, a.alpha }; };
    if (t <= k.front().t) return pose(k.front());
    if (t >= k.back().t) return pose(k.back());
    size_t i = 1; while (i < k.size() && k[i].t < t) i++;
    const ItemKey& a = k[i - 1]; const ItemKey& b = k[i];
    float u = b.t - a.t > 1e-5f ? (float)((t - a.t) / (b.t - a.t)) : 1.f; u = std::clamp(u, 0.f, 1.f);
    if (b.smooth) u = u * u * (3 - 2 * u);   // eases in and out
    return { a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, a.scale + (b.scale - a.scale) * u, a.alpha + (b.alpha - a.alpha) * u };
}
void setKey(ScreenItem& it, const ItemKey& k) {
    for (ItemKey& e : it.keys) if (std::fabs(e.t - k.t) < 0.02f) { e = k; return; }
    it.keys.push_back(k);
    std::sort(it.keys.begin(), it.keys.end(), [](const ItemKey& a, const ItemKey& b) { return a.t < b.t; });
}
float itemEnd(const ScreenItem& it, float len) {
    if (it.kind == ScreenItemKind::Sound) return it.showAt + std::max(0.3f, it.sound.length > 0 ? it.sound.length : 0.6f);
    return it.hideAt > 0 ? it.hideAt : std::max(len, it.showAt + 0.5f);
}
float screenLength(const Screen& s) {
    if (s.duration > 0) return s.duration;
    float e = 0;
    for (const ScreenItem& it : s.items) { e = std::max(e, it.showAt + it.fade); e = std::max(e, it.hideAt); for (const ItemKey& k : it.keys) e = std::max(e, k.t); }
    return std::max(10.f, e + 2);
}
std::vector<TextLine> wrapText(const std::string& s, float width, const std::function<float(const char*, const char*)>& measure) {
    std::vector<TextLine> out; const char* d = s.data(); size_t n = s.size(), p = 0;
    while (true) {   // one paragraph (up to a new line) at a time
        size_t pe = s.find('\n', p); if (pe == std::string::npos) pe = n;
        size_t ls = p, le = p, i = p;   // the line so far: [ls, le)
        while (i < pe) {
            size_t ws = i; while (ws < pe && s[ws] == ' ') ws++;          // the next word: [ws, we)
            if (ws >= pe) break;
            size_t we = ws; while (we < pe && s[we] != ' ') we++;
            if (le > ls && measure(d + ls, d + we) > width) { out.push_back({ ls, le }); ls = ws; }   // doesn't fit: a new line from this word
            le = we; i = we;
        }
        if (le < ls) le = ls;
        out.push_back({ ls, le });
        if (pe >= n) break;
        p = pe + 1;
    }
    return out;
}
size_t typedBytes(const std::string& text, const ScreenItem& it, double t) {
    if (it.typeSpeed <= 0) return text.size();
    double n = (t - it.showAt) * it.typeSpeed; if (n <= 0) return 0;
    size_t letters = (size_t)n, i = 0, seen = 0;
    while (i < text.size() && seen < letters) { i++; while (i < text.size() && ((unsigned char)text[i] & 0xC0) == 0x80) i++; seen++; }   // whole UTF-8 letters
    return i;
}

// ------------------------------------------------------------------ files
bool saveScreens(const std::string& path, const ScreenSet& set) {
    std::ofstream f(path); if (!f) return false;
    f << "screens 4\n";
    auto snd = [&](const char* k, const UiSounds& u) { f << k << " " << soundUseText(u.hover) << " " << soundUseText(u.click) << " " << soundUseText(u.back) << " " << soundUseText(u.text) << " " << soundUseText(u.letter) << "\n"; };
    snd("uisounds", set.sounds);
    f << "boot " << (set.bootOn ? 1 : 0) << " " << set.bootHelmet << " " << set.bootWire << " " << set.bootColor << " " << set.bootHud << "\n";
    f << "start " << set.startFade << " " << (set.fadeWithSound ? 1 : 0) << " " << soundUseText(set.startSound) << "\n";
    for (const Screen& s : set.screens) {
        f << "screen " << escText(s.name) << " " << (int)s.role << " " << s.fadeIn << "\n";
        const ScreenBg& b = s.bg;
        f << "bg " << (int)b.kind << " " << b.color << " " << b.color2 << " " << escText(b.file) << " " << b.fit << " " << b.speed << " " << (b.loop ? 1 : 0) << " "
          << escText(b.world) << " " << escText(b.camera) << " " << b.turn << " " << b.dim << " " << b.tilt << " " << b.height << "\n";
        if (b.kind == BgKind::Space || !b.planet.empty())
            f << "space " << escText(b.planet) << " " << b.planetSize << " " << b.planetX << " " << b.planetY << " " << b.spin << " " << b.axisTilt << " " << b.sunYaw << " " << b.sunPitch << " " << b.stars << " " << b.nebula << " " << b.nebulaColor << "\n";
        snd("ssounds", s.sounds);
        f << "music " << s.musicMode << " " << s.musicFade << " " << soundUseText(s.music) << "\n";
        f << "seq " << s.duration << " " << (s.untilVideoEnds ? 1 : 0) << " " << (s.skippable ? 1 : 0) << " " << escText(s.next) << "\n";
        if (s.useStack) f << "usestack 1\n";
        if (s.in.kind != TransKind::None || s.out.kind != TransKind::None) f << "trans " << transText(s.in) << " " << transText(s.out) << "\n";
        if (s.role == ScreenRole::ItemGet || s.role == ScreenRole::GameSaved) f << "hold " << s.holdMin << "\n";
        if (s.pointer) f << "pointer 1\n";
        if (s.shakeTime > 0 || s.pressStart) f << "shake " << s.shakeAt << " " << s.shakeTime << " " << s.shakeAmount << " " << (s.pressStart ? 1 : 0) << "\n";
        for (const StackStep& st : s.stack) {
            f << "step " << (int)st.kind << " " << escText(st.file) << " " << (st.zoom ? 1 : 0) << " " << st.zoomTo << " " << st.zoomTime << " " << st.place << " " << escText(st.text) << " "
              << st.typeSpeed << " " << st.hold << " " << st.fade << " " << st.textSize << " " << st.color << " " << st.align << " " << st.weight << " " << st.outline << " " << st.outlineColor << " " << st.textFade << " " << (st.keep ? 1 : 0) << "\n";
            if (!st.sound.empty()) f << "stsound " << soundUseText(st.sound) << "\n";
            if (st.font) f << "stfont " << st.font << "\n";
        }
        for (const ScreenItem& i : s.items) {
            const ScreenColors& c = i.col;
            f << "item " << escText(i.name) << " " << (int)i.kind << " " << i.x << " " << i.y << " " << i.w << " " << i.h << " " << escText(i.text) << " " << escText(i.image) << " "
              << i.textSize << " " << i.align << " " << c.text << " " << c.accent << " " << c.picked << " " << c.dim << " " << c.fill << " " << c.fillAlpha << " " << i.alpha << " "
              << (i.filled ? 1 : 0) << " " << (i.frame ? 1 : 0) << " " << i.rounding << " " << i.border << " " << (int)i.act << " " << escText(i.target) << " " << escText(i.group) << " " << i.speed << " " << i.slots << "\n";
            f << "anim " << i.showAt << " " << i.hideAt << " " << i.fade << " " << i.typeSpeed << " " << i.driftX << " " << i.driftY << " " << i.grow << " " << soundUseText(i.typeSound) << "\n";
            f << "anim2 " << i.fadeOut << " " << i.valign << "\n";
            if (i.weight > 0 || i.outline > 0) f << "tstyle " << i.weight << " " << i.outline << " " << i.outlineColor << "\n";
            if (i.ownTrans) f << "itrans " << transText(i.trans) << "\n";
            if (i.cards || i.pickFill != 0.25f || i.corners || !i.tip.empty() || i.font) f << "look2 " << i.cards << " " << i.pickFill << " " << (i.corners ? 1 : 0) << " " << escText(i.tip) << " " << i.font << "\n";
            for (const ItemKey& k : i.keys) f << "key " << k.t << " " << k.x << " " << k.y << " " << k.scale << " " << k.alpha << " " << (k.smooth ? 1 : 0) << "\n";
            if (!i.sound.empty()) f << "isound " << soundUseText(i.sound) << "\n";
            if (i.sharp) f << "isharp 1\n";
            if (i.style || i.fillFade > 0) f << "look3 " << i.style << " " << i.fillFade << "\n";
            if (i.inFx || i.cursor || i.code || i.pulse > 0) f << "ifx " << i.inFx << " " << i.inFxTime << " " << (i.cursor ? 1 : 0) << " " << (i.code ? 1 : 0) << " " << i.pulse << "\n";
        }
        f << "comp " << s.comp.nextId << "\n";
        for (const CompNode& n : s.comp.nodes) {
            f << "node " << n.id << " " << (int)n.type << " " << n.x << " " << n.y;
            for (float p : n.p) f << " " << p;
            for (uint32_t c : n.c) f << " " << c;
            f << " " << n.mode << " " << n.in[0] << " " << n.in[1] << " " << (n.mute ? 1 : 0) << "\n";
        }
    }
    return (bool)f;
}
bool loadScreens(const std::string& path, ScreenSet& set) {
    std::ifstream f(path); if (!f) return false;
    set = ScreenSet{}; std::string line; Screen* cur = nullptr; int version = 1;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k;
        auto readSnd = [&](UiSounds& u) { std::string rest; std::getline(l, rest); std::istringstream r(rest); std::string tok[30];
            for (int q = 0; q < 30; q++) r >> tok[q];
            auto one = [&](int at, SoundUse& su) { if (tok[at].empty()) return; std::string t6; for (int q = 0; q < 6; q++) t6 += tok[at + q] + " "; soundUseFrom(t6, su); };
            one(0, u.hover); one(6, u.click); one(12, u.back); one(18, u.text); one(24, u.letter); };   // (older files: the first three)
        if (k == "screens") l >> version;
        else if (k == "uisounds") readSnd(set.sounds);
        else if (k == "boot") { int on = 1; l >> on >> set.bootHelmet >> set.bootWire >> set.bootColor >> set.bootHud; set.bootOn = on != 0; }
        else if (k == "start") { int ws = 1; std::string rest; l >> set.startFade >> ws; std::getline(l, rest); soundUseFrom(rest, set.startSound); set.fadeWithSound = ws != 0; }
        else if (k == "screen") {
            Screen s; std::string n; int role = 0; l >> n >> role >> s.fadeIn; s.name = unescText(n);
            s.role = (ScreenRole)std::clamp(role, 0, (int)ScreenRole::COUNT - 1); s.comp = Compositor{};
            set.screens.push_back(s); cur = &set.screens.back();
        } else if (!cur) continue;
        else if (k == "bg") {
            ScreenBg& b = cur->bg; int kind = 0, loop = 1; std::string file, world, cam;
            l >> kind >> b.color >> b.color2 >> file >> b.fit >> b.speed >> loop >> world >> cam >> b.turn >> b.dim;
            { float tl, ht; if (l >> tl >> ht) { b.tilt = tl; b.height = ht; } }
            b.kind = (BgKind)std::clamp(kind, 0, (int)BgKind::COUNT - 1); b.file = unescText(file); b.world = unescText(world); b.camera = unescText(cam); b.loop = loop != 0;
        } else if (k == "space") {
            ScreenBg& b = cur->bg; std::string pl; l >> pl >> b.planetSize >> b.planetX >> b.planetY >> b.spin >> b.axisTilt >> b.sunYaw >> b.sunPitch >> b.stars >> b.nebula >> b.nebulaColor; b.planet = unescText(pl);
        } else if (k == "item") {
            ScreenItem i; ScreenColors& c = i.col; std::string n, t, im, tg, gr; int kind = 1, fl = 1, fr = 1, act = 0;
            l >> n >> kind >> i.x >> i.y >> i.w >> i.h >> t >> im >> i.textSize >> i.align >> c.text >> c.accent >> c.picked >> c.dim >> c.fill >> c.fillAlpha >> i.alpha
              >> fl >> fr >> i.rounding >> i.border >> act >> tg >> gr >> i.speed;
            if (!l) continue;
            { int sl; if (l >> sl) i.slots = std::clamp(sl, 1, 12); }
            i.name = unescText(n); i.text = unescText(t); i.image = unescText(im); i.target = unescText(tg); i.group = unescText(gr);
            i.kind = (ScreenItemKind)std::clamp(kind, 0, (int)ScreenItemKind::COUNT - 1); i.act = (ScreenAct)std::clamp(act, 0, (int)ScreenAct::COUNT - 1);
            i.filled = fl != 0; i.frame = fr != 0; cur->items.push_back(i);
        } else if (k == "ssounds") readSnd(cur->sounds);
        else if (k == "music") { std::string rest; l >> cur->musicMode >> cur->musicFade; std::getline(l, rest); soundUseFrom(rest, cur->music); cur->musicMode = std::clamp(cur->musicMode, 0, 2); }
        else if (k == "trans") { transFrom(l, cur->in); transFrom(l, cur->out); }
        else if (k == "itrans" && !cur->items.empty()) { cur->items.back().ownTrans = true; transFrom(l, cur->items.back().trans); }
        else if (k == "usestack") { int u = 0; l >> u; cur->useStack = u != 0; }
        else if (k == "step") {
            StackStep st; int kd = 1, z = 0; std::string fl, tx;
            if (l >> kd >> fl >> z >> st.zoomTo >> st.zoomTime >> st.place >> tx >> st.typeSpeed >> st.hold >> st.fade >> st.textSize >> st.color >> st.align) {
                st.kind = (StepKind)std::clamp(kd, 0, (int)StepKind::COUNT - 1); st.file = unescText(fl); st.text = unescText(tx); st.zoom = z != 0;
                { float w = 0, o = 0; uint32_t oc = 0; if (l >> w >> o >> oc) { st.weight = w; st.outline = o; st.outlineColor = oc; } }   // (older files: plain letters)
                { float tf = 0.15f; int kp = 0; if (l >> tf >> kp) { st.textFade = tf; st.keep = kp != 0; } }
                cur->stack.push_back(st);
            }
        }
        else if (k == "stfont" && !cur->stack.empty()) { int fo = 0; l >> fo; cur->stack.back().font = std::clamp(fo, 0, 2); }
        else if (k == "stsound" && !cur->stack.empty()) { std::string rest; std::getline(l, rest); soundUseFrom(rest, cur->stack.back().sound); }
        else if (k == "hold") l >> cur->holdMin;
        else if (k == "seq") { int uv = 0, sk = 1; std::string nx; l >> cur->duration >> uv >> sk >> nx; cur->untilVideoEnds = uv != 0; cur->skippable = sk != 0; cur->next = unescText(nx); }
        else if (k == "anim" && !cur->items.empty()) {
            ScreenItem& i = cur->items.back(); std::string rest;
            l >> i.showAt >> i.hideAt >> i.fade >> i.typeSpeed >> i.driftX >> i.driftY >> i.grow; std::getline(l, rest); soundUseFrom(rest, i.typeSound);
        }
        else if (k == "look2" && !cur->items.empty()) { ScreenItem& i = cur->items.back(); int co = 0; std::string tp; l >> i.cards >> i.pickFill >> co >> tp; i.corners = co != 0; i.tip = unescText(tp); i.pickFill = std::clamp(i.pickFill, 0.f, 1.f); int fo; if (l >> fo) i.font = std::clamp(fo, 0, 2); }
        else if (k == "tstyle" && !cur->items.empty()) { ScreenItem& i = cur->items.back(); l >> i.weight >> i.outline >> i.outlineColor; }
        else if (k == "anim2" && !cur->items.empty()) { ScreenItem& i = cur->items.back(); l >> i.fadeOut >> i.valign; i.valign = std::clamp(i.valign, 0, 2); }
        else if (k == "key" && !cur->items.empty()) { ItemKey kk; int sm = 1; if (l >> kk.t >> kk.x >> kk.y >> kk.scale >> kk.alpha >> sm) { kk.smooth = sm != 0; setKey(cur->items.back(), kk); } }
        else if (k == "ifx" && !cur->items.empty()) { ScreenItem& i = cur->items.back(); int cu = 0, co = 0; l >> i.inFx >> i.inFxTime >> cu >> co >> i.pulse; i.inFx = std::clamp(i.inFx, 0, IN_FX_COUNT - 1); i.cursor = cu != 0; i.code = co != 0; }
        else if (k == "isharp" && !cur->items.empty()) { int v = 0; l >> v; cur->items.back().sharp = v != 0; }
        else if (k == "look3" && !cur->items.empty()) { ScreenItem& i = cur->items.back(); l >> i.style >> i.fillFade; i.style = std::clamp(i.style, 0, 1); i.fillFade = std::clamp(i.fillFade, 0.f, 1.f); }
        else if (k == "pointer") { int v = 0; l >> v; cur->pointer = v != 0; }
        else if (k == "shake") { int ps = 0; l >> cur->shakeAt >> cur->shakeTime >> cur->shakeAmount >> ps; cur->pressStart = ps != 0; }
        else if (k == "isound" && !cur->items.empty()) { std::string rest; std::getline(l, rest); soundUseFrom(rest, cur->items.back().sound); }
        else if (k == "comp") l >> cur->comp.nextId;
        else if (k == "node") {
            CompNode n; int type = 2, mute = 0;
            l >> n.id >> type >> n.x >> n.y; for (float& p : n.p) l >> p; for (uint32_t& c : n.c) l >> c; l >> n.mode >> n.in[0] >> n.in[1] >> mute;
            if (!l || n.id <= 0) continue;
            n.type = (NodeType)std::clamp(type, 0, (int)NodeType::COUNT - 1); n.mute = mute != 0;
            cur->comp.nodes.push_back(n); cur->comp.nextId = std::max(cur->comp.nextId, n.id + 1);
        }
    }
    for (Screen& s : set.screens) for (CompNode& n : s.comp.nodes) for (int& i : n.in) if (i && !s.comp.find(i)) i = 0;   // (wires to nodes that are gone)
    for (Screen& s : set.screens) if (s.useStack) s.duration = buildStack(s.stack).length;   // (a stack lasts as long as its steps)
    if (version < 3) {   // (the story used to come right after the splash: now it comes after picking an empty file)
        int sp = set.byRole(ScreenRole::Intro), st = set.find("Story");
        if (sp >= 0 && st >= 0 && set.screens[sp].next == "Story" && set.screens[st].role == ScreenRole::Custom && set.byRole(ScreenRole::NewGameIntro) < 0) { set.screens[sp].next.clear(); set.screens[st].role = ScreenRole::NewGameIntro; }
    }
    if (version < 2 && set.byRole(ScreenRole::Files) < 0) set.screens.insert(set.screens.begin() + std::min<size_t>(1, set.screens.size()), presetScreen(ScreenRole::Files));   // (from before save files)
    return true;
}

} // namespace mc
