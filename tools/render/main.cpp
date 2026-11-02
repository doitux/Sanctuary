// Sanctuary – SPDX-License-Identifier: GPL-3.0-or-later
// Command-line renderer and live-performance benchmark for the engine.
//
//   sanctuary-render [options] <notes.txt> <out.wav>
//   sanctuary-render --bench [options] <notes.txt>
//   sanctuary-render --list-presets
//
// notes.txt lines:  <on_s> <off_s> <midi> <velocity 0..1>
//                   cc <time_s> <num> <value 0..1>   64 = sustain, 123/120 = all notes off,
//                                                    the expression CC (--expr-cc, default 11) = expression
//                   key <time_s> <pitch class 0..11> (drone key button in the UI)
// options: --mode drone|play  --preset <name|index>  --colour open|sus2|add9  --crossfade s
//          --set name=value (any parameter, repeatable)  --expr-cc N  --sr 48000  --block 128
//          --seed N  --tail seconds
#include "sanctuary/Engine.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace sanctuary;

struct Ev { double t; Event e; };

static bool readNotes(const char* path, std::vector<Ev>& out, double& end, int exprCC)
{
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    end = 0;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        if (line.rfind("cc", 0) == 0) {
            std::string cc; double t; int num; float v;
            in >> cc >> t >> num >> v;
            Event e{};
            if (num == 64) e.type = Event::Sustain;
            else if (num == 123 || num == 120) e.type = Event::AllOff;
            else if (num == exprCC) e.type = Event::Expression;
            else continue;
            e.value = v;
            out.push_back({ t, e }); end = std::max(end, t);
            continue;
        }
        if (line.rfind("key", 0) == 0) {
            std::string k; double t; int pc;
            in >> k >> t >> pc;
            Event e{}; e.type = Event::DroneKey; e.key = pc;
            out.push_back({ t, e }); end = std::max(end, t);
            continue;
        }
        double on, off; int key; float vel;
        if (!(in >> on >> off >> key >> vel)) continue;
        Event a{}; a.type = Event::NoteOn; a.key = key; a.value = vel;
        Event b{}; b.type = Event::NoteOff; b.key = key;
        out.push_back({ on, a }); out.push_back({ off, b });
        end = std::max(end, off);
    }
    std::stable_sort(out.begin(), out.end(), [](const Ev& x, const Ev& y) {
        if (x.t != y.t) return x.t < y.t;
        return x.e.type == Event::NoteOff && y.e.type != Event::NoteOff;   // offs first
    });
    return true;
}

static void writeWav(const char* path, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    FILE* f = std::fopen(path, "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path); return; }
    uint32_t n = (uint32_t)L.size(), data = n * 8, riff = 36 + data, fmt = 16, sr32 = sr, br = sr * 8;
    uint16_t pcm = 3, ch = 2, ba = 8, bps = 32;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmt, 4, 1, f); std::fwrite(&pcm, 2, 1, f); std::fwrite(&ch, 2, 1, f); std::fwrite(&sr32, 4, 1, f);
    std::fwrite(&br, 4, 1, f); std::fwrite(&ba, 2, 1, f); std::fwrite(&bps, 2, 1, f);
    std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i) { std::fwrite(&L[i], 4, 1, f); std::fwrite(&R[i], 4, 1, f); }
    std::fclose(f);
}

static bool setParam(Params& p, const std::string& kv)
{
    const auto eq = kv.find('=');
    if (eq == std::string::npos) return false;
    const std::string k = kv.substr(0, eq);
    const float v = (float)std::atof(kv.c_str() + eq + 1);
    struct { const char* name; float* ptr; } map[] = {
        { "crossfade", &p.crossfade }, { "warm", &p.warm }, { "air", &p.air }, { "shimmer", &p.shimmer },
        { "foundation", &p.foundation }, { "shape", &p.shape }, { "detune", &p.detune }, { "brightness", &p.brightness },
        { "motion", &p.motion }, { "width", &p.width }, { "attack", &p.attack }, { "release", &p.release },
        { "size", &p.size }, { "mix", &p.reverbMix }, { "shimmerverb", &p.shimmerVerb }, { "expression", &p.expression },
        { "gain", &p.gainDb },
    };
    for (auto& m : map) if (k == m.name) { *m.ptr = v; return true; }
    return false;
}

