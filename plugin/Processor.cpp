// Sanctuary – SPDX-License-Identifier: GPL-3.0-or-later
#include "Processor.h"
#include "Editor.h"

using namespace juce;
using APVTS = AudioProcessorValueTreeState;

namespace ids {
static const char* mode = "mode";
static const char* colour = "colour";
static const char* crossfade = "crossfade";
static const char* warm = "warm";
static const char* air = "air";
static const char* shimmer = "shimmer";
static const char* sparkle = "sparkle";
static const char* foundation = "foundation";
static const char* shape = "shape";
static const char* detune = "detune";
static const char* brightness = "brightness";
static const char* motion = "motion";
static const char* width = "width";
static const char* attack = "attack";
static const char* release = "release";
static const char* size = "size";
static const char* mix = "mix";
static const char* shimmerVerb = "shimmerVerb";
static const char* gain = "gain";
static const char* exprSource = "exprSource";
static const char* expression = "expression";
}

APVTS::ParameterLayout SanctuaryProcessor::layout()
{
    const sanctuary::Params d = sanctuary::preset(0).params;   // defaults = first factory program
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = [](float v, int) { return String(roundToInt(v * 100)) + " %"; };
    auto sec = [](float v, int) { return v < 1.f ? String(roundToInt(v * 1000)) + " ms" : String(v, 1) + " s"; };
    auto add = [&](const char* id, const char* name, float lo, float hi, float def, float skewMid = -1,
                   std::function<String(float, int)> fmt = {}) {
        NormalisableRange<float> r(lo, hi);
        if (skewMid > 0) r.setSkewForCentre(skewMid);
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name, r, def,
                                                          AudioParameterFloatAttributes().withStringFromValueFunction(fmt ? fmt : pct)));
    };
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID { ids::mode, 1 }, "Mode", StringArray { "Drone", "Play" }, 0));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID { ids::colour, 1 }, "Drone colour",
                                                       StringArray { "Open (1-5-8)", "Sus2", "Add9" }, 0));
    add(ids::crossfade, "Crossfade", 1.f, 10.f, 4.f, 4.f, sec);
    add(ids::warm, "Warm", 0.f, 1.f, d.warm);
    add(ids::air, "Air", 0.f, 1.f, d.air);
    add(ids::shimmer, "Shimmer", 0.f, 1.f, d.shimmer);
    add(ids::sparkle, "Sparkle", 0.f, 1.f, d.sparkle);
    add(ids::foundation, "Foundation", 0.f, 1.f, d.foundation);
    add(ids::shape, "Shape", 0.f, 1.f, d.shape);
    add(ids::detune, "Detune", 0.f, 1.f, d.detune);
    add(ids::brightness, "Brightness", 0.f, 1.f, d.brightness);
    add(ids::motion, "Motion", 0.f, 1.f, d.motion);
    add(ids::width, "Width", 0.f, 1.f, d.width);
    add(ids::attack, "Attack", 0.02f, 8.f, d.attack, 1.2f, sec);
    add(ids::release, "Release", 0.1f, 12.f, d.release, 3.f, sec);
    add(ids::size, "Hall size", 0.f, 1.f, d.size);
    add(ids::mix, "Hall mix", 0.f, 1.f, d.reverbMix);
    add(ids::shimmerVerb, "Shimmer hall", 0.f, 1.f, d.shimmerVerb);
    add(ids::gain, "Volume", -30.f, 6.f, -6.f, -1, [](float v, int) { return String(v, 1) + " dB"; });
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID { ids::exprSource, 1 }, "Expression source",
                                                       StringArray { "CC11 Expression", "CC1 Mod wheel", "CC2 Breath", "Off (use knob)" }, 0));
    add(ids::expression, "Swell", 0.f, 1.f, 1.f);
    return { p.begin(), p.end() };
}

SanctuaryProcessor::SanctuaryProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "Sanctuary", layout())
{
    events.reserve(2048);
}

bool SanctuaryProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == AudioChannelSet::stereo();
}

void SanctuaryProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.setParams(readParams());
    engine.prepare(sampleRate, std::max(samplesPerBlock, 1));
    engine.setParams(readParams());
}

