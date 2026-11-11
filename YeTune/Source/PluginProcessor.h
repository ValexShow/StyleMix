#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "TuneDSP.h"

class YeTuneProcessor : public juce::AudioProcessor
{
public:
    YeTuneProcessor();
    ~YeTuneProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // Для интерфейса: что поёт голос и куда его тянем (MIDI-номер, -1 = тишина/шум)
    std::atomic<float> shownInput { -1.0f }, shownTarget { -1.0f };

private:
    std::atomic<float>* pKey = nullptr;
    std::atomic<float>* pScale = nullptr;
    std::atomic<float>* pRetune = nullptr;
    std::atomic<float>* pHumanize = nullptr;
    std::atomic<float>* pAmount = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;

    yt::Tuner tuner;
    std::vector<float> dryDelay[2];
    int dryLen = 1, dryPos = 0;
    juce::SmoothedValue<float> mixSm, outSm;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YeTuneProcessor)
};
