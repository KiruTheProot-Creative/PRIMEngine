// demo.h : a small scene to look at (used for the automatic screenshots after every build).
#pragma once
#include "../world/world.h"
#include <string>
#include <vector>

namespace mc {

void buildDemo(World& w, const BlockRegistry& r);   // on a "Classic flat" world, around the origin

struct DemoShot { std::string name; V3 eye, target; float hour; std::string weather = "partly_cloudy"; bool lightning = false; };
std::vector<DemoShot> demoShots();

} // namespace mc
