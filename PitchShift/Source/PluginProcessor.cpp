#include "PluginProcessor.h"
#include "PluginEditor.h"

VoxShiftProcessor::VoxShiftProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VoxShiftState", createLayout())
{
    pOctave  = apvts.getRawParameterValue ("octave");
    pSemis   = apvts.getRawParameterValue ("semitones");
    pFine    = apvts.getRawParameterValue ("fine");
    pFormant = apvts.getRawParameterValue ("formant");
    pNatural = apvts.getRawParameterValue ("natural");
    pMix     = apvts.getRawParameterValue ("mix");
    pOutput  = apvts.getRawParameterValue ("output");
}

juce::AudioProcessorValueTreeState::ParameterLayout VoxShiftProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "octave", 1 }, "Octave", -2, 2, 0));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "semitones", 1 }, "Semitones", -12, 12, 0,
                                                     AudioParameterIntAttributes().withLabel ("st")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "fine", 1 }, "Fine",
                                                       NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("ct")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "formant", 1 }, "Formant",
                                                       NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("st")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "natural", 1 }, "Natural",
                                                       NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
                                                       AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
                                                       NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
                                                       AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
                                                       NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

bool VoxShiftProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

float VoxShiftProcessor::getTotalSemitones() const
{
    return pOctave->load() * 12.0f + pSemis->load() + pFine->load() / 100.0f;
}

void VoxShiftProcessor::prepareToPlay (double sampleRate, int)
{
    for (auto& s : shifters)
        s.prepare (sampleRate);

    latency = shifters[0].getLatency();
    setLatencySamples (latency);

    for (auto& d : dryDelay)
        d.assign ((size_t) latency + 1, 0.0f);
    dryPos = 0;

    mixSmoothed.reset (sampleRate, 0.05);
    gainSmoothed.reset (sampleRate, 0.05);
    mixSmoothed.setCurrentAndTargetValue (pMix->load() / 100.0f);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
}

void VoxShiftProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), kMaxChannels);
    const int n = buffer.getNumSamples();

    const float ratio = std::pow (2.0f, getTotalSemitones() / 12.0f);
    const float formantRatio = std::pow (2.0f, pFormant->load() / 12.0f);
    const float preserve = pNatural->load() / 100.0f;

    mixSmoothed.setTargetValue (pMix->load() / 100.0f);
    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));

    for (int ch = 0; ch < numCh; ++ch)
        shifters[ch].setParameters (ratio, formantRatio, preserve);

    // Обрабатываем кусками через стековый буфер (без выделения памяти в аудиопотоке).
    constexpr int chunk = 256;
    float wet[kMaxChannels][chunk];
    const int dlen = latency + 1;

    for (int start = 0; start < n; start += chunk)
    {
        const int len = juce::jmin (chunk, n - start);

        for (int ch = 0; ch < numCh; ++ch)
        {
            juce::FloatVectorOperations::copy (wet[ch], buffer.getReadPointer (ch, start), len);
            shifters[ch].process (wet[ch], len);
        }

        for (int i = 0; i < len; ++i)
        {
            const float mix = mixSmoothed.getNextValue();
            const float g = gainSmoothed.getNextValue();
            const int readPos = (dryPos + 1) % dlen; // самый старый отсчёт = задержка на latency

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* data = buffer.getWritePointer (ch, start);
                auto& d = dryDelay[ch];
                d[(size_t) dryPos] = data[i];
                const float dry = d[(size_t) readPos];
                data[i] = (dry + (wet[ch][i] - dry) * mix) * g;
            }
            dryPos = readPos;
        }
    }

    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);
}

void VoxShiftProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VoxShiftProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* VoxShiftProcessor::createEditor()
{
    return new VoxShiftEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoxShiftProcessor();
}
