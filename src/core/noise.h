// noise.h : a tiling 3D noise volume for clouds (made once at startup).
#pragma once
#include <cstdint>
#include <vector>

namespace mc {

// size^3 texels, 2 bytes each: [0] soft billowy noise (several layers of value noise), [1] puffy cells (inverted Worley).
// Both wrap around seamlessly in every direction.
std::vector<uint8_t> makeCloudNoise(int size);

} // namespace mc
