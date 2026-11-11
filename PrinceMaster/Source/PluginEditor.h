#pragma once

#include "PluginProcessor.h"

class PrinceLookAndFeel : public juce::LookAndFeel_V4
{
public:
    juce::Colour accent { 0xffc8102e };
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

class PrinceMasterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PrinceMasterEditor (PrinceMasterProcessor&);
    ~PrinceMasterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setMode (int index);
    void applyTheme();
    void drawMeter (juce::Graphics&, juce::Rectangle<int>, const juce::String& name, float db, float maxDb);

    PrinceMasterProcessor& proc;
    PrinceLookAndFeel lnf;

    juce::TextButton zavtraButton { juce::String::fromUTF8 ("ЗАВТРА") }, omtButton { "ONE MORE TIME" };

    static constexpr int numSmall = 6;
    juce::Slider powerKnob, smallKnobs[numSmall];
    juce::Label powerLabel, smallLabels[numSmall];

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Attachment> powerAtt, smallAtts[numSmall];

    int shownMode = -1;
    float shownPump = 0.0f, shownLimit = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrinceMasterEditor)
};
