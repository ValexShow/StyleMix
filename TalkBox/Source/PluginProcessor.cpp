#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Подобрано на тестовом голосе: пила после формантного фильтра громче голоса на ~4 dB
    constexpr float kMakeup = 0.63f;

    float onePoleCoef (double seconds, double sr)
    {
        return seconds <= 0.0 ? 0.0f : (float) std::exp (-1.0 / (seconds * sr));
    }
}

TalkBoxProcessor::TalkBoxProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "TalkBoxState", createLayout())
{
    pMode       = apvts.getRawParameterValue ("mode");
    pKey        = apvts.getRawParameterValue ("key");
    pScale      = apvts.getRawParameterValue ("scale");
    pOctave     = apvts.getRawParameterValue ("octave");
    pRobot      = apvts.getRawParameterValue ("robot");
    pGlide      = apvts.getRawParameterValue ("glide");
    pWave       = apvts.getRawParameterValue ("wave");
    pDetune     = apvts.getRawParameterValue ("detune");
    pPoly       = apvts.getRawParameterValue ("poly");
    pConsonants = apvts.getRawParameterValue ("consonants");
    pBright     = apvts.getRawParameterValue ("bright");
    pGate       = apvts.getRawParameterValue ("gate");
    pMix        = apvts.getRawParameterValue ("mix");
    pOutput     = apvts.getRawParameterValue ("output");
}

juce::AudioProcessorValueTreeState::ParameterLayout TalkBoxProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = AudioParameterFloatAttributes().withLabel ("%");

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, String::fromUTF8 ("Режим"),
        StringArray { String::fromUTF8 ("Авто (по голосу)"), "MIDI", String::fromUTF8 ("Сайдчейн (свой синт)") }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, String::fromUTF8 ("Тональность"),
        StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 9));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, String::fromUTF8 ("Лад"),
        StringArray { String::fromUTF8 ("Хроматика"), String::fromUTF8 ("Мажор"), String::fromUTF8 ("Минор"),
                      String::fromUTF8 ("Минор пентатоника"), String::fromUTF8 ("Дорийский"), String::fromUTF8 ("Блюз") }, 2));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "octave", 1 }, String::fromUTF8 ("Октава"), -2, 2, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "robot", 1 }, String::fromUTF8 ("Роботность"),
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "glide", 1 }, String::fromUTF8 ("Глайд"),
        NormalisableRange<float> (0.0f, 400.0f, 0.1f, 0.5f), 35.0f, AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "wave", 1 }, String::fromUTF8 ("Волна"),
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "detune", 1 }, String::fromUTF8 ("Расстройка"),
        NormalisableRange<float> (0.0f, 50.0f, 0.1f), 7.0f, AudioParameterFloatAttributes().withLabel ("ct")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "poly", 1 }, String::fromUTF8 ("Аккорды"), true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "consonants", 1 }, String::fromUTF8 ("Согласные"),
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "bright", 1 }, String::fromUTF8 ("Яркость"),
        NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 20.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gate", 1 }, String::fromUTF8 ("Гейт"),
        NormalisableRange<float> (-90.0f, -20.0f, 0.1f), -55.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, String::fromUTF8 ("Микс"),
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, String::fromUTF8 ("Громкость"),
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

bool TalkBoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (in  != juce::AudioChannelSet::mono() && in  != juce::AudioChannelSet::stereo()) return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void TalkBoxProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    vocoder.prepare (sr);
    pitch.prepare (sr);
    for (auto& v : voices) v.prepare (sr);
    heldNotes.clear();
    heldNotes.reserve (128);

    delayLen = juce::jmax (1, vocoder.getLatency());
    carrierDelay.assign ((size_t) delayLen, 0.0f);
    dryDelay.assign ((size_t) delayLen, 0.0f);
    delayPos = 0;
    setLatencySamples (vocoder.getLatency());

    scPrev = 0.0f; scPow = 0.0f;
    scPowCoef = onePoleCoef (0.03, sr);

    hpPrev = hpState = envHi = envAll = 0.0f;
    hpCoef = onePoleCoef (1.0 / (2.0 * juce::MathConstants<double>::pi * 3500.0), sr);
    envCoef = onePoleCoef (0.012, sr);

    gateEnv = gateGain = 0.0f;
    gateAtt = 1.0f - onePoleCoef (0.002, sr);
    gateRel = 1.0f - onePoleCoef (0.06, sr);
    tiltLp = 0.0f;
    tiltCoef = onePoleCoef (1.0 / (2.0 * juce::MathConstants<double>::pi * 1800.0), sr);

    for (auto* s : { &mixSm, &outSm, &brightSm, &consSm }) s->reset (sr, 0.03);
    mixSm.setCurrentAndTargetValue (pMix->load() / 100.0f);
    outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    brightSm.setCurrentAndTargetValue (pBright->load() / 100.0f);
    consSm.setCurrentAndTargetValue (pConsonants->load() / 100.0f);

    lastMode = -1;
}

