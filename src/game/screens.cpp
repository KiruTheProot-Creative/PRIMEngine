#include "screens.h"
#include "visors.h"   // escText / unescText
#include <algorithm>
#include <fstream>
#include <functional>
#include <sstream>

namespace mc {

const char* screenRoleName(ScreenRole r) {
    static const char* n[] = { "Custom", "Main menu", "Options", "Death screen", "Credits" };
    int i = (int)r; return i >= 0 && i < (int)ScreenRole::COUNT ? n[i] : "?";
}
const char* bgKindName(BgKind k) {
    static const char* n[] = { "Colour", "Picture / GIF", "Video", "A world (through a camera)", "The game behind" };
    int i = (int)k; return i >= 0 && i < (int)BgKind::COUNT ? n[i] : "?";
}
const char* screenItemKindName(ScreenItemKind k) {
    static const char* n[] = { "Panel", "Text", "Picture", "Button", "Options", "Credits roll" };
    int i = (int)k; return i >= 0 && i < (int)ScreenItemKind::COUNT ? n[i] : "?";
}
const char* screenActName(ScreenAct a) {
    static const char* n[] = { "Nothing", "New game", "Continue", "Options", "Credits", "Quit", "Go to a screen", "Back (the screen before)", "Try again", "Main menu" };
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
        s.items.push_back(button("Try again", ScreenAct::Retry, 0.38f, 0.52f, 0.24f));
        s.items.push_back(button("Main menu", ScreenAct::MainMenu, 0.38f, 0.61f, 0.24f));
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
    default: s.name = "Screen"; break;
    }
    return s;
}
ScreenSet defaultScreens() {
    ScreenSet s;
    for (ScreenRole r : { ScreenRole::MainMenu, ScreenRole::Options, ScreenRole::Death, ScreenRole::Credits }) s.screens.push_back(presetScreen(r));
    return s;
}
int ScreenSet::find(const std::string& name) const { for (int i = 0; i < (int)screens.size(); i++) if (screens[i].name == name) return i; return -1; }
int ScreenSet::byRole(ScreenRole r) const { for (int i = 0; i < (int)screens.size(); i++) if (screens[i].role == r) return i; return -1; }

// ------------------------------------------------------------------ files
bool saveScreens(const std::string& path, const ScreenSet& set) {
    std::ofstream f(path); if (!f) return false;
    f << "screens 1\n";
    for (const Screen& s : set.screens) {
        f << "screen " << escText(s.name) << " " << (int)s.role << " " << s.fadeIn << "\n";
        const ScreenBg& b = s.bg;
        f << "bg " << (int)b.kind << " " << b.color << " " << b.color2 << " " << escText(b.file) << " " << b.fit << " " << b.speed << " " << (b.loop ? 1 : 0) << " "
          << escText(b.world) << " " << escText(b.camera) << " " << b.turn << " " << b.dim << "\n";
        for (const ScreenItem& i : s.items) {
            const ScreenColors& c = i.col;
            f << "item " << escText(i.name) << " " << (int)i.kind << " " << i.x << " " << i.y << " " << i.w << " " << i.h << " " << escText(i.text) << " " << escText(i.image) << " "
              << i.textSize << " " << i.align << " " << c.text << " " << c.accent << " " << c.picked << " " << c.dim << " " << c.fill << " " << c.fillAlpha << " " << i.alpha << " "
              << (i.filled ? 1 : 0) << " " << (i.frame ? 1 : 0) << " " << i.rounding << " " << i.border << " " << (int)i.act << " " << escText(i.target) << " " << escText(i.group) << " " << i.speed << "\n";
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
    set = ScreenSet{}; std::string line; Screen* cur = nullptr;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k;
        if (k == "screen") {
            Screen s; std::string n; int role = 0; l >> n >> role >> s.fadeIn; s.name = unescText(n);
            s.role = (ScreenRole)std::clamp(role, 0, (int)ScreenRole::COUNT - 1); s.comp = Compositor{};
            set.screens.push_back(s); cur = &set.screens.back();
        } else if (!cur) continue;
        else if (k == "bg") {
            ScreenBg& b = cur->bg; int kind = 0, loop = 1; std::string file, world, cam;
            l >> kind >> b.color >> b.color2 >> file >> b.fit >> b.speed >> loop >> world >> cam >> b.turn >> b.dim;
            b.kind = (BgKind)std::clamp(kind, 0, (int)BgKind::COUNT - 1); b.file = unescText(file); b.world = unescText(world); b.camera = unescText(cam); b.loop = loop != 0;
        } else if (k == "item") {
            ScreenItem i; ScreenColors& c = i.col; std::string n, t, im, tg, gr; int kind = 1, fl = 1, fr = 1, act = 0;
            l >> n >> kind >> i.x >> i.y >> i.w >> i.h >> t >> im >> i.textSize >> i.align >> c.text >> c.accent >> c.picked >> c.dim >> c.fill >> c.fillAlpha >> i.alpha
              >> fl >> fr >> i.rounding >> i.border >> act >> tg >> gr >> i.speed;
            if (!l) continue;
            i.name = unescText(n); i.text = unescText(t); i.image = unescText(im); i.target = unescText(tg); i.group = unescText(gr);
            i.kind = (ScreenItemKind)std::clamp(kind, 0, (int)ScreenItemKind::COUNT - 1); i.act = (ScreenAct)std::clamp(act, 0, (int)ScreenAct::COUNT - 1);
            i.filled = fl != 0; i.frame = fr != 0; cur->items.push_back(i);
        } else if (k == "comp") l >> cur->comp.nextId;
        else if (k == "node") {
            CompNode n; int type = 2, mute = 0;
            l >> n.id >> type >> n.x >> n.y; for (float& p : n.p) l >> p; for (uint32_t& c : n.c) l >> c; l >> n.mode >> n.in[0] >> n.in[1] >> mute;
            if (!l || n.id <= 0) continue;
            n.type = (NodeType)std::clamp(type, 0, (int)NodeType::COUNT - 1); n.mute = mute != 0;
            cur->comp.nodes.push_back(n); cur->comp.nextId = std::max(cur->comp.nextId, n.id + 1);
        }
    }
    for (Screen& s : set.screens) for (CompNode& n : s.comp.nodes) for (int& i : n.in) if (i && !s.comp.find(i)) i = 0;   // (wires to nodes that are gone)
    return true;
}

} // namespace mc
