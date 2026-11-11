#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PrincePresets.h"

class PrinceMasterProcessor : public juce::AudioProcessor
{
public:
    PrinceMasterProcessor();
    ~PrinceMasterProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // Для индикаторов на интерфейсе
    std::atomic<float> pumpMeterDb { 0.0f };
    std::atomic<float> limiterMeterDb { 0.0f };

private:
    using Filter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                  juce::dsp::IIR::Coefficients<float>>;
    enum { hpfIdx, lowShelfIdx, mudIdx, midIdx, highShelfIdx, numFilters };

    PrinceSettings currentSettings() const;
    void updateCoefficients (const PrinceSettings& s);
    void processPump (juce::AudioBuffer<float>&, int numSamples);
    void processDistortion (juce::dsp::AudioBlock<float>& block, const PrinceSettings& s);
    void processCrush (juce::AudioBuffer<float>&, int numSamples, const PrinceSettings& s);
    void processNoise (juce::AudioBuffer<float>&, int numSamples, const PrinceSettings& s);
    void processWidth (juce::AudioBuffer<float>&, int numSamples);
    void processReverb (juce::AudioBuffer<float>&, int numSamples);
    void processOutput (juce::AudioBuffer<float>&, int numSamples);

    std::atomic<float>* modeParam   = nullptr;
    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* distParam   = nullptr;
    std::atomic<float>* noiseParam  = nullptr;
    std::atomic<float>* crushParam  = nullptr;
    std::atomic<float>* pumpParam   = nullptr;
    std::atomic<float>* spaceParam  = nullptr;
    std::atomic<float>* outputParam = nullptr;

    double sr = 44100.0;
    int numChannels = 2;

    Filter filters[numFilters];
    juce::dsp::Compressor<float> compressor;
    juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };
    juce::dsp::Reverb reverb;
    Filter sendHpf, sendLpf;
    juce::AudioBuffer<float> sendBuffer;

    // Качение
    float pumpLp1[2] {}, pumpLp2[2] {}, pumpLpCoef = 0.0f;
    float keyEnv = 0.0f, keyPeak = 1.0e-4f, keyAtt = 0.0f, keyRel = 0.0f, keyPeakDecay = 0.0f;
    float pumpGainState = 1.0f;

    // Искажение: автоматическая компенсация громкости
    float distAutoGain = 1.0f;
    float dcState[2] {}, dcPrev[2] {};

    // Цифра
    float crushHold[2] {}, crushPhase = 1.0f;
    float crushBits = 16.0f, crushDs = 1.0f;

    // Фон
    juce::Random random;
    float hissHp[2] {}, hissPrev[2] {}, hissLp[2] {};
    double humPhase = 0.0;
    float crackleEnv[2] {}, crackleSign[2] { 1.0f, 1.0f };
    float followEnv = 0.0f, followAtt = 0.0f, followRel = 0.0f;
    float humAmount = 0.0f, crackleAmount = 0.0f;

    // Выход
    juce::dsp::Gain<float> outputGain;
    float limiterEnv = 0.0f, limiterRelease = 0.999f, limiterMin = 1.0f;

    juce::SmoothedValue<float> driveSm, distMixSm, crushMixSm, noiseSm, pumpDbSm, widthSm, sendSm;

    // Кэш, чтобы не пересчитывать коэффициенты каждый блок
    int lastMode = -1;
    float lastAmount = -1.0f, lastSpace = -1.0f, lastPump = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrinceMasterProcessor)
};
