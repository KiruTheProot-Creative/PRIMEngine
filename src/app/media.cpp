#include "media.h"
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propidl.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace media {

static std::wstring wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0); std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n); return w;
}

void init() {
    static bool done = false; if (done) return; done = true;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // (already set up: fine)
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
}

int Frames::at(double t, bool loop) const {
    if (delay.size() <= 1) return 0;
    double tot = total(); if (tot <= 0) return 0;
    if (loop) t = std::fmod(std::max(0.0, t), tot); else if (t >= tot) return (int)delay.size() - 1;
    for (int i = 0; i < (int)delay.size(); i++) { if (t < delay[i]) return i; t -= delay[i]; }
    return (int)delay.size() - 1;
}

static bool metaUInt(IWICMetadataQueryReader* q, const wchar_t* name, unsigned& out) {
    if (!q) return false;
    PROPVARIANT v; PropVariantInit(&v); bool ok = false;
    if (SUCCEEDED(q->GetMetadataByName(name, &v))) {
        if (v.vt == VT_UI2) { out = v.uiVal; ok = true; } else if (v.vt == VT_UI1) { out = v.bVal; ok = true; } else if (v.vt == VT_UI4) { out = v.ulVal; ok = true; }
    }
    PropVariantClear(&v); return ok;
}

bool loadPicture(const std::string& path, Frames& out, std::string& err) {
    init(); out = Frames{};
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) { err = "no picture decoder (WIC)"; return false; }
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(wic->CreateDecoderFromFilename(wide(path).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec))) { err = "cannot read " + path; return false; }
    UINT count = 0; dec->GetFrameCount(&count); if (!count) { err = "no picture in " + path; return false; }
    GUID fmt{}; dec->GetContainerFormat(&fmt); bool gif = fmt == GUID_ContainerFormatGif;
    ComPtr<IWICMetadataQueryReader> gq; dec->GetMetadataQueryReader(&gq);
    unsigned W = 0, H = 0;
    if (gif) { metaUInt(gq.Get(), L"/logscrdesc/Width", W); metaUInt(gq.Get(), L"/logscrdesc/Height", H); }
    std::vector<uint8_t> canvas, before;
    for (UINT f = 0; f < count; f++) {
        ComPtr<IWICBitmapFrameDecode> fr; if (FAILED(dec->GetFrame(f, &fr))) break;
        UINT fw = 0, fh = 0; fr->GetSize(&fw, &fh);
        if (!W || !H) { W = fw; H = fh; }
        if (canvas.empty()) canvas.assign((size_t)W * H * 4, 0);
        if ((size_t)W * H * 4 * (out.rgba.size() + 1) > (size_t)384 << 20) break;   // (a huge GIF: as many frames as fit in 384 MB)
        ComPtr<IWICFormatConverter> cv; wic->CreateFormatConverter(&cv);
        if (FAILED(cv->Initialize(fr.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut))) { err = "cannot convert " + path; return false; }
        std::vector<uint8_t> px((size_t)fw * fh * 4); cv->CopyPixels(nullptr, fw * 4, (UINT)px.size(), px.data());
        unsigned left = 0, top = 0, delay = 10, disposal = 0;
        ComPtr<IWICMetadataQueryReader> q; fr->GetMetadataQueryReader(&q);
        if (gif) { metaUInt(q.Get(), L"/imgdesc/Left", left); metaUInt(q.Get(), L"/imgdesc/Top", top); metaUInt(q.Get(), L"/grctlext/Delay", delay); metaUInt(q.Get(), L"/grctlext/Disposal", disposal); }
        if (disposal == 3) before = canvas;
        for (UINT y = 0; y < fh; y++) for (UINT x = 0; x < fw; x++) {   // this frame over what was there
            unsigned cx = left + x, cy = top + y; if (cx >= W || cy >= H) continue;
            const uint8_t* s = &px[((size_t)y * fw + x) * 4]; if (gif && s[3] == 0) continue;
            std::memcpy(&canvas[((size_t)cy * W + cx) * 4], s, 4);
        }
        out.rgba.push_back(canvas);
        out.delay.push_back(gif ? std::max(0.02f, delay / 100.f) : 1.f);
        if (disposal == 2) for (UINT y = 0; y < fh; y++) for (UINT x = 0; x < fw; x++) { unsigned cx = left + x, cy = top + y; if (cx < W && cy < H) std::memset(&canvas[((size_t)cy * W + cx) * 4], 0, 4); }
        else if (disposal == 3 && !before.empty()) canvas = before;
        if (!gif) break;
    }
    out.w = (int)W; out.h = (int)H;
    if (out.rgba.empty()) { err = "no picture in " + path; return false; }
    return true;
}

