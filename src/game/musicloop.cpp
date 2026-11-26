#include "musicloop.h"
#include "visors.h"   // (escText)
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace mc {

bool saveMusicLoops(const std::string& path, const MusicLoops& m) {
    std::ofstream f(path); if (!f) return false;
    f << "musicloops 1\n";
    for (const auto& kv : m.byFile) {
        const MusicLoop& L = kv.second;
        f << "track " << escText(kv.first) << " " << L.bpm << " " << L.beatsPerBar << " " << L.firstBar << " " << L.loopStart << " " << L.loopEnd << " " << L.seam << " " << (L.on ? 1 : 0) << "\n";
    }
    return (bool)f;
}
bool loadMusicLoops(const std::string& path, MusicLoops& m) {
    std::ifstream f(path); if (!f) return false;
    m = MusicLoops{}; std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k;
        if (k != "track") continue;
        std::string n; MusicLoop L; int on = 1;
        if (l >> n >> L.bpm >> L.beatsPerBar >> L.firstBar >> L.loopStart >> L.loopEnd >> L.seam >> on) {
            L.bpm = std::clamp(L.bpm, 0.f, 400.f); L.beatsPerBar = std::clamp(L.beatsPerBar, 1, 16); L.firstBar = std::max(0.f, L.firstBar);
            L.loopStart = std::max(0.f, L.loopStart); L.loopEnd = std::max(0.f, L.loopEnd); L.seam = std::clamp(L.seam, 0.f, 2.f); L.on = on != 0;
            m.byFile[unescText(n)] = L;
        }
    }
    return true;
}

float snapTime(float t, const MusicLoop& L, int mode) {
    if (mode <= 0 || L.bpm <= 0) return t;
    double step = mode == 1 ? L.beat() : L.bar(); if (step <= 1e-4) return t;
    double k = std::round((t - L.firstBar) / step);
    return (float)std::max(0.0, L.firstBar + k * step);
}

std::vector<float> monoOf(const std::vector<float>& in, int channels) {
    if (channels <= 1) return in;
    size_t frames = in.size() / channels; std::vector<float> out(frames);
    for (size_t i = 0; i < frames; i++) { float s = 0; for (int c = 0; c < channels; c++) s += in[i * channels + c]; out[i] = s / channels; }
    return out;
}

