// mesher.h : turns a chunk into triangles (only faces you can see, with Minecraft-style corner shading),
// on background threads so walking around never stutters.
#pragma once
#include "world.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <array>

namespace mc {

// 32 bytes. Position relative to the chunk origin; layer = texture layer + 16384 x sway (0 none, 1 all, 2 top only);
// lit: corner darkening (bits 0-7, 255 = open), lamp light red / green / blue 0..15 (bits 8-11, 12-15, 16-19), glows itself (bit 20),
//      shininess 0..15 (bits 21-24), mirror (bit 25);
// normal: direction in 3 bytes (x, y, z; 0..255 = -1..1) + texture face (bits 24-26)
struct Vtx { float x, y, z, u, v, layer; uint32_t lit, normal; };
inline uint32_t packNormal(float nx, float ny, float nz, int face) {
    auto b = [](float v) { int q = (int)((v * 0.5f + 0.5f) * 255.f + 0.5f); return (uint32_t)(q < 0 ? 0 : q > 255 ? 255 : q); };
    return b(nx) | (b(ny) << 8) | (b(nz) << 16) | ((uint32_t)(face & 7) << 24);
}
inline uint32_t packLit(float ao, const int rgb[3], bool emissive, float shine = 0, bool mirror = false) {
    int a = (int)(ao * 255 + 0.5f); a = a < 0 ? 0 : a > 255 ? 255 : a;
    int s = (int)(shine * 15 + 0.5f); s = s < 0 ? 0 : s > 15 ? 15 : s;
    return (uint32_t)a | ((uint32_t)(rgb[0] & 15) << 8) | ((uint32_t)(rgb[1] & 15) << 12) | ((uint32_t)(rgb[2] & 15) << 16) | ((emissive ? 1u : 0u) << 20)
         | ((uint32_t)s << 21) | ((mirror ? 1u : 0u) << 25);
}
inline float shineOf(const Vtx& v) { return ((v.lit >> 21) & 15) / 15.f; }
inline int faceOf(const Vtx& v) { return (int)((v.normal >> 24) & 7); }
inline float aoOf(const Vtx& v) { return (v.lit & 255) / 255.f; }
inline int lightOf(const Vtx& v, int ch) { return (int)(v.lit >> (8 + 4 * ch)) & 15; }
inline int lightOf(const Vtx& v) { return std::max(lightOf(v, 0), std::max(lightOf(v, 1), lightOf(v, 2))); }
inline bool glowsOf(const Vtx& v) { return (v.lit >> 20) & 1; }
struct MeshData { std::vector<Vtx> v; std::vector<uint32_t> i; bool empty() const { return i.empty(); } };
struct ChunkMesh { I3 pos; uint32_t seq = 0; MeshData opaque, cutout, water; };   // water: see-through, drawn last

constexpr int PAD = CS + 2;                  // the chunk plus a one-block border from its neighbours
constexpr BlockId UNKNOWN = 0xFFFFFFFFu;         // neighbour not loaded: treated as solid so no wall of faces shows at the edge

// light sources around a chunk: 48^3 cells (the chunk +- 16), 0 = lets light through, 255 = blocks it,
// 1..254 = glows with lightColors[value] (red, green, blue levels 0..15)
constexpr int LPAD = 48;
inline int lpadIdx(int x, int y, int z) { return ((y + 16) * LPAD + (z + 16)) * LPAD + (x + 16); }
struct MeshInput {
    I3 pos; uint32_t seq = 0; std::vector<BlockId> blocks;            // PAD^3 ids
    std::vector<uint8_t> lightSrc; std::vector<std::array<uint8_t, 3>> lightColors;   // empty = no lamp nearby
};
static inline int padIdx(int x, int y, int z) { return ((y + 1) * PAD + (z + 1)) * PAD + (x + 1); }   // x,y,z in -1..16

bool gatherInput(const World& w, I3 pos, MeshInput& out);   // false if the chunk is not loaded
void buildMesh(const BlockRegistry& reg, const MeshInput& in, ChunkMesh& out);

class MeshWorkers {
public:
    explicit MeshWorkers(const BlockRegistry& r) : reg(r) {}
    ~MeshWorkers() { stop(); }
    void start(int threads);
    void stop();
    void submit(MeshInput&& in);
    void collect(std::vector<ChunkMesh>& out);   // finished meshes (moved out)
    size_t pending() const { std::lock_guard<std::mutex> l(m); return jobs.size() + busy; }
private:
    void run();
    const BlockRegistry& reg;
    mutable std::mutex m; std::condition_variable cv;
    std::deque<MeshInput> jobs; std::vector<ChunkMesh> done; size_t busy = 0;
    std::vector<std::thread> threads; bool quit = false;
};

} // namespace mc