bool loadAudio(const std::string& path, int& channels, int& rate, std::vector<float>& samples, std::string& err, double maxSeconds) {
    init(); samples.clear(); channels = rate = 0;
    ComPtr<IMFSourceReader> r;
    if (FAILED(MFCreateSourceReaderFromURL(wide(path).c_str(), nullptr, &r))) { err = "Windows cannot read this sound"; return false; }
    r->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE); r->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    ComPtr<IMFMediaType> mt; MFCreateMediaType(&mt); mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio); mt->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    if (FAILED(r->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, mt.Get()))) { err = "no sound in it"; return false; }
    ComPtr<IMFMediaType> cur; r->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &cur);
    channels = (int)MFGetAttributeUINT32(cur.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0); rate = (int)MFGetAttributeUINT32(cur.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
    if (channels <= 0 || rate <= 0) { err = "no sound in it"; return false; }
    size_t most = (size_t)(maxSeconds * rate * channels);
    for (;;) {
        DWORD idx = 0, flags = 0; LONGLONG t = 0; ComPtr<IMFSample> sm;
        if (FAILED(r->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &idx, &flags, &t, &sm))) break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sm) continue;
        ComPtr<IMFMediaBuffer> b; if (FAILED(sm->ConvertToContiguousBuffer(&b))) break;
        BYTE* p = nullptr; DWORD len = 0; if (FAILED(b->Lock(&p, nullptr, &len))) break;
        size_t n = len / sizeof(float); const float* fp = (const float*)p; samples.insert(samples.end(), fp, fp + n);
        b->Unlock();
        if (samples.size() >= most) { samples.resize(most); break; }
    }
    if (samples.empty()) { err = "no sound in it"; return false; }
    return true;
}

Video::~Video() { close(); }
void Video::close() { if (reader) { reader->Release(); reader = nullptr; } w = h = 0; rgba.clear(); ended = false; nextTime = -1; base = 0; }

bool Video::open(const std::string& path, std::string& err) {
    init(); close();
    ComPtr<IMFAttributes> at; MFCreateAttributes(&at, 1); at->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (FAILED(MFCreateSourceReaderFromURL(wide(path).c_str(), at.Get(), &reader))) { reader = nullptr; err = "Windows cannot play " + path; return false; }
    reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE); reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    ComPtr<IMFMediaType> mt; MFCreateMediaType(&mt); mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); mt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, mt.Get()))) { close(); err = "no video in " + path; return false; }
    ComPtr<IMFMediaType> cur; reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur);
    UINT32 W = 0, H = 0; MFGetAttributeSize(cur.Get(), MF_MT_FRAME_SIZE, &W, &H); w = (int)W; h = (int)H;
    UINT32 st = 0; stride = SUCCEEDED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE, &st)) ? (LONG)st : (LONG)(W * 4);
    PROPVARIANT v; PropVariantInit(&v);
    if (SUCCEEDED(reader->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &v)) && v.vt == VT_UI8) duration = v.uhVal.QuadPart / 1e7;
    PropVariantClear(&v);
    if (w <= 0 || h <= 0) { close(); err = "no video in " + path; return false; }
    rgba.assign((size_t)w * h * 4, 0); for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
    return true;
}

bool Video::readOne(double& ts) {
    DWORD idx = 0, flags = 0; LONGLONG t = 0; ComPtr<IMFSample> s;
    if (FAILED(reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &idx, &flags, &t, &s))) { ended = true; return false; }
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { ended = true; return false; }
    if (!s) return false;
    ts = t / 1e7;
    ComPtr<IMFMediaBuffer> b; if (FAILED(s->ConvertToContiguousBuffer(&b))) return false;
    BYTE* p = nullptr; DWORD len = 0; if (FAILED(b->Lock(&p, nullptr, &len))) return false;
    long as = std::labs(stride); const BYTE* row0 = stride < 0 ? p + (size_t)as * (h - 1) : p;
    if ((size_t)as * h <= len)
        for (int y = 0; y < h; y++) {
            const BYTE* r = row0 + (stride < 0 ? -(long)y * as : (long)y * as); uint8_t* o = &rgba[(size_t)y * w * 4];
            for (int x = 0; x < w; x++) { o[x * 4 + 0] = r[x * 4 + 2]; o[x * 4 + 1] = r[x * 4 + 1]; o[x * 4 + 2] = r[x * 4 + 0]; o[x * 4 + 3] = 255; }   // BGRx -> RGBA
        }
    b->Unlock(); return true;
}

bool Video::frameAt(double t, bool loop) {
    if (!reader) return false;
    t -= base; bool changed = false;
    for (int guard = 0; guard < 8 && !ended && nextTime < t; guard++) {   // the first frame at or after t (at most a few a call: it catches up)
        double ts = 0; if (readOne(ts)) { changed = true; nextTime = ts; }
    }
    if (ended && loop) {   // back to the start
        PROPVARIANT v; PropVariantInit(&v); v.vt = VT_I8; v.hVal.QuadPart = 0;
        reader->SetCurrentPosition(GUID_NULL, v); PropVariantClear(&v);
        ended = false; base += std::max(duration > 0 ? duration : nextTime, 0.1); nextTime = -1;
    }
    return changed;
}

} // namespace media
