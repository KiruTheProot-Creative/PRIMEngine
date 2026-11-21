#include "menu.h"
#include "visors.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace mc {

const char* menuKindName(MenuKind k) {
    static const char* n[] = { "Panel", "Text", "Image", "Button", "Tab bar", "Item list", "Beam list", "Scan list", "Scan text", "Scan model", "Options", "Categories", "Item detail" };
    int i = (int)k; return i >= 0 && i < (int)MenuKind::COUNT ? n[i] : "?";
}
const char* menuActName(MenuAct a) {
    static const char* n[] = { "Nothing", "Keep playing", "Start over", "Open the map", "Options", "Stop playing", "Go to a page", "Back (the page before)", "Quick save" };
    int i = (int)a; return i >= 0 && i < (int)MenuAct::COUNT ? n[i] : "?";
}

int MenuLayout::findPage(const std::string& name) const {
    for (int i = 0; i < (int)pages.size(); i++) if (pages[i].name == name) return i;
    return -1;
}
int MenuLayout::optionsPage() const {
    for (int i = 0; i < (int)pages.size(); i++) for (const MenuPane& p : pages[i].panes) for (const MenuItem& it : p.items) if (it.kind == MenuKind::Options) return i;
    return -1;
}
std::vector<int> MenuLayout::tabs() const {
    std::vector<int> t; for (int i = 0; i < (int)pages.size(); i++) if (pages[i].tab) t.push_back(i);
    return t;
}

static MenuItem item(const std::string& name, MenuKind k, float x, float y, float w, float h, const std::string& text = "", float size = 40) {
    MenuItem i; i.name = name; i.kind = k; i.x = x; i.y = y; i.w = w; i.h = h; i.text = text; i.textSize = size; return i;
}
static MenuItem button(const std::string& text, MenuAct a, float y, const std::string& target = "") {
    MenuItem i = item(text, MenuKind::Button, 0.08f, y, 0.84f, 0.1f, text, 40); i.act = a; i.target = target; return i;
}
static MenuPane pane(const std::string& name, V3 pos, float yaw, float w, float h) {
    MenuPane p; p.name = name; p.pos = pos; p.yaw = yaw; p.width = w; p.height = h; return p;
}

