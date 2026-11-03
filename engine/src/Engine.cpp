// Sanctuary – computed worship pad and drone instrument
// Copyright (C) 2026 Sanctuary contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Signal flow (per voice -> layer buses -> global):
//   Warm        5 band-limited wavetable oscillators (triangle..saw), detuned, slow random drift,
//               spread across the stereo field            -> bus low-pass (Brightness, Motion)
//   Air         white noise through two comb filters tuned to the note (L/R decorrelated)
//                                                          -> bus band-pass (700 Hz .. Brightness)
//   Shimmer     sine partials at 2f, 3f, 4f, 6f that bloom and fade slowly (no pitch modulation)
//   Foundation  organ-like fundamental + sine sub octave for low notes, centred
//   -> expression -> dry + 16-line FDN hall (with optional octave-up feedback) -> volume
//   -> DC blocker -> soft safety limiter (only above -2 dBFS)
// Everything is computed at audio rate, control values every <= 32 samples with linear ramps.
#include "sanctuary/Engine.h"
#include "Hall.h"
#include "Fft.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace sanctuary {

namespace {

constexpr int kCtl = 32;            // control-rate chunk
constexpr int kMaxVoices = 32;
constexpr int kSoftVoiceLimit = 24; // above this, the quietest released voice is faded out quickly
constexpr int kUnison = 5;
constexpr int kPartials = 4;
constexpr int kTableBits = 12;
constexpr int kTable = 1 << kTableBits;
constexpr int kLevels = 11;         // mip levels: level j holds fundamentals up to 27.5 Hz * 2^j
constexpr float kPi = 3.14159265358979f;
constexpr float kSideH1 = 0.15f, kSideH2 = 0.35f;
constexpr float kAnchorGain = 0.62f, kSideGain = 0.5f;
constexpr float kPhaseToFrac = 1.f / (float)(1u << (32 - kTableBits));

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 1) : s(seed * 2654435761u + 12345u) {}
    inline uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float uni() { return (next() >> 8) * (1.f / 16777216.f); }       // 0..1
    inline float bi() { return uni() * 2.f - 1.f; }                          // -1..1
};

// Smooth random walk: moves between random targets with smoothstep interpolation, so value and
// slope are continuous (no corners -> no chirps when it drives pitch, no clicks for gain).
struct Drift {
    float from = 0, to = 0, pos = 0, rate = 0.3f, value = 0;
    void init(Rng& r, float pMin, float pMax)
    {
        from = r.bi(); to = r.bi(); pos = r.uni();
        rate = 1.f / (pMin + r.uni() * (pMax - pMin));
        step(r, 0.f, pMin, pMax);
    }
    inline void step(Rng& r, float dt, float pMin, float pMax)
    {
        pos += dt * rate;
        if (pos >= 1.f) {
            pos -= 1.f; if (pos > 1.f) pos = 0.f;
            from = to; to = r.bi();
            rate = 1.f / (pMin + r.uni() * (pMax - pMin));
        }
        const float s = pos * pos * (3.f - 2.f * pos);
        value = from + (to - from) * s;
    }
};

