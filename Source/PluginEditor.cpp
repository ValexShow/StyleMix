#include "PluginEditor.h"

void StyleMixLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float startAngle, float endAngle, juce::Slider&)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float thick = juce::jmax (3.0f, radius * 0.12f);

    juce::Path track;
    track.addCentredArc (c.x, c.y, radius - thick, radius - thick, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (c.x, c.y, radius - thick, radius - thick, 0.0f, startAngle, angle, true);
    g.setColour (accent);
    g.strokePath (value, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float inner = radius - thick * 2.2f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3a), c.x, c.y - inner,
                                             juce::Colour (0xff141414), c.x, c.y + inner, false));
    g.fillEllipse (c.x - inner, c.y - inner, inner * 2.0f, inner * 2.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-thick * 0.35f, -inner + 4.0f, thick * 0.7f, inner * 0.45f, thick * 0.3f);
    g.setColour (accent.brighter (0.3f));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

StyleMixEditor::StyleMixEditor (StyleMixProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    for (auto* b : { &bullyButton, &militantumButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }
    bullyButton.onClick      = [this] { setStyle (0); };
    militantumButton.onClick = [this] { setStyle (1); };

    auto setupKnob = [this] (juce::Slider& s, juce::Label& l, const juce::String& text, bool big)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, big ? 90 : 70, 20);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (s);
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::FontOptions (big ? 18.0f : 14.0f, juce::Font::bold));
        addAndMakeVisible (l);
    };
    setupKnob (amountKnob, amountLabel, juce::String::fromUTF8 ("СВЕДЕНИЕ"), true);
    setupKnob (driveKnob,  driveLabel,  juce::String::fromUTF8 ("ГРЯЗЬ"), false);
    setupKnob (spaceKnob,  spaceLabel,  juce::String::fromUTF8 ("ПРОСТРАНСТВО"), false);
    setupKnob (outputKnob, outputLabel, juce::String::fromUTF8 ("ГРОМКОСТЬ"), false);

    amountAtt = std::make_unique<Attachment> (proc.apvts, "amount", amountKnob);
    driveAtt  = std::make_unique<Attachment> (proc.apvts, "drive",  driveKnob);
    spaceAtt  = std::make_unique<Attachment> (proc.apvts, "space",  spaceKnob);
    outputAtt = std::make_unique<Attachment> (proc.apvts, "output", outputKnob);

    setSize (560, 420);
    applyTheme();
    startTimerHz (20);
}

StyleMixEditor::~StyleMixEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void StyleMixEditor::setStyle (int index)
{
    if (auto* param = proc.apvts.getParameter ("style"))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
        param->endChangeGesture();
    }
    applyTheme();
}

void StyleMixEditor::applyTheme()
{
    const int style = (int) proc.apvts.getRawParameterValue ("style")->load();
    shownStyle = style;
    lnf.accent = style == 0 ? juce::Colour (0xffd9a441) : juce::Colour (0xffd3202a);

    auto styleButton = [this] (juce::TextButton& b, bool on)
    {
        b.setColour (juce::TextButton::buttonColourId, on ? lnf.accent : juce::Colour (0xff222222));
        b.setColour (juce::TextButton::textColourOffId, on ? juce::Colours::black : juce::Colours::white.withAlpha (0.6f));
    };
    styleButton (bullyButton, style == 0);
    styleButton (militantumButton, style == 1);

    for (auto* s : { &amountKnob, &driveKnob, &spaceKnob, &outputKnob })
        s->setColour (juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha (0.85f));

    repaint();
}

void StyleMixEditor::timerCallback()
{
    const int style = (int) proc.apvts.getRawParameterValue ("style")->load();
    if (style != shownStyle)
        applyTheme();

    const float gr = proc.gainReductionDb.load();
    if (std::abs (gr - shownGr) > 0.1f)
    {
        shownGr = gr;
        repaint (getLocalBounds().removeFromBottom (40));
    }
}

void StyleMixEditor::paint (juce::Graphics& g)
{
    const bool bully = shownStyle == 0;
    const auto top    = bully ? juce::Colour (0xff3b2a1a) : juce::Colour (0xff1a0a0c);
    const auto bottom = bully ? juce::Colour (0xff120c08) : juce::Colour (0xff050505);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, 0.0f, bottom, 0.0f, (float) getHeight(), false));
    g.fillAll();

    g.setColour (lnf.accent);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("STYLEMIX", 20, 12, 200, 30, juce::Justification::centredLeft);

    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.setFont (juce::FontOptions (13.0f));
    const auto desc = bully
        ? juce::String::fromUTF8 ("Тёплый грязный винтаж: плёнка, плотный низ, мягкий верх")
        : juce::String::fromUTF8 ("Тёмный и злой: тяжёлый саб, клиппер, широкое стерео, тёмный ревер");
    g.drawText (desc, 20, 98, getWidth() - 40, 18, juce::Justification::centred);

    // Индикатор компрессии
    auto meter = getLocalBounds().removeFromBottom (34).reduced (20, 10);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRoundedRectangle (meter.toFloat(), 3.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, -shownGr / 12.0f);
    g.setColour (lnf.accent.withAlpha (0.8f));
    g.fillRoundedRectangle (meter.toFloat().withWidth (meter.getWidth() * frac), 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String::fromUTF8 ("Компрессия: ") + juce::String (shownGr, 1) + " dB",
                meter, juce::Justification::centred);
}

void StyleMixEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (34);

    auto buttons = area.removeFromTop (40);
    const int bw = (buttons.getWidth() - 10) / 2;
    bullyButton.setBounds (buttons.removeFromLeft (bw));
    buttons.removeFromLeft (10);
    militantumButton.setBounds (buttons);

    area.removeFromTop (28);
    area.removeFromBottom (24);

    auto left = area.removeFromLeft (area.getWidth() / 2);
    amountLabel.setBounds (left.removeFromTop (22));
    amountKnob.setBounds (left.reduced (10, 0));

    const int sh = area.getHeight() / 3;
    for (auto [knob, label] : { std::pair { &driveKnob, &driveLabel },
                                std::pair { &spaceKnob, &spaceLabel },
                                std::pair { &outputKnob, &outputLabel } })
    {
        auto row = area.removeFromTop (sh);
        label->setBounds (row.removeFromLeft (120));
        knob->setBounds (row);
        knob->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
    }
}
