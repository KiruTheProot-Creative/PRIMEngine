#pragma once
// Items (Player editor > Items): everything Samus can find, from across the Metroid games. Each one is in the game or
// not, you have it from the start or find it later (a pickup with its name), and has its own numbers. Beams, missile
// kinds, the Morph Ball and Bombs live elsewhere (Weapons tab, Body and movement) and are only listed with these.
// Some abilities are not built yet ("coming"): they can already be set up and placed as pickups. Plain C++, tested.
#include "game.h"
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
struct ItemSet {
    std::vector<ItemDef> items;
    std::string powerBombCounter = "Power Bombs"; int powerBombStart = 0, powerBombMax = 5;
};
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
