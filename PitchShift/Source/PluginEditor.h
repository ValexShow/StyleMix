#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

class VoxShiftLookAndFeel : public juce::LookAndFeel_V4
{
public:
    juce::Colour accent { 0xff4fc3f7 };
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

class VoxShiftEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VoxShiftEditor (VoxShiftProcessor&);
    ~VoxShiftEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setOctave (int octave);
    void refreshOctaveButtons();

    VoxShiftProcessor& proc;
    VoxShiftLookAndFeel lnf;

    juce::OwnedArray<juce::TextButton> octaveButtons;
    juce::Slider semisKnob, fineKnob, formantKnob, naturalKnob, mixKnob, outputKnob;
    juce::Label semisLabel, fineLabel, formantLabel, naturalLabel, mixLabel, outputLabel;

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Attachment> semisAtt, fineAtt, formantAtt, naturalAtt, mixAtt, outputAtt;

    int shownOctave = 99;
    float shownTotal = 1000.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxShiftEditor)
};
