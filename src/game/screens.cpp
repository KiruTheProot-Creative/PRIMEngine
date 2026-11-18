#include "screens.h"
#include "visors.h"   // escText / unescText
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <sstream>

namespace mc {

const char* screenRoleName(ScreenRole r) {
    static const char* n[] = { "Custom", "Main menu", "Options", "Death screen", "Credits", "Files (save files)", "Splash (shown first)", "Intro of a new game (after an empty file)" };
    int i = (int)r; return i >= 0 && i < (int)ScreenRole::COUNT ? n[i] : "?";
}
const char* bgKindName(BgKind k) {
    static const char* n[] = { "Colour", "Picture / GIF", "Video", "A world (through a camera)", "The game behind" };
    int i = (int)k; return i >= 0 && i < (int)BgKind::COUNT ? n[i] : "?";
}
const char* screenItemKindName(ScreenItemKind k) {
    static const char* n[] = { "Panel", "Text", "Picture", "Button", "Options", "Credits roll", "Save files", "Sound" };
    int i = (int)k; return i >= 0 && i < (int)ScreenItemKind::COUNT ? n[i] : "?";
}
const char* screenActName(ScreenAct a) {
    static const char* n[] = { "Nothing", "New game", "Continue", "Options", "Credits", "Quit", "Go to a screen", "Back (the screen before)", "Try again", "Main menu", "Files (pick a save file)" };
    int i = (int)a; return i >= 0 && i < (int)ScreenAct::COUNT ? n[i] : "?";
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
Screen presetScreen(ScreenRole r) {
    Screen s; s.role = r; s.comp = defaultCompositor();
    switch (r) {
    case ScreenRole::MainMenu: {
        s.name = "Main menu";
        s.items.push_back(text("Title", "{game}", 0.08f, 0.14f, 0.84f, 0.14f, 0.11f, 0));
        ScreenItem sub = text("Subtitle", "Press a button", 0.08f, 0.28f, 0.6f, 0.05f, 0.03f, 0); sub.text = "A PRIMEngine game"; sub.col.text = 0x6CF0FF; sub.alpha = 0.7f; s.items.push_back(sub);
        float y = 0.44f;
        for (auto b : { std::make_pair("New game", ScreenAct::NewGame), std::make_pair("Continue", ScreenAct::Continue), std::make_pair("Options", ScreenAct::Options),
                        std::make_pair("Credits", ScreenAct::Credits), std::make_pair("Quit", ScreenAct::Quit) }) { s.items.push_back(button(b.first, b.second, 0.08f, y)); y += 0.085f; }
        break; }
    case ScreenRole::Options: {
        s.name = "Options";
        s.items.push_back(text("Title", "Options", 0.08f, 0.06f, 0.84f, 0.09f, 0.07f, 0));
        ScreenItem p; p.name = "Backing"; p.kind = ScreenItemKind::Panel; p.x = 0.07f; p.y = 0.17f; p.w = 0.86f; p.h = 0.68f; p.col.fillAlpha = 0.55f; s.items.push_back(p);
        ScreenItem o; o.name = "Options"; o.kind = ScreenItemKind::Options; o.x = 0.09f; o.y = 0.19f; o.w = 0.82f; o.h = 0.64f; o.textSize = 0.032f; s.items.push_back(o);
        s.items.push_back(button("Back", ScreenAct::Back, 0.08f, 0.88f, 0.2f));
        break; }
    case ScreenRole::Death: {
        s.name = "Death screen"; s.bg.kind = BgKind::Game; s.bg.dim = 0.35f;
        ScreenItem t = text("Title", "ENERGY DEPLETED", 0.1f, 0.3f, 0.8f, 0.12f, 0.08f); t.col.text = 0xFF4A3A; s.items.push_back(t);
        s.items.push_back(button("Try again", ScreenAct::Retry, 0.38f, 0.5f, 0.24f));
        s.items.push_back(button("Last save", ScreenAct::Continue, 0.38f, 0.59f, 0.24f));
        s.items.push_back(button("Main menu", ScreenAct::MainMenu, 0.38f, 0.68f, 0.24f));
        for (ScreenItem& i : s.items) if (i.kind == ScreenItemKind::Button) i.align = 1;
        s.comp = Compositor{};
        int bg = s.comp.add(NodeType::Background, 40, 60), ui = s.comp.add(NodeType::UI, 40, 240), hs = s.comp.add(NodeType::HueSat, 260, 60);
        int bl = s.comp.add(NodeType::Blur, 480, 60), over = s.comp.add(NodeType::AlphaOver, 700, 140), vig = s.comp.add(NodeType::Vignette, 920, 140), out = s.comp.add(NodeType::Output, 1140, 140);
        s.comp.find(hs)->p[1] = 0.25f; s.comp.find(bl)->p[0] = 6; s.comp.find(vig)->p[0] = 0.7f; s.comp.find(vig)->c[0] = 0x400000;
        s.comp.connect(bg, hs, 0); s.comp.connect(hs, bl, 0); s.comp.connect(bl, over, 0); s.comp.connect(ui, over, 1); s.comp.connect(over, vig, 0); s.comp.connect(vig, out, 0);
        s.fadeIn = 1.2f;
        break; }
    case ScreenRole::Credits: {
        s.name = "Credits";
        ScreenItem c; c.name = "Credits roll"; c.kind = ScreenItemKind::Credits; c.x = 0.15f; c.y = 0.05f; c.w = 0.7f; c.h = 0.8f; c.textSize = 0.036f;
        c.text = "# {game}\n\n# Made by\nYou\n\n# Made with\nPRIMEngine\n\n\nThank you for playing!";
        s.items.push_back(c);
        s.items.push_back(button("Back", ScreenAct::Back, 0.08f, 0.88f, 0.2f));
        break; }
    case ScreenRole::Files: {
        s.name = "Files";
        s.items.push_back(text("Title", "Choose a file", 0.08f, 0.06f, 0.84f, 0.09f, 0.07f, 0));
        ScreenItem f; f.name = "Save files"; f.kind = ScreenItemKind::SaveSlots; f.x = 0.08f; f.y = 0.19f; f.w = 0.84f; f.h = 0.62f; f.textSize = 0.034f; f.slots = 3; s.items.push_back(f);
        ScreenItem hint = text("Hint", "Enter: play this file      Delete twice: erase it      Esc: back", 0.08f, 0.82f, 0.84f, 0.04f, 0.022f, 0); hint.alpha = 0.55f; s.items.push_back(hint);
        s.items.push_back(button("Back", ScreenAct::Back, 0.08f, 0.88f, 0.2f));
        break; }
    case ScreenRole::Intro: {   // a splash: the game's name fades in and out, then on to the story
        s.name = "Splash"; s.bg.color = s.bg.color2 = 0x000000; s.comp = Compositor{}; s.fadeIn = 0.6f; s.duration = 4; s.next = "";
        { int bg = s.comp.add(NodeType::Background, 40, 60), ui = s.comp.add(NodeType::UI, 40, 220), over = s.comp.add(NodeType::AlphaOver, 300, 120), gl = s.comp.add(NodeType::Glare, 520, 120), out = s.comp.add(NodeType::Output, 740, 120);
          s.comp.connect(bg, over, 0); s.comp.connect(ui, over, 1); s.comp.connect(over, gl, 0); s.comp.connect(gl, out, 0); }
        ScreenItem t = text("Title", "{game}", 0.1f, 0.4f, 0.8f, 0.14f, 0.1f); t.showAt = 0.5f; t.hideAt = 3.6f; t.fade = 0.8f; t.grow = 0.02f; s.items.push_back(t);
        ScreenItem m = text("Made with", "made with PRIMEngine", 0.1f, 0.56f, 0.8f, 0.05f, 0.028f); m.showAt = 1.3f; m.hideAt = 3.4f; m.fade = 0.6f; m.alpha = 0.6f; s.items.push_back(m);
        break; }
    default: s.name = "Screen"; break;
    }
    return s;
}
static Screen storyScreen() {   // an intro like Super Metroid's: a stack of steps - text typed over the dark, page after page, then the game
    Screen s; s.name = "Story"; s.role = ScreenRole::NewGameIntro; s.comp = defaultCompositor(); s.bg.color = 0x02040A; s.bg.color2 = 0x0A1424; s.fadeIn = 1; s.useStack = true;
    for (const char* page : { "The research station on planet Zebes\nhas gone silent.", "The last message spoke of creatures\nthat drain the life from anything they touch.", "One hunter is sent to find out why." }) {
        StackStep st; st.kind = StepKind::Text; st.text = page; st.place = 1; st.hold = 2.5f; s.stack.push_back(st);
    }
    s.duration = buildStack(s.stack).length;
    ScreenItem k = text("Skip", "any key: skip", 0.7f, 0.92f, 0.26f, 0.04f, 0.02f, 2); k.alpha = 0.4f; k.showAt = 2; s.items.push_back(k);
    return s;
}
Screen presetStory() { return storyScreen(); }
ScreenSet defaultScreens() {
    ScreenSet s;
    for (ScreenRole r : { ScreenRole::MainMenu, ScreenRole::Files, ScreenRole::Options, ScreenRole::Death, ScreenRole::Credits }) s.screens.push_back(presetScreen(r));
    s.screens.push_back(presetScreen(ScreenRole::Intro)); s.screens.push_back(storyScreen());   // a splash before the main menu; the story after picking an empty file
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
    StackBuild b; float t = 0.4f; int pic = -1, txt = -1;
    auto letters = [](const std::string& s) { size_t n = 0; for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++; return n; };
    auto fadeAway = [&](int& i, float at, float over) { if (i < 0) return; b.items[i].hideAt = at + over; b.items[i].fadeOut = over; i = -1; };
    for (size_t k = 0; k < steps.size(); k++) {
        const StackStep& s = steps[k]; b.starts.push_back(t);
        switch (s.kind) {
        case StepKind::Picture: {   // a new page: the one before fades as this one comes
            float f = std::max(0.05f, s.fade);
            fadeAway(txt, t, 0.5f); fadeAway(pic, t, f);
            ScreenItem it; it.name = "Picture " + std::to_string(k + 1); it.kind = ScreenItemKind::Image; it.image = s.file; it.showAt = t; it.fade = f;
            if (s.place == 1) { it.x = 0.15f; it.y = 0.06f; it.w = 0.7f; it.h = 0.58f; } else { it.x = 0; it.y = 0; it.w = 1; it.h = 1; }
            if (s.zoom) {   // slowly closer, from when it starts to show
                ItemKey a; a.t = t; a.x = it.x; a.y = it.y; a.smooth = false; ItemKey z = a; z.t = t + f + std::max(0.1f, s.zoomTime); z.scale = std::max(0.1f, s.zoomTo); z.smooth = true;
                it.keys = { a, z };
            }
            b.items.push_back(it); pic = (int)b.items.size() - 1;
            t += f + (s.zoom ? std::max(0.1f, s.zoomTime) : 0.f);
            break; }
        case StepKind::Text: {   // the text before goes first
            if (txt >= 0) { fadeAway(txt, t, 0.4f); t += 0.45f; }
            ScreenItem it; it.name = "Text " + std::to_string(k + 1); it.kind = ScreenItemKind::Text; it.text = s.text; it.showAt = t; it.fade = 0.15f;
            it.typeSpeed = s.typeSpeed; it.typeSound = s.sound; it.textSize = s.textSize; it.col.text = s.color; it.align = s.align;
            it.weight = s.weight; it.outline = s.outline; it.outlineColor = s.outlineColor;
            it.x = 0.1f; it.w = 0.8f;
            if (s.place == 2) { it.y = 0.05f; it.h = 0.22f; it.valign = 0; } else if (s.place == 1) { it.y = 0.32f; it.h = 0.36f; it.valign = 1; } else { it.y = 0.68f; it.h = 0.26f; it.valign = 0; }
            b.items.push_back(it); txt = (int)b.items.size() - 1;
            t += (s.typeSpeed > 0 ? letters(s.text) / s.typeSpeed : 0.f) + std::max(0.f, s.hold);
            break; }
        case StepKind::Wait: t += std::max(0.f, s.hold); break;
        case StepKind::Sound: {
            ScreenItem it; it.name = "Sound " + std::to_string(k + 1); it.kind = ScreenItemKind::Sound; it.sound = s.sound; it.showAt = t; b.items.push_back(it);
            t += std::max(0.f, s.hold); break; }
        case StepKind::Clear: { float f = std::max(0.05f, s.fade); fadeAway(txt, t, f); fadeAway(pic, t, f); t += f; break; }
        default: break;
        }
    }
    float end = steps.empty() ? 0 : 1.f; fadeAway(txt, t, end); fadeAway(pic, t, end);   // at the end all of it fades away
    b.length = t + end + (steps.empty() ? 0 : 0.3f);
    return b;
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
    auto snd = [&](const char* k, const UiSounds& u) { f << k << " " << soundUseText(u.hover) << " " << soundUseText(u.click) << " " << soundUseText(u.back) << "\n"; };
    snd("uisounds", set.sounds);
    f << "start " << set.startFade << " " << (set.fadeWithSound ? 1 : 0) << " " << soundUseText(set.startSound) << "\n";
    for (const Screen& s : set.screens) {
        f << "screen " << escText(s.name) << " " << (int)s.role << " " << s.fadeIn << "\n";
        const ScreenBg& b = s.bg;
        f << "bg " << (int)b.kind << " " << b.color << " " << b.color2 << " " << escText(b.file) << " " << b.fit << " " << b.speed << " " << (b.loop ? 1 : 0) << " "
          << escText(b.world) << " " << escText(b.camera) << " " << b.turn << " " << b.dim << " " << b.tilt << " " << b.height << "\n";
        snd("ssounds", s.sounds);
        f << "music " << s.musicMode << " " << s.musicFade << " " << soundUseText(s.music) << "\n";
        f << "seq " << s.duration << " " << (s.untilVideoEnds ? 1 : 0) << " " << (s.skippable ? 1 : 0) << " " << escText(s.next) << "\n";
        if (s.useStack) f << "usestack 1\n";
        for (const StackStep& st : s.stack) {
            f << "step " << (int)st.kind << " " << escText(st.file) << " " << (st.zoom ? 1 : 0) << " " << st.zoomTo << " " << st.zoomTime << " " << st.place << " " << escText(st.text) << " "
              << st.typeSpeed << " " << st.hold << " " << st.fade << " " << st.textSize << " " << st.color << " " << st.align << " " << st.weight << " " << st.outline << " " << st.outlineColor << "\n";
            if (!st.sound.empty()) f << "stsound " << soundUseText(st.sound) << "\n";
        }
        for (const ScreenItem& i : s.items) {
            const ScreenColors& c = i.col;
            f << "item " << escText(i.name) << " " << (int)i.kind << " " << i.x << " " << i.y << " " << i.w << " " << i.h << " " << escText(i.text) << " " << escText(i.image) << " "
              << i.textSize << " " << i.align << " " << c.text << " " << c.accent << " " << c.picked << " " << c.dim << " " << c.fill << " " << c.fillAlpha << " " << i.alpha << " "
              << (i.filled ? 1 : 0) << " " << (i.frame ? 1 : 0) << " " << i.rounding << " " << i.border << " " << (int)i.act << " " << escText(i.target) << " " << escText(i.group) << " " << i.speed << " " << i.slots << "\n";
            f << "anim " << i.showAt << " " << i.hideAt << " " << i.fade << " " << i.typeSpeed << " " << i.driftX << " " << i.driftY << " " << i.grow << " " << soundUseText(i.typeSound) << "\n";
            f << "anim2 " << i.fadeOut << " " << i.valign << "\n";
            if (i.weight > 0 || i.outline > 0) f << "tstyle " << i.weight << " " << i.outline << " " << i.outlineColor << "\n";
            for (const ItemKey& k : i.keys) f << "key " << k.t << " " << k.x << " " << k.y << " " << k.scale << " " << k.alpha << " " << (k.smooth ? 1 : 0) << "\n";
            if (!i.sound.empty()) f << "isound " << soundUseText(i.sound) << "\n";
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
        auto readSnd = [&](UiSounds& u) { std::string rest; std::getline(l, rest); std::istringstream r(rest); std::string tok[18];
            for (int q = 0; q < 18; q++) r >> tok[q];
            auto one = [&](int at, SoundUse& su) { std::string t6; for (int q = 0; q < 6; q++) t6 += tok[at + q] + " "; soundUseFrom(t6, su); };
            one(0, u.hover); one(6, u.click); one(12, u.back); };
        if (k == "screens") l >> version;
        else if (k == "uisounds") readSnd(set.sounds);
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
        else if (k == "usestack") { int u = 0; l >> u; cur->useStack = u != 0; }
        else if (k == "step") {
            StackStep st; int kd = 1, z = 0; std::string fl, tx;
            if (l >> kd >> fl >> z >> st.zoomTo >> st.zoomTime >> st.place >> tx >> st.typeSpeed >> st.hold >> st.fade >> st.textSize >> st.color >> st.align) {
                st.kind = (StepKind)std::clamp(kd, 0, (int)StepKind::COUNT - 1); st.file = unescText(fl); st.text = unescText(tx); st.zoom = z != 0;
                { float w = 0, o = 0; uint32_t oc = 0; if (l >> w >> o >> oc) { st.weight = w; st.outline = o; st.outlineColor = oc; } }   // (older files: plain letters)
                cur->stack.push_back(st);
            }
        }
        else if (k == "stsound" && !cur->stack.empty()) { std::string rest; std::getline(l, rest); soundUseFrom(rest, cur->stack.back().sound); }
        else if (k == "seq") { int uv = 0, sk = 1; std::string nx; l >> cur->duration >> uv >> sk >> nx; cur->untilVideoEnds = uv != 0; cur->skippable = sk != 0; cur->next = unescText(nx); }
        else if (k == "anim" && !cur->items.empty()) {
            ScreenItem& i = cur->items.back(); std::string rest;
            l >> i.showAt >> i.hideAt >> i.fade >> i.typeSpeed >> i.driftX >> i.driftY >> i.grow; std::getline(l, rest); soundUseFrom(rest, i.typeSound);
        }
        else if (k == "tstyle" && !cur->items.empty()) { ScreenItem& i = cur->items.back(); l >> i.weight >> i.outline >> i.outlineColor; }
        else if (k == "anim2" && !cur->items.empty()) { ScreenItem& i = cur->items.back(); l >> i.fadeOut >> i.valign; i.valign = std::clamp(i.valign, 0, 2); }
        else if (k == "key" && !cur->items.empty()) { ItemKey kk; int sm = 1; if (l >> kk.t >> kk.x >> kk.y >> kk.scale >> kk.alpha >> sm) { kk.smooth = sm != 0; setKey(cur->items.back(), kk); } }
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
