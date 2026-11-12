#include "png.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../../third_party/stb/stb_image.h"

namespace mc {
namespace fs = std::filesystem;

static fs::path P(const std::string& u8) { return fs::path(u8); }

bool readImage(const std::string& path, int& w, int& h, std::vector<uint8_t>& rgba) {
    std::ifstream f(P(path), std::ios::binary); if (!f) return false;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return !bytes.empty() && readImageMemory(bytes.data(), bytes.size(), w, h, rgba);
}
bool readImageMemory(const uint8_t* data, size_t size, int& w, int& h, std::vector<uint8_t>& rgba) {
    if (!data || !size) return false;
    int n = 0; stbi_uc* px = stbi_load_from_memory(data, (int)size, &w, &h, &n, 4);
    if (!px) return false;
    rgba.assign(px, px + (size_t)w * h * 4); stbi_image_free(px);
    return w > 0 && h > 0;
}

static uint32_t crcTable[256];
static uint32_t crc(const uint8_t* d, size_t n, uint32_t c = 0xFFFFFFFFu) {
    if (!crcTable[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t k = i; for (int j = 0; j < 8; j++) k = (k & 1) ? 0xEDB88320u ^ (k >> 1) : k >> 1; crcTable[i] = k; }
    for (size_t i = 0; i < n; i++) c = crcTable[(c ^ d[i]) & 255] ^ (c >> 8);
    return c;
}
static void be32(std::vector<uint8_t>& o, uint32_t v) { o.push_back(v >> 24); o.push_back(v >> 16); o.push_back(v >> 8); o.push_back(v); }
static void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    be32(out, (uint32_t)data.size());
    size_t start = out.size(); out.insert(out.end(), type, type + 4); out.insert(out.end(), data.begin(), data.end());
    be32(out, crc(&out[start], out.size() - start) ^ 0xFFFFFFFFu);
}

bool writePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> out = pngBytes(w, h, rgba); if (out.empty()) return false;
    std::ofstream f(P(path), std::ios::binary); if (!f) return false;
    f.write((const char*)out.data(), (std::streamsize)out.size()); return (bool)f;
}
std::vector<uint8_t> pngBytes(int w, int h, const std::vector<uint8_t>& rgba) {
    if (w <= 0 || h <= 0 || rgba.size() < (size_t)w * h * 4) return {};
    std::vector<uint8_t> raw; raw.reserve((size_t)(w * 4 + 1) * h);
    for (int y = 0; y < h; y++) { raw.push_back(0); raw.insert(raw.end(), rgba.begin() + (size_t)y * w * 4, rgba.begin() + (size_t)(y + 1) * w * 4); }
    std::vector<uint8_t> z = { 0x78, 0x01 };   // zlib header, then "stored" deflate blocks
    for (size_t pos = 0; pos < raw.size() || raw.empty();) {
        size_t n = std::min<size_t>(65535, raw.size() - pos); bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0); z.push_back(n & 255); z.push_back(n >> 8); z.push_back(~n & 255); z.push_back((~n >> 8) & 255);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n); pos += n;
        if (last) break;
    }
    uint32_t a = 1, b = 0; for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    be32(z, (b << 16) | a);
    std::vector<uint8_t> out = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    std::vector<uint8_t> ihdr; be32(ihdr, (uint32_t)w); be32(ihdr, (uint32_t)h); ihdr.insert(ihdr.end(), { 8, 6, 0, 0, 0 });
    chunk(out, "IHDR", ihdr); chunk(out, "IDAT", z); chunk(out, "IEND", {});
    return out;
}

} // namespace mc
