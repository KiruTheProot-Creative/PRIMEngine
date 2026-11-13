#pragma once
// Breakable blocks (marked in the editor) and bombs (morph ball). Plain C++, tested on Linux.
#include "game.h"
#include <array>
#include <map>
#include <string>
#include <vector>

namespace mc {

// what breaks a marked block: "" any shot (or bomb or missile), "Bomb", "Missile", "Charged shot", or a beam's name
struct Breakables {
    std::map<std::array<int, 3>, std::string> marks;
    bool has(int x, int y, int z) const { return marks.count({ x, y, z }) > 0; }
    const std::string* breaker(int x, int y, int z) const { auto it = marks.find({ x, y, z }); return it == marks.end() ? nullptr : &it->second; }
    void mark(int x, int y, int z, const std::string& by) { marks[{ x, y, z }] = by; }
    void unmark(int x, int y, int z) { marks.erase({ x, y, z }); }
};
bool saveBreakables(const std::string& path, const Breakables& b);
bool loadBreakables(const std::string& path, Breakables& b);
// does this break it? weapon: a beam's name or "Bomb"; missile / charged: what kind of shot
bool breaksWith(const std::string& breaker, const std::string& weapon, bool missile, bool charged);

// a bomb going off: how fast it throws the ball up (0: too far away); radius: its reach
double bombLift(V3 bomb, V3 ballCentre, double radius, double liftSpeed);
// the blocks a bomb reaches (their middles within radius)
std::vector<std::array<int, 3>> blocksInReach(V3 at, double radius);

} // namespace mc
