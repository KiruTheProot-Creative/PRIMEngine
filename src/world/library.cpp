#include "library.h"
#include "../core/png.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace mc {
namespace fs = std::filesystem;

static fs::path P(const std::string& u8) { return fs::path(u8); }
static std::string trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
static std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
static std::string hex6(uint32_t c) { char b[16]; snprintf(b, sizeof b, "#%06X", (unsigned)(c & 0xFFFFFF)); return b; }
static bool yes(const std::string& v) { std::string l = lower(v); return l == "yes" || l == "true" || l == "1"; }

std::string Library::textureNameFor(const std::string& file) {
    std::string stem = P(file).stem().string(), n;
    for (unsigned char c : stem) n += (std::isalnum(c) || c == '-' || c == '_') ? (char)c : '_';
    return n.empty() ? "texture" : n;
}

std::map<std::string, fs::file_time_type> Library::scan() const {
    std::map<std::string, fs::file_time_type> out; std::error_code ec;
    for (auto& e : fs::directory_iterator(P(texturesDir()), ec)) {
        if (!e.is_regular_file(ec) || lower(e.path().extension().string()) != ".png") continue;
        out[e.path().stem().string()] = e.last_write_time(ec);
    }
    return out;
}
bool Library::loadTexture(BlockRegistry& r, const std::string& name) {
    int w, h; std::vector<uint8_t> px;
    if (!readImage(texturesDir() + "/" + name + ".png", w, h, px)) return false;
    r.setTexture(name, w, h, std::move(px)); return true;
}

bool Library::saveBlocks(const BlockRegistry& r) const {
    std::error_code ec; fs::create_directories(P(dir), ec);
    std::string tmp = blocksFile() + ".tmp";
    {
        std::ofstream f(P(tmp)); if (!f) return false;
        f << "# PRIMEngine blocks. Edited by the engine (E > Blocks), but you may change it by hand too.\n"
             "# shape: cube, cross (two crossed planes), plane (one plane on the face you click)\n"
             "# look: solid or see-through.  faces: +x -x top bottom +z -z (names of files in textures/)\n"
             "# Never reuse or change an id: worlds store blocks by id.\n"
          << "next_id = " << r.nextId << "\n";
        for (size_t i = 1; i < r.count(); i++) {
            if (!r.has((BlockId)i)) continue;
            const BlockDef& d = r.get((BlockId)i);
            f << "\n[block " << i << "]\nname = " << d.name
              << "\nshape = " << (d.shape == Shape::Cube ? "cube" : d.shape == Shape::Cross ? "cross" : "plane")
              << "\nlook = " << (d.render == Render::Liquid ? "liquid" : d.render == Render::Cutout ? "see-through" : "solid")
              << "\nsolid = " << (d.solid ? "yes" : "no") << "\njoin_same = " << (d.cullSame ? "yes" : "no")
              << "\nlight = " << d.light << "\nlight_color = " << hex6(d.lightColor) << "\nwaves = " << (d.waves ? "yes" : "no")
              << "\nshine = " << d.shine << "\nmirror = " << (d.mirror ? "yes" : "no")
              << "\nliquid = " << (d.liquid == 1 ? "water" : d.liquid == 2 ? "lava" : "none") << "\nspring = " << (d.spring ? "yes" : "no") << "\nfaces =";
            for (auto& t : d.faces) f << ' ' << (t.empty() ? "-" : t);
            f << "\n";
        }
        if (!f) return false;
    }
    fs::rename(P(tmp), P(blocksFile()), ec);
    if (ec) { fs::remove(P(blocksFile()), ec); fs::rename(P(tmp), P(blocksFile()), ec); }
    return !ec;
}

