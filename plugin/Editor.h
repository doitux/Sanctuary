// Sanctuary – SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Processor.h"

class SanctuaryLook : public juce::LookAndFeel_V4 {
public:
    SanctuaryLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
};

class SanctuaryEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit SanctuaryEditor(SanctuaryProcessor&);
    ~SanctuaryEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    struct Group { juce::String title; std::vector<Knob*> knobs; juce::Rectangle<int> area {}; };
    struct Combo {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
    };

    void timerCallback() override;
    Knob& knob(const char* id, const char* text);
    void setupCombo(Combo& c, const char* id, const char* text);
    void choosePreset(int index);

    SanctuaryProcessor& proc;
    SanctuaryLook look;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<Group> row1, row2;
    Combo mode, colour, exprSource;
    juce::ComboBox presetBox;
    juce::Label presetLabel;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" };
    std::array<juce::TextButton, 12> keys;
    juce::Rectangle<int> keyArea, topArea;
    juce::String status;
    float exprShown = 1.f;
    int shownKey = -2, shownProgram = -1;
    bool shownDrone = true;
};
