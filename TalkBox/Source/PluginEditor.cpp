#include "PluginEditor.h"

namespace
{
    const juce::Colour kBg      { 0xff0d1117 };
    const juce::Colour kPanel   { 0xff161b24 };
    const juce::Colour kAccent2 { 0xffff4fa3 };

    juce::String noteName (float midi)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int n = (int) std::lround (midi);
        return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
    }
}

TalkBoxLookAndFeel::TalkBoxLookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1f2633));
    setColour (juce::ComboBox::outlineColourId, juce::Colours::white.withAlpha (0.1f));
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1f2633));
    setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha (0.8f));
    setColour (juce::ToggleButton::tickColourId, accent);
}

void TalkBoxLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float startAngle, float endAngle, juce::Slider&)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float thick = juce::jmax (3.0f, radius * 0.13f);

    juce::Path track;
    track.addCentredArc (c.x, c.y, radius - thick, radius - thick, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (c.x, c.y, radius - thick, radius - thick, 0.0f, startAngle, angle, true);
    g.setGradientFill (juce::ColourGradient (accent, c.x - radius, c.y, kAccent2, c.x + radius, c.y, false));
    g.strokePath (value, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float inner = radius - thick * 2.2f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c3444), c.x, c.y - inner,
                                             juce::Colour (0xff0b0e14), c.x, c.y + inner, false));
    g.fillEllipse (c.x - inner, c.y - inner, inner * 2.0f, inner * 2.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-thick * 0.3f, -inner + 3.0f, thick * 0.6f, inner * 0.45f, thick * 0.3f);
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void TalkBoxEditor::setupKnob (Knob& k, const char* paramId, const char* utf8Name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    addAndMakeVisible (k.slider);
    k.label.setText (juce::String::fromUTF8 (utf8Name), juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (k.label);
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k.slider);
}

TalkBoxEditor::TalkBoxEditor (TalkBoxProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    const char* modeNames[] = { "АВТО (по голосу)", "MIDI (ноты)", "САЙДЧЕЙН (свой синт)" };
    for (int i = 0; i < 3; ++i)
    {
        modeButtons[i].setButtonText (juce::String::fromUTF8 (modeNames[i]));
        modeButtons[i].onClick = [this, i] { setMode (i); };
        addAndMakeVisible (modeButtons[i]);
    }

    if (auto* keyParam = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("key")))
        keyBox.addItemList (keyParam->choices, 1);
    if (auto* scaleParam = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("scale")))
        scaleBox.addItemList (scaleParam->choices, 1);
    addAndMakeVisible (keyBox);
    addAndMakeVisible (scaleBox);
    keyAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "key", keyBox);
    scaleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "scale", scaleBox);

    for (auto [l, t] : { std::pair { &keyLabel, "ТОНАЛЬНОСТЬ" }, std::pair { &scaleLabel, "ЛАД" } })
    {
        l->setText (juce::String::fromUTF8 (t), juce::dontSendNotification);
        l->setJustificationType (juce::Justification::centred);
        l->setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (l);
    }

    polyButton.setButtonText (juce::String::fromUTF8 ("Аккорды (поли)"));
    addAndMakeVisible (polyButton);
    polyAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "poly", polyButton);

    setupKnob (octave,     "octave",     "ОКТАВА");
    setupKnob (robot,      "robot",      "РОБОТНОСТЬ");
    setupKnob (glide,      "glide",      "ГЛАЙД");
    setupKnob (wave,       "wave",       "ВОЛНА");
    setupKnob (detune,     "detune",     "РАССТРОЙКА");
    setupKnob (consonants, "consonants", "СОГЛАСНЫЕ");
    setupKnob (bright,     "bright",     "ЯРКОСТЬ");
    setupKnob (gate,       "gate",       "ГЕЙТ");
    setupKnob (mix,        "mix",        "МИКС");
    setupKnob (output,     "output",     "ГРОМКОСТЬ");

    setSize (780, 470);
    refreshMode();
    startTimerHz (25);
}

TalkBoxEditor::~TalkBoxEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TalkBoxEditor::setMode (int index)
{
    if (auto* param = proc.apvts.getParameter ("mode"))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
        param->endChangeGesture();
    }
    refreshMode();
}

void TalkBoxEditor::refreshMode()
{
    shownMode = (int) proc.apvts.getRawParameterValue ("mode")->load();
    for (int i = 0; i < 3; ++i)
    {
        const bool on = i == shownMode;
        modeButtons[i].setColour (juce::TextButton::buttonColourId, on ? lnf.accent : juce::Colour (0xff1f2633));
        modeButtons[i].setColour (juce::TextButton::textColourOffId, on ? juce::Colours::black : juce::Colours::white.withAlpha (0.6f));
    }

    const bool autoMode = shownMode == TalkBoxProcessor::modeAuto;
    const bool midiMode = shownMode == TalkBoxProcessor::modeMidi;
    const bool synth = shownMode != TalkBoxProcessor::modeSidechain;
    for (auto* c : std::initializer_list<juce::Component*> { &keyBox, &scaleBox, &keyLabel, &scaleLabel,
                                                             &robot.slider, &robot.label })
        c->setEnabled (autoMode);
    octave.slider.setEnabled (autoMode);
    octave.label.setEnabled (autoMode);
    polyButton.setEnabled (midiMode);
    glide.slider.setEnabled (synth);
    glide.label.setEnabled (synth);
    for (auto* k : { &wave, &detune })
    {
        k->slider.setEnabled (synth);
        k->label.setEnabled (synth);
    }
    repaint();
}

