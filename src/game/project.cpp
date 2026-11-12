#include "project.h"
#include "game.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;
namespace mc {
namespace Projects {

static std::time_t fileTime(const fs::path& p) {
    std::error_code ec; auto ft = fs::last_write_time(p, ec); if (ec) return 0;
    auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(sys);
}

int countWorlds(const std::string& folder) {
    int n = 0; std::error_code ec;
    for (auto& e : fs::directory_iterator(fs::path(folder) / "worlds", ec)) if (e.is_directory() && fs::exists(e.path() / "world.txt")) n++;
    return n;
}

std::vector<ProjectEntry> list(const std::string& root) {
    std::vector<ProjectEntry> out; std::error_code ec;
    for (auto& e : fs::directory_iterator(fs::path(root), ec)) {
        if (!e.is_directory() || !fs::exists(e.path() / "game.txt")) continue;
        ProjectEntry p; p.folder = e.path().string();
        GameSettings g; g.load((e.path() / "game.txt").string()); p.name = g.name;
        p.worlds = countWorlds(p.folder); p.opened = fileTime(e.path() / "game.txt");
        out.push_back(p);
    }
    std::sort(out.begin(), out.end(), [](const ProjectEntry& a, const ProjectEntry& b) { return a.opened != b.opened ? a.opened > b.opened : a.name < b.name; });
    return out;
}

std::string safeFolderName(const std::string& name) {
    std::string f; for (char c : name) f += (std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_') ? c : '_';
    while (!f.empty() && f.back() == ' ') f.pop_back();
    while (!f.empty() && f.front() == ' ') f.erase(0, 1);
    return f.empty() ? "Game" : f;
}

std::string uniqueFolder(const std::string& parent, const std::string& name) {
    std::string base = safeFolderName(name), f = base; int n = 2;
    while (fs::exists(fs::path(parent) / f)) f = base + " (" + std::to_string(n++) + ")";
    return f;
}

std::string create(const std::string& root, const std::string& name) {
    std::error_code ec; fs::create_directories(fs::path(root), ec);
    fs::path dir = fs::path(root) / uniqueFolder(root, name);
    fs::create_directories(dir / "worlds", ec);
    GameSettings g; g.name = name.empty() ? "My Game" : name; g.save((dir / "game.txt").string());
    return dir.string();
}

bool remove(const std::string& folder) {
    std::error_code ec; if (!fs::exists(fs::path(folder) / "game.txt")) return false;   // only ever deletes real project folders
    fs::remove_all(fs::path(folder), ec); return !ec;
}

static void moveInto(const fs::path& from, const fs::path& to) {
    std::error_code ec; if (!fs::exists(from)) return;
    fs::rename(from, to, ec);
    if (ec) { fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec); if (!ec) fs::remove_all(from, ec); }
}

std::string migrateOld(const std::string& exeDir, const std::string& root) {
    fs::path ex(exeDir), oldGame = ex / "game", oldWorlds = ex / "worlds";
    bool hasOld = fs::exists(oldGame / "blocks.txt") || fs::exists(oldWorlds);
    if (!hasOld || !list(root).empty()) return "";
    std::string name = "My First Game";
    { GameSettings g; if (g.load((oldGame / "game.txt").string()) && !g.name.empty() && g.name != "My Game") name = g.name; }
    std::string dir = create(root, name); fs::path d(dir);
    std::error_code ec;
    for (const char* f : { "blocks.txt", "game.txt" }) if (fs::exists(oldGame / f)) { fs::remove(d / f, ec); moveInto(oldGame / f, d / f); }
    if (fs::exists(oldGame / "textures")) moveInto(oldGame / "textures", d / "textures");
    if (fs::exists(oldWorlds)) {
        for (auto& e : fs::directory_iterator(oldWorlds, ec)) moveInto(e.path(), d / "worlds" / e.path().filename());
        fs::remove(oldWorlds, ec);
    }
    { GameSettings g; g.load((d / "game.txt").string()); g.name = name; g.save((d / "game.txt").string()); }
    fs::remove(oldGame, ec);   // only if it is empty now
    return dir;
}

std::string ago(std::time_t t, std::time_t now) {
    if (t <= 0) return "never";
    double days = std::difftime(now, t) / 86400.0;
    if (days < 1) return "today";
    if (days < 2) return "yesterday";
    if (days < 60) return std::to_string((int)days) + " days ago";
    return std::to_string((int)(days / 30)) + " months ago";
}

} // namespace Projects
} // namespace mc