// Topology-preserving state-variable filter (Zavalishin / Simper), coefficients per chunk.
struct Svf {
    float ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0, k = 1.414f;
    void set(float fc, float q, float sr)
    {
        fc = std::min(fc, 0.45f * sr);
        const float g = std::tan(kPi * fc / sr);
        k = 1.f / q; a1 = 1.f / (1.f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void copyCoeffs(const Svf& o) { a1 = o.a1; a2 = o.a2; a3 = o.a3; k = o.k; }
    inline float lp(float v0)
    {
        const float v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1; ic2 = 2.f * v2 - ic2; return v2;
    }
    inline float hp(float v0)
    {
        const float v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1; ic2 = 2.f * v2 - ic2; return v0 - k * v1 - v2;
    }
    void clear() { ic1 = ic2 = 0; }
};

struct Tables {
    // kLevels x (kTable + 1). "Side" tables have a weaker fundamental and 2nd harmonic: the detuned
    // ensemble members add texture while the centre oscillator anchors the fundamental, so the pad
    // does not swell and dip by itself (equal detuned oscillators beat like random phasors).
    std::vector<float> tri, saw, triSide, sawSide;
    std::vector<float> organ, sine; // kTable + 1
    double sr = 0;

    static void additive(std::vector<float>& out, size_t at, const std::vector<float>& amp, Fft& fft)
    {
        std::vector<float> re(kTable, 0.f), im(kTable, 0.f);
        const int H = (int)amp.size() - 1;
        for (int n = 1; n <= H && n < kTable / 2; ++n) {
            const float sigma = n == 0 ? 1.f : std::sin(kPi * n / (H + 1)) / (kPi * n / (H + 1));   // Lanczos: no Gibbs ringing
            im[n] = -0.5f * amp[n] * sigma; im[kTable - n] = 0.5f * amp[n] * sigma;
        }
        fft.transform(re.data(), im.data(), true);
        for (int i = 0; i < kTable; ++i) out[at + i] = re[i];
        out[at + kTable] = re[0];
    }

    void build(double sampleRate)
    {
        if (sr == sampleRate) return;
        sr = sampleRate;
        Fft fft(kTable);
        const double cap = std::min(0.45 * sr, 15000.0);   // nothing above 15 kHz: warm, never fizzy
        tri.assign((size_t)kLevels * (kTable + 1), 0.f); saw = tri; triSide = tri; sawSide = tri;
        for (int j = 0; j < kLevels; ++j) {
            const double fmax = 27.5 * std::pow(2.0, j);
            const int H = std::max(1, std::min(kTable / 2 - 1, (int)(cap / fmax)));
            std::vector<float> a(H + 1, 0.f), b(H + 1, 0.f);
            for (int n = 1; n <= H; ++n) {
                a[n] = (n & 1) ? ((((n - 1) / 2) & 1) ? -1.f : 1.f) / (float)(n * n) : 0.f;
                b[n] = 1.f / (float)n;
            }
            additive(tri, (size_t)j * (kTable + 1), a, fft);
            additive(saw, (size_t)j * (kTable + 1), b, fft);
            a[1] *= kSideH1; b[1] *= kSideH1;
            if (H >= 2) { a[2] *= kSideH2; b[2] *= kSideH2; }
            additive(triSide, (size_t)j * (kTable + 1), a, fft);
            additive(sawSide, (size_t)j * (kTable + 1), b, fft);
        }
        auto normalise = [](std::vector<float>& t, std::vector<float>& side) {   // equal RMS (richest level)
            double e = 0; for (int i = 0; i < kTable; ++i) e += (double)t[i] * t[i];
            const float g = (float)(0.5 / std::sqrt(e / kTable));
            for (auto& x : t) x *= g;
            for (auto& x : side) x *= g;
        };
        normalise(tri, triSide); normalise(saw, sawSide);
        organ.assign(kTable + 1, 0.f); sine.assign(kTable + 1, 0.f);
        additive(organ, 0, { 0.f, 1.f, 0.32f, 0.11f, 0.04f }, fft);
        additive(sine, 0, { 0.f, 1.f }, fft);
    }

    static int level(float f)
    {
        const float x = std::log2(std::max(f * 1.03f, 1.f) / 27.5f);
        return std::max(0, std::min(kLevels - 1, (int)std::ceil(x)));
    }
};

inline float lookup(const float* t, uint32_t ph)
{
    const uint32_t i = ph >> (32 - kTableBits);
    const float fr = (float)(int32_t)(ph & ((1u << (32 - kTableBits)) - 1)) * kPhaseToFrac;   // signed: one cvt
    return t[i] + fr * (t[i + 1] - t[i]);
}

inline uint32_t phaseInc(double f, double sr) { return (uint32_t)(int64_t)(f / sr * 4294967296.0); }

inline float centsToRatio(float c)   // |c| < 60: 3rd-order series, error < 1e-6
{
    const float x = c * (0.69314718f / 1200.f);
    return 1.f + x * (1.f + x * (0.5f + x * (1.f / 6.f)));
}

inline float midiToHz(float k) { return 440.f * std::exp2((k - 69.f) / 12.f); }

} // namespace

// ------------------------------------------------------------------------------------------ voice
struct Voice {
    bool active = false;
    int key = 0;
    float freq = 0, velGain = 1, trim = 1;
    uint32_t age = 0;
    // envelope
    bool drone = false;
    bool gate = false, sustained = false, releasing = false;
    float attackPos = 0, env = 0, fastRelease = 0;      // play mode
    float fade = 0; int fadeDir = 0; float fadeSec = 0; // drone mode (gain = sin(fade * pi/2))
    // warm
    std::array<uint32_t, kUnison> ph {}, inc {};
    std::array<float, kUnison> cents {}, panPos {}, gL {}, gR {};
    std::array<Drift, kUnison> drift;
    int level = 0;
    float panWidth = -1;
    // air (comb per channel)
    std::vector<float> combL, combR;
    int combMask = 0, combPos = 0;
    float combLpL = 0, combLpR = 0, combDelay = 100;
    Drift airMove;
    // shimmer
    std::array<float, kPartials> sC {}, sS {}, sRc {}, sRs {};   // quadrature oscillators (cos, sin, rotation)
    std::array<bool, kPartials> sOn {};
    std::array<float, kPartials> sPan {}, sAmp {}, sBase {}, sGL {}, sGR {};
    std::array<Drift, kPartials> bloom;
    std::array<float, kPartials> ping {}, pingPeak {}, pingTau {}, pingAtk {};   // sparkle: bell-like lights
    // foundation
    uint32_t fPh = 0, fInc = 0, subPh = 0, subInc = 0;
    float fW = 0, subW = 0;

    float gainAt() const
    {
        if (drone) return std::sin(fade * 0.5f * kPi);
        return env;
    }
};

// ------------------------------------------------------------------------------------------ engine
struct Engine::Impl {
    double sr = 48000;
    Rng rng;
    Tables tab;
    std::array<Voice, kMaxVoices> voices;
    uint32_t ageCounter = 0;
    // warm tables blended for the current Shape (one lookup per oscillator), rebuilt per level on demand
    std::vector<float> warmC, warmS;
    std::array<float, kLevels> builtShape {};
    std::vector<float> shWin;    // pitch-shifter window

    Params target;          // as set by the host
    Params cur;             // smoothed copy
    bool first = true;
    Mode mode = Mode::Drone;
    Colour colour = Colour::Open;
    int droneKey = -1;
    bool sustain = false;
    float exprTarget = 1.f, expr = 1.f;

    // global modulation
    Drift cutoffLfo, airLfo;
    // buses
    Svf warmLp1L, warmLp1R, warmLp2L, warmLp2R, warmHpL, warmHpR;
    Svf airHpL, airHpR, airLpL, airLpR, airLp2L, airLp2R;
    Svf sendHp, shLp1, shLp2, shHp, shPostLp;
    Hall hall;
    float hallRt = -1;
    // shimmer pitch shifter (octave up)
    std::vector<float> shBuf;
    int shMask = 0, shW = 4800, shWrite = 0, shCount = 0;
    float shFb = 0.f;
    float noiseLpL = 0.f, noiseLpR = 0.f;
    // output
    float dcL = 0, dcR = 0, dcInL = 0, dcInR = 0, dcCoef = 0.999f;
    double limited = 0, limitCount = 0;
    int silentSamples = 0;     // idle bypass: no voices and the tail has died away
    // ramps (previous chunk end values)
    float gWarm = 0, gAir = 0, gShim = 0, gFound = 0, gDry = 1, gWet = 0, gOut = 0, gExpr = 1, gShFb = 0;

