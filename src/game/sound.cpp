#include "sound.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace mc {

// ------------------------------------------------------------------ WAV
static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
bool readWav(const std::vector<uint8_t>& b, WavData& out, std::string& err) {
    out = WavData{};
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) { err = "not a WAV file"; return false; }
    int fmt = 0, ch = 0, bits = 0; uint32_t rate = 0; const uint8_t* data = nullptr; size_t dataLen = 0;
    for (size_t at = 12; at + 8 <= b.size();) {
        uint32_t len = rd32(&b[at + 4]); const uint8_t* body = &b[at + 8]; size_t avail = std::min<size_t>(len, b.size() - at - 8);
        if (!std::memcmp(&b[at], "fmt ", 4) && avail >= 16) {
            fmt = rd16(body); ch = rd16(body + 2); rate = rd32(body + 4); bits = rd16(body + 14);
            if (fmt == 0xFFFE && avail >= 26) fmt = rd16(body + 24);   // WAVE_FORMAT_EXTENSIBLE: the real format is in the sub-format
        } else if (!std::memcmp(&b[at], "data", 4)) { data = body; dataLen = avail; }
        at += 8 + len + (len & 1);
    }
    if (!ch || !rate || !data) { err = "no sound data in the file"; return false; }
    if (!(fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) && !(fmt == 3 && bits == 32)) { err = "only PCM or 32-bit float WAV files (this one is format " + std::to_string(fmt) + ", " + std::to_string(bits) + " bit)"; return false; }
    int bytes = bits / 8; size_t n = dataLen / bytes; out.samples.resize(n); out.channels = ch; out.rate = (int)rate;
    for (size_t i = 0; i < n; i++) {
        const uint8_t* p = data + i * bytes; float v = 0;
        if (fmt == 3) { float f; std::memcpy(&f, p, 4); v = f; }
        else if (bits == 8) v = (p[0] - 128) / 128.f;
        else if (bits == 16) v = (int16_t)rd16(p) / 32768.f;
        else if (bits == 24) { int32_t x = (p[0] << 8) | (p[1] << 16) | ((uint32_t)p[2] << 24); v = (x >> 8) / 8388608.f; }
        else { int32_t x = (int32_t)rd32(p); v = x / 2147483648.f; }
        out.samples[i] = v;
    }
    out.samples.resize(n - n % ch);
    return true;
}
std::vector<uint8_t> wavBytes(const WavData& w) {
    std::vector<uint8_t> o; auto p32 = [&](uint32_t v) { for (int k = 0; k < 4; k++) o.push_back((uint8_t)(v >> (8 * k))); }; auto p16 = [&](uint16_t v) { o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8)); };
    uint32_t data = (uint32_t)w.samples.size() * 2;
    o.insert(o.end(), { 'R', 'I', 'F', 'F' }); p32(36 + data); o.insert(o.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' }); p32(16);
    p16(1); p16((uint16_t)w.channels); p32((uint32_t)w.rate); p32((uint32_t)w.rate * w.channels * 2); p16((uint16_t)(w.channels * 2)); p16(16);
    o.insert(o.end(), { 'd', 'a', 't', 'a' }); p32(data);
    for (float s : w.samples) p16((uint16_t)(int16_t)std::lround(std::clamp(s, -1.f, 1.f) * 32767));
    return o;
}

float randomPitch(const SoundUse& s, uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u; float r = ((seed >> 8) & 0xFFFF) / 65535.f * 2 - 1;
    return std::clamp(s.pitch * (1 + r * s.pitchVar), 0.05f, 4.f);
}

// ------------------------------------------------------------------ timelines
float Timeline::length(float animLength, const std::vector<float>& soundLengths) const {
    float L = std::max(animLength / std::max(0.05f, animSpeed), shotAt);
    for (size_t i = 0; i < sounds.size(); i++) {
        const SoundUse& s = sounds[i].sound; float full = i < soundLengths.size() ? soundLengths[i] : 0;
        float len = s.length > 0 ? s.length : std::max(0.f, full - s.trimStart);
        L = std::max(L, sounds[i].at + len / std::max(0.05f, s.pitch));
    }
    return L;
}
void TimelinePlay::step(double dt, std::vector<int>& soundsNow, bool& shotNow, double until) {
    soundsNow.clear(); shotNow = false;
    if (!running()) return;
    for (size_t i = 0; i < tl->sounds.size() && i < started.size(); i++) if (!started[i] && tl->sounds[i].at <= t + 1e-9) { started[i] = 1; soundsNow.push_back((int)i); }
    if (!shotDone && tl->shotAt <= t + 1e-9) { shotDone = true; shotNow = true; }
    t += dt;
    bool more = !shotDone; for (char c : started) more |= !c;
    if (t > until && !more) { t = -1; tl = nullptr; }
}

// ------------------------------------------------------------------ the player's sounds
float PlayerSounds::landVolume(float v) const { return std::clamp((v - landQuiet) / std::max(0.1f, landLoud - landQuiet), 0.f, 1.f); }
static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '%') o += "%25"; else if (c == ' ') o += "%20"; else if (c == '\n') o += "%0A"; else o += c; } return o.empty() ? "-" : o; }
static std::string unesc(const std::string& s) {
    if (s == "-") return ""; std::string o;
    for (size_t i = 0; i < s.size(); i++) { if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16); i += 2; } else o += s[i]; }
    return o;
}
std::string soundUseText(const SoundUse& s) {
    std::ostringstream o; o << esc(s.file) << " " << s.volume << " " << s.pitch << " " << s.pitchVar << " " << s.trimStart << " " << s.length; return o.str();
}
bool soundUseFrom(const std::string& text, SoundUse& s) {
    std::istringstream l(text); std::string f; SoundUse r;
    if (!(l >> f >> r.volume >> r.pitch >> r.pitchVar >> r.trimStart >> r.length)) return false;
    r.file = unesc(f); s = r; return true;
}
bool savePlayerSounds(const std::string& path, const PlayerSounds& p) {
    std::ofstream f(path); if (!f) return false;
    f << "sounds 1\nstep " << soundUseText(p.step) << "\njump " << soundUseText(p.jump) << "\nland " << soundUseText(p.land)
      << "\nmorph " << soundUseText(p.morph) << "\nbomb " << soundUseText(p.bomb) << "\nboom " << soundUseText(p.boom) << "\ncrumble " << soundUseText(p.crumble)
      << "\nevery " << p.stepEvery << "\nlanding " << p.landQuiet << " " << p.landLoud << "\n";
    return (bool)f;
}
bool loadPlayerSounds(const std::string& path, PlayerSounds& p) {
    std::ifstream f(path); if (!f) return false;
    p = PlayerSounds{}; std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k; std::string rest; std::getline(l, rest);
        if (k == "step") soundUseFrom(rest, p.step); else if (k == "jump") soundUseFrom(rest, p.jump); else if (k == "land") soundUseFrom(rest, p.land);
        else if (k == "morph") soundUseFrom(rest, p.morph); else if (k == "bomb") soundUseFrom(rest, p.bomb); else if (k == "boom") soundUseFrom(rest, p.boom); else if (k == "crumble") soundUseFrom(rest, p.crumble);
        else if (k == "every") { std::istringstream r(rest); r >> p.stepEvery; }
        else if (k == "landing") { std::istringstream r(rest); r >> p.landQuiet >> p.landLoud; }
    }
    return true;
}

} // namespace mc
