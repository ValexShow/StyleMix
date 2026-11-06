#pragma once

#include "PluginProcessor.h"

class TalkBoxLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TalkBoxLookAndFeel();
    juce::Colour accent { 0xff35e0c8 };
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

class TalkBoxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TalkBoxEditor (TalkBoxProcessor&);
    ~TalkBoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };

    void timerCallback() override;
    void setMode (int index);
    void refreshMode();
    void setupKnob (Knob&, const char* paramId, const char* utf8Name);

    TalkBoxProcessor& proc;
    TalkBoxLookAndFeel lnf;

    juce::TextButton modeButtons[3];
    juce::ComboBox keyBox, scaleBox;
    juce::Label keyLabel, scaleLabel;
    juce::ToggleButton polyButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt, scaleAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> polyAtt;

    Knob octave, robot, glide, wave, detune, consonants, bright, gate, mix, output;

    int shownMode = -1;
    float shownNote = -1.0f, shownLevel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TalkBoxEditor)
};