void TalkBoxEditor::timerCallback()
{
    if ((int) proc.apvts.getRawParameterValue ("mode")->load() != shownMode)
        refreshMode();

    const float n = proc.shownNote.load();
    const float lvl = proc.inputLevel.load();
    shownLevel = juce::jmax (lvl, shownLevel * 0.85f);
    if (std::abs (n - shownNote) > 0.05f || lvl > 0.0f || shownLevel > 0.001f)
    {
        shownNote = n;
        repaint (getLocalBounds().removeFromTop (60));
    }
}

void TalkBoxEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff141a26), 0.0f, 0.0f, kBg, 0.0f, (float) getHeight(), false));
    g.fillAll();

    g.setGradientFill (juce::ColourGradient (lnf.accent, 20.0f, 0.0f, kAccent2, 200.0f, 0.0f, false));
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("TALKBOX", 20, 12, 220, 34, juce::Justification::centredLeft);

    // Нота несущей и уровень входа
    auto info = juce::Rectangle<int> (getWidth() - 260, 14, 240, 30);
    g.setColour (kPanel);
    g.fillRoundedRectangle (info.toFloat(), 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    const auto noteText = shownNote < 0.0f
        ? (shownMode == TalkBoxProcessor::modeSidechain ? juce::String::fromUTF8 ("несущая: сайдчейн")
                                                        : juce::String::fromUTF8 ("нота: —"))
        : juce::String::fromUTF8 ("нота: ") + noteName (shownNote);
    g.drawText (noteText, info.reduced (10, 0), juce::Justification::centredLeft);

    auto meter = info.removeFromRight (90).reduced (8, 11);
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.fillRoundedRectangle (meter.toFloat(), 3.0f);
    const float db = juce::Decibels::gainToDecibels (shownLevel, -60.0f);
    g.setColour (lnf.accent);
    g.fillRoundedRectangle (meter.toFloat().withWidth (meter.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f)), 3.0f);

    // Панели
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (90);
    auto top = area.removeFromTop (150);
    area.removeFromTop (12);
    auto bottom = area.removeFromTop (150);
    for (auto r : { top, bottom })
    {
        g.setColour (kPanel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
    }
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (juce::String::fromUTF8 ("НОТЫ"), top.getX() + 10, top.getY() + 4, 100, 14, juce::Justification::left);
    g.drawText (juce::String::fromUTF8 ("ЗВУК"), bottom.getX() + 10, bottom.getY() + 4, 100, 14, juce::Justification::left);

    // Подсказка по текущему режиму
    const char* hints[] = {
        "Поёшь или говоришь, плагин сам слышит высоту голоса и подтягивает её к нотам лада. Ничего настраивать не нужно.",
        "Синт играет ноты из Piano roll: создай канал MIDI Out с тем же портом, что в настройках плагина (шестерёнка > MIDI > Input port).",
        "Несущая из твоего синта: в микшере FL отправь трек синта сайдчейном на трек с голосом (ПКМ по стрелке > Sidechain to this track)."
    };
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.setFont (juce::FontOptions (12.5f));
    g.drawFittedText (juce::String::fromUTF8 (hints[juce::jlimit (0, 2, shownMode)]),
                      getLocalBounds().removeFromBottom (44).reduced (24, 4), juce::Justification::centred, 2);
}

void TalkBoxEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (40);

    auto modes = area.removeFromTop (36);
    const int bw = (modes.getWidth() - 20) / 3;
    for (int i = 0; i < 3; ++i)
    {
        modeButtons[i].setBounds (modes.removeFromLeft (bw));
        modes.removeFromLeft (10);
    }
    area.removeFromTop (14);

    auto placeKnob = [] (Knob& k, juce::Rectangle<int> r)
    {
        k.label.setBounds (r.removeFromTop (18));
        k.slider.setBounds (r);
    };

    auto top = area.removeFromTop (150).reduced (10, 18);
    top.removeFromTop (0);
    {
        auto col = top.removeFromLeft (130);
        keyLabel.setBounds (col.removeFromTop (18));
        keyBox.setBounds (col.removeFromTop (26).reduced (6, 0));
        col.removeFromTop (8);
        scaleLabel.setBounds (col.removeFromTop (18));
        scaleBox.setBounds (col.removeFromTop (26).reduced (6, 0));

        auto right = top.removeFromRight (150);
        polyButton.setBounds (right.withSizeKeepingCentre (140, 28));

        const int kw = top.getWidth() / 3;
        placeKnob (octave, top.removeFromLeft (kw));
        placeKnob (robot,  top.removeFromLeft (kw));
        placeKnob (glide,  top);
    }

    area.removeFromTop (12);
    auto bottom = area.removeFromTop (150).reduced (10, 18);
    {
        Knob* ks[] = { &wave, &detune, &consonants, &bright, &gate, &mix, &output };
        const int kw = bottom.getWidth() / 7;
        for (auto* k : ks)
            placeKnob (*k, bottom.removeFromLeft (kw));
    }
}