    explicit Impl(uint32_t seed) : rng(seed) {}

    void blendLevel(int j, float shape)
    {
        const size_t o = (size_t)j * (kTable + 1);
        const float* a = &tab.tri[o]; const float* b = &tab.saw[o];
        const float* as = &tab.triSide[o]; const float* bs = &tab.sawSide[o];
        float* c = &warmC[o]; float* s = &warmS[o];
        for (int i = 0; i <= kTable; ++i) { c[i] = a[i] + shape * (b[i] - a[i]); s[i] = as[i] + shape * (bs[i] - as[i]); }
        builtShape[j] = shape;
    }

    // ---------------------------------------------------------------- voices
    Voice* allocVoice()
    {
        int nActive = 0;
        for (auto& v : voices) nActive += v.active;
        if (nActive >= kSoftVoiceLimit) {
            // fade out the quietest voice that is already on its way out (or the oldest one)
            Voice* q = nullptr; float best = 1e9f;
            for (auto& v : voices) {
                if (!v.active || v.fastRelease > 0) continue;
                const bool leaving = v.drone ? v.fadeDir < 0 : v.releasing;
                const float score = v.gainAt() + (leaving ? 0.f : 10.f) - 1e-7f * (float)(ageCounter - v.age);
                if (score < best) { best = score; q = &v; }
            }
            if (q) { q->fastRelease = 0.1f; q->releasing = true; if (q->drone) q->fadeDir = -1; }
        }
        for (auto& v : voices) if (!v.active) return &v;
        // pool exhausted (extreme): reuse the quietest voice
        Voice* q = &voices[0];
        for (auto& v : voices) if (v.gainAt() < q->gainAt()) q = &v;
        return q;
    }

    void startVoice(Voice& v, int key, float vel, bool drone)
    {
        v.active = true; v.key = key; v.age = ++ageCounter;
        v.freq = midiToHz((float)key);
        v.velGain = drone ? 1.f : 0.55f + 0.45f * vel;
        v.drone = drone;
        v.gate = true; v.sustained = false; v.releasing = false; v.fastRelease = 0;
        v.attackPos = 0; v.env = 0;
        v.fade = 0; v.fadeDir = drone ? 1 : 0; v.fadeSec = 0;
        v.level = Tables::level(v.freq * std::exp2(30.f / 1200.f));
        // ensemble: symmetric spread with a little randomness, random start phases (no flam / click)
        // oscillator 2 is the anchor: in tune, in the middle; the others spread in pitch and space
        static const float spread[kUnison] = { -1.f, -0.5f, 0.f, 0.5f, 1.f };
        static const float pans[4] = { -1.f, -0.45f, 0.45f, 1.f };
        const int rot = (int)(rng.next() % 4);
        const bool flip = rng.next() & 1;
        for (int u = 0, k = 0; u < kUnison; ++u) {
            v.cents[u] = spread[u] * (0.85f + 0.3f * rng.uni());
            v.panPos[u] = u == 2 ? 0.f : pans[(k++ * 3 + rot) % 4] * (flip ? -1.f : 1.f);
            v.ph[u] = rng.next();
            v.drift[u].init(rng, 1.5f, 4.5f);
        }
        v.panWidth = -1;
        // air comb
        std::fill(v.combL.begin(), v.combL.end(), 0.f); std::fill(v.combR.begin(), v.combR.end(), 0.f);
        v.combPos = 0; v.combLpL = v.combLpR = 0;
        v.combDelay = std::min((float)(sr / v.freq), (float)(v.combMask - 4));
        v.airMove.init(rng, 2.f, 6.f);
        // shimmer partials: octave, octave+fifth, two octaves, two octaves+fifth
        static const float ratio[kPartials] = { 2.f, 3.f, 4.f, 6.f };
        static const float base[kPartials] = { 1.f, 0.55f, 0.5f, 0.28f };
        for (int p = 0; p < kPartials; ++p) {
            const float f = v.freq * ratio[p] * std::exp2((rng.bi() * 1.2f) / 1200.f);  // ±1.2 cent: slow, gentle beating
            v.sOn[p] = f < 0.45f * (float)sr;
            const double w = 2.0 * M_PI * f / sr, ph0 = rng.uni() * 2.0 * M_PI;
            v.sRc[p] = (float)std::cos(w); v.sRs[p] = (float)std::sin(w);
            v.sC[p] = (float)std::cos(ph0); v.sS[p] = (float)std::sin(ph0);
            // fade the partials out between 3 and 9 kHz: shimmer without hiss or harshness
            const float hf = f < 1200.f ? 1.f : f > 3600.f ? 0.f : 0.5f + 0.5f * std::cos(kPi * (f - 1200.f) / 2400.f);   // glassy, never hissy
            v.sBase[p] = base[p] * hf;
            v.sPan[p] = rng.bi() * 0.9f; v.sGL[p] = v.sGR[p] = 0.70710678f;
            v.bloom[p].init(rng, 4.f, 11.f);
            v.sAmp[p] = 0;
            v.ping[p] = 0.f; v.pingPeak[p] = 0.f; v.pingAtk[p] = 0.f; v.pingTau[p] = 1.f;
        }
        // foundation: fundamental fades out above C5, sub octave only for low notes (and never below 30 Hz)
        v.fInc = phaseInc(v.freq * centsToRatio(0.f), sr);
        v.subInc = phaseInc(v.freq * 0.5, sr);
        v.fPh = v.ph[2]; v.subPh = v.fPh >> 1;   // phase-locked to the warm anchor: they add, never beat
        v.fW = std::clamp((84.f - key) / 24.f, 0.f, 1.f);
        v.subW = v.freq * 0.5f < 30.f ? 0.f : std::clamp((60.f - key) / 18.f, 0.f, 1.f);
        v.trim = 1.f;
    }

