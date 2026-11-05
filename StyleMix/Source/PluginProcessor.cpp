#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    float lerp (float a, float b, float t) { return a + (b - a) * t; }

    // Частоты интерполируем логарифмически — так на слух ровнее
    float lerpFreq (float a, float b, float t) { return std::exp (lerp (std::log (a), std::log (b), t)); }

    StyleSettings blend (const StyleSettings& n, const StyleSettings& s, float t)
    {
        StyleSettings r = s;
        r.hpfHz         = lerpFreq (n.hpfHz, s.hpfHz, t);
        r.lowShelfDb    = lerp (n.lowShelfDb, s.lowShelfDb, t);
        r.midDb         = lerp (n.midDb, s.midDb, t);
        r.presDb        = lerp (n.presDb, s.presDb, t);
        r.highShelfDb   = lerp (n.highShelfDb, s.highShelfDb, t);
        r.lpfHz         = lerpFreq (n.lpfHz, s.lpfHz, t);
        r.compThreshDb  = lerp (n.compThreshDb, s.compThreshDb, t);
        r.compRatio     = lerp (n.compRatio, s.compRatio, t);
        r.driveDb       = lerp (n.driveDb, s.driveDb, t);
        r.satAsym       = lerp (n.satAsym, s.satAsym, t);
        r.width         = lerp (n.width, s.width, t);
        r.reverbWet     = lerp (n.reverbWet, s.reverbWet, t);
        r.delayWet      = lerp (n.delayWet, s.delayWet, t);
        r.makeupDb      = lerp (n.makeupDb, s.makeupDb, t);
        return r;
    }
}

StyleMixProcessor::StyleMixProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "StyleMixState", createLayout())
{
    styleParam  = apvts.getRawParameterValue ("style");
    amountParam = apvts.getRawParameterValue ("amount");
    driveParam  = apvts.getRawParameterValue ("drive");
    spaceParam  = apvts.getRawParameterValue ("space");
    outputParam = apvts.getRawParameterValue ("output");
}

juce::AudioProcessorValueTreeState::ParameterLayout StyleMixProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "style", 1 }, "Style",
                                                        StringArray { "Bully", "Militantum" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "amount", 1 }, "Amount",
                                                       NormalisableRange<float> (0.0f, 100.0f, 0.1f), 70.0f,
                                                       AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "drive", 1 }, "Drive",
                                                       NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f,
                                                       AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "space", 1 }, "Space",
                                                       NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f,
                                                       AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
                                                       NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

bool StyleMixProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void StyleMixProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    const auto numCh = (juce::uint32) juce::jmax (1, getTotalNumOutputChannels());
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, numCh };

    for (auto& f : filters) { f.prepare (spec); f.reset(); }
    compressor.prepare (spec);
    reverb.prepare (spec);
    delay.prepare (spec);
    delay.reset();
    outputGain.prepare (spec);
    outputGain.setRampDurationSeconds (0.05);
    limiterEnv = 0.0f;
    limiterRelease = std::exp (-1.0f / (0.08f * (float) sampleRate)); // ~80 мс

    oversampling.initProcessing ((size_t) samplesPerBlock);
    oversampling.reset();
    setLatencySamples ((int) std::round (oversampling.getLatencyInSamples()));

    for (auto* v : { &driveSmoothed, &satMixSmoothed, &widthSmoothed, &delayWetSmoothed })
        v->reset (sampleRate, 0.05);
    delayTimeSmoothed.reset (sampleRate, 0.3);
    delayTimeSmoothed.setCurrentAndTargetValue ((float) (sampleRate * 0.375));

    std::fill (std::begin (dcState), std::end (dcState), 0.0f);
    std::fill (std::begin (dcPrev), std::end (dcPrev), 0.0f);
    std::fill (std::begin (delayFbState), std::end (delayFbState), 0.0f);
    lastStyle = -1;
}

StyleSettings StyleMixProcessor::currentSettings() const
{
    const auto& target = ((int) styleParam->load() == 0) ? kStyleBully : kStyleMilitantum;
    return blend (kNeutral, target, amountParam->load() / 100.0f);
}

