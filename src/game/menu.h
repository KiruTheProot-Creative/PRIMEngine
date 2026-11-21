#pragma once
// The pause menu (Player editor > Menu): pages shown as tabs (Q / E or the tab bar switch them), each made of one or more
// bent panes inside the helmet, each pane holding items: panels, text, pictures, buttons (with an action: resume, the
// map, go to another page...), a tab bar, and lists that fill themselves (items you have, beams to pick, the scan index
// with the picked entry's text and turning model). Panes in "always" show on every page (a tab bar, a title).
// Plain C++, tested.
#include "hud.h"
#include <functional>
#include <string>
#include <vector>

namespace mc {

enum class MenuKind { Panel, Text, Image, Button, TabBar, ItemList, BeamList, ScanList, ScanText, ScanModel, Options, Categories, ItemDetail, COUNT };
// Categories: a list of kinds to pick (text "items": Beams, Morph Ball, Suits...; "log": the scan categories) - an Item list / Scan list with
// text "category" on the same page shows only that kind. Item detail: the picked item, big - its model turning, its name, what it does.   // Options: the player's options (text: one group, or empty: all, with their own tabs)
const char* menuKindName(MenuKind k);
enum class MenuAct { None, Resume, Restart, Map, Settings, Quit, GoTo, Back, QuickSave, COUNT };   // QuickSave: only if the game allows it (Body and movement)
const char* menuActName(MenuAct a);

struct MenuItem {
    std::string name = "Item";
    MenuKind kind = MenuKind::Text;
    float x = 0.1f, y = 0.1f, w = 0.8f, h = 0.12f;      // on its pane, 0..1 (left to right, top to bottom)
    uint32_t color = 0x6CF0FF; float alpha = 1;
    std::string text;                                   // Text / Button: what it says; {game} {world} {energy} {maxenergy} {missiles} {scans} are filled in
    std::string image;                                  // Image: a picture from the assets
    float textSize = 40;                                // pixels on a pane 1024 wide
    int align = 1;                                      // 0 left, 1 middle, 2 right
    bool filled = false; float thickness = 3, rounding = 12;   // Panel
    MenuAct act = MenuAct::None; std::string target;    // Button: what it does (GoTo: the page's name)
    bool ownColors = false; uint32_t c2 = 0xD8EEF8, c3 = 0xFFFFFF, c4 = 0x587888;   // Options: its own text / picked / dim colours (color: the accent)
};

struct MenuPane {
    std::string name = "Pane";
    V3 pos{ 0, 0, 0.9 };                                // its middle, in camera space
    float yaw = 0, pitch = 0, roll = 0;
    float width = 0.9f, height = 0.6f, bendX = 0.35f, bendY = 0.2f;
    uint32_t color = 0x6CF0FF;                          // its frame and glow
    float backing = 0.55f;                              // how dark the glass behind is (0: none)
    float bloom = 0.7f;
    bool frame = true;                                  // a thin line around it
    std::vector<MenuItem> items;
};

struct MenuPage {
    std::string name = "Main";
    bool tab = true;                                    // listed in the tab bar (Q / E go through these)
    std::vector<MenuPane> panes;
};

struct MenuLayout {
    std::vector<MenuPane> always;                       // on every page
    std::vector<MenuPage> pages;                        // the first one is where the menu opens
    float slide = 0.22f;                                // seconds a new page takes to slide in
    int findPage(const std::string& name) const;        // -1: none
    std::vector<int> tabs() const;                      // the pages in the tab bar, in order
    int optionsPage() const;                            // the page with an Options item (-1: none)
};
MenuPage defaultOptionsPage();                          // "Options": Sound, Graphics, Controls on one wide pane
MenuLayout defaultMenu();                               // the three-pane helmet menu: tabs on top (System, Inventory, Log), Options / Quit below
MenuLayout classicMenu();                               // the first layout: Main, Inventory, Logbook, Options
bool saveMenu(const std::string& path, const MenuLayout& m);
bool loadMenu(const std::string& path, MenuLayout& m);

constexpr int MENU_PANE_W = 1024;                       // a pane's picture is this many pixels wide
int menuPaneHeight(const MenuPane& p);                  // ... and this high
HudPlane menuPanePlane(const MenuPane& p);              // its surface (the same bend as a HUD plane)
// {game}, {energy}... in a text: value(name) gives what to put there ("" for names it does not know: left as they are)
std::string menuFill(const std::string& text, const std::function<std::string(const std::string&)>& value);

} // namespace mc
