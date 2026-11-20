#include "surfaces.h"
#include "visors.h"   // escText
#include <fstream>
#include <sstream>

namespace mc {

Surface* SurfaceSet::find(const std::string& name) { for (Surface& s : list) if (s.name == name) return &s; return nullptr; }
const Surface* SurfaceSet::find(const std::string& name) const { for (const Surface& s : list) if (s.name == name) return &s; return nullptr; }

SurfaceSet defaultSurfaces() {
    SurfaceSet s;
    for (const char* n : { "Metal", "Grate", "Rock", "Dirt", "Sand", "Wood", "Glass", "Organic", "Snow", "Ice", "Water" }) { Surface x; x.name = n; if (x.name == "Water" || x.name == "Snow") x.replace = true; s.list.push_back(x); }
    return s;
}

bool saveSurfaces(const std::string& path, const SurfaceSet& s) {
    std::ofstream f(path); if (!f) return false;
    f << "surfaces 1\n";
    for (const Surface& x : s.list) {
        f << "surface " << escText(x.name) << " " << x.volume << " " << (x.replace ? 1 : 0) << "\n";
        const std::pair<const char*, const SoundSet*> sets[] = { { "step", &x.step }, { "jump", &x.jump }, { "land", &x.land }, { "roll", &x.roll } };
        for (auto& st : sets) for (const SoundUse& u : st.second->sounds) if (!u.empty()) f << "sound " << st.first << " " << escText(soundUseText(u)) << "\n";
    }
    return (bool)f;
}
bool loadSurfaces(const std::string& path, SurfaceSet& s) {
    std::ifstream f(path); if (!f) return false;
    s = SurfaceSet{}; std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k;
        if (k == "surface") { Surface x; std::string n; int rep = 0; if (l >> n >> x.volume >> rep) { x.name = unescText(n); x.replace = rep != 0; if (!x.name.empty() && !s.find(x.name)) s.list.push_back(x); } }
        else if (k == "sound" && !s.list.empty()) {
            std::string which, t; if (!(l >> which >> t)) continue; SoundUse u; if (!soundUseFrom(unescText(t), u)) continue;
            Surface& x = s.list.back();
            SoundSet* set = which == "step" ? &x.step : which == "jump" ? &x.jump : which == "land" ? &x.land : which == "roll" ? &x.roll : nullptr;
            if (set) set->sounds.push_back(u);
        }
    }
    return true;
}

}