    void releaseVoice(Voice& v)
    {
        v.gate = false; v.sustained = false;
        if (v.drone) { v.fadeDir = -1; }
        else v.releasing = true;
    }

    // ---------------------------------------------------------------- drone
    void droneChord(int pc, int* keys, int& n) const
    {
        const int root = 40 + ((pc - 4 + 12) % 12);   // E2 .. D#3: low enough to carry, high enough to stay clear
        n = 0;
        keys[n++] = root; keys[n++] = root + 7; keys[n++] = root + 12;
        if (colour == Colour::Sus2) keys[n++] = root + 14;
        if (colour == Colour::Add9) keys[n++] = root + 26;
    }

    void fadeOutDrone(float seconds)
    {
        for (auto& v : voices)
            if (v.active && v.drone && v.fadeDir >= 0) { v.fadeDir = -1; v.fadeSec = seconds; }
    }

    void droneSelect(int pc, bool toggle)
    {
        pc = ((pc % 12) + 12) % 12;
        if (toggle && pc == droneKey) { fadeOutDrone(0.f); droneKey = -1; return; }
        fadeOutDrone(0.f);
        droneKey = pc;
        int keys[6], n;
        droneChord(pc, keys, n);
        for (int i = 0; i < n; ++i) startVoice(*allocVoice(), keys[i], 1.f, true);
    }

    void allOff()
    {
        for (auto& v : voices) {
            if (!v.active) continue;
            v.gate = false; v.sustained = false;
            if (v.drone) { v.fadeDir = -1; v.fadeSec = 0.3f; }
            else { v.releasing = true; v.fastRelease = 0.12f; }
        }
        droneKey = -1; sustain = false;
    }

    // ---------------------------------------------------------------- events
    void handle(const Event& e)
    {
        switch (e.type) {
        case Event::NoteOn:
            if (mode == Mode::Drone) { droneSelect(e.key % 12, true); break; }
            for (auto& v : voices)                      // re-strike: the old voice releases underneath
                if (v.active && !v.drone && v.key == e.key && !v.releasing) releaseVoice(v);
            startVoice(*allocVoice(), e.key, e.value, false);
            break;
        case Event::NoteOff:
            if (mode == Mode::Drone) break;
            for (auto& v : voices)
                if (v.active && !v.drone && v.key == e.key && v.gate) {
                    if (sustain) { v.gate = false; v.sustained = true; } else releaseVoice(v);
                }
            break;
        case Event::Sustain:
            sustain = e.value >= 0.5f;
            if (!sustain)
                for (auto& v : voices) if (v.active && v.sustained) releaseVoice(v);
            break;
        case Event::Expression: exprTarget = std::clamp(e.value, 0.f, 1.f); break;
        case Event::AllOff: allOff(); break;
        case Event::DroneKey: if (mode == Mode::Drone) droneSelect(e.key, true); break;
        }
    }

    void applyModeAndColour()
    {
        if (target.mode != mode) {
            // switching modes: everything sounding fades out naturally
            for (auto& v : voices) if (v.active) { v.gate = false; v.sustained = false; if (v.drone) v.fadeDir = -1; else v.releasing = true; }
            droneKey = -1; mode = target.mode;
        }
        if (target.colour != colour) {
            colour = target.colour;
            if (mode == Mode::Drone && droneKey >= 0) droneSelect(droneKey, false);   // re-voice with a crossfade
        }
    }

    // ---------------------------------------------------------------- rendering
    void smoothParams(float dt)
    {
        const float a = first ? 1.f : 1.f - std::exp(-dt / 0.06f);
        auto sm = [a](float& c, float t) { c += a * (t - c); };
        sm(cur.warm, target.warm); sm(cur.air, target.air); sm(cur.shimmer, target.shimmer); sm(cur.sparkle, target.sparkle); sm(cur.foundation, target.foundation);
        sm(cur.shape, target.shape); sm(cur.detune, target.detune); sm(cur.brightness, target.brightness);
        sm(cur.motion, target.motion); sm(cur.width, target.width); sm(cur.size, target.size);
        sm(cur.reverbMix, target.reverbMix); sm(cur.shimmerVerb, target.shimmerVerb); sm(cur.gainDb, target.gainDb);
        sm(cur.expression, target.expression);
        cur.attack = target.attack; cur.release = target.release; cur.crossfade = target.crossfade;
        const float ae = first ? 1.f : 1.f - std::exp(-dt / 0.025f);       // pedal: fast but click-free
        expr += ae * (exprTarget * cur.expression - expr);
        first = false;
    }

    bool onWarm = true, onAir = true, onShim = true, onFound = true, onShFb = true;

