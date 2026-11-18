#include "savegame.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace mc {

namespace fs = std::filesystem;

bool saveGame(const std::string& dir, const SaveGame& g) {
    std::error_code ec; fs::create_directories(fs::u8path(dir), ec);
    std::string tmp = dir + "/save.tmp";
    {
        std::ofstream f(fs::u8path(tmp)); if (!f) return false;
        f.precision(17);
        f << "save 1\n";
        f << "world " << escText(g.world) << "\nplace " << escText(g.place) << "\ntime " << g.playTime << "\ncompletion " << g.completion
          << "\nhealth " << g.health << " " << g.maxHealth << "\nsaved " << g.savedAt << "\npos " << g.pos.x << " " << g.pos.y << " " << g.pos.z << " " << g.yaw
          << "\nweapons " << g.beam << " " << g.missile << " " << g.visor << "\n";
        for (const std::string& k : g.keys) f << "key " << escText(k) << "\n";
        for (auto& c : g.counts) f << "count " << escText(c.first) << " " << c.second << "\n";
        for (auto& w : g.taken) { f << "taken " << escText(w.first); for (uint32_t id : w.second) f << " " << id; f << "\n"; }
        for (const ScanEntry& e : g.scans) f << "scan " << e.id << " " << escText(e.name) << " " << escText(e.category) << " " << escText(e.text) << " " << escText(e.model) << " " << (e.important ? 1 : 0) << "\n";
        auto set = [&](const char* k, const std::set<std::string>& s) { for (const std::string& v : s) f << k << " " << escText(v) << "\n"; };
        set("gone", g.enemyGone); set("cleared", g.zonesCleared); set("station", g.stationsUsed); set("flag", g.flags);
        for (const std::string& m : g.maps) f << "map " << escText(m) << "\n";
        f << "end\n";
        if (!f) return false;
    }
    fs::rename(fs::u8path(tmp), fs::u8path(dir + "/save.txt"), ec);   // whole, or not at all
    return !ec;
}

bool loadGame(const std::string& dir, SaveGame& g) {
    std::ifstream f(fs::u8path(dir + "/save.txt")); if (!f) return false;
    g = SaveGame{}; std::string line; bool ended = false;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k; std::string a;
        if (k == "world") { l >> a; g.world = unescText(a); }
        else if (k == "place") { l >> a; g.place = unescText(a); }
        else if (k == "time") l >> g.playTime;
        else if (k == "completion") l >> g.completion;
        else if (k == "health") l >> g.health >> g.maxHealth;
        else if (k == "saved") l >> g.savedAt;
        else if (k == "pos") l >> g.pos.x >> g.pos.y >> g.pos.z >> g.yaw;
        else if (k == "weapons") l >> g.beam >> g.missile >> g.visor;
        else if (k == "key") { l >> a; g.keys.insert(unescText(a)); }
        else if (k == "count") { int n = 0; l >> a >> n; g.counts[unescText(a)] = n; }
        else if (k == "taken") { l >> a; auto& s = g.taken[unescText(a)]; uint32_t id; while (l >> id) s.insert(id); }
        else if (k == "scan") { ScanEntry e; std::string n, c, t, m; int imp = 0; l >> e.id >> n >> c >> t >> m >> imp; e.name = unescText(n); e.category = unescText(c); e.text = unescText(t); e.model = unescText(m); e.important = imp != 0; g.scans.push_back(e); }
        else if (k == "gone") { l >> a; g.enemyGone.insert(unescText(a)); }
        else if (k == "cleared") { l >> a; g.zonesCleared.insert(unescText(a)); }
        else if (k == "station") { l >> a; g.stationsUsed.insert(unescText(a)); }
        else if (k == "flag") { l >> a; g.flags.insert(unescText(a)); }
        else if (k == "map") { l >> a; g.maps.push_back(unescText(a)); }
        else if (k == "end") ended = true;
    }
    return ended;   // (a file cut short is not a save)
}

bool eraseGame(const std::string& dir) { std::error_code ec; fs::remove_all(fs::u8path(dir), ec); return !ec; }

std::string playTimeText(double s) {
    long m = (long)(s / 60); char b[32]; std::snprintf(b, sizeof b, "%ld:%02ld", m / 60, m % 60); return b;
}

} // namespace mc
