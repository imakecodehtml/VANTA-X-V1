#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace vanta
{
struct PresetInfo { juce::File file; juce::String name, category; bool favorite = false; };

// User-content preset infrastructure. Ships NO presets: the library is whatever the user creates/imports.
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& s);

    juce::File getRootFolder() const { return root; }
    juce::File getSamplesFolder() const { return root.getChildFile ("Samples"); }
    juce::File getWavetablesFolder() const { return root.getChildFile ("Wavetables"); }

    void scan();                                             // rebuild list from disk
    const std::vector<PresetInfo>& getAll() const { return list; }
    std::vector<PresetInfo> search (const juce::String& text, const juce::String& category = {}, bool favoritesOnly = false) const;
    juce::StringArray getCategories() const;

    bool save (const juce::String& name, const juce::String& category = {});
    bool load (const juce::File& f);
    void fillMissingParameters (juce::ValueTree& state) const;   // adds <PARAM> nodes with defaults for parameters an older state does not contain
    bool importPreset (const juce::File& f);                 // copy into library
    bool exportPreset (const PresetInfo& p, const juce::File& dest) const;
    bool remove (const PresetInfo& p);
    void setFavorite (const PresetInfo& p, bool fav);
    bool isFavorite (const juce::File& f) const;

    void loadNext (int delta);                               // wraps; no-op on empty library
    juce::String getCurrentName() const { return currentName; }
    bool isEmpty() const { return list.empty(); }

    bool copyAB();                                           // A/B comparison
    void toggleAB();
    bool isB() const { return onB; }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::File root;
    std::vector<PresetInfo> list;
    juce::String currentName;
    juce::StringArray favorites;
    juce::ValueTree slotA, slotB; bool onB = false;
    void loadFavorites(); void saveFavorites() const;
    juce::File favFile() const { return root.getChildFile ("favorites.txt"); }
};
}
