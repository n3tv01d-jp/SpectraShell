// Copyright (C) 2026 Spectra
//
// This file is part of SpectraShell. It is licensed under the GNU Affero
// General Public License, version 3 (AGPLv3). See the LICENSE file in the
// project root for the full license text.

#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    namespace Colours
    {
        const juce::Colour bg       { 0xff101218 };
        const juce::Colour panel    { 0xff181b24 };
        const juce::Colour outline  { 0xff2a2f3d };
        const juce::Colour text     { 0xffd8dce6 };
        const juce::Colour dim      { 0xff7d8496 };
        const juce::Colour accent   { 0xff5ee6d0 };   // teal
        const juce::Colour accent2  { 0xffb48cff };   // violet
    }

    int openEditors = 0;   // message thread only

    const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    struct NoteSet { const char* name; std::vector<int> intervals; };

    const std::vector<NoteSet>& noteSets()
    {
        static const std::vector<NoteSet> sets {
            { "Major triad",      { 0, 4, 7 } },
            { "Minor triad",      { 0, 3, 7 } },
            { "Major 7",          { 0, 4, 7, 11 } },
            { "Minor 7",          { 0, 3, 7, 10 } },
            { "Dominant 7",       { 0, 4, 7, 10 } },
            { "Sus2",             { 0, 2, 7 } },
            { "Sus4",             { 0, 5, 7 } },
            { "Power (1-5)",      { 0, 7 } },
            { "Root only",        { 0 } },
            { "Major scale",      { 0, 2, 4, 5, 7, 9, 11 } },
            { "Natural minor",    { 0, 2, 3, 5, 7, 8, 10 } },
            { "Harmonic minor",   { 0, 2, 3, 5, 7, 8, 11 } },
            { "Dorian",           { 0, 2, 3, 5, 7, 9, 10 } },
            { "Major pentatonic", { 0, 2, 4, 7, 9 } },
            { "Minor pentatonic", { 0, 3, 5, 7, 10 } },
            { "Whole tone",       { 0, 2, 4, 6, 8, 10 } },
            { "Chromatic",        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
            { "Clear",            {} },
        };
        return sets;
    }
}

//==============================================================================
ShellLookAndFeel::ShellLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, Colours::dim);
    setColour (juce::ComboBox::backgroundColourId, Colours::panel);
    setColour (juce::ComboBox::outlineColourId, Colours::outline);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::arrowColourId, Colours::accent);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::text);

    regular = juce::Typeface::createSystemTypefaceFor (BinaryData::DejaVuSans_ttf,
                                                       (size_t) BinaryData::DejaVuSans_ttfSize);
    bold    = juce::Typeface::createSystemTypefaceFor (BinaryData::DejaVuSansBold_ttf,
                                                       (size_t) BinaryData::DejaVuSansBold_ttfSize);
}

juce::Typeface::Ptr ShellLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    // The UI only uses the default font family, so every request maps to the embedded fonts.
    if (auto tf = font.isBold() ? bold : regular)
        return tf;

    return LookAndFeel_V4::getTypefaceForFont (font);   // embedded font failed to load
}

void ShellLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float track = 3.5f;

    juce::Path bgArc;
    bgArc.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::outline);
    g.strokePath (bgArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path valArc;
    valArc.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, angle, true);
    g.setGradientFill (juce::ColourGradient (Colours::accent, bounds.getBottomLeft(),
                                             Colours::accent2, bounds.getTopRight(), false));
    g.strokePath (valArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobR = radius - track * 2.8f;
    g.setColour (Colours::panel.brighter (0.12f));
    g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -knobR + 2.0f, 3.0f, knobR * 0.55f, 1.5f);
    g.setColour (Colours::text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

//==============================================================================
NoteKeyboard::NoteKeyboard (SpectraShellProcessor& p) : proc (p) {}

juce::Rectangle<float> NoteKeyboard::keyBounds (int pc) const
{
    static const int whiteIndex[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    const auto b = getLocalBounds().toFloat();
    const float ww = b.getWidth() / 7.0f;

    if (! isBlack (pc))
        return { b.getX() + whiteIndex[pc] * ww, b.getY(), ww, b.getHeight() };

    const float bw = ww * 0.6f;
    const float cx = b.getX() + (whiteIndex[pc] + 1) * ww;
    return { cx - bw * 0.5f, b.getY(), bw, b.getHeight() * 0.6f };
}

int NoteKeyboard::keyAt (juce::Point<float> pt) const
{
    for (int pc = 0; pc < 12; ++pc)
        if (isBlack (pc) && keyBounds (pc).contains (pt)) return pc;
    for (int pc = 0; pc < 12; ++pc)
        if (! isBlack (pc) && keyBounds (pc).contains (pt)) return pc;
    return -1;
}

void NoteKeyboard::paint (juce::Graphics& g)
{
    auto drawKey = [&] (int pc)
    {
        const bool black = isBlack (pc);
        auto r = keyBounds (pc).reduced (1.5f, 0.0f);
        const float glow = juce::jlimit (0.0f, 1.0f, std::sqrt (levels[(size_t) pc]) * 1.4f);
        const bool midiHeld = ((midiMask >> pc) & 1u) != 0;

        juce::Colour base = black ? juce::Colour (0xff1c1f29) : juce::Colour (0xff2b3040);
        if (enabled[(size_t) pc])
            base = base.interpolatedWith (Colours::accent, black ? 0.35f : 0.28f);
        base = base.interpolatedWith (Colours::accent2, glow * 0.65f);

        g.setColour (base);
        g.fillRoundedRectangle (r, 5.0f);

        if (glow > 0.01f)
        {
            g.setColour (Colours::accent.withAlpha (glow * 0.5f));
            g.drawRoundedRectangle (r.reduced (1.0f), 5.0f, 2.0f);
        }

        g.setColour (black ? Colours::bg : Colours::outline);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);

        if (midiHeld)
        {
            g.setColour (Colours::accent2);
            g.fillEllipse (r.getCentreX() - 4.0f, r.getBottom() - (black ? 14.0f : 40.0f), 8.0f, 8.0f);
        }

        if (! black)
        {
            g.setColour (enabled[(size_t) pc] ? Colours::text : Colours::dim);
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawText (kNoteNames[pc], r.removeFromBottom (24.0f), juce::Justification::centred);
        }
        else
        {
            g.setColour (enabled[(size_t) pc] ? Colours::text : Colours::dim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (kNoteNames[pc], r.removeFromBottom (24.0f).translated (0, -10.0f), juce::Justification::centred);
        }
    };

    for (int pc = 0; pc < 12; ++pc) if (! isBlack (pc)) drawKey (pc);
    for (int pc = 0; pc < 12; ++pc) if (isBlack (pc))   drawKey (pc);
}

void NoteKeyboard::mouseDown (const juce::MouseEvent& e)
{
    const int pc = keyAt (e.position);
    if (pc < 0) return;

    if (auto* param = proc.apvts.getParameter (ParamIDs::note (pc)))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->getValue() > 0.5f ? 0.0f : 1.0f);
        param->endChangeGesture();
    }
    refresh();
}

void NoteKeyboard::refresh()
{
    bool changed = false;
    const uint16_t mm = proc.getMidiMask();
    if (mm != midiMask) { midiMask = mm; changed = true; }

    for (int pc = 0; pc < 12; ++pc)
    {
        const bool en = proc.apvts.getRawParameterValue (ParamIDs::note (pc))->load() > 0.5f;
        const float lv = proc.getPitchClassLevel (pc);
        if (en != enabled[(size_t) pc] || std::abs (lv - levels[(size_t) pc]) > 0.002f)
        {
            enabled[(size_t) pc] = en;
            levels[(size_t) pc] = lv;
            changed = true;
        }
    }
    if (changed) repaint();
}

//==============================================================================
SpectraShellEditor::SpectraShellEditor (SpectraShellProcessor& p)
    : AudioProcessorEditor (&p), proc (p), keyboard (p)
{
    // Font lookups go through the default LookAndFeel (see ShellLookAndFeel). Fonts resolved
    // before this point may be cached with the system typeface, so clear that cache.
    ++openEditors;
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf.get());
    juce::Typeface::clearTypefaceCache();
    setLookAndFeel (&lnf.get());
    addAndMakeVisible (keyboard);

    addChoice (root, "Root");
    for (int i = 0; i < 12; ++i) root.box.addItem (kNoteNames[i], i + 1);
    root.box.setSelectedId (1, juce::dontSendNotification);
    root.box.onChange = [this] { applyNoteSet(); };

    addChoice (noteSet, "Chord / Scale");
    noteSet.box.setTextWhenNothingSelected ("Pick to set notes...");
    for (size_t i = 0; i < noteSets().size(); ++i)
        noteSet.box.addItem (noteSets()[i].name, (int) i + 1);
    noteSet.box.onChange = [this] { applyNoteSet(); };

    addChoice (source, "Note Source");
    source.box.addItemList (proc.apvts.getParameter (ParamIDs::source)->getAllValueStrings(), 1);
    source.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, ParamIDs::source, source.box);

    addChoice (harmonics, "Harmonics");
    harmonics.box.addItemList (proc.apvts.getParameter (ParamIDs::harmonics)->getAllValueStrings(), 1);
    harmonics.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, ParamIDs::harmonics, harmonics.box);

    addKnob (knobs[0], ParamIDs::octave,     "Octave");
    addKnob (knobs[1], ParamIDs::stack,      "Stack");
    addKnob (knobs[2], ParamIDs::tune,       "Tune");
    addKnob (knobs[3], ParamIDs::width,      "Width");
    addKnob (knobs[4], ParamIDs::decay,      "Decay");
    addKnob (knobs[5], ParamIDs::release,    "Release");
    addKnob (knobs[6], ParamIDs::brightness, "Brightness");
    addKnob (knobs[7], ParamIDs::lowCut,     "Low Cut");
    addKnob (knobs[8], ParamIDs::mix,        "Mix");
    addKnob (knobs[9], ParamIDs::output,     "Output");

    voicesLabel.setJustificationType (juce::Justification::centredRight);
    voicesLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (voicesLabel);

    setSize (800, 420);
    startTimerHz (30);
}

