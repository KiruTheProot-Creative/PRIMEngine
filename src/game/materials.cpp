#include "materials.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace mc {

TexMaterial guessMaterial(const std::string& name) {
    std::string n = name; for (char& c : n) c = (char)std::tolower((unsigned char)c);
    auto has = [&](const char* w) { return n.find(w) != std::string::npos; };
    TexMaterial m;
    if (has("iron") || has("gold") || has("copper") || has("metal") || has("steel") || has("chrome")) { m.metal = 0.85f; m.shine = 0.6f; m.coat = 0.5f; }
    else if (has("diamond") || has("emerald") || has("gem") || has("crystal") || has("amethyst")) { m.shine = 0.7f; m.coat = 0.4f; }
    else if (has("glass") || has("ice")) { m.shine = 0.8f; m.coat = 0; m.bumps = 0.2f; }
    else if (has("polished") || has("marble") || has("quartz") || has("tile")) { m.shine = 0.35f; m.coat = 0.6f; }
    if (has("lava") || has("magma")) { m.glow = 1; m.glowFrom = 0.45f; }
    else if (has("lamp") || has("lantern") || has("glow") || has("torch") || has("light") || has("neon")) { m.glow = 1; m.glowFrom = 0.55f; }
    if (has("leaves") || has("leaf") || has("grass_side") || has("wool")) m.bumps = 0.6f;
    return m;
}

TexMaterial Materials::of(const std::string& name) const {
    auto it = set.find(name); return it != set.end() ? it->second : guessMaterial(name);
}

std::vector<float> Materials::table(const BlockRegistry& reg) const {
    std::vector<float> t(std::max<size_t>(1, reg.textureCount()) * 8, 0.f);
    for (size_t i = 0; i < reg.textureCount(); i++) {
        TexMaterial m = i == 0 ? TexMaterial{} : of(reg.image((int)i).name);
        float* p = &t[i * 8]; p[0] = m.shine; p[1] = m.metal; p[2] = m.glow; p[3] = m.glowFrom; p[4] = m.bumps; p[5] = m.coat;
    }
    return t;
}

static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '%') o += "%25"; else if (c == ' ') o += "%20"; else if (c == '\n') o += "%0A"; else o += c; } return o.empty() ? "-" : o; }
static std::string unesc(const std::string& s) {
    if (s == "-") return ""; std::string o;
    for (size_t i = 0; i < s.size(); i++) { if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16); i += 2; } else o += s[i]; }
    return o;
}
bool saveMaterials(const std::string& path, const Materials& m) {
    std::ofstream f(path); if (!f) return false;
    f << "materials 1\n";
    for (auto& kv : m.set) { const TexMaterial& t = kv.second; f << "tex " << esc(kv.first) << " " << t.shine << " " << t.metal << " " << t.glow << " " << t.glowFrom << " " << t.bumps << " " << t.coat << "\n"; }
    return (bool)f;
}
bool loadMaterials(const std::string& path, Materials& m) {
    std::ifstream f(path); if (!f) return false;
    m = Materials{}; std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k, n; l >> k; if (k != "tex") continue;
        TexMaterial t; if (l >> n >> t.shine >> t.metal >> t.glow >> t.glowFrom >> t.bumps >> t.coat) m.set[unesc(n)] = t;
    }
    return true;
}

} // namespace mc
