#include "assetmeta.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace px {

const char* blendModeName(BlendMode m) { return m == BlendMode::Inertialize ? "inertialize" : m == BlendMode::Crossfade ? "crossfade" : "cut"; }
const char* blendCurveName(BlendCurve c) { return c == BlendCurve::Linear ? "linear" : c == BlendCurve::EaseIn ? "ease_in" : c == BlendCurve::EaseOut ? "ease_out" : "smooth"; }

static std::string quote(const std::string& s) {
    std::string o = "\""; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o + "\"";
}
// splits a line into tokens; "quoted strings" keep spaces; key=value pairs stay one token (value may be quoted)
static std::vector<std::string> tokens(const std::string& line) {
    std::vector<std::string> out; std::string cur; bool inq = false, any = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (inq) { if (c == '\\' && i + 1 < line.size()) cur += line[++i]; else if (c == '"') inq = false; else cur += c; }
        else if (c == '"') { inq = true; any = true; }
        else if (c == ' ' || c == '\t') { if (any || !cur.empty()) out.push_back(cur); cur.clear(); any = false; }
        else cur += c;
    }
    if (any || !cur.empty()) out.push_back(cur);
    return out;
}

bool AssetMeta::load(const std::string& path) {
    std::ifstream f(path); if (!f) return false;
    *this = AssetMeta{};
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto t = tokens(line); if (t.empty() || t[0][0] == '#') continue;
        auto F = [&](size_t i) { return i < t.size() ? (float)std::atof(t[i].c_str()) : 0.f; };
        const std::string& k = t[0];
        if (k == "import_scale") importScale = F(1);
        else if (k == "pixel_filter") pixelFilter = F(1) != 0;
        else if (k == "auto_fit") { autoFit = F(1) != 0; fitLength = t.size() > 2 ? F(2) : fitLength; }
        else if (k == "view_pos") viewPos = { F(1), F(2), F(3) };
        else if (k == "view_rot") viewRotDeg = { F(1), F(2), F(3) };
        else if (k == "view_scale") viewScale = F(1);
        else if (k == "reflect") reflect = F(1);
        else if (k == "blend_mode") blendMode = t.size() > 1 && t[1] == "crossfade" ? BlendMode::Crossfade : t.size() > 1 && t[1] == "cut" ? BlendMode::Cut : BlendMode::Inertialize;
        else if (k == "blend_curve") { std::string v = t.size() > 1 ? t[1] : ""; blendCurve = v == "linear" ? BlendCurve::Linear : v == "ease_in" ? BlendCurve::EaseIn : v == "ease_out" ? BlendCurve::EaseOut : BlendCurve::Smooth; }
        else if (k == "blend_time") blendTime = F(1);
        else if (k == "default_clip" && t.size() > 1) defaultClip = t[1];
        else if (k == "clip" && t.size() > 1) {
            ClipMeta c; c.fileName = t[1]; c.displayName = t[1];
            for (size_t i = 2; i < t.size(); i++) {
                size_t eq = t[i].find('='); if (eq == std::string::npos) continue;
                std::string kk = t[i].substr(0, eq), vv = t[i].substr(eq + 1);
                if (kk == "name") c.displayName = vv;
                else if (kk == "loop") c.s.loop = vv == "1";
                else if (kk == "speed") c.s.speed = (float)std::atof(vv.c_str());
            }
            clips.push_back(c);
        }
    }
    return true;
}

bool AssetMeta::save(const std::string& path) const {
    std::ofstream f(path, std::ios::trunc); if (!f) return false;
    char b[256];
    f << "# PRIMEngine asset settings. Written by the editor; safe to delete (defaults come back).\n";
    snprintf(b, sizeof b, "import_scale %g\npixel_filter %d\nauto_fit %d %g\n", importScale, pixelFilter ? 1 : 0, autoFit ? 1 : 0, fitLength); f << b;
    snprintf(b, sizeof b, "view_pos %g %g %g\nview_rot %g %g %g\nview_scale %g\nreflect %g\n", viewPos.x, viewPos.y, viewPos.z, viewRotDeg.x, viewRotDeg.y, viewRotDeg.z, viewScale, reflect); f << b;
    snprintf(b, sizeof b, "blend_mode %s\nblend_curve %s\nblend_time %g\n", blendModeName(blendMode), blendCurveName(blendCurve), blendTime); f << b;
    if (!defaultClip.empty()) f << "default_clip " << quote(defaultClip) << "\n";
    for (auto& c : clips) {
        snprintf(b, sizeof b, " loop=%d speed=%g", c.s.loop ? 1 : 0, c.s.speed);
        f << "clip " << quote(c.fileName) << " name=" << quote(c.displayName) << b << "\n";
    }
    return (bool)f;
}

void AssetMeta::applyTo(const Model& m, std::vector<ClipSettings>& settings, std::vector<std::string>& names) {
    settings.assign(m.clips.size(), ClipSettings{}); names.assign(m.clips.size(), "");
    for (size_t i = 0; i < m.clips.size(); i++) {
        names[i] = m.clips[i].name;
        bool found = false;
        for (auto& c : clips) if (c.fileName == m.clips[i].name) { settings[i] = c.s; names[i] = c.displayName; found = true; break; }
        if (!found) {
            // first import: guess loops from the name, the user can change it in the editor
            std::string lower; for (char ch : m.clips[i].name) lower += (char)std::tolower((unsigned char)ch);
            settings[i].loop = lower.find("idle") != std::string::npos || lower.find("loop") != std::string::npos || lower.find("walk") != std::string::npos || lower.find("run") != std::string::npos;
            clips.push_back({ m.clips[i].name, m.clips[i].name, settings[i] });
        }
    }
}

void AssetMeta::capture(const Model& m, const std::vector<ClipSettings>& settings, const std::vector<std::string>& names) {
    for (size_t i = 0; i < m.clips.size() && i < settings.size(); i++) {
        bool found = false;
        for (auto& c : clips) if (c.fileName == m.clips[i].name) { c.s = settings[i]; c.displayName = names[i]; found = true; break; }
        if (!found) clips.push_back({ m.clips[i].name, names[i], settings[i] });
    }
}

} // namespace px