// ---- the tempo
static float at(const std::vector<float>& v, double x) {   // linear in between frames
    if (x < 0) return 0; size_t i = (size_t)x; if (i + 1 >= v.size()) return i < v.size() ? v[i] : 0;
    double f = x - (double)i; return (float)(v[i] * (1 - f) + v[i + 1] * f);
}
BpmGuess detectBpm(const float* x, size_t n, int rate, int bpb, float minB, float maxB) {
    BpmGuess g; if (!x || rate <= 0 || n < (size_t)rate * 4) return g;
    bpb = std::clamp(bpb, 1, 16); minB = std::max(20.f, minB); maxB = std::max(minB + 1, maxB);
    const int hop = std::max(1, rate / 100); const double fps = (double)rate / hop;   // (100 looks a second)
    size_t F = n / hop; if (F < 64) return g;
    // the sound in three bands (low: kicks, high: hats and clicks), their energy each look
    const double PI = 3.14159265358979;
    float aL = (float)(1 - std::exp(-2 * PI * 150 / rate)), aH = (float)(1 - std::exp(-2 * PI * 2500 / rate));
    std::vector<float> eL(F, 0), eM(F, 0), eH(F, 0); float lp = 0, lp2 = 0;
    for (size_t f = 0; f < F; f++) {
        double sL = 0, sM = 0, sH = 0;
        for (int k = 0; k < hop; k++) { float v = x[f * hop + k]; lp += aL * (v - lp); lp2 += aH * (v - lp2); float lo = lp, hi = v - lp2, mi = lp2 - lp; sL += lo * lo; sM += mi * mi; sH += hi * hi; }
        eL[f] = (float)std::log(sL / hop + 1e-9); eM[f] = (float)std::log(sM / hop + 1e-9); eH[f] = (float)std::log(sH / hop + 1e-9);
    }
    // where it gets louder suddenly (a hit): rising energy, the slow swell taken away
    std::vector<float> on(F, 0), onLow(F, 0);
    for (size_t f = 1; f < F; f++) { float dl = std::max(0.f, eL[f] - eL[f - 1]), dm = std::max(0.f, eM[f] - eM[f - 1]), dh = std::max(0.f, eH[f] - eH[f - 1]); on[f] = dl + dm + 1.5f * dh; onLow[f] = dl; }
    { const int W = (int)(fps * 0.5); std::vector<float> m(F); double s = 0; for (size_t f = 0; f < F; f++) { s += on[f]; if (f >= (size_t)W) s -= on[f - W]; m[f] = (float)(s / std::min<size_t>(f + 1, W)); }
      for (size_t f = 0; f < F; f++) on[f] = std::max(0.f, on[f] - m[f]); }
    size_t Fu = std::min(F, (size_t)(fps * 180));   // (the first three minutes are plenty)
    // how often: the lag at which the hits line up with themselves best (gently preferring tempos near 120)
    auto prior = [](double b) { double z = std::log2(b / 120.0) / 0.9; return std::exp(-0.5 * z * z); };
    int lag0 = (int)std::floor(fps * 60 / maxB), lag1 = (int)std::ceil(fps * 60 / minB); double best = -1; int bestLag = 0; double sumSc = 0; int nSc = 0;
    for (int lag = std::max(2, lag0); lag <= lag1 && (size_t)lag < Fu / 2; lag++) {
        double s = 0; for (size_t f = 0; f + lag < Fu; f++) s += on[f] * on[f + lag];
        s /= (double)(Fu - lag); double w = s * prior(fps * 60 / lag); sumSc += w; nSc++;
        if (w > best) { best = w; bestLag = lag; }
    }
    if (bestLag <= 0 || best <= 0) return g;
    // finer: every tempo near it, with the best place for its beats - the one whose beats land on the most hits
    auto comb = [&](double bpm, double& phase) {
        double P = fps * 60 / bpm, bestS = -1; int K = (int)((double)Fu / P) - 1; if (K < 4) return -1.0;
        for (double ph = 0; ph < P; ph += 0.5) { double s = 0; for (int k = 0; k < K; k++) s += at(on, ph + k * P); if (s > bestS) { bestS = s; phase = ph; } }
        return bestS / K;
    };
    double b0 = fps * 60 / bestLag, bestB = b0, bestPh = 0, bestScore = -1, startRatio = 0, startFrame = 0; bool startAligned = false;
    for (double b = b0 * 0.96; b <= b0 * 1.04; b += 0.05) { double ph = 0; double s = comb(b, ph) * prior(b); if (s > bestScore) { bestScore = s; bestB = b; bestPh = ph; } }
    for (double mul : { 0.5, 2.0 }) {   // twice as fast or half as fast: the stronger beat wins (with the gentle preference)
        double b = bestB * mul; if (b < minB || b > maxB) continue; double ph = 0; double s = comb(b, ph) * prior(b);
        if (s > bestScore * 1.05) { bestScore = s; bestB = b; bestPh = ph; }
    }
    double P = fps * 60 / bestB;
    {   // music usually starts on bar 1: where it starts (the first clear sound) wins if its grid fits the hits nearly as well
        float peak = 0; for (size_t f = 0; f < std::min(F, (size_t)(fps * 10)); f++) peak = std::max(peak, std::max(eL[f], std::max(eM[f], eH[f])));
        size_t f0 = 0; float quiet = peak - 9.2f;   // (about 40 dB under the loudest: the sound has started)
        while (f0 < F && std::max(eL[f0], std::max(eM[f0], eH[f0])) < quiet) f0++;
        int K = (int)((double)Fu / P) - 1; double sStart = 0, sBest = 0;
        for (int k = 0; k < K; k++) { sStart += at(on, std::fmod((double)f0, P) + k * P); sBest += at(on, bestPh + k * P); }
        startRatio = sBest > 0 ? sStart / sBest : 0;
        if (startRatio >= 0.5) { bestPh = std::fmod((double)f0, P); startAligned = true; startFrame = (double)f0; }
    }
    // where bar 1 is: of the beats in a bar, the one with the heaviest low hits (or where the music starts)
    int K = (int)((double)Fu / (P * bpb)) - 1; int bestJ = 0; double bestJS = -1;
    for (int j = 0; j < bpb && K > 1; j++) { double s = 0; for (int k = 0; k < K; k++) { double fr = bestPh + (j + k * bpb) * P; s += at(onLow, fr) * 2 + at(on, fr); } if (s > bestJS) { bestJS = s; bestJ = j; } }
    if (startAligned) bestJ = (int)std::llround((startFrame - bestPh) / P) % bpb;   // (bar 1 where the music starts)
    double first = (bestPh + bestJ * P) / fps, bar = 60.0 / bestB * bpb; first = std::fmod(first, bar); if (first < 0) first += bar;
    g.bpm = (float)(std::round(bestB * 100) / 100); g.firstBar = (float)first; g.startRatio = (float)startRatio;
    double mean = nSc ? sumSc / nSc : 0; g.confidence = (float)std::clamp(best > 0 ? (best - mean) / best : 0.0, 0.0, 1.0);
    return g;
}

