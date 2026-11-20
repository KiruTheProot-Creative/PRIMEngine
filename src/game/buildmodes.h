#pragma once
// Build modes (like the Effortless Building mod): lines, walls, floors, cubes, circles, cylinders, spheres, pyramids, cones,
// domes, diagonal lines and walls, slopes. The editor picks the points with the mouse (start, corner, height);
// this works out which blocks the shape covers. Plain C++ (tested on Linux).
#include "../core/math.h"
#include <cstddef>
#include <vector>

namespace mc {

enum class BuildMode { Normal, Line, Wall, Floor, Cube, DiagLine, DiagWall, Slope, Circle, Cylinder, Sphere, Pyramid, Cone, Dome, COUNT };
enum BuildFill { FILL_SOLID = 0, FILL_HOLLOW = 1, FILL_WALLS = 2, FILL_EDGES = 3 };   // walls: no top and bottom; edges: a frame (cubes)

const char* buildModeName(BuildMode m);
int buildSteps(BuildMode m);                       // clicks it takes: 1 (one block), 2 (start, end), 3 (start, corner, height)
std::vector<int> buildFills(BuildMode m);          // the fill choices it has (empty: none)
const char* buildFillName(BuildMode m, int fill);
bool buildHasCentre(BuildMode m);                  // can start from its middle instead of a corner
I3 buildSnap(BuildMode m, I3 a, I3 b);             // the second point as the mode uses it (a wall: straight along x or z)

// the blocks a shape covers; a: start, b: the second point (as picked), h: the height (y) for 3-step modes.
// At most `limit` blocks (capped: set when there would be more). Shapes are at most 256 blocks across each way.
std::vector<I3> buildShape(BuildMode m, int fill, bool fromCentre, I3 a, I3 b, int h, size_t limit = 50000, bool* capped = nullptr);
I3 buildBoxSize(const std::vector<I3>& cells);     // how big it is (x, y, z), for "12 x 4 x 3"

}
