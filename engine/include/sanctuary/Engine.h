// Sanctuary – computed worship pad and drone instrument
// Copyright (C) 2026 Sanctuary contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include <cstdint>
#include <string>

namespace sanctuary {

enum class Mode : int { Drone = 0, Play = 1 };
enum class Colour : int { Open = 0, Sus2 = 1, Add9 = 2 };

// Everything a user can change. All values are smoothed inside the engine (no zipper noise).
// The defaults are the first factory program, "Warm Worship".
struct Params {
    Mode mode = Mode::Drone;
    Colour colour = Colour::Open;   // drone voicing: 1-5-8, + 2nd (sus2, middle), + 9th (add9, high)
    float crossfade = 4.0f;         // 1 .. 10 s   drone key change / fade in / fade out (equal power)
    // layers 0 .. 1
    float warm = 0.85f;             // detuned ensemble (band-limited triangle .. saw)
    float air = 0.25f;              // breath: noise through combs tuned to the notes
    float shimmer = 0.25f;          // additive octave / fifth partials that slowly bloom and fade
    float sparkle = 0.3f;           // 0 .. 1  the shimmer partials light up like small bells, one by one
    float foundation = 0.5f;        // sine/organ fundamental + sub octave for low notes
    // tone
    float shape = 0.45f;            // 0 = triangle (soft) .. 1 = saw (rich)
    float detune = 0.4f;            // 0 .. 1 -> ensemble spread 0 .. 25 cents
    float brightness = 0.42f;       // 0 .. 1 -> low-pass 300 Hz .. 6 kHz (rounded top: pads sit under a piano)
    float motion = 0.45f;           // 0 .. 1 depth of all slow modulation (drift, filter, partials)
    float width = 0.8f;             // 0 .. 1 stereo spread of the ensemble and air
    // play-mode envelope
    float attack = 1.2f;            // 0.02 .. 8 s
    float release = 3.0f;           // 0.1 .. 12 s
    // space
    float size = 0.6f;              // 0 .. 1 -> hall decay 1.5 .. 14 s
    float reverbMix = 0.38f;        // 0 .. 1
    float shimmerVerb = 0.12f;      // 0 .. 1 octave-up feedback inside the hall
    // output
    float expression = 1.0f;        // 0 .. 1 (pedal fully up = silent)
    float gainDb = -6.0f;           // output volume
};

struct Event {
    enum Type : uint8_t { NoteOn, NoteOff, Sustain, Expression, AllOff, DroneKey } type;
    int offset = 0;            // sample offset inside the block
    int key = 0;               // MIDI note, or pitch class 0..11 for DroneKey
    float value = 0;           // velocity 0..1, sustain 0/1, expression 0..1
};

struct Preset {
    const char* name;
    const char* description;
    Params params;
};
int numPresets();
const Preset& preset(int index);
int findPreset(const std::string& name);    // case-insensitive, -1 if unknown

class Engine {
public:
    explicit Engine(uint32_t seed = 1);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void prepare(double sampleRate, int maxBlock);      // allocates (not real-time safe)
    void setParams(const Params& p);                    // real-time safe
    // real-time: events must be sorted by offset
    void process(float* left, float* right, int numSamples, const Event* events, int numEvents);
    void reset();

    int activeVoices() const;
    int droneKey() const;          // pitch class of the drone that is (fading) in, -1 = none
    float limiterActivity() const; // fraction of samples the safety limiter touched (diagnostics)

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace sanctuary
