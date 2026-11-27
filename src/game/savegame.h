#pragma once
#include <array>
// Save files (like Metroid Prime): saved only at Save Stations, into the file you picked on the Files screen. A file
// remembers where you saved, what you carry, what you took / defeated / scanned / mapped in every world, the game's
// flags, how long you have played and how much you have found. Plain C++, tested.
#include "../core/math.h"
#include "visors.h"
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mc {

// what happened in one world, so it is still that way when you come back (and in the save file): blocks broken for good,
// event cards that ran (an "only once" one never again), objects events hid / showed / switched off / locked, doors
// events opened, platforms they set moving
struct WorldMemory {
    struct Fired { int card = 0; std::string name; int count = 0; bool counted = false; };   // (the card's name: still the same card?)
    std::vector<std::array<int, 4>> broken;            // x, y, z, the block that was there
    std::vector<Fired> fired;
    std::set<uint32_t> hidden, off, locked, held, kicked;   // object ids
    std::set<uint32_t> triggered;                       // trigger zones that work only once, used up
    bool empty() const { return broken.empty() && fired.empty() && hidden.empty() && off.empty() && locked.empty() && held.empty() && kicked.empty() && triggered.empty(); }
};

struct SaveGame {
    // what the Files screen shows
    std::string world, place;                          // the world, and the Save Station's name
    double playTime = 0;                               // seconds
    float completion = 0;                              // 0..1: the items found of all there are
    float health = 0, maxHealth = 0;
    int64_t savedAt = 0;                               // when (seconds since 1970)
    // where you go on
    V3 pos{ 0, 0, 0 }; double yaw = 0;
    // what you have
    std::set<std::string> keys; std::map<std::string, int> counts;
    std::map<std::string, std::set<uint32_t>> taken;    // per world: pickups taken for good
    int beam = 0, missile = 0, visor = 0;
    std::vector<ScanEntry> scans;
    std::set<std::string> enemyGone, zonesCleared, stationsUsed, flags;
    std::set<std::string> doorsOpened;                  // "world:id": weapon doors opened once with the right weapon (any shot opens them now)
    std::vector<std::string> maps;                     // worlds with a seen map (map_<i>.bin next to the file, i: the place here)
    std::map<std::string, WorldMemory> memory;         // per world: what happened there
    std::set<std::string> sitesOpen, sitesClosed, sitesVisited;   // landing sites unlocked / locked by event cards (else as each site says), and landed at once (then the short landing)
};
bool saveGame(const std::string& dir, const SaveGame& g);   // dir/save.txt (the folder is made)
bool loadGame(const std::string& dir, SaveGame& g);
bool eraseGame(const std::string& dir);
std::string playTimeText(double seconds);            // "0:07", "12:34" (hours : minutes)

} // namespace mc