sanctuary::Params SanctuaryProcessor::readParams() const
{
    auto v = [this](const char* id) { return params.getRawParameterValue(id)->load(); };
    sanctuary::Params p;
    p.mode = (int)v(ids::mode) == 0 ? sanctuary::Mode::Drone : sanctuary::Mode::Play;
    p.colour = (sanctuary::Colour)jlimit(0, 2, (int)v(ids::colour));
    p.crossfade = v(ids::crossfade);
    p.warm = v(ids::warm); p.air = v(ids::air); p.shimmer = v(ids::shimmer); p.sparkle = v(ids::sparkle); p.foundation = v(ids::foundation);
    p.shape = v(ids::shape); p.detune = v(ids::detune); p.brightness = v(ids::brightness);
    p.motion = v(ids::motion); p.width = v(ids::width);
    p.attack = v(ids::attack); p.release = v(ids::release);
    p.size = v(ids::size); p.reverbMix = v(ids::mix); p.shimmerVerb = v(ids::shimmerVerb);
    p.gainDb = v(ids::gain);
    p.expression = (int)v(ids::exprSource) == 3 ? v(ids::expression) : 1.f;
    return p;
}

void SanctuaryProcessor::setCurrentProgram(int index)
{
    index = jlimit(0, sanctuary::numPresets() - 1, index);
    const auto& pr = sanctuary::preset(index).params;
    auto set = [this](const char* id, float value) {
        if (auto* prm = params.getParameter(id)) prm->setValueNotifyingHost(prm->convertTo0to1(value));
    };
    set(ids::warm, pr.warm); set(ids::air, pr.air); set(ids::shimmer, pr.shimmer); set(ids::sparkle, pr.sparkle); set(ids::foundation, pr.foundation);
    set(ids::shape, pr.shape); set(ids::detune, pr.detune); set(ids::brightness, pr.brightness);
    set(ids::motion, pr.motion); set(ids::width, pr.width); set(ids::attack, pr.attack); set(ids::release, pr.release);
    set(ids::size, pr.size); set(ids::mix, pr.reverbMix); set(ids::shimmerVerb, pr.shimmerVerb);
    currentProgram.store(index);
}

const String SanctuaryProcessor::getProgramName(int index)
{
    if (index < 0 || index >= sanctuary::numPresets()) return {};
    return sanctuary::preset(index).name;
}

void SanctuaryProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const auto t0 = Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();
    engine.setParams(readParams());

    const int src = (int)params.getRawParameterValue(ids::exprSource)->load();
    const int exprCC = src == 0 ? 11 : src == 1 ? 1 : src == 2 ? 2 : -1;
    using E = sanctuary::Event;
    events.clear();
    if (exprCC < 0) ccExpression = 1.f;                      // knob mode: the Swell knob is the expression
    events.push_back({ E::Expression, 0, 0, ccExpression });
    const int key = uiKeyRequest.exchange(-1);
    if (key >= 0) events.push_back({ E::DroneKey, 0, key, 1.f });

    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        const int off = jlimit(0, n - 1, meta.samplePosition);
        if (events.size() >= events.capacity()) break;
        if (m.isNoteOn()) events.push_back({ E::NoteOn, off, m.getNoteNumber(), m.getFloatVelocity() });
        else if (m.isNoteOff()) events.push_back({ E::NoteOff, off, m.getNoteNumber(), 0.f });
        else if (m.isSustainPedalOn()) events.push_back({ E::Sustain, off, 0, 1.f });
        else if (m.isSustainPedalOff()) events.push_back({ E::Sustain, off, 0, 0.f });
        else if (m.isAllNotesOff() || m.isAllSoundOff()) events.push_back({ E::AllOff, off, 0, 0.f });
        else if (m.isController() && m.getControllerNumber() == exprCC) {
            ccExpression = m.getControllerValue() / 127.f;
            events.push_back({ E::Expression, off, 0, ccExpression });
        }
    }
    midi.clear();

    float* L = buffer.getWritePointer(0);
    float* R = buffer.getWritePointer(1);
    engine.process(L, R, n, events.data(), (int)events.size());

    // load meter for the editor
    const double used = Time::highResolutionTicksToSeconds(Time::getHighResolutionTicks() - t0);
    const double avail = n / getSampleRate();
    load += 0.05f * ((float)(used / avail) - load);
    uiLoad.store(load); uiVoices.store(engine.activeVoices()); uiDroneKey.store(engine.droneKey());
    uiExpression.store(exprCC < 0 ? params.getRawParameterValue(ids::expression)->load() : ccExpression);
}

void SanctuaryProcessor::getStateInformation(MemoryBlock& destData)
{
    auto state = params.copyState();
    state.setProperty("program", currentProgram.load(), nullptr);
    std::unique_ptr<XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SanctuaryProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(params.state.getType())) {
        auto vt = ValueTree::fromXml(*xml);
        currentProgram.store(jlimit(0, sanctuary::numPresets() - 1, (int)vt.getProperty("program", 0)));
        params.replaceState(vt);
    }
}

AudioProcessorEditor* SanctuaryProcessor::createEditor() { return new SanctuaryEditor(*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SanctuaryProcessor(); }
