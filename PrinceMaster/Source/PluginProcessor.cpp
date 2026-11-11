#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    float lerp (float a, float b, float t) { return a + (b - a) * t; }

    // Частоты интерполируем логарифмически — так на слух ровнее
    float lerpFreq (float a, float b, float t) { return std::exp (lerp (std::log (a), std::log (b), t)); }

    float onePoleCoef (double sampleRate, float ms)
    {
        return std::exp (-1.0f / (0.001f * ms * (float) sampleRate));
    }

    PrinceSettings blend (const PrinceSettings& n, const PrinceSettings& m, float t)
    {
        PrinceSettings r = m;
        r.hpfHz         = lerpFreq (n.hpfHz, m.hpfHz, t);
        r.lowShelfDb    = lerp (n.lowShelfDb, m.lowShelfDb, t);
        r.mudDb         = lerp (n.mudDb, m.mudDb, t);
        r.midDb         = lerp (n.midDb, m.midDb, t);
        r.highShelfDb   = lerp (n.highShelfDb, m.highShelfDb, t);
        r.pumpDb        = lerp (n.pumpDb, m.pumpDb, t);
        r.driveDb       = lerp (n.driveDb, m.driveDb, t);
        r.distAsym      = lerp (n.distAsym, m.distAsym, t);
        r.distMix       = lerp (n.distMix, m.distMix, t);
        r.crushBits     = lerp (n.crushBits, m.crushBits, t);
        r.crushDownsample = lerp (n.crushDownsample, m.crushDownsample, t);
        r.crushMix      = lerp (n.crushMix, m.crushMix, t);
        r.compThreshDb  = lerp (n.compThreshDb, m.compThreshDb, t);
        r.compRatio     = lerp (n.compRatio, m.compRatio, t);
        // шум смешиваем по амплитуде, а не по децибелам
        r.noiseDb       = juce::Decibels::gainToDecibels (t * juce::Decibels::decibelsToGain (m.noiseDb), -120.0f);
        r.width         = lerp (n.width, m.width, t);
        r.reverbSend    = lerp (n.reverbSend, m.reverbSend, t);
        r.makeupDb      = lerp (n.makeupDb, m.makeupDb, t);
        return r;
    }

    float softClip (float x)
    {
        constexpr float knee = 0.7f;
        const float a = std::abs (x);
        if (a <= knee) return x;
        const float y = knee + (1.0f - knee) * std::tanh ((a - knee) / (1.0f - knee));
        return x < 0.0f ? -y : y;
    }
}

PrinceMasterProcessor::PrinceMasterProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PrinceMasterState", createLayout())
{
    modeParam   = apvts.getRawParameterValue ("mode");
    amountParam = apvts.getRawParameterValue ("amount");
    distParam   = apvts.getRawParameterValue ("dist");
    noiseParam  = apvts.getRawParameterValue ("noise");
    crushParam  = apvts.getRawParameterValue ("crush");
    pumpParam   = apvts.getRawParameterValue ("pump");
    spaceParam  = apvts.getRawParameterValue ("space");
    outputParam = apvts.getRawParameterValue ("output");
}

