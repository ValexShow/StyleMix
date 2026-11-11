#pragma once

#include "PluginProcessor.h"

class YeTuneLookAndFeel : public juce::LookAndFeel_V4
{
public:
    YeTuneLookAndFeel();
    juce::Colour accent { 0xffffc23d };
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

class YeTuneEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit YeTuneEditor (YeTuneProcessor&);
    ~YeTuneEditor() override;

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
    void setupKnob (Knob&, const char* paramId, const char* utf8Name);
    void setParam (const char* id, float value);

    YeTuneProcessor& proc;
    YeTuneLookAndFeel lnf;

    juce::ComboBox keyBox, scaleBox;
    juce::Label keyLabel, scaleLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt, scaleAtt;
    juce::TextButton hardButton, softButton;

    Knob retune, humanize, amount, mix, output;

    float shownIn = -1.0f, shownTarget = -1.0f;
    int shownMask = 0;
    juce::Rectangle<int> displayArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YeTuneEditor)
};