    void renderVoice(Voice& v, int n, float dt, float* wL, float* wR, float* aL, float* aR, float* sL, float* sR, float* fM,
                     const float* nL, const float* nR)
    {
        // ---- envelope (start/end of chunk, linear in between)
        const float g0 = v.gainAt();
        if (v.drone) {
            const float sec = v.fastRelease > 0 ? v.fastRelease : v.fadeSec > 0 ? v.fadeSec : cur.crossfade;
            v.fade = std::clamp(v.fade + (float)v.fadeDir * dt / std::max(0.01f, sec), 0.f, 1.f);
            if (v.fade >= 1.f && v.fadeDir > 0) v.fadeDir = 0;
        } else if (v.releasing) {
            const float tau = v.fastRelease > 0 ? v.fastRelease / 4.f : std::max(0.02f, cur.release) / 6.9f;
            v.env *= std::exp(-dt / tau);
        } else if (v.attackPos < 1.f) {
            v.attackPos = std::min(1.f, v.attackPos + dt / std::max(0.005f, cur.attack));
            v.env = 0.5f - 0.5f * std::cos(kPi * v.attackPos);    // raised cosine: soft start, soft landing
        }
        const bool done = v.drone ? (v.fadeDir < 0 && v.fade <= 0.f) : (v.releasing && v.env < 3e-4f);
        const float g1 = done ? 0.f : v.gainAt();          // the last chunk lands exactly on zero
        const float k = v.velGain * v.trim;
        float env[kCtl];
        const float dg = (g1 - g0) / (float)n;
        for (int i = 0; i < n; ++i) env[i] = k * (g0 + dg * (float)(i + 1));

        // ---- warm ensemble
        const float motion = cur.motion;
        const float spreadCents = 25.f * cur.detune * cur.detune + 3.f * cur.detune;
        const float driftCents = 1.f + 7.f * motion;
        if (std::fabs(v.panWidth - cur.width) > 0.002f) {
            v.panWidth = cur.width;
            for (int u = 0; u < kUnison; ++u) {
                const float p = std::clamp(v.panPos[u] * cur.width, -1.f, 1.f);
                v.gL[u] = std::cos((p + 1.f) * 0.25f * kPi); v.gR[u] = std::sin((p + 1.f) * 0.25f * kPi);
            }
            for (int p = 0; p < kPartials; ++p) {
                const float pan = std::clamp(v.sPan[p] * cur.width, -1.f, 1.f);
                v.sGL[p] = std::cos((pan + 1.f) * 0.25f * kPi); v.sGR[p] = std::sin((pan + 1.f) * 0.25f * kPi);
            }
        }
        if (onWarm) {
            if (std::fabs(builtShape[v.level] - cur.shape) > 0.004f) blendLevel(v.level, cur.shape);
            const float* tc = &warmC[(size_t)v.level * (kTable + 1)];
            const float* ts = &warmS[(size_t)v.level * (kTable + 1)];
            static_assert(kUnison == 5, "the loop below is written for anchor + 4 sides");
            float gl[kUnison], gr[kUnison];
            for (int u = 0; u < kUnison; ++u) {
                v.drift[u].step(rng, dt, 1.5f, 4.5f);
                const float c = u == 2 ? 0.f : v.cents[u] * spreadCents + v.drift[u].value * driftCents;   // anchor stays put
                v.inc[u] = phaseInc(v.freq * centsToRatio(c), sr);
                const float w = u == 2 ? kAnchorGain : kSideGain;
                gl[u] = v.gL[u] * w; gr[u] = v.gR[u] * w;
            }
            // one pass for all five oscillators: phases stay in registers, the bus is touched once
            uint32_t p0 = v.ph[0], p1 = v.ph[1], p2 = v.ph[2], p3 = v.ph[3], p4 = v.ph[4];
            const uint32_t i0 = v.inc[0], i1 = v.inc[1], i2 = v.inc[2], i3 = v.inc[3], i4 = v.inc[4];
            for (int i = 0; i < n; ++i) {
                const float s0 = lookup(ts, p0), s1 = lookup(ts, p1), s2 = lookup(tc, p2), s3 = lookup(ts, p3), s4 = lookup(ts, p4);
                const float l = gl[0] * s0 + gl[1] * s1 + gl[2] * s2 + gl[3] * s3 + gl[4] * s4;
                const float r = gr[0] * s0 + gr[1] * s1 + gr[2] * s2 + gr[3] * s3 + gr[4] * s4;
                wL[i] += l * env[i]; wR[i] += r * env[i];
                p0 += i0; p1 += i1; p2 += i2; p3 += i3; p4 += i4;
            }
            v.ph[0] = p0; v.ph[1] = p1; v.ph[2] = p2; v.ph[3] = p3; v.ph[4] = p4;
        }

        // ---- air: noise through combs tuned to the note (feedback 0.88: clearly pitched, still breathy)
        v.airMove.step(rng, dt, 2.f, 6.f);
        if (onAir) {
            const float fb = 0.88f, inG = 0.47f;        // sqrt(1 - fb^2)
            const float mv = 1.f + 0.45f * motion * v.airMove.value;
            const float D = v.combDelay;
            float* bl = v.combL.data(); float* br = v.combR.data();
            const int mask = v.combMask;
            int w = v.combPos;
            float lpl = v.combLpL, lpr = v.combLpR;
            const int di = (int)D; const float df = D - (float)di;
            for (int i = 0; i < n; ++i) {
                const int r0 = (w - di) & mask, r1 = (w - di - 1) & mask;
                const float yl = bl[r0] + df * (bl[r1] - bl[r0]);
                const float yr = br[r0] + df * (br[r1] - br[r0]);
                lpl += 0.45f * (yl - lpl); lpr += 0.45f * (yr - lpr);    // loop damping: a soft, round breath
                const float ol = inG * nL[i] + fb * lpl, orr = inG * nR[i] + fb * lpr;
                bl[w] = ol; br[w] = orr;
                w = (w + 1) & mask;
                const float g = env[i] * mv;
                aL[i] += g * ol; aR[i] += g * orr;
            }
            v.combPos = w; v.combLpL = lpl; v.combLpR = lpr;
        }

        // ---- shimmer partials: each blooms and fades on its own slow cycle (amplitude only)
        if (onShim) {
            const float bright = 0.55f + 0.9f * cur.brightness;
            static_assert(kPartials == 4, "the loop below is written for 4 partials");
            float al0[kPartials], ar0[kPartials], dl[kPartials], dr[kPartials];
            for (int p = 0; p < kPartials; ++p) {
                v.bloom[p].step(rng, dt, 4.f + 6.f * (1.f - motion), 9.f + 8.f * (1.f - motion));
                const float b = 0.5f + 0.5f * v.bloom[p].value;              // 0..1
                const float bloom = (1.f - motion) * 0.6f + motion * (b * b * 1.6f);
                // sparkle: now and then a partial lights up quickly and fades like a small bell
                const float sp = cur.sparkle;
                if (sp > 0.f) {
                    if (v.pingAtk[p] > 0.f) {
                        v.ping[p] += (v.pingPeak[p] - v.ping[p]) * (1.f - std::exp(-dt / 0.012f));
                        v.pingAtk[p] -= dt;
                    } else v.ping[p] *= std::exp(-dt / v.pingTau[p]);
                    const float rate = sp * (0.12f + 0.45f * motion);          // lights per second per partial
                    if (rng.uni() < rate * dt) {
                        v.pingPeak[p] = 0.6f + 0.6f * rng.uni(); v.pingAtk[p] = 0.035f;
                        v.pingTau[p] = (0.5f + 1.5f * rng.uni()) * (p >= 2 ? 0.7f : 1.f);
                    }
                }
                const float level = (1.f - 0.7f * sp) * bloom + sp * 2.2f * v.ping[p];
                const float a1 = v.sOn[p] ? v.sBase[p] * level * std::min(1.f, bright * (p == 0 ? 1.f : 0.8f)) : 0.f;
                const float a0 = v.sAmp[p]; v.sAmp[p] = a1;
                al0[p] = a0 * v.sGL[p]; ar0[p] = a0 * v.sGR[p];
                dl[p] = (a1 - a0) * v.sGL[p] / (float)n; dr[p] = (a1 - a0) * v.sGR[p] / (float)n;
            }
            float c[kPartials], q[kPartials];
            for (int p = 0; p < kPartials; ++p) {    // keep the oscillators on the unit circle
                const float m = 1.5f - 0.5f * (v.sC[p] * v.sC[p] + v.sS[p] * v.sS[p]);
                c[p] = v.sC[p] * m; q[p] = v.sS[p] * m;
            }
            const float rc0 = v.sRc[0], rc1 = v.sRc[1], rc2 = v.sRc[2], rc3 = v.sRc[3];
            const float rs0 = v.sRs[0], rs1 = v.sRs[1], rs2 = v.sRs[2], rs3 = v.sRs[3];
            float c0 = c[0], c1 = c[1], c2 = c[2], c3 = c[3], s0 = q[0], s1 = q[1], s2 = q[2], s3 = q[3];
            for (int i = 0; i < n; ++i) {
                float t0;
                t0 = c0 * rc0 - s0 * rs0; s0 = s0 * rc0 + c0 * rs0; c0 = t0;
                t0 = c1 * rc1 - s1 * rs1; s1 = s1 * rc1 + c1 * rs1; c1 = t0;
                t0 = c2 * rc2 - s2 * rs2; s2 = s2 * rc2 + c2 * rs2; c2 = t0;
                t0 = c3 * rc3 - s3 * rs3; s3 = s3 * rc3 + c3 * rs3; c3 = t0;
                const float t = (float)(i + 1);
                const float l = (al0[0] + dl[0] * t) * s0 + (al0[1] + dl[1] * t) * s1 + (al0[2] + dl[2] * t) * s2 + (al0[3] + dl[3] * t) * s3;
                const float r = (ar0[0] + dr[0] * t) * s0 + (ar0[1] + dr[1] * t) * s1 + (ar0[2] + dr[2] * t) * s2 + (ar0[3] + dr[3] * t) * s3;
                sL[i] += l * env[i]; sR[i] += r * env[i];
            }
            v.sC[0] = c0; v.sC[1] = c1; v.sC[2] = c2; v.sC[3] = c3; v.sS[0] = s0; v.sS[1] = s1; v.sS[2] = s2; v.sS[3] = s3;
        }

        // ---- foundation (mono, centred)
        if (onFound && (v.fW > 0.f || v.subW > 0.f)) {
            const float* og = tab.organ.data(); const float* st = tab.sine.data();
            uint32_t a = v.fPh, b = v.subPh; const uint32_t ia = v.fInc, ib = v.subInc;
            const float wf = v.fW * 0.55f, ws = v.subW * 0.6f;
            for (int i = 0; i < n; ++i)
                fM[i] += env[i] * (wf * lookup(og, a + ia * (uint32_t)i) + ws * lookup(st, b + ib * (uint32_t)i));
            v.fPh = a + ia * (uint32_t)n; v.subPh = b + ib * (uint32_t)n;
        }

        if (done) v.active = false;
    }