juce::AudioProcessorValueTreeState::ParameterLayout PrinceMasterProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = [&] (const char* id, const char* name, float max, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
                                                           NormalisableRange<float> (0.0f, max, 0.1f), def,
                                                           AudioParameterFloatAttributes().withLabel ("%")));
    };

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode",
                                                        StringArray { "Zavtra", "One More Time" }, 0));
    percent ("amount", "Power", 100.0f, 70.0f);
    percent ("dist",   "Distortion", 200.0f, 100.0f);
    percent ("noise",  "Noise", 200.0f, 100.0f);
    percent ("crush",  "Digital", 200.0f, 100.0f);
    percent ("pump",   "Pump", 200.0f, 100.0f);
    percent ("space",  "Space", 200.0f, 100.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
                                                       NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

bool PrinceMasterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void PrinceMasterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    numChannels = juce::jlimit (1, 2, getTotalNumOutputChannels());
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, (juce::uint32) numChannels };

    for (auto& f : filters) { f.prepare (spec); f.reset(); }
    sendHpf.prepare (spec); sendHpf.reset();
    sendLpf.prepare (spec); sendLpf.reset();
    *sendHpf.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (sr, 250.0f, 0.707f);
    *sendLpf.state = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (sr, juce::jmin (6000.0f, (float) sr * 0.45f), 0.707f);

    compressor.prepare (spec);
    compressor.reset();
    reverb.prepare (spec);
    reverb.reset();
    outputGain.prepare (spec);
    outputGain.setRampDurationSeconds (0.05);

    sendBuffer.setSize (numChannels, samplesPerBlock, false, true, false);

    oversampling.initProcessing ((size_t) samplesPerBlock);
    oversampling.reset();
    setLatencySamples ((int) std::round (oversampling.getLatencyInSamples()));

    // Качение
    pumpLpCoef   = std::exp (-juce::MathConstants<float>::twoPi * 120.0f / (float) sr);
    keyAtt       = onePoleCoef (sr, 2.0f);
    keyPeakDecay = onePoleCoef (sr, 3000.0f);
    keyEnv = 0.0f; keyPeak = 1.0e-4f; pumpGainState = 1.0f;
    std::fill (std::begin (pumpLp1), std::end (pumpLp1), 0.0f);
    std::fill (std::begin (pumpLp2), std::end (pumpLp2), 0.0f);

    // Фон
    followAtt = onePoleCoef (sr, 10.0f);
    followRel = onePoleCoef (sr, 400.0f);
    followEnv = 0.0f;
    humPhase = 0.0;
    for (int ch = 0; ch < 2; ++ch)
    {
        hissHp[ch] = hissPrev[ch] = hissLp[ch] = 0.0f;
        crackleEnv[ch] = 0.0f;
        dcState[ch] = dcPrev[ch] = 0.0f;
        crushHold[ch] = 0.0f;
    }
    crushPhase = 1.0f;
    distAutoGain = 1.0f;

    limiterEnv = 0.0f;
    limiterRelease = onePoleCoef (sr, 80.0f);

    for (auto* v : { &driveSm, &distMixSm, &crushMixSm, &noiseSm, &pumpDbSm, &widthSm, &sendSm })
        v->reset (sampleRate, 0.05);
    widthSm.setCurrentAndTargetValue (1.0f);
    driveSm.setCurrentAndTargetValue (1.0f);

    lastMode = -1;
}

PrinceSettings PrinceMasterProcessor::currentSettings() const
{
    const auto& target = ((int) modeParam->load() == 0) ? kModeZavtra : kModeOneMoreTime;
    auto s = blend (kNeutral, target, amountParam->load() / 100.0f);

    // Ручки 0..200 %: 100 % = как задумано в режиме
    const float dist  = distParam->load()  / 100.0f;
    const float noise = noiseParam->load() / 100.0f;
    const float crush = crushParam->load() / 100.0f;
    const float pump  = pumpParam->load()  / 100.0f;
    const float space = spaceParam->load() / 100.0f;

    s.driveDb *= dist;
    s.distMix  = juce::jmin (1.0f, s.distMix * dist);

    s.crushMix = juce::jmin (1.0f, s.crushMix * crush);
    s.crushBits = crush <= 1.0f ? lerp (16.0f, s.crushBits, crush)
                                : juce::jmax (4.0f, s.crushBits - (crush - 1.0f) * 3.0f);
    s.crushDownsample = 1.0f + (s.crushDownsample - 1.0f) * crush;

    s.noiseDb += juce::Decibels::gainToDecibels (noise, -120.0f);
    s.pumpDb  *= pump;

    s.reverbSend *= space;
    s.width = 1.0f + (s.width - 1.0f) * juce::jmin (space, 1.5f);
    return s;
}

