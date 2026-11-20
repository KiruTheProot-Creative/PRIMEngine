#pragma once
// Items (Player editor > Items): everything Samus can find, from across the Metroid games. Each one is in the game or
// not, you have it from the start or find it later (a pickup with its name), and has its own numbers. Beams, missile
// kinds, the Morph Ball and Bombs live elsewhere (Weapons tab, Body and movement) and are only listed with these.
// Some abilities are not built yet ("coming"): they can already be set up and placed as pickups. Plain C++, tested.
#include "game.h"
#include "sound.h"
#include <functional>
#include <string>
#include <vector>

namespace mc {

enum class ItemType {
    EnergyTank, MissileExpansion, PowerBombExpansion, PowerBomb, Suit, HiJump, SpaceJump, SpeedBooster, SpringBall, ChargeBeam,
    ScrewAttack, GrappleBeam, SpiderBall, BoostBall, Shinespark, WallJump, ScanVisor, ThermalVisor, XRayVisor, COUNT
};
const char* itemTypeName(ItemType t);
bool itemWorks(ItemType t);                  // false: listed, but its ability is still coming
bool itemCounted(ItemType t);                // tanks and expansions: you collect several (a counted pickup), each adds more
const char* itemGroup(ItemType t);           // where it is listed
const char* itemValueName(ItemType t, int k); // what its numbers mean (nullptr: not used)

struct ItemDef {
    ItemType type = ItemType::EnergyTank;
    std::string name;                        // shown, and the pickup that gives it
    bool used = true;                        // in this game at all
    bool startsOwned = false;
    float v[3] = { 0, 0, 0 };                // its numbers (see itemValueName)
    uint32_t color = 0xFFFFFF;               // suits: their colour (shown in the list)
};
// the item pop-up (the "Item get" screen, Screens editor): what it says when you pick something up. By pickup name: an item,
// a beam, a missile kind, "Morph Ball", "Bombs", a key or collectible. Texts: {name} {missiles} {key:ball} {key:fire} ... are filled in.
enum PopupStyle { POPUP_BIG = 0, POPUP_BANNER = 1, POPUP_NONE = 2 };   // big: the game stops for the "Item get" screen; banner: a strip, the game goes on; none: just the sound
struct GetPopup {
    std::string name;                        // the pickup it is for
    int style = POPUP_BIG;
    bool bigFirstOnly = true;                // counted ones (tanks, expansions): big the first time, a banner after
    std::string title, text;
    SoundUse fanfare;                        // plays with it (empty: the shared one)
    std::string model;                       // turning in the pop-up (empty: the pickup's own model)
};
enum PopupHint { HINT_NONE = 0, HINT_BEAM, HINT_MISSILE, HINT_MORPH, HINT_BOMBS, HINT_KEY };   // what a name is, for its default text

struct ItemSet {
    std::vector<ItemDef> items;
    std::string powerBombCounter = "Power Bombs"; int powerBombStart = 0, powerBombMax = 5;
    std::vector<GetPopup> popups;            // the ones set up (the rest: their default)
    SoundUse fanfare;                        // the shared fanfare (empty: the "item get" sound)
    float musicDuck = 0.15f;                 // the music while a big pop-up shows (0 silent .. 1 as loud as before)
    const ItemDef* byName(const std::string& n) const { for (const ItemDef& d : items) if (d.name == n) return &d; return nullptr; }
};
GetPopup defaultPopup(const ItemSet& s, const std::string& name, int hint);   // a ready-made text for it
GetPopup popupFor(const ItemSet& s, const std::string& name, int hint);       // its own, or the default
void setPopup(ItemSet& s, const GetPopup& p);                                 // stores it (replaces the one with its name)
bool popupCounted(const ItemSet& s, const std::string& name);                 // a tank / expansion (you find several)
int popupStyleNow(const GetPopup& p, bool counted, int howMany);              // the style for this pickup (the second expansion: a banner)
ItemSet defaultItems();                      // the Metroid set (what works is in the game; what is coming is not, yet)

// what the items you have do. have(name): how many of it you have (picked up); the ones you start with count already
struct ItemEffects {
    float maxHealth = 100, damageTaken = 1, lavaTaken = 1; bool freeInLiquid = false;
    float jumpHeight = 1.25f; int airJumps = 0;
    bool speedBooster = false; float boostAfter = 1.5f, boostSpeed = 2;
    bool springBall = false; float springJump = 1.2f;
    bool canCharge = true;                   // the Charge Beam (when it is in the game): no charging until you have it
    bool powerBombs = false; float pbReach = 6, pbFuse = 2, pbSphere = 12;
    int missileBonus = 0, powerBombBonus = 0;
    int suit = -1;                           // the best suit you have (index into items; -1: the plain one)
};
ItemEffects itemEffects(const ItemSet& s, const PlayerRules& base, const std::function<int(const std::string&)>& have);
int itemCount(const ItemDef& d, const std::function<int(const std::string&)>& have);   // how many of it you have (with the one you start with)

bool saveItems(const std::string& path, const ItemSet& s);
bool loadItems(const std::string& path, ItemSet& s);   // a missing item (from a newer list) is added as it is in defaultItems

} // namespace mc