void StyleMixProcessor::updateFilters (const StyleSettings& s)
{
    using AC = juce::dsp::IIR::ArrayCoefficients<float>;
    const auto db = [] (float d) { return juce::Decibels::decibelsToGain (d); };
    const float nyq = (float) sr * 0.49f;

    *filters[hpfIdx].state       = AC::makeHighPass (sr, s.hpfHz, 0.707f);
    *filters[lowShelfIdx].state  = AC::makeLowShelf (sr, s.lowShelfHz, 0.7f, db (s.lowShelfDb));
    *filters[midIdx].state       = AC::makePeakFilter (sr, s.midHz, s.midQ, db (s.midDb));
    *filters[presIdx].state      = AC::makePeakFilter (sr, s.presHz, s.presQ, db (s.presDb));
    *filters[highShelfIdx].state = AC::makeHighShelf (sr, juce::jmin (s.highShelfHz, nyq), 0.7f, db (s.highShelfDb));
    *filters[lpfIdx].state       = AC::makeLowPass (sr, juce::jmin (s.lpfHz, nyq), 0.707f);

    compressor.setThreshold (s.compThreshDb);
    compressor.setRatio (juce::jmax (1.0f, s.compRatio));
    compressor.setAttack (s.compAttackMs);
    compressor.setRelease (s.compReleaseMs);
}

void StyleMixProcessor::processSaturation (juce::dsp::AudioBlock<float>& block, const StyleSettings& s, float driveKnob)
{
    // Сатурация на двукратной передискретизации, чтобы не было алиасинга
    auto up = oversampling.processSamplesUp (block);
    const auto n = up.getNumSamples();
    const auto numCh = juce::jmin ((size_t) 2, up.getNumChannels());

    driveSmoothed.setTargetValue (juce::Decibels::decibelsToGain (s.driveDb * driveKnob));
    satMixSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, s.driveDb > 0.01f ? 1.0f : 0.0f));

    const float asym = s.satAsym;
    const float hard = s.satHard;

    for (size_t i = 0; i < n; ++i)
    {
        // сглаживатели настроены на исходную частоту — шагаем через сэмпл
        const float g   = (i % 2 == 0) ? driveSmoothed.getNextValue() : driveSmoothed.getCurrentValue();
        const float mix = (i % 2 == 0) ? satMixSmoothed.getNextValue() : satMixSmoothed.getCurrentValue();
        const float comp = 1.0f / std::pow (g, 0.85f); // грубая компенсация громкости

        for (size_t ch = 0; ch < numCh; ++ch)
        {
            auto* d = up.getChannelPointer (ch);
            const float x  = d[i];
            const float xd = g * (x + asym * x * x);
            const float soft = std::tanh (xd);
            const float clip = juce::jlimit (-1.0f, 1.0f, xd * 1.1f) * 0.92f;
            const float y = lerp (soft, clip, hard) * comp;
            d[i] = lerp (x, y, mix);
        }
    }

    oversampling.processSamplesDown (block);

    // DC-блокер (асимметрия даёт постоянную составляющую)
    for (size_t ch = 0; ch < numCh; ++ch)
    {
        auto* d = block.getChannelPointer (ch);
        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const float x = d[i];
            const float y = x - dcPrev[ch] + 0.9995f * dcState[ch];
            dcPrev[ch] = x;
            dcState[ch] = y;
            d[i] = y;
        }
    }
}

void StyleMixProcessor::processWidth (juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (buffer.getNumChannels() < 2) return;
    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getWritePointer (1);
    for (int i = 0; i < numSamples; ++i)
    {
        const float w = widthSmoothed.getNextValue();
        const float m = 0.5f * (l[i] + r[i]);
        const float s = 0.5f * (l[i] - r[i]) * w;
        l[i] = m + s;
        r[i] = m - s;
    }
}

void StyleMixProcessor::processLimiter (juce::AudioBuffer<float>& buffer, int numSamples)
{
    // Простой пиковый лимитер: потолок -0.3 dBFS, мгновенная атака, плавное отпускание.
    // Ничего не трогает, пока сигнал ниже потолка.
    const float ceiling = juce::Decibels::decibelsToGain (-0.3f);
    const int numCh = buffer.getNumChannels();
    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            peak = juce::jmax (peak, std::abs (buffer.getSample (ch, i)));

        limiterEnv = juce::jmax (peak, limiterEnv * limiterRelease);
        const float g = limiterEnv > ceiling ? ceiling / limiterEnv : 1.0f;

        for (int ch = 0; ch < numCh; ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
    }
}