void PrinceMasterProcessor::updateCoefficients (const PrinceSettings& s)
{
    using AC = juce::dsp::IIR::ArrayCoefficients<float>;
    const auto db = [] (float d) { return juce::Decibels::decibelsToGain (d); };
    const float nyq = (float) sr * 0.45f;

    *filters[hpfIdx].state       = AC::makeHighPass (sr, s.hpfHz, 0.707f);
    *filters[lowShelfIdx].state  = AC::makeLowShelf (sr, s.lowShelfHz, 0.7f, db (s.lowShelfDb));
    *filters[mudIdx].state       = AC::makePeakFilter (sr, s.mudHz, s.mudQ, db (s.mudDb));
    *filters[midIdx].state       = AC::makePeakFilter (sr, juce::jmin (s.midHz, nyq), s.midQ, db (s.midDb));
    *filters[highShelfIdx].state = AC::makeHighShelf (sr, juce::jmin (s.highShelfHz, nyq), 0.7f, db (s.highShelfDb));

    compressor.setThreshold (s.compThreshDb);
    compressor.setRatio (juce::jmax (1.0f, s.compRatio));
    compressor.setAttack (s.compAttackMs);
    compressor.setRelease (s.compReleaseMs);

    keyRel = onePoleCoef (sr, s.pumpReleaseMs);

    juce::dsp::Reverb::Parameters rp;
    rp.roomSize = s.reverbSize;
    rp.damping  = s.reverbDamp;
    rp.wetLevel = 0.33f;   // внутри JUCE Reverb мокрый сигнал умножается на 3
    rp.dryLevel = 0.0f;    // ревер работает на посыле, сухой сигнал не трогаем
    rp.width    = 1.0f;
    reverb.setParameters (rp);

    humAmount     = s.humAmount;
    crackleAmount = s.crackleAmount;
}

// Качение: бочка (всё ниже ~120 Гц) «толкает» середину и верх вниз — как сайдчейн.
void PrinceMasterProcessor::processPump (juce::AudioBuffer<float>& buffer, int numSamples)
{
    const int nch = juce::jmin (numChannels, buffer.getNumChannels());
    float maxDuck = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float depthDb = pumpDbSm.getNextValue();
        float low[2] {}, key = 0.0f;

        for (int ch = 0; ch < nch; ++ch)
        {
            const float x = buffer.getSample (ch, i);
            pumpLp1[ch] = x + pumpLpCoef * (pumpLp1[ch] - x);
            pumpLp2[ch] = pumpLp1[ch] + pumpLpCoef * (pumpLp2[ch] - pumpLp1[ch]);
            low[ch] = pumpLp2[ch];
            key += std::abs (low[ch]);
        }
        key /= (float) nch;

        const float c = key > keyEnv ? keyAtt : keyRel;
        keyEnv = key + c * (keyEnv - key);
        keyPeak = juce::jmax (keyEnv, juce::jmax (1.0e-4f, keyPeak * keyPeakDecay));

        // тихий низ (< -50 dB) не качает
        const float gate = juce::jlimit (0.0f, 1.0f, (keyPeak - 0.003f) / 0.01f);
        const float ratio = keyEnv / keyPeak;
        const float duckDb = depthDb * ratio * ratio * gate;
        maxDuck = juce::jmax (maxDuck, duckDb);

        const float g = juce::Decibels::decibelsToGain (-duckDb);
        const float gLow = lerp (1.0f, g, 0.2f);   // сама бочка почти не проседает

        for (int ch = 0; ch < nch; ++ch)
        {
            const float x = buffer.getSample (ch, i);
            const float high = x - low[ch];
            buffer.setSample (ch, i, low[ch] * gLow + high * g);
        }
    }

    pumpMeterDb.store (maxDuck);
}

