#pragma once
// Music loops (the Audio editor): each track's loop points - an intro that plays once, then a stretch that repeats - its tempo as a
// grid to snap them to (found by listening for the beats, or typed / tapped), and a crossfade at the seam that hides the jump.
// Plain C++, tested on Linux; the app plays the result with XAudio2 (sample-accurate, no gap).
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mc {

struct MusicLoop {
    float bpm = 0;                 // beats a minute (0: not known yet - no grid)
    int beatsPerBar = 4;           // 4/4, 3/4, 6/8 ...
    float firstBar = 0;            // seconds: where bar 1 starts (the grid lines up from here)
    float loopStart = 0;           // seconds: the loop starts here (before it: the intro, played once)
    float loopEnd = 0;             // seconds: ... and jumps back here (0: the end of the track; after it: never played)
    float seam = 0.02f;            // seconds of crossfade at the seam (0: a hard jump)
    bool on = true;                // off: the track plays whole, round and round (as before)
    bool operator==(const MusicLoop& o) const { return bpm == o.bpm && beatsPerBar == o.beatsPerBar && firstBar == o.firstBar && loopStart == o.loopStart && loopEnd == o.loopEnd && seam == o.seam && on == o.on; }
    bool operator!=(const MusicLoop& o) const { return !(*this == o); }
    float beat() const { return bpm > 0 ? 60.f / bpm : 0.f; }
    float bar() const { return beat() * (beatsPerBar > 0 ? beatsPerBar : 4); }
};
struct MusicLoops { std::map<std::string, MusicLoop> byFile; const MusicLoop* find(const std::string& file) const { auto it = byFile.find(file); return it == byFile.end() ? nullptr : &it->second; } };
bool saveMusicLoops(const std::string& path, const MusicLoops& m);
bool loadMusicLoops(const std::string& path, MusicLoops& m);

// snapping: 0 off, 1 to beats, 2 to bars (needs a bpm)
float snapTime(float t, const MusicLoop& L, int mode);

// the tempo: listens for where the sound starts (the beats), finds how often they come and where bar 1 is.
// mono: one channel of samples. Returns the bpm (0: none found); firstBar: seconds; confidence: 0..1
struct BpmGuess { float bpm = 0, firstBar = 0, confidence = 0, startRatio = 0; };   // startRatio: how well a grid from where the music starts fits (>= 0.5: used)
BpmGuess detectBpm(const float* mono, size_t n, int rate, int beatsPerBar = 4, float minBpm = 60, float maxBpm = 200);
std::vector<float> monoOf(const std::vector<float>& interleaved, int channels);

// the loop as it will play: frames (per channel) where the intro ends and the loop starts / where it jumps back
struct LoopFrames { uint32_t start = 0, end = 0; float seam = 0; bool ok = false; };
LoopFrames loopFrames(const MusicLoop& L, size_t frames, int rate);
// the samples with the seam crossfaded in: the stretch just before the loop's end blends into what plays just before its start,
// so the jump back lands where the sound already is (no click, no cut-off tail). Too little before the start: the sound after the
// end is blended into the loop's start instead. out ends at the loop's end.
void bakeLoop(const std::vector<float>& in, int channels, int rate, const MusicLoop& L, std::vector<float>& out, LoopFrames& lf);
// a crossfade long enough for this seam: short where the sound already flows on (only against a click), longer where it changes
// or a tail rings on past the end
float autoSeam(const std::vector<float>& in, int channels, int rate, const MusicLoop& L);
// how hard the jump is without a crossfade (0 smooth .. 1+ a click)
float seamJump(const std::vector<float>& in, int channels, int rate, const MusicLoop& L);

} // namespace mc
