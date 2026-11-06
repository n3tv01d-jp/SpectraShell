#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    juce::NormalisableRange<float> skewedRange (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    juce::AudioParameterFloatAttributes withText (std::function<juce::String (float)> fn)
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [fn] (float v, int) { return fn (v); });
    }
}

SpectraShellProcessor::SpectraShellProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SpectraShell", createLayout())
{
    for (int pc = 0; pc < 12; ++pc)
        noteParams[(size_t) pc] = apvts.getRawParameterValue (ParamIDs::note (pc));

    source     = apvts.getRawParameterValue (ParamIDs::source);
    octave     = apvts.getRawParameterValue (ParamIDs::octave);
    stack      = apvts.getRawParameterValue (ParamIDs::stack);
    tune       = apvts.getRawParameterValue (ParamIDs::tune);
    width      = apvts.getRawParameterValue (ParamIDs::width);
    decay      = apvts.getRawParameterValue (ParamIDs::decay);
    release    = apvts.getRawParameterValue (ParamIDs::release);
    brightness = apvts.getRawParameterValue (ParamIDs::brightness);
    harmonics  = apvts.getRawParameterValue (ParamIDs::harmonics);
    lowCut     = apvts.getRawParameterValue (ParamIDs::lowCut);
    mix        = apvts.getRawParameterValue (ParamIDs::mix);
    output     = apvts.getRawParameterValue (ParamIDs::output);
}

juce::AudioProcessorValueTreeState::ParameterLayout SpectraShellProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Default chord: C minor (C, D#, G).
    for (int pc = 0; pc < 12; ++pc)
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { ParamIDs::note (pc), 1 }, String ("Note ") + kNoteNames[pc],
            pc == 0 || pc == 3 || pc == 7));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::source, 1 }, "Note Source",
        StringArray { "Manual", "MIDI", "Manual + MIDI" }, 0));

    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::octave, 1 }, "Octave", 1, 6, 3));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ParamIDs::stack, 1 }, "Stack", 1, spectra::kMaxStack, 2));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::tune, 1 }, "Tune", NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
        withText ([] (float v) { return String (v, 1) + " ct"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::width, 1 }, "Width", NormalisableRange<float> (0.0f, 50.0f, 0.1f), 8.0f,
        withText ([] (float v) { return String (v, 1) + " ct"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::decay, 1 }, "Decay", skewedRange (0.05f, 10.0f, 1.5f), 1.5f,
        withText ([] (float v) { return String (v, 2) + " s"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::release, 1 }, "Release", skewedRange (0.02f, 10.0f, 0.8f), 0.5f,
        withText ([] (float v) { return String (v, 2) + " s"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::brightness, 1 }, "Brightness", NormalisableRange<float> (0.0f, 1.0f), 0.65f,
        withText ([] (float v) { return String (roundToInt (v * 100.0f)) + " %"; })));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::harmonics, 1 }, "Harmonics", StringArray { "Full", "Hollow" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::lowCut, 1 }, "Low Cut", skewedRange (20.0f, 2000.0f, 200.0f), 80.0f,
        withText ([] (float v) { return String (roundToInt (v)) + " Hz"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::mix, 1 }, "Mix", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        withText ([] (float v) { return String (roundToInt (v * 100.0f)) + " %"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::output, 1 }, "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        withText ([] (float v) { return String (v, 1) + " dB"; })));

    return layout;
}

bool SpectraShellProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SpectraShellProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    heldNotes.fill (false);
}

void SpectraShellProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            heldNotes[(size_t) m.getNoteNumber()] = true;
        else if (m.isNoteOff())
            heldNotes[(size_t) m.getNoteNumber()] = false;
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            heldNotes.fill (false);
    }

    uint16_t midiMask = 0;
    for (int n = 0; n < 128; ++n)
        if (heldNotes[(size_t) n])
            midiMask |= (uint16_t) (1u << (n % 12));
    midiMaskForUi.store (midiMask, std::memory_order_relaxed);

    uint16_t manualMask = 0;
    for (int pc = 0; pc < 12; ++pc)
        if (noteParams[(size_t) pc]->load() > 0.5f)
            manualMask |= (uint16_t) (1u << pc);

    const int src = (int) source->load();
    spectra::Params p;
    p.gateMask   = src == 0 ? manualMask : src == 1 ? midiMask : (uint16_t) (manualMask | midiMask);
    p.baseOctave = (int) octave->load();
    p.stack      = (int) stack->load();
    p.tuneCents  = tune->load();
    p.widthCents = width->load();
    p.decaySec   = decay->load();
    p.releaseSec = release->load();
    p.brightness = brightness->load();
    p.hollow     = harmonics->load() > 0.5f;
    p.lowCutHz   = lowCut->load();
    p.mix        = mix->load();
    p.outGainDb  = output->load();
    engine.setParams (p);

    engine.process (buffer.getArrayOfWritePointers(), juce::jmin (numOut, buffer.getNumChannels()),
                    buffer.getNumSamples());
}

juce::AudioProcessorEditor* SpectraShellProcessor::createEditor()
{
    return new SpectraShellEditor (*this);
}

void SpectraShellProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpectraShellProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectraShellProcessor();
}