int main(int argc, char** argv)
{
    int sr = 48000, block = 128, exprCC = 11; uint32_t seed = 1; float tail = 6.f;
    bool bench = false;
    Params p = preset(0).params;
    std::vector<std::string> sets;
    std::string modeS = "drone", colourS, presetS; float crossfade = -1;
    std::vector<const char*> pos;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--sr") sr = std::atoi(next());
        else if (a == "--block") block = std::atoi(next());
        else if (a == "--seed") seed = (uint32_t)std::atoi(next());
        else if (a == "--tail") tail = (float)std::atof(next());
        else if (a == "--bench") bench = true;
        else if (a == "--mode") modeS = next();
        else if (a == "--colour" || a == "--color") colourS = next();
        else if (a == "--preset") presetS = next();
        else if (a == "--crossfade") crossfade = (float)std::atof(next());
        else if (a == "--expr-cc") exprCC = std::atoi(next());
        else if (a == "--set") sets.push_back(next());
        else if (a == "--list-presets") {
            for (int k = 0; k < numPresets(); ++k) std::printf("%d  %-18s %s\n", k, preset(k).name, preset(k).description);
            return 0;
        } else pos.push_back(argv[i]);
    }
    if (pos.size() < (bench ? 1u : 2u)) { std::fprintf(stderr, "usage: see source header\n"); return 2; }
    if (!presetS.empty()) {
        int idx = (presetS[0] >= '0' && presetS[0] <= '9') ? std::atoi(presetS.c_str()) : findPreset(presetS);
        if (idx < 0 || idx >= numPresets()) { std::fprintf(stderr, "unknown preset %s\n", presetS.c_str()); return 2; }
        p = preset(idx).params;
    }
    p.mode = modeS == "play" ? Mode::Play : Mode::Drone;
    if (colourS == "sus2") p.colour = Colour::Sus2;
    else if (colourS == "add9") p.colour = Colour::Add9;
    if (crossfade > 0) p.crossfade = crossfade;
    for (auto& s : sets) if (!setParam(p, s)) { std::fprintf(stderr, "unknown parameter %s\n", s.c_str()); return 2; }

    std::vector<Ev> evs; double end;
    if (!readNotes(pos[0], evs, end, exprCC)) { std::fprintf(stderr, "cannot read %s\n", pos[0]); return 1; }

    Engine eng(seed);
    eng.setParams(p);
    eng.prepare(sr, block);
    eng.setParams(p);

    const long total = (long)((end + tail) * sr);
    std::vector<float> L, R; if (!bench) { L.reserve(total); R.reserve(total); }
    std::vector<float> bl(block), br(block);
    std::vector<Event> blockEv;
    size_t ei = 0;
    double sum = 0, worst = 0; long blocks = 0; std::vector<double> times; int peakVoices = 0;
    for (long t0 = 0; t0 < total; t0 += block) {
        const int n = (int)std::min<long>(block, total - t0);
        blockEv.clear();
        while (ei < evs.size() && (long)(evs[ei].t * sr) < t0 + n) {
            Event e = evs[ei].e; e.offset = (int)std::max<long>(0, (long)(evs[ei].t * sr) - t0);
            blockEv.push_back(e); ++ei;
        }
        auto a = std::chrono::steady_clock::now();
        eng.process(bl.data(), br.data(), n, blockEv.data(), (int)blockEv.size());
        auto b = std::chrono::steady_clock::now();
        double us = std::chrono::duration<double, std::micro>(b - a).count();
        sum += us; worst = std::max(worst, us); ++blocks; times.push_back(us);
        peakVoices = std::max(peakVoices, eng.activeVoices());
        if (!bench) { L.insert(L.end(), bl.begin(), bl.begin() + n); R.insert(R.end(), br.begin(), br.begin() + n); }
    }
    const double blockUs = 1e6 * block / sr;
    std::sort(times.begin(), times.end());
    auto pct = [&](double q) { return times[std::min(times.size() - 1, (size_t)(q * times.size()))]; };
    std::printf("Perzentile: 50 %% %.0f µs, 99 %% %.0f µs, 99,9 %% %.0f µs (Zeitfenster %.0f µs)\n", pct(0.5), pct(0.99), pct(0.999), blockUs);
    std::printf("Blöcke %ld à %d, Ø %.1f µs (%.2f %% eines Kerns), schlechtester Block %.1f µs (%.0f %% des Zeitfensters), max %d Stimmen, Limiter %.4f %% der Samples\n",
                blocks, block, sum / blocks, 100.0 * sum / blocks / blockUs, worst, 100.0 * worst / blockUs, peakVoices,
                100.0 * eng.limiterActivity());
    if (!bench) writeWav(pos[1], L, R, sr);
    return 0;
}
