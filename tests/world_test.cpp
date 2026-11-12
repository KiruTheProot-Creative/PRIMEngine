// World core tests: chunks at negative coordinates, far away blocks, streaming, saving, ray casts, collision.
#include "../src/world/world.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
using namespace mc;
int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)

static void flat(World& w, const BlockRegistry& r) { w.gen.kind = Generator::Flat; w.gen.layers = { { r.find("stone"), 3 }, { r.find("dirt"), 2 }, { r.find("grass"), 1 } }; }

int main() {
    BlockRegistry reg; registerBuiltinBlocks(reg);
    CHECK(reg.count() == 13 && reg.find("glass") != AIR && reg.textureCount() >= 12, "%zu blocks, %zu textures registered", reg.count(), reg.textureCount());
    CHECK(floorDiv(-1, 16) == -1 && floorMod(-1, 16) == 15 && floorDiv(-16, 16) == -1 && floorDiv(-17, 16) == -2 && floorDiv(15, 16) == 0, "floor division for negative coordinates");

    // ---- flat world, blocks across chunk borders and far away
    {
        World w(reg); flat(w, reg);
        CHECK(w.get(0, -1, 0) == AIR, "nothing is loaded yet");
        w.loadChunk({ 0, -1, 0 });
        CHECK(w.get(3, -1, 7) == reg.find("grass") && w.get(3, -2, 7) == reg.find("dirt") && w.get(3, -6, 7) == reg.find("stone") && w.get(3, -7, 7) == AIR && w.get(3, 0, 7) == AIR,
              "flat layers: grass at y=-1, dirt, stone, void below");
        w.dirty.clear();
        w.set(-1, 5, -1, reg.find("planks"));
        CHECK(w.get(-1, 5, -1) == reg.find("planks") && w.chunk({ -1, 0, -1 }) && w.chunk({ -1, 0, -1 })->modified, "placing at (-1, 5, -1) lands in chunk (-1, 0, -1)");
        CHECK(w.dirty.count({ -1, 0, -1 }) == 1, "the chunk is marked for a new mesh");
        w.loadChunk({ 0, 0, 0 }); w.dirty.clear(); w.set(15, 3, 3, reg.find("stone"));
        CHECK(w.dirty.count({ 0, 0, 0 }) && !w.dirty.count({ 1, 0, 0 }), "a block on a border marks only loaded neighbours");
        int far = 2000000000; w.set(far, 100, -far, reg.find("bricks"));
        CHECK(w.get(far, 100, -far) == reg.find("bricks"), "a block two billion blocks away works");
    }
    // ---- streaming: loads nearest first, unloads far chunks
    {
        World w(reg); flat(w, reg);
        w.stream({ 0, 0, 0 }, 4, 2, 1000);
        size_t n = w.loadedCount();
        CHECK(n > 200 && n < 400 && w.chunk({ 0, 0, 0 }) && w.chunk({ 4, 0, 0 }) && !w.chunk({ 6, 0, 0 }), "radius 4: %zu chunks loaded around the player", n);
        World w2(reg); flat(w2, reg); w2.stream({ 0, 0, 0 }, 4, 2, 5);
        CHECK(w2.loadedCount() == 5 && w2.chunk({ 0, 0, 0 }), "load budget per frame is respected, nearest first");
        w.stream({ 100, 0, 0 }, 4, 2, 1000);
        CHECK(!w.chunk({ 0, 0, 0 }) && w.chunk({ 100, 0, 0 }) && w.loadedCount() == n, "moving far away unloads the old chunks (%zu loaded)", w.loadedCount());
    }
    // ---- saving: changed chunks survive unloading and restarting; untouched ones are not written
    {
        std::string dir = "/tmp/mc_world_test"; std::filesystem::remove_all(dir);
        {
            World w(reg); flat(w, reg); w.store = std::make_unique<RegionStore>(dir);
            w.stream({ 0, 0, 0 }, 3, 1, 1000);
            w.set(2, 0, 2, reg.find("lamp")); w.set(-40, -1, 33, AIR); w.set(-5000, 70, 9000, reg.find("glass"));
            w.stream({ 50, 0, 50 }, 3, 1, 1000);   // walk away: the changed chunks unload and get saved
            CHECK(!w.chunk({ 0, 0, 0 }), "changed chunk unloaded");
            w.saveAll();
        }
        size_t files = 0; for (auto& e : std::filesystem::directory_iterator(dir)) { (void)e; files++; }
        CHECK(files == 3, "only regions with changes were written (%zu files)", files);
        World w(reg); flat(w, reg); w.store = std::make_unique<RegionStore>(dir);
        w.loadChunk({ 0, 0, 0 }); w.loadChunk({ -3, -1, 2 }); w.loadChunk(chunkOf(-5000, 70, 9000));
        CHECK(w.get(2, 0, 2) == reg.find("lamp") && w.get(-40, -1, 33) == AIR && w.get(-41, -1, 33) == reg.find("grass") && w.get(-5000, 70, 9000) == reg.find("glass"),
              "after a restart: placed and broken blocks are still there");
    }
    // ---- ray casts and collision
    {
        World w(reg); flat(w, reg); w.stream({ 0, 0, 0 }, 3, 1, 1000);
        Hit h; bool ok = w.raycast({ 0.5, 5.2, 0.5 }, { 0, -1, 0 }, 20, h);
        CHECK(ok && h.y == -1 && h.ny == 1 && std::fabs(h.t - 5.2) < 1e-9, "looking down hits the grass top (t=%.3f)", h.t);
        w.set(3, 0, 0, reg.find("stone"));
        ok = w.raycast({ -0.5, 0.5, 0.5 }, { 1, 0, 0 }, 20, h);
        CHECK(ok && h.x == 3 && h.nx == -1, "looking sideways hits the block's -X face");
        ok = w.raycast({ 0.5, 0.5, -5.5 }, { 0, 0.02, 1 }, 40, h);
        CHECK(!ok, "nothing in the way: no hit");
        V3 feet{ 0.5, 3, 0.5 }, vel{ 0, 0, 0 }; bool g = false;
        for (int i = 0; i < 120; i++) { vel.y -= 32.0 / 60; feet = w.moveBox(feet, vel, 0.3, 1.8, 1 / 60.0, g); }
        CHECK(g && std::fabs(feet.y) < 1e-3, "falls and lands on the ground (y=%.4f)", feet.y);
        for (int i = 0; i < 60; i++) { vel.x = 4.3; vel.y -= 32.0 / 60; feet = w.moveBox(feet, vel, 0.3, 1.8, 1 / 60.0, g); }
        CHECK(std::fabs(feet.x - 2.7) < 1e-3, "walking into a block stops at its face (x=%.4f)", feet.x);
        vel = { 0, 9, 0 }; double peak = 0;
        for (int i = 0; i < 60; i++) { vel.x = 4.3; vel.y -= 32.0 / 60; feet = w.moveBox(feet, vel, 0.3, 1.8, 1 / 60.0, g); peak = std::max(peak, feet.y); }
        CHECK(peak > 1.0 && feet.x > 3.5, "jumping clears a one-block step (peak %.2f, x=%.2f)", peak, feet.x);
        World e(reg); e.gen.kind = Generator::Void; e.loadChunk({ 0, 0, 0 });
        V3 f2{ 0.5, 1, 0.5 }, v2{ 0, -20, 0 }; bool g2;
        f2 = e.moveBox(f2, v2, 0.3, 1.8, 0.1, g2);
        CHECK(f2.y >= 0 && g2, "unloaded chunks are solid: you never fall into the unknown (y=%.3f)", f2.y);
    }
    // ---- speed
    {
        World w(reg); flat(w, reg);
        auto t0 = std::chrono::steady_clock::now(); w.stream({ 0, 0, 0 }, 16, 4, 100000);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        printf("      streaming radius 16: %zu chunks in %.0f ms\n", w.loadedCount(), ms);
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
