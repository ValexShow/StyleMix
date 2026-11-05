#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "StylePresets.h"

class StyleMixProcessor : public juce::AudioProcessor
{
public:
    StyleMixProcessor();
    ~StyleMixProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // Для индикатора на интерфейсе
    std::atomic<float> gainReductionDb { 0.0f };

private:
    using Filter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                  juce::dsp::IIR::Coefficients<float>>;
    enum { hpfIdx, lowShelfIdx, midIdx, presIdx, highShelfIdx, lpfIdx, numFilters };

    StyleSettings currentSettings() const;
    void updateFilters (const StyleSettings& s);
    void processSaturation (juce::dsp::AudioBlock<float>& block, const StyleSettings& s, float driveKnob);
    void processWidth (juce::AudioBuffer<float>& buffer, int numSamples);
    void processLimiter (juce::AudioBuffer<float>& buffer, int numSamples);
    void processDelay (juce::AudioBuffer<float>& buffer, int numSamples, const StyleSettings& s, float space);

    std::atomic<float>* styleParam  = nullptr;
    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* driveParam  = nullptr;
    std::atomic<float>* spaceParam  = nullptr;
    std::atomic<float>* outputParam = nullptr;

    double sr = 44100.0;
    Filter filters[numFilters];
    juce::dsp::Compressor<float> compressor;
    juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true };
    juce::dsp::Reverb reverb;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay { 192000 * 2 };
    juce::dsp::Gain<float> outputGain;
    float limiterEnv = 0.0f, limiterRelease = 0.999f;

    juce::SmoothedValue<float> driveSmoothed, satMixSmoothed, widthSmoothed, delayWetSmoothed, delayTimeSmoothed;
    float dcState[2] {}, dcPrev[2] {}, delayFbState[2] {};

    // Кэш, чтобы не пересчитывать коэффициенты каждый блок
    int lastStyle = -1;
    float lastAmount = -1.0f, lastSpace = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StyleMixProcessor)
};