void PrinceMasterProcessor::processDistortion (juce::dsp::AudioBlock<float>& block, const PrinceSettings& s)
{
    const auto nch = juce::jmin ((size_t) numChannels, block.getNumChannels());
    const auto numSamples = block.getNumSamples();

    float preSq = 0.0f;
    for (size_t ch = 0; ch < nch; ++ch)
        for (size_t i = 0; i < numSamples; ++i)
            preSq += block.getSample ((int) ch, (int) i) * block.getSample ((int) ch, (int) i);

    driveSm.setTargetValue (juce::Decibels::decibelsToGain (s.driveDb));
    distMixSm.setTargetValue (s.distMix);

    auto up = oversampling.processSamplesUp (block);
    const auto n = up.getNumSamples();
    const float asym = s.distAsym;
    const float hard = s.distHard;

    for (size_t i = 0; i < n; ++i)
    {
        // сглаживатели настроены на исходную частоту — шагаем через сэмпл
        const float g   = (i % 2 == 0) ? driveSm.getNextValue()   : driveSm.getCurrentValue();
        const float mix = (i % 2 == 0) ? distMixSm.getNextValue() : distMixSm.getCurrentValue();
        const float comp = 1.0f / std::sqrt (g);

        for (size_t ch = 0; ch < nch; ++ch)
        {
            auto* d = up.getChannelPointer (ch);
            const float x  = d[i];
            const float xd = g * (x + asym * x * x);
            const float fuzz = std::tanh (xd);
            const float clip = juce::jlimit (-1.0f, 1.0f, xd * 1.2f) * 0.9f;
            d[i] = x + (lerp (fuzz, clip, hard) * comp - x) * mix;
        }
    }

    oversampling.processSamplesDown (block);

    // DC-блокер (асимметрия даёт постоянную составляющую)
    float postSq = 0.0f;
    for (size_t ch = 0; ch < nch; ++ch)
    {
        auto* d = block.getChannelPointer (ch);
        for (size_t i = 0; i < numSamples; ++i)
        {
            const float x = d[i];
            const float y = x - dcPrev[ch] + 0.9995f * dcState[ch];
            dcPrev[ch] = x;
            dcState[ch] = y;
            d[i] = y;
            postSq += y * y;
        }
    }

    // Автокомпенсация: искажение меняет характер, но не громкость
    float target = 1.0f;
    if (preSq > 1.0e-6f * (float) numSamples && postSq > 1.0e-9f)
        target = juce::jlimit (0.25f, 2.0f, std::sqrt (preSq / postSq));

    const float blockSec = (float) numSamples / (float) sr;
    const float a = 1.0f - std::exp (-blockSec / 0.3f);
    const float newGain = distAutoGain + (target - distAutoGain) * a;
    for (size_t ch = 0; ch < nch; ++ch)
    {
        auto* d = block.getChannelPointer (ch);
        for (size_t i = 0; i < numSamples; ++i)
            d[i] *= lerp (distAutoGain, newGain, (float) i / (float) numSamples);
    }
    distAutoGain = newGain;
}

// Цифра: понижение разрядности + «ступеньки» от понижения частоты
void PrinceMasterProcessor::processCrush (juce::AudioBuffer<float>& buffer, int numSamples, const PrinceSettings& s)
{
    crushMixSm.setTargetValue (s.crushMix);
    if (s.crushMix < 1.0e-4f && ! crushMixSm.isSmoothing())
    {
        crushMixSm.skip (numSamples);
        return;
    }

    const int nch = juce::jmin (numChannels, buffer.getNumChannels());
    const float levels = std::pow (2.0f, juce::jmax (1.0f, s.crushBits - 1.0f));
    const float step = 1.0f / juce::jmax (1.0f, s.crushDownsample);

    for (int i = 0; i < numSamples; ++i)
    {
        const float mix = crushMixSm.getNextValue();
        crushPhase += step;
        const bool take = crushPhase >= 1.0f;
        if (take) crushPhase -= 1.0f;

        for (int ch = 0; ch < nch; ++ch)
        {
            const float x = buffer.getSample (ch, i);
            if (take)
                crushHold[ch] = std::round (x * levels) / levels;
            buffer.setSample (ch, i, x + (crushHold[ch] - x) * mix);
        }
    }
}

