// shipcuts: the gunship's ready-made cutscenes, fitted to a landing site (its pad, its lift, its thrusters):
// stepping onto the lift, the take-off, and the landing (out over the planet, re-entry, down over the pad, the pilot out)
#pragma once
#include "cutscene.h"
#include "objects.h"
#include <functional>

namespace mc {

V3 shipSpot(const WorldObject& o, V3 local);           // a spot on the ship (its own x right, y up, z front) in the world
V3 shipExitSpot(const WorldObject& o);                 // where the pilot stands after landing: out from under the wing
double shipExitYaw(const WorldObject& o);              // ... looking away from the ship (radians, like the player's)
// solid (may be empty): is there a block at this spot - the cameras keep out of the ground and walls (lifted until they are clear)
using SolidAt = std::function<bool(V3)>;
Cutscene shipBoardCut(const WorldObject& o, V3 from, const SolidAt& solid = {});  // from where the player stands onto the lift (it holds there: the menu)
Cutscene shipTakeoffCut(const WorldObject& o, bool shortOne, const SolidAt& solid = {});
Cutscene shipLandingCut(const WorldObject& o, bool shortOne, const SolidAt& solid = {});

} // namespace mc
