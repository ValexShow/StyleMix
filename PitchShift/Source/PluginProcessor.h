#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "FormantPitchShifter.h"

class VoxShiftProcessor : public juce::AudioProcessor
{
public:
    VoxShiftProcessor();
    ~VoxShiftProcessor() override = default;

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

    // Итоговый сдвиг в полутонах (для экрана)
    float getTotalSemitones() const;

private:
    static constexpr int kMaxChannels = 2;

    pitchshift::FormantPitchShifter shifters[kMaxChannels];
    std::vector<float> dryDelay[kMaxChannels];
    int dryPos = 0, latency = 0;

    juce::SmoothedValue<float> mixSmoothed, gainSmoothed;

    std::atomic<float>* pOctave = nullptr;
    std::atomic<float>* pSemis = nullptr;
    std::atomic<float>* pFine = nullptr;
    std::atomic<float>* pFormant = nullptr;
    std::atomic<float>* pNatural = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxShiftProcessor)
};