// Фон: шипение плёнки, гул 50 Гц и треск. Дышит вместе с треком:
// громче на громких местах, почти пропадает в тишине.
void PrinceMasterProcessor::processNoise (juce::AudioBuffer<float>& buffer, int numSamples, const PrinceSettings& s)
{
    const float level = s.noiseDb > -100.0f ? juce::Decibels::decibelsToGain (s.noiseDb) : 0.0f;
    noiseSm.setTargetValue (level);
    if (level <= 0.0f && ! noiseSm.isSmoothing())
    {
        noiseSm.skip (numSamples);
        return;
    }

    const int nch = juce::jmin (numChannels, buffer.getNumChannels());
    const float hpCoef = std::exp (-juce::MathConstants<float>::twoPi * 2500.0f / (float) sr);
    const float lpCoef = std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (11000.0f, (float) sr * 0.4f) / (float) sr);
    const float crackleDecay = onePoleCoef (sr, 0.4f);
    const float crackleProb = 7.0f / (float) sr;
    const double humInc = juce::MathConstants<double>::twoPi * 50.0 / sr;

    for (int i = 0; i < numSamples; ++i)
    {
        float in = 0.0f;
        for (int ch = 0; ch < nch; ++ch)
            in = juce::jmax (in, std::abs (buffer.getSample (ch, i)));
        const float c = in > followEnv ? followAtt : followRel;
        followEnv = in + c * (followEnv - in);

        const float gain = noiseSm.getNextValue() * (0.03f + 0.97f * juce::jmin (1.0f, followEnv * 2.5f));

        humPhase += humInc;
        if (humPhase > juce::MathConstants<double>::twoPi) humPhase -= juce::MathConstants<double>::twoPi;
        const float p = (float) humPhase;
        const float hum = 0.6f * std::sin (p) + 0.25f * std::sin (3.0f * p) + 0.15f * std::sin (5.0f * p);

        for (int ch = 0; ch < nch; ++ch)
        {
            const float w = random.nextFloat() * 2.0f - 1.0f;
            hissHp[ch] = hpCoef * (hissHp[ch] + w - hissPrev[ch]);
            hissPrev[ch] = w;
            hissLp[ch] = hissHp[ch] + lpCoef * (hissLp[ch] - hissHp[ch]);

            if (random.nextFloat() < crackleProb)
            {
                crackleEnv[ch] = 0.3f + 0.7f * random.nextFloat();
                crackleSign[ch] = random.nextBool() ? 1.0f : -1.0f;
            }
            const float crackle = crackleSign[ch] * crackleEnv[ch] * (0.5f + 0.5f * std::abs (w));
            crackleEnv[ch] *= crackleDecay;

            const float noise = 1.5f * hissLp[ch] + humAmount * 0.6f * hum + crackleAmount * 4.0f * crackle;
            buffer.setSample (ch, i, buffer.getSample (ch, i) + noise * gain);
        }
    }
}

void PrinceMasterProcessor::processWidth (juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (buffer.getNumChannels() < 2 || numChannels < 2)
    {
        widthSm.skip (numSamples);
        return;
    }
    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getWritePointer (1);
    for (int i = 0; i < numSamples; ++i)
    {
        const float w = widthSm.getNextValue();
        const float m = 0.5f * (l[i] + r[i]);
        const float s = 0.5f * (l[i] - r[i]) * w;
        l[i] = m + s;
        r[i] = m - s;
    }
}

