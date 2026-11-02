// Sanctuary – SPDX-License-Identifier: GPL-3.0-or-later
// Hall: 16-line feedback delay network with slowly modulated delay lengths (no metallic
// ringing), Householder feedback, frequency-dependent decay, diffused input and
// decorrelated stereo outputs.
// Adapted from OpenSection's Hall.h (https://github.com/doitux/OpenSection, GPL-3.0-or-later,
// same author): longer lines for a bigger space, decay that can change in real time (Size knob)
// and a per-sample interface so the shimmer feedback can run inside the loop.
#pragma once
#include <vector>
#include <cmath>
#include <array>
#include <algorithm>

namespace sanctuary {

class Diffuser {   // chain of allpasses: turns sparse echoes into a dense, smooth cloud
public:
    void prepare(double sr, const std::vector<float>& ms, float g)
    {
        g_ = g; st_.clear();
        for (float m : ms) { St s; s.buf.assign(std::max(1, (int)(m * 0.001 * sr)), 0.f); st_.push_back(std::move(s)); }
    }
    void clear() { for (auto& s : st_) { std::fill(s.buf.begin(), s.buf.end(), 0.f); s.pos = 0; } }
    inline float process(float x)
    {
        for (auto& s : st_) {
            const float d = s.buf[s.pos];
            const float y = -g_ * x + d;
            s.buf[s.pos] = x + g_ * y;
            if (++s.pos == (int)s.buf.size()) s.pos = 0;
            x = y;
        }
        return x;
    }
private:
    struct St { std::vector<float> buf; int pos = 0; };
    std::vector<St> st_;
    float g_ = 0.6f;
};

class Hall {
public:
    static constexpr int N = 16;

    void prepare(double sr, float predelay = 0.03f)
    {
        sr_ = sr;
        // mutually prime-ish lengths 43..149 ms: a large church-like space with a dense, even tail
        static const float ms[N] = { 43.1f, 47.3f, 52.9f, 57.1f, 61.7f, 67.3f, 71.9f, 77.9f,
                                     83.3f, 89.9f, 97.1f, 103.7f, 113.3f, 121.9f, 131.3f, 149.1f };
        // all lines share one size, mask and write position (cheaper per sample, vectorises)
        size_ = 1; while (size_ < (int)(ms[N - 1] * 0.001 * sr) + 64) size_ <<= 1;
        mask_ = size_ - 1; wpos_ = 0;
        buf_.assign((size_t)N * size_, 0.f);
        for (int i = 0; i < N; ++i) {
            len_[i] = (float)(ms[i] * 0.001 * sr);
            lp_[i] = 0.f;
            const double w = 2 * M_PI * (0.11 + 0.053 * i) / sr;   // slow modulation as a rotating phasor
            rc_[i] = (float)std::cos(w); rs_[i] = (float)std::sin(w);
            mc_[i] = std::cos(0.7f * i); ms_[i] = std::sin(0.7f * i);
        }
        modDepth_ = (float)(0.0003 * sr);                      // ±0.3 ms: breaks up resonances, no audible wobble
        inDiff_.prepare(sr, { 4.3f, 3.1f, 8.6f, 6.3f, 11.7f }, 0.7f);
        pre_.assign((size_t)std::max(1.0, predelay * sr), 0.f); prePos_ = 0;
        // energy normalisation, measured once at a reference decay; the tail energy grows ~ with rt60
        setDecay(kRefRt, 0.5f);
        normRef_ = 1.f; norm_ = 1.f;
        clear();
        double e = 0; float l, r;
        const int n = (int)(kRefRt * 1.3 * sr);
        for (int i = 0; i < n; ++i) { l = r = 0.f; tick(i == 0 ? 1.f : 0.f, l, r); e += 0.5 * ((double)l * l + (double)r * r); }
        normRef_ = (float)(1.0 / std::sqrt(e + 1e-20));
        setDecay(kRefRt, 0.5f);
        clear();
    }

