#pragma once

#include "PluginProcessor.h"

class StyleMixLookAndFeel : public juce::LookAndFeel_V4
{
public:
    juce::Colour accent { 0xffd9a441 };
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

class StyleMixEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit StyleMixEditor (StyleMixProcessor&);
    ~StyleMixEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setStyle (int index);
    void applyTheme();

    StyleMixProcessor& proc;
    StyleMixLookAndFeel lnf;

    juce::TextButton bullyButton { "BULLY" }, militantumButton { "MILITANTUM" };
    juce::Slider amountKnob, driveKnob, spaceKnob, outputKnob;
    juce::Label amountLabel, driveLabel, spaceLabel, outputLabel;

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Attachment> amountAtt, driveAtt, spaceAtt, outputAtt;

    int shownStyle = -1;
    float shownGr = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StyleMixEditor)
};
