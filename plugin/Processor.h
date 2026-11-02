// Sanctuary – SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "sanctuary/Engine.h"
#include <atomic>

class SanctuaryProcessor : public juce::AudioProcessor {
public:
    SanctuaryProcessor();
    ~SanctuaryProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sanctuary"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    // factory presets as host programs
    int getNumPrograms() override { return sanctuary::numPresets(); }
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState params;

    // editor <-> audio thread
    std::atomic<int> uiKeyRequest { -1 };       // drone key button clicked (pitch class), -1 = none
    std::atomic<int> uiDroneKey { -1 }, uiVoices { 0 };
    std::atomic<float> uiLoad { 0.f }, uiExpression { 1.f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    sanctuary::Params readParams() const;

    sanctuary::Engine engine;
    std::vector<sanctuary::Event> events;
    std::atomic<int> currentProgram { 0 };
    float ccExpression = 1.f;
    float load = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SanctuaryProcessor)
};
