#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// Shared by every SpectraShell editor in the process (via SharedResourcePointer), and
// installed as JUCE's default LookAndFeel while an editor is open. JUCE 8 resolves every
// font through the *default* LookAndFeel, so this is what makes all text - including combo
// box popups and slider text boxes - use the embedded fonts instead of system fonts.
class ShellLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ShellLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

private:
    juce::Typeface::Ptr regular, bold;
};

// One-octave keyboard: click a key to toggle that pitch class.
// Keys glow with the live resonance level of their pitch class.
class NoteKeyboard : public juce::Component
{
public:
    explicit NoteKeyboard (SpectraShellProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();   // called from the editor timer

private:
    juce::Rectangle<float> keyBounds (int pc) const;
    int keyAt (juce::Point<float>) const;
    static bool isBlack (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

    SpectraShellProcessor& proc;
    std::array<float, 12> levels {};
    std::array<bool, 12> enabled {};
    uint16_t midiMask = 0;
};

class SpectraShellEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpectraShellEditor (SpectraShellProcessor&);
    ~SpectraShellEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    void timerCallback() override;
    void addKnob (Knob&, const juce::String& paramId, const juce::String& name);
    void addChoice (Choice&, const juce::String& name);
    void applyNoteSet();

    SpectraShellProcessor& proc;
    juce::SharedResourcePointer<ShellLookAndFeel> lnf;   // declared before the components that use it
    NoteKeyboard keyboard;

    Choice root, noteSet, source, harmonics;
    std::array<Knob, 10> knobs;
    juce::Label voicesLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectraShellEditor)
};