void StyleMixProcessor::processDelay (juce::AudioBuffer<float>& buffer, int numSamples, const StyleSettings& s, float space)
{
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                if (*b > 20.0) bpm = *b;

    delayTimeSmoothed.setTargetValue ((float) juce::jmin (sr * 1.9, sr * 60.0 / bpm * s.delayBeats));
    delayWetSmoothed.setTargetValue (s.delayWet * space);

    const float fbCoef = std::exp (-juce::MathConstants<float>::twoPi * s.delayToneHz / (float) sr);
    const bool stereo = buffer.getNumChannels() > 1;
    auto* l = buffer.getWritePointer (0);
    auto* r = stereo ? buffer.getWritePointer (1) : l;

    for (int i = 0; i < numSamples; ++i)
    {
        const float t   = delayTimeSmoothed.getNextValue();
        const float wet = delayWetSmoothed.getNextValue();

        const float dl = delay.popSample (0, t);
        const float dr = delay.popSample (stereo ? 1 : 0, t);

        // тёмная петля обратной связи
        delayFbState[0] = dl + fbCoef * (delayFbState[0] - dl);
        delayFbState[1] = dr + fbCoef * (delayFbState[1] - dr);

        const float in = stereo ? 0.5f * (l[i] + r[i]) : l[i];
        if (stereo)
        {
            // пинг-понг: вход -> левый, левый -> правый, правый -> левый
            delay.pushSample (0, in + delayFbState[1] * s.delayFeedback);
            delay.pushSample (1, delayFbState[0] * s.delayFeedback);
            l[i] += dl * wet;
            r[i] += dr * wet;
        }
        else
        {
            delay.pushSample (0, in + delayFbState[0] * s.delayFeedback);
            l[i] += dl * wet;
        }
    }
}

void StyleMixProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    const auto s       = currentSettings();
    const int style    = (int) styleParam->load();
    const float amount = amountParam->load();
    const float drive  = driveParam->load() / 100.0f;
    const float space  = spaceParam->load() / 100.0f;

    if (style != lastStyle || ! juce::exactlyEqual (amount, lastAmount) || ! juce::exactlyEqual (space, lastSpace))
    {
        updateFilters (s);

        juce::dsp::Reverb::Parameters rp;
        rp.roomSize = s.reverbSize;
        rp.damping  = s.reverbDamp;
        rp.wetLevel = juce::jlimit (0.0f, 1.0f, s.reverbWet * space);
        rp.dryLevel = 0.5f; // внутри JUCE Reverb сухой сигнал умножается на 2, так что 0.5 = без изменений
        rp.width    = 1.0f;
        reverb.setParameters (rp);

        lastStyle = style; lastAmount = amount; lastSpace = space;
    }
    widthSmoothed.setTargetValue (s.width);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);

    // 1. EQ: срез низа, полки, середина, presence, срез верха
    for (auto& f : filters)
        f.process (ctx);

    // 2. Склеивающий компрессор
    float preRms = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        preRms = juce::jmax (preRms, buffer.getRMSLevel (ch, 0, numSamples));
    compressor.process (ctx);
    float postRms = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        postRms = juce::jmax (postRms, buffer.getRMSLevel (ch, 0, numSamples));
    if (preRms > 1.0e-5f)
        gainReductionDb.store (juce::Decibels::gainToDecibels (postRms / preRms, -40.0f));

    // 3. Сатурация
    processSaturation (block, s, drive);

    // 4. Стерео-ширина
    processWidth (buffer, numSamples);

    // 5. Пространство: дилей + ревер
    processDelay (buffer, numSamples, s, space);
    if (s.reverbWet * space > 0.0001f)
        reverb.process (ctx);

    // 6. Компенсация громкости + выход + лимитер
    outputGain.setGainDecibels (s.makeupDb + outputParam->load());
    outputGain.process (ctx);
    processLimiter (buffer, numSamples);
}

juce::AudioProcessorEditor* StyleMixProcessor::createEditor()
{
    return new StyleMixEditor (*this);
}

void StyleMixProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void StyleMixProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StyleMixProcessor();
}
