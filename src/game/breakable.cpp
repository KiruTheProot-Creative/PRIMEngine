#include "breakable.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace mc {

static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '%') o += "%25"; else if (c == ' ') o += "%20"; else if (c == '\n') o += "%0A"; else o += c; } return o.empty() ? "-" : o; }
static std::string unesc(const std::string& s) {
    if (s == "-") return ""; std::string o;
    for (size_t i = 0; i < s.size(); i++) { if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16); i += 2; } else o += s[i]; }
    return o;
}
bool saveBreakables(const std::string& path, const Breakables& b) {
    std::ofstream f(path); if (!f) return false;
    f << "breakable 1\n";
    for (auto& kv : b.marks) f << kv.first[0] << " " << kv.first[1] << " " << kv.first[2] << " " << esc(kv.second) << "\n";
    return (bool)f;
}
bool loadBreakables(const std::string& path, Breakables& b) {
    b.marks.clear(); std::ifstream f(path); if (!f) return false;
    std::string line; std::getline(f, line);
    while (std::getline(f, line)) { std::istringstream l(line); int x, y, z; std::string by; if (l >> x >> y >> z >> by) b.mark(x, y, z, unesc(by)); }
    return true;
}
bool breaksWith(const std::string& br, const std::string& weapon, bool missile, bool charged) {
    bool bomb = weapon == "Bomb";
    if (br.empty()) return true;                         // any shot, bomb or missile
    if (br == "Bomb") return bomb;
    if (br == "Missile") return missile;
    if (br == "Charged shot") return charged && !missile && !bomb;
    return !missile && !bomb && weapon == br;            // that beam (normal or charged)
}
double bombLift(V3 b, V3 c, double radius, double lift) {
    V3 d = c - b; double dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (dist > radius) return 0;
    return lift * std::clamp(1.25 - dist / radius * 0.5, 0.5, 1.0);   // right on top: the full lift, at the edge of its reach: about two thirds
}
std::vector<std::array<int, 3>> blocksInReach(V3 at, double r) {
    std::vector<std::array<int, 3>> out;
    int x0 = (int)std::floor(at.x - r), x1 = (int)std::floor(at.x + r), y0 = (int)std::floor(at.y - r), y1 = (int)std::floor(at.y + r), z0 = (int)std::floor(at.z - r), z1 = (int)std::floor(at.z + r);
    for (int x = x0; x <= x1; x++) for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++) {
        double dx = x + 0.5 - at.x, dy = y + 0.5 - at.y, dz = z + 0.5 - at.z;
        if (dx * dx + dy * dy + dz * dz <= r * r) out.push_back({ x, y, z });
    }
    return out;
}

} // namespace mc