    void renderChunk(float* outL, float* outR, int n)
    {
        const float dt = (float)n / (float)sr;
        smoothParams(dt);

        onWarm = target.warm > 0.f || gWarm > 1e-6f; onAir = target.air > 0.f || gAir > 1e-6f;
        onShim = target.shimmer > 0.f || gShim > 1e-6f; onFound = target.foundation > 0.f || gFound > 1e-6f;
        onShFb = target.shimmerVerb > 0.f || gShFb > 1e-6f;
        bool anyVoice = false;
        for (auto& v : voices) anyVoice |= v.active;
        if (!anyVoice && silentSamples > (int)(0.5 * sr)) {
            std::memset(outL, 0, sizeof(float) * n); std::memset(outR, 0, sizeof(float) * n);
            return;
        }
        float wL[kCtl] = {}, wR[kCtl] = {}, aL[kCtl] = {}, aR[kCtl] = {}, sL[kCtl] = {}, sR[kCtl] = {}, fM[kCtl] = {};
        float nL[kCtl], nR[kCtl];
        for (int i = 0; i < n; ++i) {                          // darker noise: the breath has no hiss on top
            noiseLpL += 0.3f * (rng.bi() - noiseLpL); noiseLpR += 0.3f * (rng.bi() - noiseLpR);
            nL[i] = 2.2f * noiseLpL; nR[i] = 2.2f * noiseLpR;
        }

        for (auto& v : voices)
            if (v.active) renderVoice(v, n, dt, wL, wR, aL, aR, sL, sR, fM, nL, nR);

        // ---- bus filters (coefficients per chunk; cutoff moves slowly with Motion)
        cutoffLfo.step(rng, dt, 5.f, 11.f);
        airLfo.step(rng, dt, 3.f, 8.f);
        const float b = cur.brightness;
        const float fc = std::min(7000.f, 300.f * std::pow(6000.f / 300.f, b) * std::exp2(0.8f * cur.motion * cutoffLfo.value));
        warmLp1L.set(fc, 0.5412f, (float)sr); warmLp2L.set(fc, 1.3066f, (float)sr);   // 4th-order Butterworth
        warmLp1R.copyCoeffs(warmLp1L); warmLp2R.copyCoeffs(warmLp2L);
        const float fa = 1000.f * std::pow(2.6f, b) * std::exp2(0.4f * cur.motion * airLfo.value);   // 1.0 .. 2.6 kHz, rounded
        airLpL.set(fa, 0.5412f, (float)sr); airLpR.copyCoeffs(airLpL);
        airLp2L.set(fa, 1.3066f, (float)sr); airLp2R.copyCoeffs(airLp2L);

        // ---- layer levels and global gains (linear ramps across the chunk)
        const float exprGain = expr * expr;                       // audio taper; pedal up = silent
        const float lw = cur.warm * 0.9f, la = cur.air * 0.55f, ls = cur.shimmer * 0.42f, lf = cur.foundation * 0.8f;
        const float mix = cur.reverbMix;
        const float dryG = 1.f - 0.55f * mix * mix, wetG = 1.15f * mix;
        const float outG = std::pow(10.f, cur.gainDb / 20.f) * 0.5f;
        const float shFbG = 0.62f * cur.shimmerVerb;
        const float ramp = 1.f / (float)n;
        auto lerp = [ramp](float a0, float a1, int i) { return a0 + (a1 - a0) * ramp * (float)(i + 1); };

        const float rt = 1.5f * std::pow(14.f / 1.5f, cur.size);
        if (std::fabs(rt - hallRt) > 0.004f * rt) { hall.setDecay(rt, 0.42f); hallRt = rt; }

        const float width = cur.width;
        float peak = 0.f;
        for (int i = 0; i < n; ++i) {
            // warm: 4th-order low-pass, 35 Hz high-pass
            float xl = warmHpL.hp(warmLp2L.lp(warmLp1L.lp(wL[i])));
            float xr = warmHpR.hp(warmLp2R.lp(warmLp1R.lp(wR[i])));
            // air: band 700 Hz .. brightness, width as mid/side
            float al = airLp2L.lp(airLpL.lp(airHpL.hp(aL[i]))), ar = airLp2R.lp(airLpR.lp(airHpR.hp(aR[i])));
            const float am = 0.5f * (al + ar), as = 0.5f * (al - ar) * width;
            al = am + as; ar = am - as;
            const float e = lerp(gExpr, exprGain, i);
            const float dl = e * (lerp(gWarm, lw, i) * xl + lerp(gAir, la, i) * al + lerp(gShim, ls, i) * sL[i] + lerp(gFound, lf, i) * fM[i]);
            const float dr = e * (lerp(gWarm, lw, i) * xr + lerp(gAir, la, i) * ar + lerp(gShim, ls, i) * sR[i] + lerp(gFound, lf, i) * fM[i]);

            // hall with octave-up feedback (shimmer reverb)
            const float send = sendHp.hp(0.5f * (dl + dr)) + shFb;
            float hl, hr;
            hall.process(send, hl, hr);
            if (onShFb) {
                // band-limit the loop before shifting so the doubled spectrum stays far below Nyquist
                float x = shLp2.lp(shLp1.lp(shHp.hp(0.5f * (hl + hr))));
                shBuf[shWrite] = x;
                // two taps reading at double speed, sin windows (constant power), 100 ms grains
                const int c2 = shCount + shW / 2 < shW ? shCount + shW / 2 : shCount - shW / 2;
                const int d1 = shW - shCount, d2 = shW - c2;
                const float w1 = shWin[shCount], w2 = shWin[c2];
                const float y = w1 * shBuf[(shWrite - d1) & shMask] + w2 * shBuf[(shWrite - d2) & shMask];
                shWrite = (shWrite + 1) & shMask;
                shCount = (shCount + 1) % shW;       // delay shrinks by 1/sample -> read speed 2 -> +12 semitones
                shFb = shPostLp.lp(y) * lerp(gShFb, shFbG, i);
            }
            const float dG = lerp(gDry, dryG, i), wG = lerp(gWet, wetG, i), oG = lerp(gOut, outG, i);
            float L = oG * (dG * dl + wG * hl), R = oG * (dG * dr + wG * hr);
            // DC blocker
            const float yl = L - dcInL + dcCoef * dcL; dcInL = L; dcL = yl;
            const float yr = R - dcInR + dcCoef * dcR; dcInR = R; dcR = yr;
            outL[i] = limit(yl); outR[i] = limit(yr);
            peak = std::max(peak, std::fabs(yl) + std::fabs(yr));
        }
        silentSamples = (!anyVoice && peak < 2e-6f) ? silentSamples + n : 0;
        gExpr = exprGain; gWarm = lw; gAir = la; gShim = ls; gFound = lf; gDry = dryG; gWet = wetG; gOut = outG; gShFb = shFbG;
    }