bool Library::load(BlockRegistry& r, std::string* report) {
    std::error_code ec; fs::create_directories(P(texturesDir()), ec);
    if (!fs::exists(P(blocksFile()))) {   // first run: write the starter blocks as real files
        BlockRegistry b; registerBuiltinBlocks(b);
        for (size_t i = 1; i < b.textureCount(); i++) {
            const TexImage& t = b.image((int)i); std::string path = texturesDir() + "/" + t.name + ".png";
            if (!fs::exists(P(path))) writePng(path, t.w, t.h, t.rgba);
        }
        saveBlocks(b);
    }
    r = BlockRegistry{};
    stamps = scan(); int bad = 0;
    for (auto& s : stamps) if (!loadTexture(r, s.first)) bad++;
    std::ifstream f(P(blocksFile())); std::string line; BlockId cur = AIR; BlockDef d; int blocks = 0;
    bool seenLight = false, seenWaves = false, seenShine = false;
    BlockRegistry builtin; registerBuiltinBlocks(builtin);   // files from before "light"/"waves" existed: starter blocks keep their glow and sway
    auto flush = [&] {
        if (cur != AIR) {
            BlockId b = builtin.find(d.name);
            if (!seenLight && b) d.light = builtin.get(b).light;
            if (!seenWaves && b) d.waves = builtin.get(b).waves;
            if (!seenShine && b) d.shine = builtin.get(b).shine;
            r.put(cur, d); blocks++;
        }
        cur = AIR; d = BlockDef{}; seenLight = seenWaves = seenShine = false;
    };
    while (std::getline(f, line)) {
        line = trim(line); if (line.empty() || line[0] == '#') continue;
        if (line.rfind("[block", 0) == 0) { flush(); int id = std::atoi(line.c_str() + 6); if (id > 0 && id <= MAX_BLOCK_TYPES) cur = (BlockId)id; continue; }
        size_t eq = line.find('='); if (eq == std::string::npos) continue;
        std::string k = lower(trim(line.substr(0, eq))), v = trim(line.substr(eq + 1));
        if (k == "next_id") { r.nextId = std::max(r.nextId, std::atoi(v.c_str())); continue; }
        if (cur == AIR) continue;
        if (k == "name") d.name = v;
        else if (k == "shape") { std::string l = lower(v); d.shape = l == "cross" ? Shape::Cross : l == "plane" ? Shape::Plane : Shape::Cube; }
        else if (k == "look") { std::string l = lower(v); d.render = l == "solid" ? Render::Opaque : l == "liquid" ? Render::Liquid : Render::Cutout; }
        else if (k == "liquid") { std::string l = lower(v); d.liquid = l == "water" ? 1 : l == "lava" ? 2 : 0; }
        else if (k == "spring") d.spring = yes(v);
        else if (k == "solid") d.solid = yes(v);
        else if (k == "join_same") d.cullSame = yes(v);
        else if (k == "light") { d.light = std::max(0, std::min(15, std::atoi(v.c_str()))); seenLight = true; }
        else if (k == "waves") { d.waves = yes(v); seenWaves = true; }
        else if (k == "shine") { d.shine = std::max(0.f, std::min(1.f, (float)std::atof(v.c_str()))); seenShine = true; }
        else if (k == "mirror") d.mirror = yes(v);
        else if (k == "light_color") { std::string h = v; if (!h.empty() && h[0] == '#') h.erase(0, 1); d.lightColor = (uint32_t)std::strtoul(h.c_str(), nullptr, 16) & 0xFFFFFF; }
        else if (k == "faces") {
            std::istringstream ss(v); std::vector<std::string> n; std::string t; while (ss >> t) n.push_back(t == "-" ? "" : t);
            for (int i = 0; i < 6; i++) d.faces[i] = n.empty() ? "" : n[std::min(i, (int)n.size() - 1)];
        }
    }
    flush();
    // block files from before water and lava: add them (and their pictures)
    if (!r.liquidBlock(1) || !r.liquidBlock(2)) {
        BlockRegistry b; registerBuiltinBlocks(b);
        for (const char* t : { "water", "lava" }) {
            std::string path = texturesDir() + "/" + t + ".png"; const TexImage& im = b.image(b.textureIndex(t));
            if (!fs::exists(P(path))) { writePng(path, im.w, im.h, im.rgba); stamps[t] = fs::last_write_time(P(path), ec); loadTexture(r, t); }
        }
        addBuiltinLiquids(r); r.resolve(); saveBlocks(r);
    }
    r.resolve();
    if (report) *report = std::to_string(blocks) + " blocks, " + std::to_string(r.textureCount() - 1) + " textures" + (bad ? ", " + std::to_string(bad) + " images could not be read" : "");
    return true;
}

