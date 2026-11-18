#pragma once
// Pictures and videos for the screens (Windows only): pictures and GIFs through WIC (every GIF frame put together, with
// its delay), videos through Media Foundation (MP4, WMV, AVI, MOV... whatever Windows can play), decoded frame by frame.
#include <cstdint>
#include <string>
#include <vector>

struct IMFSourceReader;

namespace media {

void init();   // COM and Media Foundation (once)

struct Frames {
    int w = 0, h = 0;
    std::vector<std::vector<uint8_t>> rgba;   // each frame, w x h x 4
    std::vector<float> delay;                 // seconds each frame shows (pictures: one frame)
    float total() const { float t = 0; for (float d : delay) t += d; return t; }
    int at(double t, bool loop) const;        // which frame shows t seconds in
};
bool loadPicture(const std::string& utf8Path, Frames& out, std::string& err);   // png, jpg, bmp, gif (all frames), tif...

// any sound Windows can read (MP3, M4A / AAC, WMA, WAV...) as floats, channels interleaved (at most `maxSeconds`)
bool loadAudio(const std::string& utf8Path, int& channels, int& rate, std::vector<float>& samples, std::string& err, double maxSeconds = 900);

class Video {
public:
    ~Video();
    bool open(const std::string& utf8Path, std::string& err);
    void close();
    // decodes up to t seconds in (from the start); true when the picture changed. At the end: loops (loop) or stays on the last frame.
    bool frameAt(double t, bool loop);
    int w = 0, h = 0; double duration = 0;
    bool done() const { return ended; }        // (not looping) it has played to the end
    std::vector<uint8_t> rgba;                 // the current frame
private:
    IMFSourceReader* reader = nullptr;
    long stride = 0; double nextTime = 0, base = 0; bool ended = false;
    bool readOne(double& ts);
};

} // namespace media