    inline float limit(float x)
    {
        // transparent below -1.9 dBFS; above, a soft knee that never exceeds -0.2 dBFS
        constexpr float kT = 0.8f, kR = 0.18f;
        const float ax = std::fabs(x);
        limitCount += 1;
        if (ax <= kT) return x;
        limited += 1;
        const float y = kT + kR * std::tanh((ax - kT) / kR);
        return x < 0 ? -y : y;
    }
};

// ------------------------------------------------------------------------------------------ API
Engine::Engine(uint32_t seed) : d(std::make_unique<Impl>(seed)) {}
Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int)
{
    auto& m = *d;
    m.sr = sampleRate;
    m.tab.build(sampleRate);
    int combSize = 1; while (combSize < (int)(sampleRate / 20.0) + 8) combSize <<= 1;
    for (auto& v : m.voices) {
        v.combL.assign(combSize, 0.f); v.combR.assign(combSize, 0.f); v.combMask = combSize - 1; v.active = false;
    }
    m.hall.prepare(sampleRate, 0.035f);
    m.hallRt = -1;
    m.shW = (int)(0.1 * sampleRate) & ~1;
    int sz = 1; while (sz < m.shW + 4) sz <<= 1;
    m.shBuf.assign(sz, 0.f); m.shMask = sz - 1; m.shWrite = 0; m.shCount = 0; m.shFb = 0;
    m.shWin.resize(m.shW);
    for (int i = 0; i < m.shW; ++i) m.shWin[i] = (float)std::sin(M_PI * i / m.shW);
    m.warmC.assign(m.tab.tri.size(), 0.f); m.warmS = m.warmC;
    for (int j = 0; j < kLevels; ++j) m.blendLevel(j, m.target.shape);
    const float sr = (float)sampleRate;
    m.warmHpL.set(35.f, 0.707f, sr); m.warmHpR.copyCoeffs(m.warmHpL);
    m.airHpL.set(450.f, 0.6f, sr); m.airHpR.copyCoeffs(m.airHpL);
    m.sendHp.set(110.f, 0.707f, sr);
    m.shHp.set(300.f, 0.707f, sr); m.shLp1.set(3200.f, 0.5412f, sr); m.shLp2.set(3200.f, 1.3066f, sr);
    m.shPostLp.set(7000.f, 0.707f, sr);
    m.dcCoef = (float)std::exp(-2.0 * M_PI * 6.0 / sampleRate);
    m.cutoffLfo.init(m.rng, 5.f, 11.f); m.airLfo.init(m.rng, 3.f, 8.f);
    reset();
}

