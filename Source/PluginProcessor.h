#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ResonatorEngine.h"

namespace ParamIDs
{
    inline juce::String note (int pc) { return "note_" + juce::String (pc); }
    inline constexpr const char* source     = "source";
    inline constexpr const char* octave     = "octave";
    inline constexpr const char* stack      = "stack";
    inline constexpr const char* tune       = "tune";
    inline constexpr const char* width      = "width";
    inline constexpr const char* decay      = "decay";
    inline constexpr const char* release    = "release";
    inline constexpr const char* brightness = "brightness";
    inline constexpr const char* harmonics  = "harmonics";
    inline constexpr const char* lowCut     = "lowcut";
    inline constexpr const char* mix        = "mix";
    inline constexpr const char* output     = "output";
}

class SpectraShellProcessor : public juce::AudioProcessor
{
public:
    SpectraShellProcessor();
    ~SpectraShellProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // UI feedback (read from the message thread).
    float    getPitchClassLevel (int pc) const noexcept { return engine.getPitchClassLevel (pc); }
    int      getActiveVoiceCount() const noexcept      { return engine.getActiveVoiceCount(); }
    uint16_t getMidiMask() const noexcept              { return midiMaskForUi.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    spectra::ResonatorEngine engine;

    std::array<std::atomic<float>*, 12> noteParams {};
    std::atomic<float>* source = nullptr;
    std::atomic<float>* octave = nullptr;
    std::atomic<float>* stack = nullptr;
    std::atomic<float>* tune = nullptr;
    std::atomic<float>* width = nullptr;
    std::atomic<float>* decay = nullptr;
    std::atomic<float>* release = nullptr;
    std::atomic<float>* brightness = nullptr;
    std::atomic<float>* harmonics = nullptr;
    std::atomic<float>* lowCut = nullptr;
    std::atomic<float>* mix = nullptr;
    std::atomic<float>* output = nullptr;

    std::array<bool, 128> heldNotes {};
    std::atomic<uint16_t> midiMaskForUi { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectraShellProcessor)
};