    // real-time safe: 16 pow() calls, call only when the values change
    void setDecay(float rt60, float hfRatio)
    {
        for (int i = 0; i < N; ++i) {
            const double t = len_[i] / sr_;
            g_[i] = (float)std::pow(10.0, -3.0 * t / rt60);
            const double gHi = std::pow(10.0, -3.0 * t / (rt60 * hfRatio));
            const double r = gHi / g_[i];
            damp_[i] = (float)((1 - r) / (1 + r));
        }
        norm_ = normRef_ * std::sqrt(kRefRt / rt60);
    }

    void clear()
    {
        std::fill(buf_.begin(), buf_.end(), 0.f);
        inDiff_.clear();
        std::fill(pre_.begin(), pre_.end(), 0.f);
        lp_.fill(0.f);
    }

    // one sample, mono in -> normalised stereo out
    inline void process(float in, float& outL, float& outR)
    {
        tick(in, outL, outR);
        outL *= norm_; outR *= norm_;
    }

private:
    static constexpr float kRefRt = 4.0f;

    inline void tick(float in, float& outL, float& outR)
    {
        float x = pre_[prePos_]; pre_[prePos_] = in;
        if (++prePos_ == (int)pre_.size()) prePos_ = 0;
        x = inDiff_.process(x);
        float o[N], rp[N], sum = 0.f;
        const float wp = (float)(wpos_ + size_);              // + size: read positions stay positive
        for (int i = 0; i < N; ++i) {
            const float c = mc_[i] * rc_[i] - ms_[i] * rs_[i];
            ms_[i] = ms_[i] * rc_[i] + mc_[i] * rs_[i]; mc_[i] = c;
            rp[i] = wp - (len_[i] + modDepth_ * ms_[i]);
        }
        for (int i = 0; i < N; ++i) {
            const int i0 = (int)rp[i]; const float fr = rp[i] - (float)i0;
            const float* line = &buf_[(size_t)i * size_];
            const float a = line[i0 & mask_], b = line[(i0 + 1) & mask_];
            o[i] = a + fr * (b - a);
        }
        for (int i = 0; i < N; ++i) {
            lp_[i] = o[i] + damp_[i] * (lp_[i] - o[i]);
            o[i] = lp_[i] * g_[i];
            sum += o[i];
        }
        sum *= 2.f / N;
        for (int i = 0; i < N; ++i) buf_[(size_t)i * size_ + wpos_] = o[i] - sum + x * kIn[i];
        wpos_ = (wpos_ + 1) & mask_;
        if (++renorm_ == 4096) {                               // keep the phasors on the unit circle
            renorm_ = 0;
            for (int i = 0; i < N; ++i) { const float m = 1.f / std::sqrt(mc_[i] * mc_[i] + ms_[i] * ms_[i]); mc_[i] *= m; ms_[i] *= m; }
        }
        float l = 0, r = 0;
        for (int i = 0; i < N; i += 2) { l += kOut[i] * o[i]; r += kOut[i + 1] * o[i + 1]; }
        outL = l; outR = r;
    }

    static constexpr float kIn[N] = { 1, -1, 1, 1, -1, 1, -1, -1, 1, 1, -1, 1, -1, -1, 1, -1 };
    static constexpr float kOut[N] = { 1, 1, -1, 1, 1, -1, -1, -1, 1, -1, -1, 1, -1, 1, 1, 1 };
    double sr_ = 48000;
    std::vector<float> buf_;
    int size_ = 1, mask_ = 0, wpos_ = 0;
    std::array<float, N> len_ {}, g_ {}, damp_ {}, lp_ {}, rc_ {}, rs_ {}, mc_ {}, ms_ {};
    int renorm_ = 0;
    float modDepth_ = 10.f, norm_ = 1.f, normRef_ = 1.f;
    Diffuser inDiff_;
    std::vector<float> pre_;
    int prePos_ = 0;
};

} // namespace sanctuary