SpectraShellEditor::~SpectraShellEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);

    // Last open editor: hand the default LookAndFeel back to JUCE before ours is destroyed.
    if (--openEditors == 0 && &juce::LookAndFeel::getDefaultLookAndFeel() == &lnf.get())
    {
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        juce::Typeface::clearTypefaceCache();
    }
}

void SpectraShellEditor::addKnob (Knob& k, const juce::String& paramId, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    k.slider.setColour (juce::Slider::textBoxTextColourId, Colours::text);
    addAndMakeVisible (k.slider);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (k.label);
}

void SpectraShellEditor::addChoice (Choice& c, const juce::String& name)
{
    c.label.setText (name, juce::dontSendNotification);
    c.label.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (c.label);
    addAndMakeVisible (c.box);
}

void SpectraShellEditor::applyNoteSet()
{
    const int setIdx = noteSet.box.getSelectedId() - 1;
    if (setIdx < 0 || setIdx >= (int) noteSets().size())
        return;

    const int r = root.box.getSelectedId() - 1;
    const auto& iv = noteSets()[(size_t) setIdx].intervals;

    for (int pc = 0; pc < 12; ++pc)
    {
        const int rel = (pc - r + 12) % 12;
        const bool on = std::find (iv.begin(), iv.end(), rel) != iv.end();
        if (auto* param = proc.apvts.getParameter (ParamIDs::note (pc)))
        {
            if ((param->getValue() > 0.5f) != on)
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (on ? 1.0f : 0.0f);
                param->endChangeGesture();
            }
        }
    }
    keyboard.refresh();
}

// Under Wine the window uses JUCE's software renderer (see cmake/PatchJuceWine.cmake).
void SpectraShellEditor::timerCallback()
{
    keyboard.refresh();
    voicesLabel.setText (juce::String (proc.getActiveVoiceCount()) + " voices", juce::dontSendNotification);

   #if JUCE_WINDOWS
    // JUCE 8 flushes repaints on the display's vblank signal, which Wine may not
    // deliver. Flushing them here too keeps the UI redrawing either way.
    if (auto* peer = getPeer())
        peer->performAnyPendingRepaintsNow();
   #endif
}

void SpectraShellEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);

    auto header = getLocalBounds().removeFromTop (48).toFloat();
    const juce::Font titleFont (juce::FontOptions (24.0f, juce::Font::bold));
    const float titleWidth = juce::GlyphArrangement::getStringWidth (titleFont, "SpectraShell");
    g.setFont (titleFont);
    g.setGradientFill (juce::ColourGradient (Colours::accent, header.getX() + 20.0f, 0.0f,
                                             Colours::accent2, header.getX() + 20.0f + titleWidth, 0.0f, false));
    g.drawText ("SpectraShell", header.reduced (20.0f, 0.0f), juce::Justification::centredLeft);

    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("harmonic resonator", header.reduced (20.0f, 0.0f).withTrimmedLeft (titleWidth + 12.0f),
                juce::Justification::centredLeft);

    // Knob panel
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (juce::Rectangle<float> (12.0f, 262.0f, (float) getWidth() - 24.0f, 146.0f), 8.0f);
}

void SpectraShellEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto header = area.removeFromTop (36);
    voicesLabel.setBounds (header.removeFromRight (120).reduced (8, 0));

    keyboard.setBounds (area.removeFromTop (150).reduced (8, 0));
    area.removeFromTop (8);

    auto choiceRow = area.removeFromTop (48).reduced (8, 0);
    const int cw = choiceRow.getWidth() / 4;
    for (auto* c : { &root, &noteSet, &source, &harmonics })
    {
        auto cell = choiceRow.removeFromLeft (cw).reduced (4, 0);
        c->label.setBounds (cell.removeFromTop (18));
        c->box.setBounds (cell.removeFromTop (26));
    }

    area.removeFromTop (14);
    auto knobRow = area.reduced (4, 6);
    const int kw = knobRow.getWidth() / (int) knobs.size();
    for (auto& k : knobs)
    {
        auto cell = knobRow.removeFromLeft (kw);
        k.label.setBounds (cell.removeFromTop (18));
        k.slider.setBounds (cell);
    }
}
