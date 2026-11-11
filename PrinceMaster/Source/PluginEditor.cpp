#include "PluginEditor.h"

void PrinceLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float startAngle, float endAngle, juce::Slider&)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float thick = juce::jmax (3.0f, radius * 0.12f);

    juce::Path track;
    track.addCentredArc (c.x, c.y, radius - thick, radius - thick, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (c.x, c.y, radius - thick, radius - thick, 0.0f, startAngle, angle, true);
    g.setColour (accent);
    g.strokePath (value, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float inner = radius - thick * 2.2f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2c30), c.x, c.y - inner,
                                             juce::Colour (0xff0c0c0e), c.x, c.y + inner, false));
    g.fillEllipse (c.x - inner, c.y - inner, inner * 2.0f, inner * 2.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-thick * 0.35f, -inner + 4.0f, thick * 0.7f, inner * 0.45f, thick * 0.3f);
    g.setColour (accent.brighter (0.3f));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

PrinceMasterEditor::PrinceMasterEditor (PrinceMasterProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    for (auto* b : { &zavtraButton, &omtButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }
    zavtraButton.onClick = [this] { setMode (0); };
    omtButton.onClick    = [this] { setMode (1); };

    auto setupKnob = [this] (juce::Slider& s, juce::Label& l, const char* text, bool big)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, big ? 90 : 70, 18);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha (0.85f));
        addAndMakeVisible (s);
        l.setText (juce::String::fromUTF8 (text), juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::FontOptions (big ? 20.0f : 13.0f, juce::Font::bold));
        addAndMakeVisible (l);
    };

    setupKnob (powerKnob, powerLabel, "СИЛА", true);
    powerAtt = std::make_unique<Attachment> (proc.apvts, "amount", powerKnob);

    const char* names[numSmall] = { "ИСКАЖЕНИЕ", "ФОН", "ЦИФРА", "КАЧ", "ПРОСТРАНСТВО", "ГРОМКОСТЬ" };
    const char* ids[numSmall]   = { "dist", "noise", "crush", "pump", "space", "output" };
    for (int i = 0; i < numSmall; ++i)
    {
        setupKnob (smallKnobs[i], smallLabels[i], names[i], false);
        smallAtts[i] = std::make_unique<Attachment> (proc.apvts, ids[i], smallKnobs[i]);
    }

    setSize (680, 470);
    applyTheme();
    startTimerHz (20);
}

PrinceMasterEditor::~PrinceMasterEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PrinceMasterEditor::setMode (int index)
{
    if (auto* param = proc.apvts.getParameter ("mode"))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
        param->endChangeGesture();
    }
    applyTheme();
}

void PrinceMasterEditor::applyTheme()
{
    const int mode = (int) proc.apvts.getRawParameterValue ("mode")->load();
    shownMode = mode;
    lnf.accent = mode == 0 ? juce::Colour (0xffc8102e) : juce::Colour (0xff3fd0ff);

    auto styleButton = [this] (juce::TextButton& b, bool on)
    {
        b.setColour (juce::TextButton::buttonColourId, on ? lnf.accent : juce::Colour (0xff1c1c20));
        b.setColour (juce::TextButton::textColourOffId, on ? juce::Colours::black : juce::Colours::white.withAlpha (0.6f));
    };
    styleButton (zavtraButton, mode == 0);
    styleButton (omtButton, mode == 1);
    repaint();
}

void PrinceMasterEditor::timerCallback()
{
    const int mode = (int) proc.apvts.getRawParameterValue ("mode")->load();
    if (mode != shownMode)
        applyTheme();

    // плавное падение стрелок индикаторов
    shownPump  = juce::jmax (proc.pumpMeterDb.load(), shownPump * 0.85f);
    shownLimit = juce::jmin (proc.limiterMeterDb.load(), shownLimit * 0.85f);
    repaint (getLocalBounds().removeFromBottom (44));
}

void PrinceMasterEditor::drawMeter (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& name, float db, float maxDb)
{
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRoundedRectangle (r.toFloat(), 3.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, std::abs (db) / maxDb);
    g.setColour (lnf.accent.withAlpha (0.8f));
    g.fillRoundedRectangle (r.toFloat().withWidth ((float) r.getWidth() * frac), 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (name + juce::String (std::abs (db), 1) + " dB", r, juce::Justification::centred);
}

void PrinceMasterEditor::paint (juce::Graphics& g)
{
    const bool zavtra = shownMode == 0;
    const auto top    = zavtra ? juce::Colour (0xff1e0a0d) : juce::Colour (0xff081620);
    const auto bottom = juce::Colour (0xff050506);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, 0.0f, bottom, 0.0f, (float) getHeight(), false));
    g.fillAll();

    // лёгкое «зерно» на фоне — под стать звуку
    juce::Random rnd (1234);
    g.setColour (juce::Colours::white.withAlpha (0.035f));
    for (int i = 0; i < 900; ++i)
        g.fillRect (rnd.nextInt (getWidth()), rnd.nextInt (getHeight()), 1, 1);

    g.setColour (lnf.accent);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("PRINCE MASTER", 20, 12, 300, 30, juce::Justification::centredLeft);
    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String::fromUTF8 ("мастер-шина"), getWidth() - 220, 12, 200, 30, juce::Justification::centredRight);

    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.setFont (juce::FontOptions (13.0f));
    const auto desc = zavtra
        ? juce::String::fromUTF8 ("Фонящий размашистый хип-хоп: толстый низ, фузз, шипение и гул, огромный тёмный зал")
        : juce::String::fromUTF8 ("Дигикор: качение от бочки, жёсткий цифровой клиппер, битовая грязь, яркий верх");
    g.drawText (desc, 20, 98, getWidth() - 40, 18, juce::Justification::centred);

    auto meters = getLocalBounds().removeFromBottom (44).reduced (20, 12);
    auto left = meters.removeFromLeft (meters.getWidth() / 2).withTrimmedRight (6);
    meters.removeFromLeft (6);
    drawMeter (g, left, juce::String::fromUTF8 ("Качение: -"), shownPump, 12.0f);
    drawMeter (g, meters, juce::String::fromUTF8 ("Лимитер: -"), shownLimit, 12.0f);
}

void PrinceMasterEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (34);

    auto buttons = area.removeFromTop (40);
    const int bw = (buttons.getWidth() - 10) / 2;
    zavtraButton.setBounds (buttons.removeFromLeft (bw));
    buttons.removeFromLeft (10);
    omtButton.setBounds (buttons);

    area.removeFromTop (30);
    area.removeFromBottom (30);

    auto left = area.removeFromLeft (230);
    powerLabel.setBounds (left.removeFromTop (26));
    powerKnob.setBounds (left.reduced (6, 0));

    area.removeFromLeft (10);
    const int cols = 3;
    const int cw = area.getWidth() / cols;
    const int rh = area.getHeight() / 2;
    for (int i = 0; i < numSmall; ++i)
    {
        auto cell = juce::Rectangle<int> (area.getX() + (i % cols) * cw, area.getY() + (i / cols) * rh, cw, rh).reduced (4);
        smallLabels[i].setBounds (cell.removeFromTop (18));
        smallKnobs[i].setBounds (cell);
    }
}