// Ревер на посыле: в зал уходит только середина (без низа и колючего верха),
// поэтому бочка и саб остаются сухими и плотными.
void PrinceMasterProcessor::processReverb (juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (sendSm.getTargetValue() < 1.0e-4f && ! sendSm.isSmoothing())
    {
        sendSm.skip (numSamples);
        return;
    }

    const int nch = juce::jmin (numChannels, buffer.getNumChannels());
    if (sendBuffer.getNumSamples() < numSamples)
        sendBuffer.setSize (numChannels, numSamples, false, false, true);

    for (int ch = 0; ch < nch; ++ch)
        sendBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = sendSm.getNextValue();
        for (int ch = 0; ch < nch; ++ch)
            sendBuffer.setSample (ch, i, sendBuffer.getSample (ch, i) * g);
    }

    juce::dsp::AudioBlock<float> sendBlock (sendBuffer.getArrayOfWritePointers(), (size_t) nch, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> ctx (sendBlock);
    sendHpf.process (ctx);
    sendLpf.process (ctx);
    reverb.process (ctx);

    for (int ch = 0; ch < nch; ++ch)
        buffer.addFrom (ch, 0, sendBuffer, ch, 0, numSamples);
}

// Выход: мягкий клиппер «дожимает» громкость, пиковый лимитер держит потолок -0.3 dBFS.
void PrinceMasterProcessor::processOutput (juce::AudioBuffer<float>& buffer, int numSamples)
{
    const float ceiling = juce::Decibels::decibelsToGain (-0.3f);
    const int nch = juce::jmin (numChannels, buffer.getNumChannels());
    float minGain = 1.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < nch; ++ch)
        {
            const float y = softClip (buffer.getSample (ch, i));
            buffer.setSample (ch, i, y);
            peak = juce::jmax (peak, std::abs (y));
        }

        limiterEnv = juce::jmax (peak, limiterEnv * limiterRelease);
        const float g = limiterEnv > ceiling ? ceiling / limiterEnv : 1.0f;
        minGain = juce::jmin (minGain, g);

        for (int ch = 0; ch < nch; ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
    }

    limiterMeterDb.store (juce::Decibels::gainToDecibels (minGain, -40.0f));
}

void PrinceMasterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
    if (numSamples == 0)
        return;

    const auto s       = currentSettings();
    const int mode     = (int) modeParam->load();
    const float amount = amountParam->load();
    const float space  = spaceParam->load();
    const float pump   = pumpParam->load();

    if (mode != lastMode || ! juce::exactlyEqual (amount, lastAmount)
        || ! juce::exactlyEqual (space, lastSpace) || ! juce::exactlyEqual (pump, lastPump))
    {
        updateCoefficients (s);
        lastMode = mode; lastAmount = amount; lastSpace = space; lastPump = pump;
    }

    pumpDbSm.setTargetValue (s.pumpDb);
    widthSm.setTargetValue (s.width);
    sendSm.setTargetValue (s.reverbSend);

    juce::dsp::AudioBlock<float> fullBlock (buffer);
    auto block = fullBlock.getSubsetChannelBlock (0, (size_t) juce::jmin (numChannels, buffer.getNumChannels()));
    juce::dsp::ProcessContextReplacing<float> ctx (block);

    // 1. EQ: срез низа, толстый низ, чистка «бубнения», гнусавая середина, верх
    for (auto& f : filters)
        f.process (ctx);

    // 2. Качение от бочки
    processPump (buffer, numSamples);

    // 3. Искажение (фузз / клиппер)
    processDistortion (block, s);

    // 4. Цифровая грязь
    processCrush (buffer, numSamples, s);

    // 5. Склейка
    compressor.process (ctx);

    // 6. Фон: шипение, гул, треск
    processNoise (buffer, numSamples, s);

    // 7. Ширина и зал
    processWidth (buffer, numSamples);
    processReverb (buffer, numSamples);

    // 8. Громкость, клиппер, лимитер
    outputGain.setGainDecibels (s.makeupDb + outputParam->load());
    outputGain.process (ctx);
    processOutput (buffer, numSamples);
}

juce::AudioProcessorEditor* PrinceMasterProcessor::createEditor()
{
    return new PrinceMasterEditor (*this);
}

void PrinceMasterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PrinceMasterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PrinceMasterProcessor();
}
