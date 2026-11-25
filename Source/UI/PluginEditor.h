#pragma once
#include "../PluginProcessor.h"
#include "Widgets.h"
#include "EnginePages.h"
#include "ModPages.h"

class VantaEditor : public juce::AudioProcessorEditor
{
public:
    explicit VantaEditor (VantaProcessor&);
    ~VantaEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr float kW = 1456.f, kH = 816.f;

private:
    struct Placed { juce::Component* c; juce::Rectangle<float> r; };
    struct PanelDef { juce::Rectangle<float> r; juce::String title; };

    // page groups: 0 = always visible (header), kVA / kGran / kAdd = engine pages, kShared = every engine page (engine select, wheel, master),
    // kGA = shared by Granular + Additive (filter panel). Legacy calls pass bool: true -> kVA, false -> none.
    enum Group { kNone = 0, kVA = 1, kShared = 2, kGran = 3, kAdd = 4, kGA = 5, kMod = 6, kMat = 7, kMatA = 8, kMatB = 9, kGroups = 10 };
    template <class T, class... A> T* mk (int group, float x, float y, float w, float h, A&&... a)
    {
        auto p = std::make_unique<T> (std::forward<A> (a)...); T* raw = p.get();
        addAndMakeVisible (*raw); layout.push_back ({ raw, { x, y, w, h } });
        if (group > 0) groups[group].push_back (raw);
        owned.push_back (std::move (p)); return raw;
    }
    void panel (int group, float x, float y, float w, float h, const juce::String& t) { panels[group].push_back ({ { x, y, w, h }, t }); }
    void buildHeader();
    void buildVA();
    void buildGranular();
    void buildAdditive();
    void buildFilterPanel();
    void buildMod();
    void buildMatrix();
    void updateVisibility();
    float aval (const char* id) const { return processor.apvts.getRawParameterValue (id)->load(); }

    VantaProcessor& processor;
    vui::VantaLNF lnf;
    vui::HarmonicEdit harm;                       // declared before `owned`: the widgets referencing it are destroyed first
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<Placed> layout;
    std::vector<juce::Component*> groups[kGroups];
    std::vector<PanelDef> panels[kGroups];
    std::unique_ptr<juce::ParameterAttachment> engineAtt;
    vui::NavTabs* nav = nullptr;
    vui::EngineSelect* engineSel = nullptr;
    juce::MidiKeyboardComponent* keyboard = nullptr;
    std::unique_ptr<juce::FileChooser> chooser;
    int page = 1, lfoSel = 0, envSel = 0, matPage = 0;
    std::vector<juce::Component*> lfoSet[8], envSet[8];   // per-LFO / per-envelope control sets (only the selected one is visible)
    float scale = 1.f;

    friend struct HeaderStats; friend class PresetBar;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VantaEditor)
};
