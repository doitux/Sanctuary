// Sanctuary – SPDX-License-Identifier: GPL-3.0-or-later
// Factory programs. They set the sound (layers, tone, envelope, space) but leave the
// performance settings alone: mode, colour, crossfade, expression and volume stay as the player set them.
#include "sanctuary/Engine.h"
#include <cctype>

namespace sanctuary {

namespace {
Params make(float warm, float air, float shimmer, float sparkle, float foundation, float shape, float detune, float brightness,
            float motion, float width, float attack, float release, float size, float mix, float shimmerVerb)
{
    Params p;
    p.warm = warm; p.air = air; p.shimmer = shimmer; p.sparkle = sparkle; p.foundation = foundation;
    p.shape = shape; p.detune = detune; p.brightness = brightness; p.motion = motion; p.width = width;
    p.attack = attack; p.release = release; p.size = size; p.reverbMix = mix; p.shimmerVerb = shimmerVerb;
    return p;
}

//                         warm  air   shim  spark found shape detun brigh motn  width att  rel   size mix   shVerb
const Preset kPresets[] = {
    { "Warm Worship", "The all-rounder: warm, round ensemble, a breath of air, a few glassy lights, medium hall",
      make(0.85f, 0.16f, 0.30f, 0.30f, 0.50f, 0.25f, 0.40f, 0.36f, 0.45f, 0.80f, 1.2f, 3.0f, 0.60f, 0.38f, 0.10f) },
    { "Shimmer Heaven", "Octaves and fifths bloom and light up like small bells above a soft bed, octave-up hall",
      make(0.50f, 0.22f, 0.90f, 0.60f, 0.35f, 0.15f, 0.35f, 0.45f, 0.65f, 1.00f, 2.0f, 5.0f, 0.85f, 0.55f, 0.45f) },
    { "Air & Light", "Soft breath tuned to the chord, very little body, rounded on top",
      make(0.40f, 0.75f, 0.45f, 0.35f, 0.25f, 0.10f, 0.30f, 0.38f, 0.55f, 1.00f, 1.5f, 4.0f, 0.70f, 0.50f, 0.18f) },
    { "Deep Drone", "Dark, slow and grounded: organ fundamental and sub, little top",
      make(0.65f, 0.10f, 0.10f, 0.05f, 1.00f, 0.25f, 0.25f, 0.20f, 0.30f, 0.60f, 2.5f, 5.0f, 0.65f, 0.32f, 0.00f) },
    { "Felt Ambient", "Soft triangle ensemble, intimate room, gentle air and slow glassy lights",
      make(0.70f, 0.30f, 0.35f, 0.45f, 0.40f, 0.00f, 0.18f, 0.28f, 0.40f, 0.70f, 0.6f, 3.5f, 0.45f, 0.42f, 0.06f) },
    { "Soft Strings Pad", "Rounded ensemble with wide detune, like a quiet synth string section",
      make(1.00f, 0.15f, 0.15f, 0.10f, 0.30f, 0.55f, 0.62f, 0.55f, 0.50f, 0.90f, 0.9f, 2.0f, 0.50f, 0.32f, 0.00f) },
    { "Dark Prayer", "Low and closed, long cathedral tail",
      make(0.80f, 0.15f, 0.06f, 0.05f, 0.70f, 0.40f, 0.45f, 0.14f, 0.40f, 0.75f, 2.5f, 6.0f, 0.92f, 0.50f, 0.00f) },
    { "Bright Morning", "Open and present, still round on top: lighter ensemble, airy, glittering lights",
      make(0.75f, 0.38f, 0.55f, 0.55f, 0.30f, 0.40f, 0.50f, 0.58f, 0.55f, 0.90f, 0.8f, 2.5f, 0.55f, 0.40f, 0.22f) },
};
constexpr int kNum = (int)(sizeof(kPresets) / sizeof(kPresets[0]));
} // namespace

int numPresets() { return kNum; }
const Preset& preset(int index) { return kPresets[index < 0 ? 0 : index >= kNum ? kNum - 1 : index]; }

int findPreset(const std::string& name)
{
    auto low = [](std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    const std::string n = low(name);
    for (int i = 0; i < kNum; ++i) if (low(kPresets[i].name) == n) return i;
    for (int i = 0; i < kNum; ++i) if (low(kPresets[i].name).find(n) != std::string::npos) return i;
    return -1;
}

} // namespace sanctuary