// the three-pane helmet menu: a tab bar on top, three bent panes (a list to pick from, the picked thing big, what is in it), actions below
MenuLayout defaultMenu() {
    MenuLayout m; m.slide = 0.2f;
    {   // on every page: the tabs on top, the actions below
        MenuPane t = pane("Tabs", { 0, 0.43, 1.02 }, 0, 0.95f, 0.11f); t.bendY = 0; t.bendX = 0.2f; t.backing = 0.45f;
        t.items.push_back(item("Tabs", MenuKind::TabBar, 0.03f, 0.12f, 0.94f, 0.76f, "", 38));
        m.always.push_back(t);
        MenuPane b = pane("Actions", { 0, -0.47, 1.02 }, 0, 0.95f, 0.1f); b.bendY = 0; b.bendX = 0.2f; b.backing = 0.45f;
        MenuItem o = item("Options", MenuKind::Button, 0.02f, 0.14f, 0.28f, 0.72f, "Options", 34); o.act = MenuAct::Settings; b.items.push_back(o);
        MenuItem q = item("Quick save", MenuKind::Button, 0.36f, 0.14f, 0.28f, 0.72f, "Quick save", 34); q.act = MenuAct::QuickSave; b.items.push_back(q);
        MenuItem x = item("Quit", MenuKind::Button, 0.7f, 0.14f, 0.28f, 0.72f, "Quit", 34); x.act = MenuAct::Quit; b.items.push_back(x);
        m.always.push_back(b);
    }
    auto side = [&](const std::string& name, float x, float yaw) { MenuPane p = pane(name, { x, -0.02, 0.98 }, yaw, 0.42f, 0.72f); p.bendX = 0.15f; p.bendY = 0.12f; return p; };
    {   // System: where you are and how you are doing, the map, starting over
        MenuPage pg; pg.name = "System";
        MenuPane l = side("Status", -0.62f, -24);
        l.items.push_back(item("Title", MenuKind::Text, 0.07f, 0.04f, 0.86f, 0.1f, "Status", 40));
        MenuItem e = item("Energy", MenuKind::Text, 0.07f, 0.18f, 0.86f, 0.08f, "Energy  {energy} / {maxenergy}", 30); e.align = 0; l.items.push_back(e);
        MenuItem mi = item("Missiles", MenuKind::Text, 0.07f, 0.28f, 0.86f, 0.08f, "Missiles  {missiles}", 30); mi.align = 0; l.items.push_back(mi);
        MenuItem sc = item("Scans", MenuKind::Text, 0.07f, 0.38f, 0.86f, 0.08f, "Scanned  {scans}", 30); sc.align = 0; l.items.push_back(sc);
        MenuPane c = pane("System", { 0, -0.02, 0.92 }, 0, 0.72f, 0.72f); c.bendX = 0.25f; c.bendY = 0.12f;
        c.items.push_back(item("Title", MenuKind::Text, 0.05f, 0.04f, 0.9f, 0.13f, "{game}", 56));
        MenuItem w = item("World", MenuKind::Text, 0.05f, 0.17f, 0.9f, 0.06f, "{world}", 28); w.alpha = 0.6f; c.items.push_back(w);
        c.items.push_back(button("Keep playing", MenuAct::Resume, 0.32f));
        c.items.push_back(button("Map  (M)", MenuAct::Map, 0.45f));
        c.items.push_back(button("Start over", MenuAct::Restart, 0.58f));
        MenuPane r = side("Help", 0.62f, 24);
        r.items.push_back(item("Title", MenuKind::Text, 0.07f, 0.04f, 0.86f, 0.1f, "Keys", 40));
        MenuItem k = item("Keys", MenuKind::Text, 0.07f, 0.18f, 0.86f, 0.7f, "Q / E  pages\nW / S  pick\nEnter  use\nEsc  back to the game", 28); k.align = 0; r.items.push_back(k);
        pg.panes.push_back(l); pg.panes.push_back(c); pg.panes.push_back(r); m.pages.push_back(pg);
    }
    {   // Inventory: kinds on the left, the picked item in the middle, what you have of that kind on the right (?????? for what you haven't found)
        MenuPage pg; pg.name = "Inventory";
        MenuPane l = side("Kinds", -0.62f, -24);
        l.items.push_back(item("Kinds", MenuKind::Categories, 0.06f, 0.05f, 0.88f, 0.9f, "items", 34));
        MenuPane c = pane("Item", { 0, -0.02, 0.92 }, 0, 0.72f, 0.72f); c.bendX = 0.25f; c.bendY = 0.12f;
        c.items.push_back(item("Detail", MenuKind::ItemDetail, 0.05f, 0.04f, 0.9f, 0.92f, "", 32));
        MenuPane r = side("Items", 0.62f, 24);
        r.items.push_back(item("Items", MenuKind::ItemList, 0.06f, 0.05f, 0.88f, 0.9f, "category", 32));
        pg.panes.push_back(l); pg.panes.push_back(c); pg.panes.push_back(r); m.pages.push_back(pg);
    }
    {   // Log: the scan categories, the picked entry (its model and its text), the entries of that category
        MenuPage pg; pg.name = "Log";
        MenuPane l = side("Categories", -0.62f, -24);
        l.items.push_back(item("Categories", MenuKind::Categories, 0.06f, 0.05f, 0.88f, 0.9f, "log", 34));
        MenuPane c = pane("Entry", { 0, -0.02, 0.92 }, 0, 0.72f, 0.72f); c.bendX = 0.25f; c.bendY = 0.12f;
        c.items.push_back(item("Model", MenuKind::ScanModel, 0.3f, 0.03f, 0.4f, 0.38f));
        c.items.push_back(item("Text", MenuKind::ScanText, 0.06f, 0.42f, 0.88f, 0.55f, "", 30));
        MenuPane r = side("Entries", 0.62f, 24);
        MenuItem n = item("Count", MenuKind::Text, 0.06f, 0.03f, 0.88f, 0.07f, "{scans} scanned", 26); n.alpha = 0.6f; r.items.push_back(n);
        r.items.push_back(item("Entries", MenuKind::ScanList, 0.06f, 0.11f, 0.88f, 0.85f, "category", 32));
        pg.panes.push_back(l); pg.panes.push_back(c); pg.panes.push_back(r); m.pages.push_back(pg);
    }
    MenuPage op = defaultOptionsPage(); op.tab = false; m.pages.push_back(op);   // (Options: the button below opens it)
    return m;
}

