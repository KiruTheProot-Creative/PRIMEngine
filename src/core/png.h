// png.h : read and write PNG images (RGBA, 8 bits per channel).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mc {

// Reads a PNG (or other image stb_image understands) from a UTF-8 path. Returns false if it cannot.
bool readImage(const std::string& path, int& w, int& h, std::vector<uint8_t>& rgba);
bool readImageMemory(const uint8_t* data, size_t size, int& w, int& h, std::vector<uint8_t>& rgba);   // PNG / JPEG ... bytes
std::vector<uint8_t> pngBytes(int w, int h, const std::vector<uint8_t>& rgba);                       // a PNG file, in memory
// Writes an uncompressed-deflate PNG. Small textures, so size does not matter; any program can open it.
bool writePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba);

} // namespace mc
