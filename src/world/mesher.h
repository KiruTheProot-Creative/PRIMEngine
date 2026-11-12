// mesher.h : turns a chunk into triangles (only faces you can see, with Minecraft-style corner shading),
// on background threads so walking around never stutters.
#pragma once
#include "world.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace mc {

// 32 bytes. Position relative to the chunk origin; shade = ambient occlusion (1 = open);
// normal = direction packed in 3 bytes (x, y, z; 0..255 = -1..1) + the texture face in the top byte
struct Vtx { float x, y, z, u, v, layer, shade; uint32_t normal; };
inline uint32_t packNormal(float nx, float ny, float nz, int face) {
    auto b = [](float v) { int q = (int)((v * 0.5f + 0.5f) * 255.f + 0.5f); return (uint32_t)(q < 0 ? 0 : q > 255 ? 255 : q); };
    return b(nx) | (b(ny) << 8) | (b(nz) << 16) | ((uint32_t)face << 24);
}
inline int faceOf(const Vtx& v) { return (int)(v.normal >> 24); }
struct MeshData { std::vector<Vtx> v; std::vector<uint32_t> i; bool empty() const { return i.empty(); } };
struct ChunkMesh { I3 pos; uint32_t seq = 0; MeshData opaque, cutout; };

constexpr int PAD = CS + 2;                  // the chunk plus a one-block border from its neighbours
constexpr BlockId UNKNOWN = 0xFFFFFFFFu;         // neighbour not loaded: treated as solid so no wall of faces shows at the edge

struct MeshInput { I3 pos; uint32_t seq = 0; std::vector<BlockId> blocks; };   // PAD^3 ids
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