MenuLayout classicMenu() {
    MenuLayout m;
    {   // on every page: the tabs
        MenuPane t = pane("Tabs", { 0, 0.42, 1.0 }, 0, 1.15f, 0.12f); t.bendY = 0; t.backing = 0.5f;
        t.items.push_back(item("Tabs", MenuKind::TabBar, 0.03f, 0.14f, 0.94f, 0.72f, "", 40));
        m.always.push_back(t);
    }
    {   // Main
        MenuPage pg; pg.name = "Main";
        MenuPane p = pane("Main", { 0, -0.06, 0.9 }, 0, 0.78f, 0.64f);
        p.items.push_back(item("Title", MenuKind::Text, 0.05f, 0.04f, 0.9f, 0.15f, "PAUSED", 78));
        MenuItem g = item("Game name", MenuKind::Text, 0.05f, 0.19f, 0.9f, 0.07f, "{game}", 30); g.alpha = 0.6f; p.items.push_back(g);
        p.items.push_back(button("Keep playing", MenuAct::Resume, 0.31f));
        p.items.push_back(button("Start over", MenuAct::Restart, 0.43f));
        p.items.push_back(button("Map  (M)", MenuAct::Map, 0.55f));
        p.items.push_back(button("Options", MenuAct::Settings, 0.67f));
        p.items.push_back(button("Stop playing  (F5)", MenuAct::Quit, 0.79f));
        pg.panes.push_back(p); m.pages.push_back(pg);
    }
    {   // Inventory: what you have, and the beams / missiles to pick
        MenuPage pg; pg.name = "Inventory";
        MenuPane a = pane("Items", { -0.43, -0.05, 1.0 }, -18, 0.56f, 0.62f);
        a.items.push_back(item("Title", MenuKind::Text, 0.05f, 0.03f, 0.9f, 0.1f, "Items", 46));
        a.items.push_back(item("Item list", MenuKind::ItemList, 0.05f, 0.15f, 0.9f, 0.81f, "", 36));
        MenuPane b = pane("Arms", { 0.43, -0.05, 1.0 }, 18, 0.56f, 0.62f);
        b.items.push_back(item("Title", MenuKind::Text, 0.05f, 0.03f, 0.9f, 0.1f, "Beams and missiles", 46));
        b.items.push_back(item("Beam list", MenuKind::BeamList, 0.05f, 0.15f, 0.9f, 0.81f, "", 34));
        pg.panes.push_back(a); pg.panes.push_back(b); m.pages.push_back(pg);
    }
    {   // Logbook: the scan index
        MenuPage pg; pg.name = "Logbook";
        MenuPane a = pane("Entries", { -0.5, -0.05, 1.0 }, -20, 0.5f, 0.62f);
        a.items.push_back(item("Title", MenuKind::Text, 0.05f, 0.03f, 0.9f, 0.1f, "Scan index", 46));
        MenuItem c = item("Count", MenuKind::Text, 0.05f, 0.12f, 0.9f, 0.06f, "{scans} scanned", 26); c.alpha = 0.6f; a.items.push_back(c);
        a.items.push_back(item("Scan list", MenuKind::ScanList, 0.05f, 0.2f, 0.9f, 0.77f, "", 36));
        MenuPane mo = pane("Model", { 0, -0.03, 1.12 }, 0, 0.46f, 0.58f); mo.bendX = 0.1f; mo.bendY = 0.05f; mo.backing = 0.15f; mo.frame = false; mo.bloom = 0;
        mo.items.push_back(item("Model", MenuKind::ScanModel, 0.02f, 0.02f, 0.96f, 0.96f));
        MenuPane t = pane("Entry", { 0.5, -0.05, 1.0 }, 20, 0.5f, 0.62f);
        t.items.push_back(item("Text", MenuKind::ScanText, 0.06f, 0.04f, 0.88f, 0.92f, "", 36));
        pg.panes.push_back(a); pg.panes.push_back(mo); pg.panes.push_back(t); m.pages.push_back(pg);
    }
    m.pages.push_back(defaultOptionsPage());
    return m;
}

MenuPage defaultOptionsPage() {
    MenuPage pg; pg.name = "Options";
    MenuPane p = pane("Options", { 0, -0.07, 0.95 }, 0, 1.12f, 0.7f); p.bendX = 0.38f; p.bendY = 0.16f; p.backing = 0.62f;
    p.items.push_back(item("Options", MenuKind::Options, 0.04f, 0.05f, 0.92f, 0.9f, "", 32));
    pg.panes.push_back(p); return pg;
}

