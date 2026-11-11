#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::StringArray kKeys { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

YeTuneProcessor::YeTuneProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "YeTune", createLayout())
{
    pKey      = apvts.getRawParameterValue ("key");
    pScale    = apvts.getRawParameterValue ("scale");
    pRetune   = apvts.getRawParameterValue ("retune");
    pHumanize = apvts.getRawParameterValue ("humanize");
    pAmount   = apvts.getRawParameterValue ("amount");
    pMix      = apvts.getRawParameterValue ("mix");
    pOutput   = apvts.getRawParameterValue ("output");
}

juce::AudioProcessorValueTreeState::ParameterLayout YeTuneProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, String::fromUTF8 ("Тональность"), kKeys, 9));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, String::fromUTF8 ("Лад"),
        StringArray { String::fromUTF8 ("Хроматический (все ноты)"), String::fromUTF8 ("Мажор"), String::fromUTF8 ("Минор"),
                      String::fromUTF8 ("Гармонический минор"), String::fromUTF8 ("Минорная пентатоника"),
                      String::fromUTF8 ("Мажорная пентатоника"), String::fromUTF8 ("Блюз") }, 2));

    auto msAttr = AudioParameterFloatAttributes().withLabel ("ms")
        .withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? String::fromUTF8 ("0 (робот)") : String (v, 0) + " ms"; });
    NormalisableRange<float> retuneRange (0.0f, 400.0f, 1.0f);
    retuneRange.setSkewForCentre (60.0f);
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "retune", 1 }, String::fromUTF8 ("Скорость"), retuneRange, 0.0f, msAttr));

    auto pct = AudioParameterFloatAttributes().withLabel ("%")
        .withStringFromValueFunction ([] (float v, int) { return String (v, 0) + " %"; });
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "humanize", 1 }, String::fromUTF8 ("Живость"),
                                                  NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "amount", 1 }, String::fromUTF8 ("Сила"),
                                                  NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, String::fromUTF8 ("Микс"),
                                                  NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, String::fromUTF8 ("Громкость"),
                                                  NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")
                                                      .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    return l;
}

bool YeTuneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void YeTuneProcessor::prepareToPlay (double sampleRate, int)
{
    tuner.prepare (sampleRate);
    setLatencySamples (tuner.getLatency());

    dryLen = tuner.getLatency() + 1;
    for (auto& d : dryDelay) d.assign ((size_t) dryLen, 0.0f);
    dryPos = 0;

    mixSm.reset (sampleRate, 0.05);
    outSm.reset (sampleRate, 0.05);
    mixSm.setCurrentAndTargetValue (pMix->load() * 0.01f);
    outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
}

void YeTuneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());
    for (int c = numCh; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());
    if (numCh == 0) return;

    const int key = (int) pKey->load();
    const int scale = (int) pScale->load();
    tuner.mask = yt::scaleMask (key, scale);
    tuner.retuneMs = pRetune->load();
    tuner.humanize = pHumanize->load() * 0.01f;
    tuner.amount = pAmount->load() * 0.01f;
    mixSm.setTargetValue (pMix->load() * 0.01f);
    outSm.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));

    float* ch[2] = { buffer.getWritePointer (0), buffer.getWritePointer (numCh > 1 ? 1 : 0) };
    float x[2], y[2];

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        for (int c = 0; c < numCh; ++c) x[c] = ch[c][i];
        tuner.process (x, y, numCh);

        const float mix = mixSm.getNextValue();
        const float out = outSm.getNextValue();
        for (int c = 0; c < numCh; ++c)
        {
            const float dry = dryDelay[c][(size_t) dryPos];
            dryDelay[c][(size_t) dryPos] = x[c];
            ch[c][i] = (y[c] * mix + dry * (1.0f - mix)) * out;
        }
        if (++dryPos >= dryLen) dryPos = 0;
    }

    shownInput.store (tuner.detectedMidi);
    shownTarget.store (tuner.targetMidi);
}

juce::AudioProcessorEditor* YeTuneProcessor::createEditor() { return new YeTuneEditor (*this); }

void YeTuneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void YeTuneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new YeTuneProcessor(); }
