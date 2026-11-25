#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Audio/ParamList.h"
#include "Voices/VoiceManager.h"
#include "Presets/PresetManager.h"
#include "Audio/SampleBank.h"
#include "Modulation/ModSystem.h"

struct ParamPtrs
{
#define X(n) std::atomic<float>* n = nullptr;
    VANTA_PARAM_LIST(X)
#undef X
    std::atomic<float>* addAmp[vanta::kAddHarm] {};      // add_h01 .. add_h32
    std::atomic<float>* addPhase[vanta::kAddHarm] {};    // add_p01 .. add_p32
};

struct ModPtrs   // raw parameter pointers of the Stage 4 modulation system (read on the audio thread)
{
    std::atomic<float>* lfo[vanta::mod::kLfos][9] {};  std::atomic<float>* env[vanta::mod::kEnvs][10] {};
    std::atomic<float>* stepV[vanta::mod::kSteps] {};  std::atomic<float>* step[8] {};
    std::atomic<float>* macro[vanta::mod::kMacros] {}; std::atomic<float>* slot[vanta::mod::kSlots][4] {};
};

class VantaProcessor : public juce::AudioProcessor, private juce::ValueTree::Listener, private juce::AsyncUpdater
{
public:
    VantaProcessor();
    ~VantaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "VANTA X"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    void panic() { panicRequested.store (true); }

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    vanta::PresetManager presets;

    // ---- Stage 3: granular sample management (message thread) ----
    bool loadSample (const juce::File& f, juce::String& error);   // also stored in the plugin state / presets
    void useBuiltInSample();
    juce::String getSampleMissingName() const { return sampleMissingName; }   // non-empty: state referenced a file that was not found
    vanta::SampleBank samples;
    vanta::GrainViz grainViz;                                      // audio -> UI: recent grains
    std::atomic<int> grainCount { 0 };

    // ---- Stage 4: modulation, macros, MIDI learn (message-thread API; the audio thread only reads atomics) ----
    juce::String getMacroName (int i) const;
    void setMacroName (int i, const juce::String& name);
    struct MidiMapping { int cc; int paramIndex; };
    const std::vector<juce::RangedAudioParameter*>& getAllParams() const { return allParams; }
    std::vector<MidiMapping> getMidiMappings() const;
    void beginMidiLearn (int paramIndex) { learnParam.store (paramIndex); }
    void cancelMidiLearn() { learnParam.store (-1); }
    int  getLearningParam() const { return learnParam.load(); }
    int  getLastCC() const { return lastCC.load(); }
    void removeMidiMapping (int cc);
    void clearMidiMappings();

    std::atomic<float> cpuLoad { 0.f }, levelL { 0.f }, levelR { 0.f };
    std::atomic<int>   voiceCount { 0 };
    std::atomic<float> uiBend { 0.f };      // pitch wheel from the editor (-1..1)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void readParams();
    void handleMidi (const juce::MidiMessage&);
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
    void valueTreeRedirected (juce::ValueTree&) override { needMapSync.store (true); triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    void syncSampleFromState();
public:
    // Thread-safe snapshot of the additive parameters (audio thread + editor), already passed through additive::shape().
    void loadAdditiveParams (vanta::AdditiveParams&) const noexcept;
private:

    void readModParams (double bpm);
    void persistMidiMap();
    void syncMidiMapFromState();
    ParamPtrs P; ModPtrs M;
    vanta::mod::ModContext modctx;
    std::vector<juce::RangedAudioParameter*> allParams;
    std::atomic<int> ccMap[128];
    std::atomic<int> learnParam { -1 }, lastCC { -1 };
    std::atomic<bool> midiMapDirty { false }, needMapSync { false };
    vanta::SynthParams sp;
    vanta::VoiceManager voices;
    juce::SmoothedValue<float> gainSm, panSm;
    double sr = 44100.0;
    float lastUiBend = 0.f;
    std::atomic<bool> panicRequested { false };
    // granular window table (rebuilt on the audio thread only when type/width/shape change; no allocation)
    std::array<float, vanta::grainwin::kTable + 1> grainTable {};
    int winType = -1; float winWidth = -1.f, winShape = -1.f;
    const vanta::SampleData* audioSample = nullptr;
    juce::String sampleMissingName, missingPath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VantaProcessor)
};