void TalkBoxProcessor::allNotesOff()
{
    for (auto& v : voices) v.kill();
    heldNotes.clear();
}

void TalkBoxProcessor::handleMidi (const juce::MidiMessage& m, bool poly)
{
    noiseSeed = noiseSeed * 1664525u + 1013904223u;

    if (m.isNoteOn())
    {
        const int note = m.getNoteNumber();
        const float vel = m.getFloatVelocity();
        if (poly)
        {
            tb::SynthVoice* target = nullptr;
            for (auto& v : voices) if (! v.isActive()) { target = &v; break; }
            if (target == nullptr) for (auto& v : voices) if (! v.isHeld()) { target = &v; break; }
            if (target == nullptr) target = &voices[(size_t) (voiceCounter++ % kNumVoices)];
            target->noteOn (note, vel, false, noiseSeed);
        }
        else
        {
            heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
            heldNotes.push_back (note);
            voices[0].noteOn (note, vel, true, noiseSeed);
        }
    }
    else if (m.isNoteOff())
    {
        const int note = m.getNoteNumber();
        if (poly)
        {
            for (auto& v : voices)
                if (v.isHeld() && v.getNote() == note) v.noteOff();
        }
        else
        {
            heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
            if (heldNotes.empty()) voices[0].noteOff();
            else if (voices[0].getNote() == note) voices[0].noteOn (heldNotes.back(), 0.8f, true, noiseSeed);
        }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        for (auto& v : voices) v.noteOff();
        heldNotes.clear();
    }
}

void TalkBoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int mainIn  = getMainBusNumInputChannels();
    const int numOut  = getMainBusNumOutputChannels();
    int scChannels = 0;
    if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
        scChannels = scBus->getNumberOfChannels();
    scChannels = juce::jmin (scChannels, buffer.getNumChannels() - mainIn);

    const int mode = (int) pMode->load();
    const bool poly = pPoly->load() > 0.5f;

    if (mode != lastMode || (mode == modeMidi && poly != lastPoly))
    {
        allNotesOff();
        if (mode == modeAuto) voices[0].noteOn ((int) std::lround (autoNote), 1.0f, false, 7u);
        lastMode = mode;
        lastPoly = poly;
    }

    const int key = (int) pKey->load();
    const int scale = (int) pScale->load();
    const float octave = 12.0f * std::round (pOctave->load());
    const float robot = pRobot->load() / 100.0f;
    const float glideMs = pGlide->load();
    const float glideCoef = glideMs < 0.5f ? 0.0f : onePoleCoef (glideMs * 0.001, sr);
    const float wave = pWave->load() / 100.0f;
    const float detune = pDetune->load();
    const float gateThresh = juce::Decibels::decibelsToGain (pGate->load());

    mixSm.setTargetValue (pMix->load() / 100.0f);
    outSm.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    brightSm.setTargetValue (pBright->load() / 100.0f);
    consSm.setTargetValue (pConsonants->load() / 100.0f);

    auto midiIt = midi.cbegin();
    float peak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // MIDI-события в точный момент внутри блока
        if (mode == modeMidi)
        {
            for (; midiIt != midi.cend() && (*midiIt).samplePosition <= i; ++midiIt)
                handleMidi ((*midiIt).getMessage(), poly);
        }

        float voice = 0.0f;
        for (int ch = 0; ch < mainIn; ++ch) voice += buffer.getSample (ch, i);
        if (mainIn > 0) voice /= (float) mainIn;
        peak = juce::jmax (peak, std::abs (voice));

        // Шипящие: доля энергии выше ~3.5 кГц
        hpState = voice + hpCoef * (hpState - voice);
        const float hi = voice - hpState;
        envHi  = hi * hi + envCoef * (envHi - hi * hi);
        envAll = voice * voice + envCoef * (envAll - voice * voice);
        const float unvoiced = juce::jlimit (0.0f, 1.0f, (envHi / (envAll + 1.0e-12f) - 0.2f) / 0.35f);

        //------------------------------------------------------------------ несущая
        float carrier = 0.0f;
        if (mode == modeAuto)
        {
            pitch.push (voice);
            if (pitch.isVoiced())
            {
                const float raw = pitch.getMidiNote();
                const float snapped = (float) tb::nearestScaleNote (raw, key, scale);
                autoNote = juce::jlimit (24.0f, 96.0f, raw + robot * (snapped - raw));
            }
            voices[0].setTarget (autoNote + octave);
            carrier = voices[0].render (glideCoef, wave, detune);
        }
        else if (mode == modeMidi)
        {
            int active = 0;
            for (auto& v : voices)
            {
                if (! v.isActive()) continue;
                carrier += v.render (poly ? 0.0f : glideCoef, wave, detune);
                ++active;
            }
            if (active > 1) carrier /= std::sqrt ((float) active);
        }
        else
        {
            float sc = 0.0f;
            for (int ch = 0; ch < scChannels; ++ch) sc += buffer.getSample (mainIn + ch, i);
            if (scChannels > 0) sc /= (float) scChannels;
            const float pe = sc - tb::kPreEmph * scPrev;
            scPrev = sc;
            scPow = pe * pe + scPowCoef * (scPow - pe * pe);
            carrier = pe / std::sqrt (juce::jmax (scPow, 1.0e-6f));
        }

        // Шум для согласных: в основном там, где в голосе шипение
        noiseSeed = noiseSeed * 1664525u + 1013904223u;
        const float noise = ((float) (noiseSeed >> 8) * (1.0f / 16777216.0f) * 2.0f - 1.0f) * 1.732f;
        carrier += noise * consSm.getNextValue() * (0.08f + 1.2f * unvoiced);

        //------------------------------------------------------------------ задержка + фильтр
        const float carrierD = carrierDelay[(size_t) delayPos];
        const float dryD = dryDelay[(size_t) delayPos];
        carrierDelay[(size_t) delayPos] = carrier;
        dryDelay[(size_t) delayPos] = voice;
        if (++delayPos >= delayLen) delayPos = 0;

        float wet = vocoder.process (voice, carrierD) * kMakeup;

        tiltLp = wet + tiltCoef * (tiltLp - wet);
        wet += brightSm.getNextValue() * (wet - tiltLp);

        // Гейт по задержанному голосу (совпадает по времени с выходом)
        const float a = std::abs (dryD);
        gateEnv += (a > gateEnv ? 0.3f : 0.0005f) * (a - gateEnv);
        const float gTarget = gateEnv > gateThresh ? 1.0f : 0.0f;
        gateGain += (gTarget - gateGain) * (gTarget > gateGain ? gateAtt : gateRel);
        wet *= gateGain;

        const float mix = mixSm.getNextValue();
        const float out = (wet * mix + dryD * (1.0f - mix)) * outSm.getNextValue();

        for (int ch = 0; ch < numOut; ++ch) buffer.setSample (ch, i, out);
    }

    for (int ch = numOut; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, numSamples);

    inputLevel.store (peak);
    if (mode == modeAuto) shownNote.store (autoNote + octave);
    else if (mode == modeMidi)
    {
        float n = -1.0f;
        for (auto& v : voices) if (v.isHeld()) { n = (float) v.getNote(); }
        shownNote.store (n);
    }
    else shownNote.store (-1.0f);
}

void TalkBoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void TalkBoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* TalkBoxProcessor::createEditor() { return new TalkBoxEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TalkBoxProcessor(); }
