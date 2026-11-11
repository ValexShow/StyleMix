#include "PluginEditor.h"

namespace
{
    const juce::Colour kBg      { 0xff0e0d10 };
    const juce::Colour kPanel   { 0xff1a181e };
    const juce::Colour kAccent2 { 0xffff5a36 };
    const char* kNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    juce::String noteName (float midi)
    {
        const int n = (int) std::lround (midi);
        return juce::String (kNames[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
    }
}

YeTuneLookAndFeel::YeTuneLookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff25222b));
    setColour (juce::ComboBox::outlineColourId, juce::Colours::white.withAlpha (0.1f));
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff25222b));
    setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha (0.8f));
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff25222b));
}

void YeTuneLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
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
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff35313c), c.x, c.y - inner,
                                             juce::Colour (0xff0c0b0e), c.x, c.y + inner, false));
    g.fillEllipse (c.x - inner, c.y - inner, inner * 2.0f, inner * 2.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-thick * 0.3f, -inner + 3.0f, thick * 0.6f, inner * 0.45f, thick * 0.3f);
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void YeTuneEditor::setupKnob (Knob& k, const char* paramId, const char* utf8Name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (k.slider);
    k.label.setText (juce::String::fromUTF8 (utf8Name), juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (k.label);
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k.slider);
}

void YeTuneEditor::setParam (const char* id, float value)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

YeTuneEditor::YeTuneEditor (YeTuneProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

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

    // Быстрые пресеты
    hardButton.setButtonText (juce::String::fromUTF8 ("ЖЁСТКО (как у Kanye)"));
    hardButton.onClick = [this] { setParam ("retune", 0.0f); setParam ("humanize", 0.0f); setParam ("amount", 100.0f); setParam ("mix", 100.0f); };
    softButton.setButtonText (juce::String::fromUTF8 ("ЕСТЕСТВЕННО"));
    softButton.onClick = [this] { setParam ("retune", 70.0f); setParam ("humanize", 50.0f); setParam ("amount", 100.0f); setParam ("mix", 100.0f); };
    for (auto* b : { &hardButton, &softButton })
    {
        b->setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.85f));
        addAndMakeVisible (b);
    }

    setupKnob (retune,   "retune",   "СКОРОСТЬ");
    setupKnob (humanize, "humanize", "ЖИВОСТЬ");
    setupKnob (amount,   "amount",   "СИЛА");
    setupKnob (mix,      "mix",      "МИКС");
    setupKnob (output,   "output",   "ГРОМКОСТЬ");

    setSize (720, 440);
    startTimerHz (30);
}

YeTuneEditor::~YeTuneEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void YeTuneEditor::timerCallback()
{
    const float in = proc.shownInput.load();
    const float tg = proc.shownTarget.load();
    const int m = yt::scaleMask ((int) proc.apvts.getRawParameterValue ("key")->load(),
                                 (int) proc.apvts.getRawParameterValue ("scale")->load());
    if (std::abs (in - shownIn) > 0.01f || std::abs (tg - shownTarget) > 0.01f || m != shownMask)
    {
        shownIn = in; shownTarget = tg; shownMask = m;
        repaint (displayArea);
    }
}

void YeTuneEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1820), 0.0f, 0.0f, kBg, 0.0f, (float) getHeight(), false));
    g.fillAll();

    g.setGradientFill (juce::ColourGradient (lnf.accent, 20.0f, 0.0f, kAccent2, 180.0f, 0.0f, false));
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("YETUNE", 20, 12, 220, 34, juce::Justification::centredLeft);
    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String::fromUTF8 ("автотюн для вокала"), 150, 22, 200, 20, juce::Justification::centredLeft);

    // --- Дисплей: нота голоса -> целевая нота, отклонение, клавиши лада
    auto d = displayArea;
    g.setColour (kPanel);
    g.fillRoundedRectangle (d.toFloat(), 8.0f);
    auto inner = d.reduced (14, 10);

    auto textRow = inner.removeFromTop (34);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    if (shownIn < 0.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.drawText (juce::String::fromUTF8 ("жду голос…"), textRow, juce::Justification::centred);
    }
    else
    {
        const auto left = textRow.removeFromLeft (textRow.getWidth() / 2 - 30);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawText (noteName (shownIn), left, juce::Justification::centredRight);
        g.drawText (juce::String::fromUTF8 ("→"), textRow.removeFromLeft (60), juce::Justification::centred);
        g.setColour (lnf.accent);
        g.drawText (noteName (shownTarget), textRow, juce::Justification::centredLeft);
    }

    // Полоса отклонения: насколько голос мимо ноты (±50 центов)
    inner.removeFromTop (6);
    auto bar = inner.removeFromTop (10).withSizeKeepingCentre (inner.getWidth() * 2 / 3, 10);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRoundedRectangle (bar.toFloat(), 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.3f));
    g.fillRect (bar.getCentreX(), bar.getY() - 2, 1, bar.getHeight() + 4);
    if (shownIn >= 0.0f && shownTarget >= 0.0f)
    {
        const float cents = juce::jlimit (-50.0f, 50.0f, (shownIn - shownTarget) * 100.0f);
        const float cx = (float) bar.getCentreX() + cents / 50.0f * (float) bar.getWidth() * 0.5f;
        g.setColour (std::abs (cents) < 10.0f ? juce::Colour (0xff5be37a) : kAccent2);
        g.fillEllipse (cx - 6.0f, (float) bar.getCentreY() - 6.0f, 12.0f, 12.0f);
    }
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String::fromUTF8 ("ниже"), bar.getX() - 50, bar.getY() - 3, 44, 16, juce::Justification::centredRight);
    g.drawText (juce::String::fromUTF8 ("выше"), bar.getRight() + 6, bar.getY() - 3, 44, 16, juce::Justification::centredLeft);

    // 12 нот: светлые — разрешённые в ладу, жёлтая — куда тянем сейчас
    inner.removeFromTop (12);
    auto keys = inner.removeFromTop (30);
    const int kw = keys.getWidth() / 12;
    keys = keys.withSizeKeepingCentre (kw * 12, keys.getHeight());
    const int targetPc = shownTarget >= 0.0f ? ((int) std::lround (shownTarget) % 12 + 12) % 12 : -1;
    for (int i = 0; i < 12; ++i)
    {
        auto r = keys.removeFromLeft (kw).reduced (2, 0).toFloat();
        const bool allowed = (shownMask >> i) & 1;
        const bool black = i == 1 || i == 3 || i == 6 || i == 8 || i == 10;
        g.setColour (i == targetPc ? lnf.accent
                     : allowed     ? juce::Colours::white.withAlpha (black ? 0.55f : 0.75f)
                                   : juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (i == targetPc || allowed ? juce::Colours::black.withAlpha (0.8f) : juce::Colours::white.withAlpha (0.3f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (kNames[i], r, juce::Justification::centred);
    }

    // --- Панель ручек
    auto knobPanel = getLocalBounds().reduced (20);
    knobPanel.removeFromTop (displayArea.getBottom() - 20 + 12 + 40 + 12);
    knobPanel = knobPanel.removeFromTop (150);
    g.setColour (kPanel);
    g.fillRoundedRectangle (knobPanel.toFloat(), 8.0f);

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (juce::FontOptions (12.5f));
    g.drawFittedText (juce::String::fromUTF8 ("Выбери тональность бита. СКОРОСТЬ 0 = жёсткий роботский эффект (Kanye, 808s), "
                                              "больше = мягче и незаметнее. ЖИВОСТЬ смягчает начало нот."),
                      getLocalBounds().removeFromBottom (44).reduced (24, 4), juce::Justification::centred, 2);
}

void YeTuneEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (40);

    displayArea = area.removeFromTop (110);
    area.removeFromTop (12);

    auto row = area.removeFromTop (40);
    {
        auto keyCol = row.removeFromLeft (230);
        keyLabel.setBounds (keyCol.removeFromLeft (100));
        keyBox.setBounds (keyCol.reduced (0, 6));
        row.removeFromLeft (10);
        auto scaleCol = row.removeFromLeft (260);
        scaleLabel.setBounds (scaleCol.removeFromLeft (50));
        scaleBox.setBounds (scaleCol.reduced (0, 6));
    }
    {
        auto buttons = getLocalBounds().reduced (20).removeFromTop (34);
        softButton.setBounds (buttons.removeFromRight (120).reduced (0, 4));
        buttons.removeFromRight (8);
        hardButton.setBounds (buttons.removeFromRight (170).reduced (0, 4));
    }
    area.removeFromTop (12);

    auto knobs = area.removeFromTop (150).reduced (10, 12);
    Knob* ks[] = { &retune, &humanize, &amount, &mix, &output };
    const int kw = knobs.getWidth() / 5;
    for (auto* k : ks)
    {
        auto r = knobs.removeFromLeft (kw);
        k->label.setBounds (r.removeFromTop (18));
        k->slider.setBounds (r);
    }
}
