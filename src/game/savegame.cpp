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
        set("gone", g.enemyGone); set("cleared", g.zonesCleared); set("station", g.stationsUsed); set("flag", g.flags); set("dooropen", g.doorsOpened);
        set("siteopen", g.sitesOpen); set("siteclosed", g.sitesClosed); set("sitevisited", g.sitesVisited);
        for (const std::string& m : g.maps) f << "map " << escText(m) << "\n";
        for (auto& w : g.memory) {
            if (w.second.empty()) continue;
            const WorldMemory& m = w.second; f << "mem " << escText(w.first) << "\n";
            for (auto& b : m.broken) f << "mb " << b[0] << " " << b[1] << " " << b[2] << " " << b[3] << "\n";
            for (auto& q : m.fired) f << "mf " << q.card << " " << q.count << " " << (q.counted ? 1 : 0) << " " << escText(q.name) << "\n";
            auto ids = [&](const char* k, const std::set<uint32_t>& s) { if (s.empty()) return; f << k; for (uint32_t id : s) f << " " << id; f << "\n"; };
            ids("mh", m.hidden); ids("mt", m.triggered); ids("mo", m.off); ids("ml", m.locked); ids("md", m.held); ids("mk", m.kicked);
        }
        f << "end\n";
        if (!f) return false;
    }
    fs::rename(fs::u8path(tmp), fs::u8path(dir + "/save.txt"), ec);   // whole, or not at all
    return !ec;
}

bool loadGame(const std::string& dir, SaveGame& g) {
    std::ifstream f(fs::u8path(dir + "/save.txt")); if (!f) return false;
    g = SaveGame{}; std::string line; bool ended = false; WorldMemory* mem = nullptr;
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
        else if (k == "siteopen") { l >> a; g.sitesOpen.insert(unescText(a)); }
        else if (k == "siteclosed") { l >> a; g.sitesClosed.insert(unescText(a)); }
        else if (k == "sitevisited") { l >> a; g.sitesVisited.insert(unescText(a)); }
        else if (k == "dooropen") { l >> a; g.doorsOpened.insert(unescText(a)); }
        else if (k == "map") { l >> a; g.maps.push_back(unescText(a)); }
        else if (k == "mem") { l >> a; mem = &g.memory[unescText(a)]; }
        else if (k == "mb" && mem) { std::array<int, 4> b{}; l >> b[0] >> b[1] >> b[2] >> b[3]; mem->broken.push_back(b); }
        else if (k == "mf" && mem) { WorldMemory::Fired q; int c = 0; l >> q.card >> q.count >> c >> a; q.counted = c != 0; q.name = unescText(a); mem->fired.push_back(q); }
        else if ((k == "mh" || k == "mt" || k == "mo" || k == "ml" || k == "md" || k == "mk") && mem) {
            std::set<uint32_t>& s = k == "mh" ? mem->hidden : k == "mt" ? mem->triggered : k == "mo" ? mem->off : k == "ml" ? mem->locked : k == "md" ? mem->held : mem->kicked;
            uint32_t id; while (l >> id) s.insert(id);
        }
        else if (k == "end") ended = true;
    }
    return ended;   // (a file cut short is not a save)
}

bool eraseGame(const std::string& dir) { std::error_code ec; fs::remove_all(fs::u8path(dir), ec); return !ec; }

std::string playTimeText(double s) {
    long m = (long)(s / 60); char b[32]; std::snprintf(b, sizeof b, "%ld:%02ld", m / 60, m % 60); return b;
}

} // namespace mc
