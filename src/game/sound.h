#pragma once
// Sounds (WAV), the player's sounds, and timelines (an animation, sounds and the moment the shot leaves, laid out in time).
// Plain C++, tested on Linux; the app plays them with XAudio2.
#include <cstdint>
#include <string>
#include <vector>

namespace mc {

// a WAV file, read into floats (-1..1), channels interleaved
struct WavData { int channels = 0, rate = 0; std::vector<float> samples; float seconds() const { return channels && rate ? (float)samples.size() / channels / rate : 0.f; } };
bool readWav(const std::vector<uint8_t>& bytes, WavData& out, std::string& err);   // PCM 8 / 16 / 24 / 32 bit or 32-bit float, any rate, mono or more
std::vector<uint8_t> wavBytes(const WavData& w);                                     // 16-bit PCM (for tests and tools)

// one use of a sound: which file (in the project's sounds folder), how loud, pitch (and a random spread), and a cut of it
struct SoundUse {
    std::string file;
    float volume = 1, pitch = 1, pitchVar = 0.05f;      // pitchVar: random up / down by this much each time (steps don't sound robotic)
    float trimStart = 0, length = 0;                    // seconds into the file to start, how long (0: to the end)
    bool empty() const { return file.empty(); }
};
float randomPitch(const SoundUse& s, uint32_t& seed);   // pitch with the random spread

// sounds placed on a timeline
struct SoundClip { SoundUse sound; float at = 0; };     // at: seconds after the timeline starts
struct Timeline {
    float animSpeed = 1;                                // the animation plays this much faster (2: twice as fast)
    float shotAt = 0;                                   // seconds: when the shot leaves (fire / charged / missile)
    std::vector<SoundClip> sounds;
    // how long it runs (the animation at its speed, the last sound, the shot)
    float length(float animLength, const std::vector<float>& soundLengths) const;
};
// plays a timeline: tells when each sound starts and when the shot leaves
struct TimelinePlay {
    const Timeline* tl = nullptr; double t = -1; std::vector<char> started; bool shotDone = false;
    void start(const Timeline& timeline) { tl = &timeline; t = 0; started.assign(timeline.sounds.size(), 0); shotDone = false; }
    bool running() const { return tl && t >= 0; }
    // advances; fills which sounds start now and whether the shot leaves now; stops after `until` seconds
    void step(double dt, std::vector<int>& soundsNow, bool& shotNow, double until);
};

// several sounds for one thing: each time one of them, picked at random (never the same one twice in a row)
struct SoundSet {
    std::vector<SoundUse> sounds; int last = -1;
    bool empty() const { for (const SoundUse& s : sounds) if (!s.empty()) return false; return true; }
    const SoundUse* pick(uint32_t& seed);
};

// the player's own sounds
struct PlayerSounds {
    SoundSet step, jump, land;
    SoundSet morph, bomb, boom, crumble;                // morph ball in / out, a bomb laid, a bomb going off, a breakable block breaking
    SoundSet roll, ballLand;                            // morph ball: the rolling loop (louder and higher the faster it rolls), and its own landing
    float rollPitch = 0.35f;                            // how much higher the roll sounds at full speed (0 = always the same)
    void rollMix(float speed, float topSpeed, float& volume, float& pitch) const;   // the roll loop at this speed: volume 0..1, pitch factor
    float stepEvery = 1.7f;                             // blocks walked per step sound
    float landQuiet = 4, landLoud = 20;                 // a landing plays from this falling speed on (landLoud: older files, not used)
    float landVolume(float fallSpeed) const;            // 0 below landQuiet, else 1 (always the same volume)
    // the game's own sounds while playing: the helmet, the menus, pickups, alerts
    SoundSet visor, visorCombat, visorScan, visorThermal, visorXRay;   // switching visors (a visor's own, else the one for all)
    SoundSet scanTarget, scanLoop, scanDone, scanNew, scanOpen, scanClose;   // Scan Visor: onto something scannable, scanning (a loop, rising), done, a new entry, its text up / away
    SoundSet menuOpen, menuClose, mapOpen, mapClose;    // the pause menu, the map
    SoundSet beamSwitch, missileSwitch, empty, lockOn, lockOff;   // another beam / missile, firing with none left, lock-on onto / off
    SoundSet pickHealth, pickAmmo, itemGet, saved, message;       // energy / ammo picked up, an item (an upgrade), the game saved, a message
    SoundSet hurt, lowHealth, death;                    // getting hurt, the low-energy alarm (again and again), dying (the hit)
    SoundSet deathStatic, deathPowerDown, deathOff;     // the death sequence: static (a loop, rising and fading), the suit powering down, the picture switching off
    float powerDownFall = 0.5f;                         // the power-down sound's pitch falls this much (0: not at all .. 1: an octave) as the suit dies
    SoundSet doorOpen, doorClose, doorField, doorLocked; // doors (each door can have its own open / close): opening, closing, its forcefield shot away, won't open
    SoundSet loopCombat, loopScan, loopThermal, loopXRay; // a loop playing all the time you are in that visor (the helmet's hum, the scan visor's whirr ...)
    float visorLoopVolume = 1;                          // those loops, this loud
    float scanPitch = 0.5f;                             // the scanning loop goes this much higher as it fills
    float lowHealthAt = 30, lowHealthEvery = 1.2f;      // the alarm: below this much energy (every: older files, no longer used)
    float lowHealthFade = 6, lowHealthQuiet = 0.15f;    // the alarm is a loop: full volume at first, then quieter over this many seconds, down to this (getting hurt again: loud again)
    float lowHealthVolume(float sinceLoud) const;       // its volume this long after it was last loud
};
// the game's own sounds (above): their keys in the file, for the editor and saving
struct GameSoundSlot { const char* key; const char* label; SoundSet PlayerSounds::* set; const char* group; };
const std::vector<GameSoundSlot>& gameSoundSlots();
bool savePlayerSounds(const std::string& path, const PlayerSounds& p);
bool loadPlayerSounds(const std::string& path, PlayerSounds& p);
std::string soundUseText(const SoundUse& s);            // one line (for saving with other things)
bool soundUseFrom(const std::string& text, SoundUse& s);

} // namespace mc
