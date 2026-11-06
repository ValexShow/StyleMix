#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "TalkBoxDSP.h"

class TalkBoxProcessor : public juce::AudioProcessor
{
public:
    TalkBoxProcessor();
    ~TalkBoxProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    enum Mode { modeAuto = 0, modeMidi, modeSidechain };

    // Для интерфейса
    std::atomic<float> shownNote { -1.0f }; // нота несущей (MIDI-номер), -1 = тишина
    std::atomic<float> inputLevel { 0.0f };

private:
    static constexpr int kNumVoices = 8;

    void handleMidi (const juce::MidiMessage& m, bool poly);
    void allNotesOff();
    float renderCarrier (int mode, const juce::AudioBuffer<float>& buffer, int sample, int scChannels, int scOffset);

    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pKey = nullptr;
    std::atomic<float>* pScale = nullptr;
    std::atomic<float>* pOctave = nullptr;
    std::atomic<float>* pRobot = nullptr;
    std::atomic<float>* pGlide = nullptr;
    std::atomic<float>* pWave = nullptr;
    std::atomic<float>* pDetune = nullptr;
    std::atomic<float>* pPoly = nullptr;
    std::atomic<float>* pConsonants = nullptr;
    std::atomic<float>* pBright = nullptr;
    std::atomic<float>* pGate = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;

    double sr = 44100.0;
    tb::FormantVocoder vocoder;
    tb::PitchTracker pitch;
    std::array<tb::SynthVoice, kNumVoices> voices;
    std::vector<int> heldNotes; // для моно-режима (последняя нажатая нота)
    uint32_t noiseSeed = 12345u, voiceCounter = 0;
    int lastMode = -1;
    bool lastPoly = true;

    // Компенсация задержки анализа: несущая и сухой голос задерживаются на столько же
    std::vector<float> carrierDelay, dryDelay;
    int delayLen = 1, delayPos = 0;

    // Сайдчейн: пре-эмфазис + нормировка мощности
    float scPrev = 0.0f, scPow = 0.0f, scPowCoef = 0.999f;

    // Определение шипящих (с, ш, ф) для добавления шума
    float hpPrev = 0.0f, hpState = 0.0f, hpCoef = 0.5f, envHi = 0.0f, envAll = 0.0f, envCoef = 0.999f;

    // Гейт и тембр
    float gateEnv = 0.0f, gateGain = 0.0f, gateAtt = 0.01f, gateRel = 0.001f;
    float tiltLp = 0.0f, tiltCoef = 0.5f;
    float autoNote = 57.0f;

    juce::SmoothedValue<float> mixSm, outSm, brightSm, consSm;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TalkBoxProcessor)
};