// ---- the loop as it plays
LoopFrames loopFrames(const MusicLoop& L, size_t frames, int rate) {
    LoopFrames lf; if (!frames || rate <= 0) return lf;
    double s = std::max(0.0, (double)L.loopStart * rate), e = L.loopEnd > 0 ? (double)L.loopEnd * rate : (double)frames;
    lf.start = (uint32_t)std::min<double>(std::llround(s), frames - 1); lf.end = (uint32_t)std::clamp<double>(std::llround(e), lf.start + 1.0, (double)frames);
    lf.ok = L.on && lf.end > lf.start + (uint32_t)(rate * 0.05);
    lf.seam = std::min((float)L.seam, (float)(lf.end - lf.start) / rate * 0.5f);
    return lf;
}
void bakeLoop(const std::vector<float>& in, int ch, int rate, const MusicLoop& L, std::vector<float>& out, LoopFrames& lf) {
    ch = std::max(1, ch); size_t frames = in.size() / ch; lf = loopFrames(L, frames, rate);
    if (!lf.ok) { out = in; lf.start = 0; lf.end = (uint32_t)frames; lf.seam = 0; return; }
    out.assign(in.begin(), in.begin() + (size_t)lf.end * ch);
    size_t S = (size_t)std::llround((double)lf.seam * rate);
    if (S < 2) { lf.seam = 0; return; }
    const double PI = 3.14159265358979;
    if (lf.start >= S) {   // the end blends into what plays just before the start: at the jump, it is already there
        for (size_t i = 0; i < S; i++) {
            double gg = (i + 0.5) / S, fin = std::sin(gg * PI / 2), fout = std::cos(gg * PI / 2);
            size_t e = lf.end - S + i, s = lf.start - S + i;
            for (int c = 0; c < ch; c++) out[e * ch + c] = (float)(in[e * ch + c] * fout + in[s * ch + c] * fin);
        }
    } else if (frames >= lf.end + S) {   // nothing before the start: what plays after the end (its tail) blends into the loop's start
        for (size_t i = 0; i < S; i++) {
            double gg = (i + 0.5) / S, fin = std::sin(gg * PI / 2), fout = std::cos(gg * PI / 2);
            size_t s = lf.start + i, t = lf.end + i;
            for (int c = 0; c < ch; c++) out[s * ch + c] = (float)(in[s * ch + c] * fin + in[t * ch + c] * fout);
        }
    } else {   // short on both sides: as long as there is room before the start
        size_t S2 = lf.start; if (S2 < 2) { lf.seam = 0; return; }
        MusicLoop L2 = L; L2.seam = (float)S2 / rate; bakeLoop(in, ch, rate, L2, out, lf); return;
    }
}
static double rmsOf(const std::vector<float>& m, size_t a, size_t b) { if (b <= a || b > m.size()) return 0; double s = 0; for (size_t i = a; i < b; i++) s += (double)m[i] * m[i]; return std::sqrt(s / (b - a)); }
static double corrOf(const std::vector<float>& m, size_t a, size_t b, size_t n) {
    if (a + n > m.size() || b + n > m.size() || !n) return 0; double ab = 0, aa = 0, bb = 0;
    for (size_t i = 0; i < n; i++) { ab += (double)m[a + i] * m[b + i]; aa += (double)m[a + i] * m[a + i]; bb += (double)m[b + i] * m[b + i]; }
    return aa > 1e-12 && bb > 1e-12 ? ab / std::sqrt(aa * bb) : 0;
}
float autoSeam(const std::vector<float>& in, int ch, int rate, const MusicLoop& L) {
    std::vector<float> m = monoOf(in, ch); LoopFrames lf = loopFrames(L, m.size(), rate); if (!lf.ok) return 0;
    size_t w = (size_t)(rate * 0.1);
    // does what comes before / after the end sound like what comes before / after the start? (then the jump is hardly a change)
    double cPre = lf.start >= w ? corrOf(m, lf.start - w, lf.end - w, w) : 0, cPost = lf.end + w <= m.size() ? corrOf(m, lf.start, lf.end, w) : 0;
    double c = std::max(0.0, std::max(cPre, cPost));
    size_t t = (size_t)(rate * 0.3);
    double before = rmsOf(m, lf.end > t ? lf.end - t : 0, lf.end), after = rmsOf(m, lf.end, std::min(m.size(), (size_t)lf.end + t)), atStart = rmsOf(m, lf.start, std::min(m.size(), (size_t)lf.start + t));
    double tail = before > 1e-6 ? std::min(1.0, after / before) : 0;                               // something rings on past the end (it would be cut off)
    double levels = before > 1e-6 && atStart > 1e-6 ? std::min(1.0, std::fabs(20 * std::log10(atStart / before)) / 12) : 0;   // a jump in loudness
    double s = 0.01 + (1 - c) * (0.08 + 0.2 * tail + 0.12 * levels);
    float lenCap = (float)(lf.end - lf.start) / rate * 0.5f;
    return (float)std::min<double>(std::clamp(std::round(s * 1000) / 1000, 0.005, 0.5), lenCap);
}
float seamJump(const std::vector<float>& in, int ch, int rate, const MusicLoop& L) {
    ch = std::max(1, ch); size_t frames = in.size() / ch; LoopFrames lf = loopFrames(L, frames, rate); if (!lf.ok || lf.end < 2) return 0;
    size_t w = std::max<size_t>(8, (size_t)(rate * 0.01)); double worst = 0;
    for (int c = 0; c < ch; c++) {
        double typ = 0; size_t a = lf.end > w + 1 ? lf.end - w : 1;
        for (size_t i = a; i < lf.end; i++) typ += std::fabs(in[i * ch + c] - in[(i - 1) * ch + c]);
        typ /= std::max<size_t>(1, lf.end - a);
        double jump = std::fabs(in[(lf.end - 1) * ch + c] - in[(size_t)lf.start * ch + c]);
        worst = std::max(worst, jump / std::max(typ * 3, 1e-4));
    }
    return (float)worst;
}

} // namespace mc