void Engine::reset()
{
    auto& m = *d;
    for (auto& v : m.voices) v.active = false;
    m.droneKey = -1; m.sustain = false; m.first = true;
    m.hall.clear();
    std::fill(m.shBuf.begin(), m.shBuf.end(), 0.f); m.shFb = 0;
    for (Svf* f : { &m.warmLp1L, &m.warmLp1R, &m.warmLp2L, &m.warmLp2R, &m.warmHpL, &m.warmHpR, &m.airHpL, &m.airHpR,
                    &m.airLpL, &m.airLpR, &m.airLp2L, &m.airLp2R, &m.sendHp, &m.shLp1, &m.shLp2, &m.shHp, &m.shPostLp })
        f->clear();
    m.dcL = m.dcR = m.dcInL = m.dcInR = 0;
    m.gExpr = m.gWarm = m.gAir = m.gShim = m.gFound = m.gWet = m.gOut = m.gShFb = 0; m.gDry = 1;
    m.cur = m.target; m.mode = m.target.mode; m.colour = m.target.colour;
    m.limited = m.limitCount = 0; m.silentSamples = 0;
}

void Engine::setParams(const Params& p)
{
    auto& t = d->target;
    t = p;
    t.crossfade = std::clamp(p.crossfade, 1.f, 10.f);
    t.attack = std::clamp(p.attack, 0.02f, 8.f);
    t.release = std::clamp(p.release, 0.1f, 12.f);
    for (float* x : { &t.warm, &t.air, &t.shimmer, &t.foundation, &t.shape, &t.detune, &t.brightness, &t.motion,
                      &t.width, &t.size, &t.reverbMix, &t.shimmerVerb, &t.expression })
        *x = std::clamp(*x, 0.f, 1.f);
}

void Engine::process(float* left, float* right, int numSamples, const Event* events, int numEvents)
{
    auto& m = *d;
    m.applyModeAndColour();
    int pos = 0, ei = 0;
    while (pos < numSamples) {
        while (ei < numEvents && events[ei].offset <= pos) m.handle(events[ei++]);
        int end = std::min(numSamples, pos + kCtl);
        if (ei < numEvents && events[ei].offset < end) end = std::max(pos + 1, events[ei].offset);
        m.renderChunk(left + pos, right + pos, end - pos);
        pos = end;
    }
    while (ei < numEvents) m.handle(events[ei++]);
}

int Engine::activeVoices() const
{
    int n = 0;
    for (auto& v : d->voices) n += v.active;
    return n;
}

int Engine::droneKey() const { return d->mode == Mode::Drone ? d->droneKey : -1; }

float Engine::limiterActivity() const { return d->limitCount > 0 ? (float)(d->limited / d->limitCount) : 0.f; }

} // namespace sanctuary
