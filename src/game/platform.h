#pragma once
// Moving platforms and elevators (G2): any object can follow a path of points.
// Plain C++ (tested on Linux): the path, how platforms move along it, copies, a spawner, carrying the player.
#include "game.h"
#include <vector>

namespace mc {

enum class PathMode { PingPong, Loop, Once };          // back and forth / round and round / once to the end
enum class PathTrigger { Always, StandOn, Shot, Event };   // what starts it (Event: an event card says "move the platform")
const char* pathModeName(PathMode m);
const char* pathTriggerName(PathTrigger t);

struct PlatformSettings {
    std::vector<V3> points;          // the path, as offsets from the object's own place; the first point is always (0, 0, 0)
    PathMode mode = PathMode::PingPong;
    PathTrigger trigger = PathTrigger::Always;
    float speed = 3;                 // blocks per second
    float wait = 1;                  // seconds it waits at a stop
    bool stopEvery = false;          // stop at every point (else only at the ends)
    float comeBack = 0;              // elevators: go back to the start after this many seconds with nobody on it (0: stays)
    int copies = 1;                  // more of it, spread along the path (always-moving paths)
    bool spawner = false;            // a new one starts at the beginning every `every` seconds and goes away at the end
    float every = 3;
};

// a path as a line through its points
struct PathLine {
    std::vector<V3> pts; std::vector<double> at;   // at[i]: distance along the path to point i
    bool loop = false; double length = 0;
    PathLine(const std::vector<V3>& points, bool loop);
    V3 pos(double s) const;                          // offset at distance s (wraps for loops)
    int pointAt(double s) const;                     // the point at s (within a hair), or -1
};

// one platform on its path
struct Mover {
    double s = 0; int dir = 1;                       // where along the path, which way
    float pause = 0;                                 // waiting at a stop
    bool moving = false; double target = 0;          // on demand: going to this stop
    float idle = 0;                                  // how long nobody has stood on it
    bool needOff = false;                            // arrived with you on it: step off and on again to send it back
    bool alive = true;
};

class PlatformRun {
public:
    std::vector<Mover> movers;
    void reset(const PlatformSettings& p);           // copies spread out, or one at the start
    // stoodOn / shot: per mover; returns how far each mover's offset moved this step
    void tick(const PlatformSettings& p, double dt, const std::vector<bool>& stoodOn, const std::vector<bool>& shot);
    std::vector<V3> offsets(const PlatformSettings& p) const;
private:
    float spawnT = 0;
};

// is a player standing on top of this box? (feet within a hair of its top, and over it)
bool standsOn(const Box3& b, V3 feet, double halfW);

} // namespace mc
