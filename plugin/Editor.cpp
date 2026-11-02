// Sanctuary – SPDX-License-Identifier: GPL-3.0-or-later
// Layout and look follow OpenSection (same author); teal accent so the two are easy to tell apart.
#include "Editor.h"

using namespace juce;

namespace col {
const Colour bg { 0xff111719 }, panel { 0xff192225 }, line { 0xff283639 };
const Colour text { 0xffe4ece9 }, dim { 0xff86979a }, accent { 0xff3cc8b4 }, accent2 { 0xffe0c98a };
}

static const char* kKeyNames[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
static constexpr float kW = 960.f, kH = 600.f;

SanctuaryLook::SanctuaryLook()
{
    setColour(Slider::textBoxTextColourId, col::text);
    setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour(Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour(Slider::textBoxHighlightColourId, col::accent.withAlpha(0.3f));
    setColour(TextEditor::outlineColourId, Colours::transparentBlack);
    setColour(TextEditor::focusedOutlineColourId, col::accent);
    setColour(TextEditor::backgroundColourId, col::panel);
    setColour(Label::textColourId, col::dim);
    setColour(ComboBox::backgroundColourId, col::panel);
    setColour(ComboBox::outlineColourId, col::line);
    setColour(ComboBox::textColourId, col::text);
    setColour(ComboBox::arrowColourId, col::accent);
    setColour(PopupMenu::backgroundColourId, col::panel);
    setColour(PopupMenu::textColourId, col::text);
    setColour(PopupMenu::highlightedBackgroundColourId, col::accent.withAlpha(0.3f));
    setColour(TextButton::textColourOffId, col::text);
    setColour(TextButton::textColourOnId, col::bg);
}

void SanctuaryLook::drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider&)
{
    const auto b = Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(6.f);
    const float r = jmin(b.getWidth(), b.getHeight()) / 2.f, cx = b.getCentreX(), cy = b.getCentreY();
    const float ang = start + pos * (end - start), th = 4.f;
    Path track; track.addCentredArc(cx, cy, r - th, r - th, 0.f, start, end, true);
    g.setColour(col::line); g.strokePath(track, PathStrokeType(th, PathStrokeType::curved, PathStrokeType::rounded));
    Path val; val.addCentredArc(cx, cy, r - th, r - th, 0.f, start, ang, true);
    g.setColour(col::accent); g.strokePath(val, PathStrokeType(th, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour(col::panel.brighter(0.15f)); g.fillEllipse(cx - r * 0.62f, cy - r * 0.62f, r * 1.24f, r * 1.24f);
    Path tick; tick.addRoundedRectangle(-1.5f, -r * 0.6f, 3.f, r * 0.32f, 1.5f);
    g.setColour(col::text); g.fillPath(tick, AffineTransform::rotation(ang).translated(cx, cy));
}

void SanctuaryLook::drawButtonBackground(Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(1.5f);
    const float rad = jmin(6.f, r.getHeight() * 0.25f);
    Colour c = b.getToggleState() ? col::accent : col::panel.brighter(0.08f);
    if (!b.isEnabled()) c = c.withAlpha(0.35f);
    else if (down) c = c.brighter(0.25f);
    else if (over) c = c.brighter(0.12f);
    g.setColour(c); g.fillRoundedRectangle(r, rad);
    g.setColour(b.getToggleState() ? col::accent.brighter(0.3f) : col::line);
    g.drawRoundedRectangle(r, rad, 1.f);
}

void SanctuaryLook::drawButtonText(Graphics& g, TextButton& b, bool, bool)
{
    g.setFont(FontOptions(jmin(17.f, b.getHeight() * 0.42f), b.getToggleState() ? Font::bold : Font::plain));
    Colour c = b.getToggleState() ? col::bg : col::text;
    if (!b.isEnabled()) c = col::dim.withAlpha(0.5f);
    g.setColour(c);
    g.drawText(b.getButtonText(), b.getLocalBounds(), Justification::centred);
}

SanctuaryEditor::Knob& SanctuaryEditor::knob(const char* id, const char* text)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle(Slider::TextBoxBelow, false, 70, 16);
    k->slider.setPopupDisplayEnabled(false, false, this);
    k->slider.setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
    k->slider.setColour(Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    k->label.setText(text, dontSendNotification);
    k->label.setJustificationType(Justification::centred);
    k->label.setFont(FontOptions(13.f));
    addAndMakeVisible(k->slider); addAndMakeVisible(k->label);
    k->att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(proc.params, id, k->slider);
    knobs.push_back(std::move(k));
    return *knobs.back();
}

void SanctuaryEditor::setupCombo(Combo& c, const char* id, const char* text)
{
    c.box.addItemList(proc.params.getParameter(id)->getAllValueStrings(), 1);
    addAndMakeVisible(c.box);
    c.att = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(proc.params, id, c.box);
    c.label.setText(text, dontSendNotification); c.label.setFont(FontOptions(13.f));
    addAndMakeVisible(c.label);
}

SanctuaryEditor::SanctuaryEditor(SanctuaryProcessor& p) : AudioProcessorEditor(p), proc(p)
{
    setLookAndFeel(&look);
    row1.push_back({ "LAYERS", { &knob("warm", "Warm"), &knob("air", "Air"), &knob("shimmer", "Shimmer"), &knob("foundation", "Foundation") } });
    row1.push_back({ "TONE", { &knob("shape", "Shape"), &knob("detune", "Detune"), &knob("brightness", "Brightness"),
                               &knob("motion", "Motion"), &knob("width", "Width") } });
    row2.push_back({ "ENVELOPE", { &knob("attack", "Attack"), &knob("release", "Release"), &knob("crossfade", "Crossfade") } });
    row2.push_back({ "HALL", { &knob("size", "Size"), &knob("mix", "Mix"), &knob("shimmerVerb", "Shimmer") } });
    row2.push_back({ "OUTPUT", { &knob("expression", "Swell"), &knob("gain", "Volume") } });

    setupCombo(mode, "mode", "Mode");
    setupCombo(colour, "colour", "Colour");
    setupCombo(exprSource, "exprSource", "Expression pedal");

    for (int i = 0; i < sanctuary::numPresets(); ++i) presetBox.addItem(sanctuary::preset(i).name, i + 1);
    presetBox.setSelectedId(proc.getCurrentProgram() + 1, dontSendNotification);
    presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) choosePreset(presetBox.getSelectedId() - 1); };
    presetLabel.setText("Preset", dontSendNotification); presetLabel.setFont(FontOptions(13.f));
    addAndMakeVisible(presetBox); addAndMakeVisible(presetLabel);
    const int np = sanctuary::numPresets();
    prevPreset.onClick = [this, np] { choosePreset((proc.getCurrentProgram() + np - 1) % np); };
    nextPreset.onClick = [this, np] { choosePreset((proc.getCurrentProgram() + 1) % np); };
    addAndMakeVisible(prevPreset); addAndMakeVisible(nextPreset);

    for (int k = 0; k < 12; ++k) {
        keys[(size_t)k].setButtonText(kKeyNames[k]);
        keys[(size_t)k].setClickingTogglesState(false);
        keys[(size_t)k].onClick = [this, k] { proc.uiKeyRequest.store(k); };
        keys[(size_t)k].setTooltip("Drone key (click again to fade out)");
        addAndMakeVisible(keys[(size_t)k]);
    }

    setResizable(true, true);
    setResizeLimits((int)kW, (int)kH, (int)(kW * 2), (int)(kH * 2));
    getConstrainer()->setFixedAspectRatio(kW / kH);
    setSize((int)kW, (int)kH);
    startTimerHz(15);
}

SanctuaryEditor::~SanctuaryEditor() { setLookAndFeel(nullptr); }

void SanctuaryEditor::choosePreset(int index)
{
    proc.setCurrentProgram(index);
    proc.updateHostDisplay(AudioProcessorListener::ChangeDetails().withProgramChanged(true));
    presetBox.setSelectedId(index + 1, dontSendNotification);
    shownProgram = index;
}

void SanctuaryEditor::timerCallback()
{
    const bool drone = (int)proc.params.getRawParameterValue("mode")->load() == 0;
    const int key = proc.uiDroneKey.load();
    String m = drone ? (key >= 0 ? String("Drone ") + kKeyNames[key] : String("Drone (no key)")) : String("Play");
    status = m + String::fromUTF8("  \xc2\xb7  ") + String(proc.uiVoices.load()) + " voices"
           + String::fromUTF8("  \xc2\xb7  CPU ") + String(roundToInt(proc.uiLoad.load() * 100.f)) + " %";
    exprShown = proc.uiExpression.load();
    if (key != shownKey || drone != shownDrone) {
        for (int k = 0; k < 12; ++k) {
            keys[(size_t)k].setToggleState(drone && k == key, dontSendNotification);
            keys[(size_t)k].setEnabled(drone);
        }
        colour.box.setEnabled(drone);
        shownKey = key; shownDrone = drone;
        repaint(keyArea);
    }
    if (proc.getCurrentProgram() != shownProgram) {
        shownProgram = proc.getCurrentProgram();
        presetBox.setSelectedId(shownProgram + 1, dontSendNotification);
    }
    repaint(0, 0, getWidth(), roundToInt(60 * getWidth() / kW));
}

void SanctuaryEditor::paint(Graphics& g)
{
    const float s = getWidth() / kW;
    g.fillAll(col::bg);
    g.setColour(col::text);
    g.setFont(FontOptions(26.f * s, Font::bold));
    g.drawText("Sanctuary", Rectangle<float>(20 * s, 10 * s, 300 * s, 30 * s), Justification::left);
    g.setColour(col::dim);
    g.setFont(FontOptions(12.f * s));
    g.drawText(String::fromUTF8("computed worship pad & drone  \xc2\xb7  v" JucePlugin_VersionString),
               Rectangle<float>(22 * s, 36 * s, 400 * s, 14 * s), Justification::left);
    g.drawText(status, Rectangle<float>(480 * s, 14 * s, 460 * s, 16 * s), Justification::right);
    // expression meter
    const auto m = Rectangle<float>(820 * s, 38 * s, 120 * s, 5 * s);
    g.setColour(col::line); g.fillRoundedRectangle(m, 2.5f * s);
    g.setColour(col::accent2); g.fillRoundedRectangle(m.withWidth(m.getWidth() * jlimit(0.f, 1.f, exprShown)), 2.5f * s);
    g.setFont(FontOptions(11.f * s));
    g.drawText("pedal", Rectangle<float>(760 * s, 33 * s, 55 * s, 14 * s), Justification::right);

    auto panelWithTitle = [&](Rectangle<int> area, const String& title) {
        auto a = area.toFloat();
        g.setColour(col::panel); g.fillRoundedRectangle(a, 8.f * s);
        g.setColour(col::accent.withAlpha(0.85f));
        g.setFont(FontOptions(11.f * s, Font::bold));
        g.drawText(title, a.removeFromTop(24 * s).reduced(12 * s, 0), Justification::left);
    };
    panelWithTitle(keyArea, shownDrone ? "DRONE KEY" : "DRONE KEY  (switch Mode to Drone)");
    for (auto* row : { &row1, &row2 })
        for (auto& gr : *row) panelWithTitle(gr.area, gr.title);
}

void SanctuaryEditor::resized()
{
    const float s = getWidth() / kW;
    auto S = [s](float v) { return roundToInt(v * s); };
    auto r = getLocalBounds().reduced(S(16));
    r.removeFromTop(S(46));

    // selectors
    topArea = r.removeFromTop(S(34));
    {
        auto t = topArea;
        auto place = [&](Label& l, Component& c, int lw, int cw) {
            l.setFont(FontOptions(13.f * s));
            l.setBounds(t.removeFromLeft(S((float)lw)));
            c.setBounds(t.removeFromLeft(S((float)cw)).reduced(0, S(3)));
            t.removeFromLeft(S(14));
        };
        presetLabel.setFont(FontOptions(13.f * s));
        presetLabel.setBounds(t.removeFromLeft(S(48)));
        prevPreset.setBounds(t.removeFromLeft(S(26)).reduced(0, S(3)));
        presetBox.setBounds(t.removeFromLeft(S(170)).reduced(S(3), S(3)));
        nextPreset.setBounds(t.removeFromLeft(S(26)).reduced(0, S(3)));
        t.removeFromLeft(S(14));
        place(mode.label, mode.box, 42, 90);
        place(colour.label, colour.box, 48, 130);
        place(exprSource.label, exprSource.box, 112, 150);
    }
    r.removeFromTop(S(8));

    // drone keys
    keyArea = r.removeFromTop(S(84));
    {
        auto k = keyArea.reduced(S(10), S(6));
        k.removeFromTop(S(20));
        const int w = k.getWidth() / 12;
        for (int i = 0; i < 12; ++i) keys[(size_t)i].setBounds(k.removeFromLeft(w).reduced(S(3), 0));
    }
    r.removeFromTop(S(10));

    auto layoutRow = [&](std::vector<Group>& groups, Rectangle<int> area) {
        const int gap = S(10);
        float total = 0; for (auto& gr : groups) total += std::max(1.6f, (float)gr.knobs.size());
        const int avail = area.getWidth() - gap * ((int)groups.size() - 1);
        for (auto& gr : groups) {
            const int w = (int)(avail * std::max(1.6f, (float)gr.knobs.size()) / total);
            gr.area = area.removeFromLeft(w);
            area.removeFromLeft(gap);
            auto a = gr.area.reduced(S(6));
            a.removeFromTop(S(22));
            const int kw = a.getWidth() / (int)gr.knobs.size();
            for (auto* kn : gr.knobs) {
                auto c = a.removeFromLeft(kw);
                kn->label.setBounds(c.removeFromTop(S(18)));
                kn->label.setFont(FontOptions(13.f * s));
                kn->slider.setTextBoxStyle(Slider::TextBoxBelow, false, S(70), S(16));
                const int maxH = S(104);                           // same knob size everywhere
                if (c.getHeight() > maxH) c = c.withSizeKeepingCentre(c.getWidth(), maxH);
                kn->slider.setBounds(c.withSizeKeepingCentre(std::min(c.getWidth(), S(80)), c.getHeight()));
            }
        }
    };
    const int rowH = (r.getHeight() - S(10)) / 2;
    layoutRow(row1, r.removeFromTop(rowH));
    r.removeFromTop(S(10));
    layoutRow(row2, r);
}
