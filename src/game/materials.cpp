#include "materials.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace mc {

TexMaterial guessMaterial(const std::string& name) {
    // the name's words ("Iron_Block", "glowStone", "grass tile 2" -> iron block / glow stone / grass tile 2); a guess word
    // must start one of them, so "slice" is not ice and "twilight" not light
    std::vector<std::string> words; std::string w;
    for (size_t i = 0; i < name.size(); i++) {
        unsigned char c = (unsigned char)name[i];
        bool upperStart = std::isupper(c) && i > 0 && std::islower((unsigned char)name[i - 1]);
        if (!std::isalpha(c) || upperStart) { if (!w.empty()) words.push_back(w); w.clear(); }
        if (std::isalpha(c)) w += (char)std::tolower(c);
    }
    if (!w.empty()) words.push_back(w);
    auto has = [&](const char* g) { for (const std::string& x : words) if (x.rfind(g, 0) == 0) return true; return false; };
    TexMaterial m;
    if (has("iron") || has("gold") || has("copper") || has("metal") || has("steel") || has("chrome")) { m.metal = 0.85f; m.shine = 0.6f; m.coat = 0.5f; }
    else if (has("diamond") || has("emerald") || has("gem") || has("crystal") || has("amethyst")) { m.shine = 0.7f; m.coat = 0.4f; }
    else if (has("glass") || has("ice")) { m.shine = 0.8f; m.coat = 0; m.bumps = 0.2f; }
    else if (has("polished") || has("marble") || has("quartz") || has("tile")) { m.shine = 0.35f; m.coat = 0.6f; }
    if (has("lava") || has("magma")) { m.glow = 1; m.glowFrom = 0.45f; }
    else if (has("lamp") || has("lantern") || has("glow") || has("torch") || has("light") || has("neon")) { m.glow = 1; m.glowFrom = 0.55f; }
    if (has("leaves") || has("leaf") || has("wool")) m.bumps = 0.6f;
    return m;
}

TexMaterial Materials::of(const std::string& name) const {
    auto it = set.find(name); return it != set.end() ? it->second : guess ? guessMaterial(name) : TexMaterial{};
}

const char* shinePresetName(int i) {
    static const char* n[] = { "Studio streak", "Sharp dot", "Soft band", "Rim (edges)", "Your own picture" };
    return n[i < 0 ? 0 : i > SHINE_PRESETS ? SHINE_PRESETS : i];
}
std::vector<float> Materials::table(const BlockRegistry& reg) const {
    std::vector<float> t(std::max<size_t>(1, reg.textureCount()) * 12, 0.f);
    for (size_t i = 0; i < reg.textureCount(); i++) {
        const TexImage& im = reg.image((int)i);
        TexMaterial m = i == 0 ? TexMaterial{} : of(im.variant() ? im.baseName : im.name);
        float tint = 0;
        if (im.variant()) {   // a face glowing on its own: its glow on top of the texture's material
            const FaceGlow& g = im.glow;
            if (g.mode == 1) { m.glow = g.strength; m.glowFrom = 0; }
            else if (g.mode == 2) { m.glow = g.strength; m.glowFrom = g.from; }
            else if (g.mode == 3) m.emit = g.strength;
            if ((g.color & 0xFFFFFF) != 0xFFFFFF) tint = (float)(g.color & 0xFFFFFF);   // (exact: 24 bits fit a float)
        }
        float* p = &t[i * 12]; p[0] = m.shine; p[1] = m.metal; p[2] = m.glow; p[3] = m.glowFrom; p[4] = m.bumps; p[5] = m.coat; p[6] = m.emit; p[7] = tint; p[8] = m.rough;
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
    for (auto& kv : m.set) { const TexMaterial& t = kv.second; f << "tex " << esc(kv.first) << " " << t.shine << " " << t.metal << " " << t.glow << " " << t.glowFrom << " " << t.bumps << " " << t.coat << " " << esc(t.emitMap) << " " << t.emit << " " << t.lights
      << " " << (int)t.fake << " " << t.fakeStrength << " " << t.fakeTint << " " << t.fakeBeam << " " << t.fakeWorld << " " << t.fakePreset << " " << esc(t.fakeMap) << " " << esc(t.fakeMask);
      for (const Glint& g : t.glints) f << " " << g.on << " " << g.yaw << " " << g.pitch << " " << g.color << " " << g.sharp << " " << g.power;
      f << " rough " << t.rough << " real " << (t.realLight ? 1 : 0);
      f << "\n"; }
    return (bool)f;
}
bool loadMaterials(const std::string& path, Materials& m) {
    std::ifstream f(path); if (!f) return false;
    m = Materials{}; std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k, n; l >> k; if (k != "tex") continue;
        TexMaterial t; if (!(l >> n >> t.shine >> t.metal >> t.glow >> t.glowFrom >> t.bumps >> t.coat)) continue;
        std::string em; if (l >> em >> t.emit >> t.lights) t.emitMap = unesc(em);   // (older files: no emission map)
        { int fk; float fs; uint32_t ft; int fb, fw, fp; std::string fm, fma;   // fake shine (newer files)
          if (l >> fk >> fs >> ft >> fb >> fw >> fp >> fm >> fma) {
              t.fake = (FakeShine)std::clamp(fk, 0, (int)FakeShine::COUNT - 1); t.fakeStrength = fs; t.fakeTint = ft; t.fakeBeam = fb != 0; t.fakeWorld = fw != 0;
              t.fakePreset = std::clamp(fp, 0, SHINE_PRESETS); t.fakeMap = unesc(fm); t.fakeMask = unesc(fma);
              for (Glint& g : t.glints) { int on; Glint r; if (l >> on >> r.yaw >> r.pitch >> r.color >> r.sharp >> r.power) { r.on = on != 0; g = r; } }
          } }
        { std::string rk; float rv; if (l >> rk >> rv && rk == "rough") t.rough = std::clamp(rv, 0.f, 1.f); }   // (newer files)
        { std::string rk; int rv; if (l >> rk >> rv && rk == "real") t.realLight = rv != 0; }
        m.set[unesc(n)] = t;
    }
    return true;
}

} // namespace mc