bool Library::texturesChanged() const { return scan() != stamps; }

int Library::reloadTextures(BlockRegistry& r) {
    auto now = scan(); int changed = 0;
    for (auto it = stamps.begin(); it != stamps.end();) {
        if (!now.count(it->first)) { r.removeTexture(it->first); it = stamps.erase(it); changed++; } else ++it;
    }
    for (auto& s : now) {
        auto it = stamps.find(s.first);
        if (it != stamps.end() && it->second == s.second) continue;
        if (loadTexture(r, s.first)) changed++;   // unreadable (half saved?): keeps the old picture until the file changes again
        stamps[s.first] = s.second;
    }
    r.resolve();
    return changed;
}

BlockDef Library::blockFromTexture(const BlockRegistry& r, const std::string& tex, ImportAs as) {
    BlockDef d; std::string nice = tex; std::replace(nice.begin(), nice.end(), '_', ' ');
    d.name = r.uniqueName(nice);
    for (auto& f : d.faces) f = tex;
    int ti = r.textureIndex(tex); bool see = ti > 0 && r.image(ti).transparent;
    d.shape = as == ImportAs::CrossPlanes ? Shape::Cross : as == ImportAs::FlatPlanes ? Shape::Plane : Shape::Cube;
    d.render = (see || d.shape != Shape::Cube) ? Render::Cutout : Render::Opaque;
    d.solid = d.shape == Shape::Cube;
    d.waves = d.shape == Shape::Cross;   // imported plants sway
    return d;
}

Library::ImportResult Library::import(const std::vector<std::string>& files, ImportAs as, BlockRegistry& r) {
    ImportResult res; std::error_code ec; fs::create_directories(P(texturesDir()), ec);
    for (auto& file : files) {
        int w, h; std::vector<uint8_t> px;
        if (!readImage(file, w, h, px)) { res.failed.push_back(P(file).filename().string()); continue; }
        std::string name = textureNameFor(file), dest = texturesDir() + "/" + name + ".png";
        if (!fs::equivalent(P(file), P(dest), ec)) {
            ec.clear(); fs::copy_file(P(file), P(dest), fs::copy_options::overwrite_existing, ec);
            if (ec) { res.failed.push_back(P(file).filename().string()); continue; }
        }
        r.setTexture(name, w, h, std::move(px)); stamps[name] = fs::last_write_time(P(dest), ec);
        res.textures.push_back(name);
    }
    r.resolve();
    if (as != ImportAs::TexturesOnly)
        for (auto& t : res.textures) {
            bool used = false;   // re-importing a texture just updates it, no second block
            for (size_t i = 1; i < r.count() && !used; i++) if (r.has((BlockId)i)) for (auto& f : r.get((BlockId)i).faces) used |= f == t;
            if (used) continue;
            BlockId id = r.add(blockFromTexture(r, t, as)); if (id) res.blocks.push_back(id);
        }
    if (!res.blocks.empty()) saveBlocks(r);
    return res;
}

bool Library::deleteTexture(const std::string& name, BlockRegistry& r) {
    std::error_code ec; fs::remove(P(texturesDir() + "/" + name + ".png"), ec);
    stamps.erase(name);
    return r.removeTexture(name);
}

} // namespace mc
