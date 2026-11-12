// Library tests: default files, PNG round trip, blocks.txt round trip, import, hot reload, deleting, plane meshes.
#include "../src/world/library.h"
#include "../src/world/mesher.h"
#include "../src/core/png.h"
#include <cstdio>
#include <filesystem>
#include <thread>
using namespace mc;
namespace fs = std::filesystem;
int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)

static std::vector<uint8_t> solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    std::vector<uint8_t> p((size_t)w * h * 4); for (size_t i = 0; i < p.size(); i += 4) { p[i] = r; p[i + 1] = g; p[i + 2] = b; p[i + 3] = a; } return p;
}

int main() {
    std::string base = (fs::temp_directory_path() / "prim_library_test").u8string();
    fs::remove_all(base); fs::create_directories(base);

    // PNG writer -> stb_image reader
    { std::vector<uint8_t> px(37 * 21 * 4); for (size_t i = 0; i < px.size(); i++) px[i] = (uint8_t)(i * 7);
      CHECK(writePng(base + "/a.png", 37, 21, px), "png written");
      int w, h; std::vector<uint8_t> back; CHECK(readImage(base + "/a.png", w, h, back) && w == 37 && h == 21 && back == px, "png reads back identical (%dx%d)", w, h); }

    // first run writes the default blocks; loading them gives the same blocks as the built-in ones
    Library lib; lib.dir = base + "/game";
    BlockRegistry r; std::string rep; lib.load(r, &rep);
    BlockRegistry b; registerBuiltinBlocks(b);
    CHECK(fs::exists(lib.blocksFile()) && fs::exists(lib.texturesDir() + "/grass_top.png"), "default files written (%s)", rep.c_str());
    bool same = r.count() == b.count();
    for (size_t i = 1; i < b.count() && same; i++) {
        const BlockDef &x = r.get((BlockId)i), &y = b.get((BlockId)i);
        same = x.name == y.name && x.render == y.render && x.solid == y.solid && x.cullSame == y.cullSame;
        for (int f = 0; f < 6; f++) same = same && x.faces[f] == y.faces[f] && r.image(x.tex[f]).rgba == b.image(y.tex[f]).rgba;
    }
    CHECK(same, "loaded blocks match the built-in ones, ids included (grass = %d)", r.find("grass"));
    CHECK(r.layerSize() == 16, "layer size 16");

    // import: a 64x64 opaque image and a 32x32 see-through one; becomes blocks, layer size grows
    writePng(base + "/Red Rock.png", 64, 64, solid(64, 64, 200, 30, 30));
    auto flower = solid(32, 32, 0, 0, 0, 0); for (int y = 8; y < 32; y++) { uint8_t* p = &flower[(y * 32 + 16) * 4]; p[1] = 200; p[3] = 255; }
    writePng(base + "/flower.png", 32, 32, flower);
    writePng(base + "/broken.png", 4, 4, solid(4, 4, 1, 1, 1)); { FILE* f = fopen((base + "/broken.png").c_str(), "r+b"); fputs("junk", f); fclose(f); }
    auto res = lib.import({ base + "/Red Rock.png", base + "/broken.png" }, ImportAs::Blocks, r);
    CHECK(res.textures.size() == 1 && res.textures[0] == "Red_Rock" && res.failed.size() == 1 && res.blocks.size() == 1, "import: 1 ok, 1 broken file reported");
    BlockId rock = res.blocks[0];
    CHECK(r.get(rock).name == "Red Rock" && r.get(rock).render == Render::Opaque && r.get(rock).shape == Shape::Cube, "rock block: '%s', solid cube", r.get(rock).name.c_str());
    auto res2 = lib.import({ base + "/flower.png" }, ImportAs::CrossPlanes, r);
    BlockId fl = res2.blocks.at(0);
    CHECK(r.get(fl).shape == Shape::Cross && r.get(fl).render == Render::Cutout && !r.get(fl).solid, "flower: see-through cross plane you can walk through");
    CHECK(r.layerSize() == 64, "layer size grows to 64 (is %d)", r.layerSize());
    auto L = r.layer(r.textureIndex("Red_Rock"), 16); CHECK(L[0] == 200 && L[3] == 255, "64px texture shrinks to 16 keeping colour");
    auto res3 = lib.import({ base + "/Red Rock.png" }, ImportAs::Blocks, r);
    CHECK(res3.textures.size() == 1 && res3.blocks.empty(), "importing the same texture again updates it, no second block");

    // everything survives a reload from disk
    BlockRegistry r2; Library lib2; lib2.dir = lib.dir; lib2.load(r2);
    CHECK(r2.get(rock).name == "Red Rock" && r2.get(fl).shape == Shape::Cross && r2.get(fl).tex[0] == r2.textureIndex("flower"), "blocks.txt round trip");

    // hot reload: repaint a texture on disk
    CHECK(!lib.texturesChanged(), "nothing changed yet");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    writePng(lib.texturesDir() + "/stone.png", 16, 16, solid(16, 16, 0, 0, 255));
    fs::last_write_time(fs::path(lib.texturesDir() + "/stone.png"), fs::file_time_type::clock::now() + std::chrono::seconds(2));
    CHECK(lib.texturesChanged(), "repainted texture noticed");
    int n = lib.reloadTextures(r);
    CHECK(n == 1 && r.image(r.get(r.find("stone")).tex[0]).rgba[2] == 255, "stone is blue now (%d changed)", n);

    // deleting: texture -> faces show the missing texture; block -> id kept free, placed ones show 'missing'
    lib.deleteTexture("Red_Rock", r);
    CHECK(r.get(rock).tex[0] == 0 && !fs::exists(lib.texturesDir() + "/Red_Rock.png"), "deleted texture: block shows the missing texture");
    r.remove(rock);
    CHECK(!r.has(rock) && r.get(rock).name == "missing block" && r.get(rock).solid, "deleted block reads as 'missing block'");
    BlockDef nd; nd.name = "new"; BlockId again = r.add(nd);
    CHECK(again != rock && again > fl, "a deleted block's id is never given to a new block (%d)", again);
    lib.saveBlocks(r); { BlockRegistry r3; Library l3; l3.dir = lib.dir; l3.load(r3); BlockDef z; z.name = "z";
      CHECK(r3.add(z) == again + 1 && !r3.has(rock), "after reloading, new ids still continue after the deleted ones"); }
    CHECK(r.uniqueName("new") == "new 2" && r.uniqueName("new", again) == "new", "unique names");

    // states: a plane keeps its facing in the upper bits
    BlockId v = withState(fl, F_NZ); CHECK(typeOf(v) == fl && stateOf(v) == F_NZ && r.get(v).shape == Shape::Cross, "block state bits");

    // meshes: cross = 2 two-sided quads in the cutout pass; plane = 1 two-sided quad; neither hides the stone face next to it
    {
        BlockRegistry m; registerBuiltinBlocks(m);
        m.setTexture("p", 16, 16, flower);
        BlockDef c; c.name = "cross"; c.shape = Shape::Cross; c.render = Render::Cutout; c.solid = false; for (auto& f : c.faces) f = "p"; BlockId cid = m.add(c);
        BlockDef p = c; p.name = "plane"; p.shape = Shape::Plane; BlockId pid = m.add(p);
        World w(m); w.gen.kind = Generator::Void; w.loadChunk({ 0, 0, 0 });
        for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++) w.loadChunk({ x, y, z });
        w.set(4, 4, 4, cid); w.set(8, 4, 4, withState(pid, F_PZ)); w.set(8, 4, 3, m.find("stone"));
        MeshInput in; gatherInput(w, { 0, 0, 0 }, in); ChunkMesh cm; buildMesh(m, in, cm);
        CHECK(cm.cutout.i.size() == 3 * 12 && cm.cutout.v.size() == 12, "cross + plane: 3 two-sided quads (%zu indices)", cm.cutout.i.size());
        CHECK(cm.opaque.i.size() == 6 * 6, "stone next to the plane keeps all 6 faces");
        float minZ = 99; for (size_t k = 8; k < 12; k++) minZ = std::min(minZ, cm.cutout.v[k].z);
        CHECK(minZ > 4.0f && minZ < 4.05f, "plane hugs the wall it was placed on (z = %.4f)", minZ);
        CHECK(w.boxFree({ 4.5, 4, 4.5 }, 0.3, 1.8), "you can walk through the cross plane");
    }

    fs::remove_all(base);
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