static void writePane(std::ostream& f, const MenuPane& p) {
    f << "pane " << escText(p.name) << " " << p.pos.x << " " << p.pos.y << " " << p.pos.z << " " << p.yaw << " " << p.pitch << " " << p.roll << " " << p.width << " " << p.height
      << " " << p.bendX << " " << p.bendY << " " << p.color << " " << p.backing << " " << p.bloom << " " << (p.frame ? 1 : 0) << "\n";
    for (const MenuItem& i : p.items)
        f << "item " << escText(i.name) << " " << (int)i.kind << " " << i.x << " " << i.y << " " << i.w << " " << i.h << " " << i.color << " " << i.alpha << " " << escText(i.text) << " " << escText(i.image)
          << " " << i.textSize << " " << i.align << " " << (i.filled ? 1 : 0) << " " << i.thickness << " " << i.rounding << " " << (int)i.act << " " << escText(i.target) << " " << (i.ownColors ? 1 : 0) << " " << i.c2 << " " << i.c3 << " " << i.c4 << "\n";
}
bool saveMenu(const std::string& path, const MenuLayout& m) {
    std::ofstream f(path); if (!f) return false;
    f << "menu 2\nslide " << m.slide << "\nalways\n";
    for (const MenuPane& p : m.always) writePane(f, p);
    for (const MenuPage& pg : m.pages) { f << "page " << escText(pg.name) << " " << (pg.tab ? 1 : 0) << "\n"; for (const MenuPane& p : pg.panes) writePane(f, p); }
    return (bool)f;
}
bool loadMenu(const std::string& path, MenuLayout& m) {
    std::ifstream f(path); if (!f) return false;
    m = MenuLayout{}; std::string line; std::vector<MenuPane>* panes = &m.always; int version = 1;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k;
        if (k == "menu") l >> version;
        else if (k == "slide") { float s; if (l >> s) m.slide = std::clamp(s, 0.f, 2.f); }
        else if (k == "always") panes = &m.always;
        else if (k == "page") { MenuPage pg; std::string n; int t = 1; l >> n >> t; pg.name = unescText(n); pg.tab = t != 0; m.pages.push_back(pg); panes = &m.pages.back().panes; }
        else if (k == "pane") {
            MenuPane p; std::string n; int fr = 1;
            l >> n >> p.pos.x >> p.pos.y >> p.pos.z >> p.yaw >> p.pitch >> p.roll >> p.width >> p.height >> p.bendX >> p.bendY >> p.color >> p.backing >> p.bloom >> fr;
            p.name = unescText(n); p.frame = fr != 0; p.width = std::max(0.02f, p.width); p.height = std::max(0.02f, p.height); panes->push_back(p);
        } else if (k == "item" && !panes->empty()) {
            MenuItem i; std::string n, t, im, tg; int kind = 1, fl = 0, act = 0;
            l >> n >> kind >> i.x >> i.y >> i.w >> i.h >> i.color >> i.alpha >> t >> im >> i.textSize >> i.align >> fl >> i.thickness >> i.rounding >> act >> tg;
            i.name = unescText(n); i.text = unescText(t); i.image = unescText(im); i.target = unescText(tg); i.filled = fl != 0;
            i.kind = (MenuKind)std::clamp(kind, 0, (int)MenuKind::COUNT - 1); i.act = (MenuAct)std::clamp(act, 0, (int)MenuAct::COUNT - 1);
            { int own = 0; uint32_t c2, c3, c4; if (l >> own >> c2 >> c3 >> c4) { i.ownColors = own != 0; i.c2 = c2; i.c3 = c3; i.c4 = c4; } }   // (older files: none)
            panes->back().items.push_back(i);
        }
    }
    if (version < 2 && m.optionsPage() < 0) {   // menus from before the Options page: it is added (Settings buttons now lead there)
        m.pages.push_back(defaultOptionsPage());
        for (MenuPage& pg : m.pages) for (MenuPane& p : pg.panes) for (MenuItem& it : p.items) if (it.kind == MenuKind::Button && it.act == MenuAct::Settings && it.text == "Settings") it.text = it.name = "Options";
    }
    return true;
}

int menuPaneHeight(const MenuPane& p) { return std::clamp((int)std::lround(MENU_PANE_W * (double)p.height / std::max(0.01f, p.width)), 16, 2048); }
HudPlane menuPanePlane(const MenuPane& p) {
    HudPlane h; h.name = p.name; h.pos = p.pos; h.yaw = p.yaw; h.pitch = p.pitch; h.roll = p.roll; h.width = p.width; h.height = p.height;
    h.bendX = p.bendX; h.bendY = p.bendY; h.sway = 0.5f; h.opacity = 1; h.glow = false; h.bloom = p.bloom; h.lightUp = 0; h.elements.clear();
    return h;
}

std::string menuFill(const std::string& text, const std::function<std::string(const std::string&)>& value) {
    std::string o;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '{') {
            size_t e = text.find('}', i);
            if (e != std::string::npos) { std::string v = value(text.substr(i + 1, e - i - 1)); if (!v.empty()) { o += v; i = e; continue; } }
        }
        o += text[i];
    }
    return o;
}

} // namespace mc
