#include "PluginEditor.h"

void VoxShiftLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
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
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff34383f), c.x, c.y - inner,
                                             juce::Colour (0xff121418), c.x, c.y + inner, false));
    g.fillEllipse (c.x - inner, c.y - inner, inner * 2.0f, inner * 2.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-thick * 0.35f, -inner + 4.0f, thick * 0.7f, inner * 0.45f, thick * 0.3f);
    g.setColour (accent.brighter (0.3f));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

VoxShiftEditor::VoxShiftEditor (VoxShiftProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    for (int oct = -2; oct <= 2; ++oct)
    {
        auto* b = octaveButtons.add (new juce::TextButton (oct == 0 ? juce::String ("0")
                                                                      : (oct > 0 ? "+" : "") + juce::String (oct)));
        b->onClick = [this, oct] { setOctave (oct); };
        addAndMakeVisible (b);
    }

    auto setupKnob = [this] (juce::Slider& s, juce::Label& l, const char* text)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 20);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha (0.85f));
        addAndMakeVisible (s);
        l.setText (juce::String::fromUTF8 (text), juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (l);
    };
    setupKnob (semisKnob,   semisLabel,   "ПОЛУТОНЫ");
    setupKnob (fineKnob,    fineLabel,    "ТОЧНО");
    setupKnob (formantKnob, formantLabel, "ТЕМБР");
    setupKnob (naturalKnob, naturalLabel, "ЕСТЕСТВЕННОСТЬ");
    setupKnob (mixKnob,     mixLabel,     "МИКС");
    setupKnob (outputKnob,  outputLabel,  "ГРОМКОСТЬ");

    semisAtt   = std::make_unique<Attachment> (proc.apvts, "semitones", semisKnob);
    fineAtt    = std::make_unique<Attachment> (proc.apvts, "fine", fineKnob);
    formantAtt = std::make_unique<Attachment> (proc.apvts, "formant", formantKnob);
    naturalAtt = std::make_unique<Attachment> (proc.apvts, "natural", naturalKnob);
    mixAtt     = std::make_unique<Attachment> (proc.apvts, "mix", mixKnob);
    outputAtt  = std::make_unique<Attachment> (proc.apvts, "output", outputKnob);

    for (auto* s : { &semisKnob, &fineKnob, &formantKnob, &naturalKnob, &mixKnob, &outputKnob })
        s->setDoubleClickReturnValue (true, s == &naturalKnob || s == &mixKnob ? 100.0 : 0.0);

    setSize (720, 400);
    refreshOctaveButtons();
    startTimerHz (20);
}

VoxShiftEditor::~VoxShiftEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void VoxShiftEditor::setOctave (int octave)
{
    if (auto* param = proc.apvts.getParameter ("octave"))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) octave));
        param->endChangeGesture();
    }
    refreshOctaveButtons();
}

void VoxShiftEditor::refreshOctaveButtons()
{
    shownOctave = (int) proc.apvts.getRawParameterValue ("octave")->load();
    for (int i = 0; i < octaveButtons.size(); ++i)
    {
        const bool on = (i - 2) == shownOctave;
        auto* b = octaveButtons[i];
        b->setColour (juce::TextButton::buttonColourId, on ? lnf.accent : juce::Colour (0xff1e2228));
        b->setColour (juce::TextButton::textColourOffId, on ? juce::Colours::black : juce::Colours::white.withAlpha (0.7f));
    }
    repaint();
}

void VoxShiftEditor::timerCallback()
{
    if ((int) proc.apvts.getRawParameterValue ("octave")->load() != shownOctave)
        refreshOctaveButtons();

    const float total = proc.getTotalSemitones();
    if (std::abs (total - shownTotal) > 0.001f)
    {
        shownTotal = total;
        repaint (0, 0, getWidth(), 60);
    }
}

void VoxShiftEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b2430), 0.0f, 0.0f,
                                             juce::Colour (0xff07090c), 0.0f, (float) getHeight(), false));
    g.fillAll();

    g.setColour (lnf.accent);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("VOXSHIFT", 20, 12, 200, 30, juce::Justification::centredLeft);

    // Итоговый сдвиг
    juce::String total;
    const float t = shownTotal;
    total << (t > 0.0f ? "+" : "") << juce::String (t, 2) << juce::String::fromUTF8 (" полутонов");
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText (total, getWidth() - 260, 12, 240, 30, juce::Justification::centredRight);

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::String::fromUTF8 ("ОКТАВА"), 20, 52, 100, 20, juce::Justification::centredLeft);

    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String::fromUTF8 ("Форманты остаются на месте: без «бурундука» и без «демона». "
                                        "ТЕМБР двигает форманты отдельно (минус — толще, плюс — тоньше)."),
                getLocalBounds().removeFromBottom (30).reduced (20, 0), juce::Justification::centred, true);
}

void VoxShiftEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (54);

    auto buttons = area.removeFromTop (40);
    const int gap = 8;
    const int bw = (buttons.getWidth() - gap * 4) / 5;
    for (auto* b : octaveButtons)
    {
        b->setBounds (buttons.removeFromLeft (bw));
        buttons.removeFromLeft (gap);
    }

    area.removeFromTop (20);
    area.removeFromBottom (24);

    const int kw = area.getWidth() / 6;
    for (auto [knob, label] : { std::pair { &semisKnob, &semisLabel },
                                std::pair { &fineKnob, &fineLabel },
                                std::pair { &formantKnob, &formantLabel },
                                std::pair { &naturalKnob, &naturalLabel },
                                std::pair { &mixKnob, &mixLabel },
                                std::pair { &outputKnob, &outputLabel } })
    {
        auto col = area.removeFromLeft (kw);
        label->setBounds (col.removeFromTop (22));
        knob->setBounds (col.reduced (2, 10));
    }
}
