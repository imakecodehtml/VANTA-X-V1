#include "PresetManager.h"
using namespace juce;

namespace vanta
{
static const char* kExt = ".vxp";

PresetManager::PresetManager (AudioProcessorValueTreeState& s) : apvts (s)
{
    root = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("VANTA X");
    root.getChildFile ("Presets").createDirectory();
    getSamplesFolder().createDirectory();
    getWavetablesFolder().createDirectory();
    loadFavorites();
    scan();
}

void PresetManager::scan()
{
    list.clear();
    const auto presetsDir = root.getChildFile ("Presets");
    for (const auto& f : presetsDir.findChildFiles (File::findFiles, true, String ("*") + kExt))
    {
        PresetInfo p; p.file = f; p.name = f.getFileNameWithoutExtension();
        const auto parent = f.getParentDirectory();
        p.category = parent == presetsDir ? String() : parent.getFileName();
        p.favorite = isFavorite (f);
        list.push_back (p);
    }
    std::sort (list.begin(), list.end(), [] (const PresetInfo& a, const PresetInfo& b)
               { return a.name.compareIgnoreCase (b.name) < 0; });
}

std::vector<PresetInfo> PresetManager::search (const String& text, const String& cat, bool favOnly) const
{
    std::vector<PresetInfo> r;
    for (const auto& p : list)
    {
        if (text.isNotEmpty() && ! p.name.containsIgnoreCase (text) && ! p.category.containsIgnoreCase (text)) continue;
        if (cat.isNotEmpty() && p.category != cat) continue;
        if (favOnly && ! p.favorite) continue;
        r.push_back (p);
    }
    return r;
}

StringArray PresetManager::getCategories() const
{
    StringArray c;
    for (const auto& p : list) if (p.category.isNotEmpty()) c.addIfNotAlreadyThere (p.category);
    c.sortNatural();
    return c;
}

bool PresetManager::save (const String& name, const String& category)
{
    const auto clean = File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    auto dir = root.getChildFile ("Presets");
    if (category.isNotEmpty()) dir = dir.getChildFile (File::createLegalFileName (category));
    dir.createDirectory();
    auto state = apvts.copyState();
    state.setProperty ("presetName", clean, nullptr);
    state.setProperty ("presetCategory", category, nullptr);
    auto xml = state.createXml();
    if (xml == nullptr || ! xml->writeTo (dir.getChildFile (clean + kExt))) return false;
    currentName = clean; scan();
    return true;
}

bool PresetManager::load (const File& f)
{
    if (auto xml = parseXML (f))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = ValueTree::fromXml (*xml);
            fillMissingParameters (tree);
            apvts.replaceState (tree);
            currentName = f.getFileNameWithoutExtension();
            return true;
        }
    return false;
}

void PresetManager::fillMissingParameters (ValueTree& tree) const
{
    // MIDI mappings belong to the user's controller setup: a preset without a MIDIMAP node keeps the current mappings.
    if (! tree.getChildWithName ("MIDIMAP").isValid())
    {
        const auto cur = apvts.state.getChildWithName ("MIDIMAP");
        if (cur.isValid()) tree.appendChild (cur.createCopy(), nullptr);
    }
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            if (! tree.getChildWithProperty ("id", rp->paramID).isValid())
            {
                ValueTree c ("PARAM");
                c.setProperty ("id", rp->paramID, nullptr);
                c.setProperty ("value", rp->convertFrom0to1 (rp->getDefaultValue()), nullptr);
                tree.appendChild (c, nullptr);
            }
}

bool PresetManager::importPreset (const File& f)
{
    if (! f.existsAsFile()) return false;
    const auto dest = root.getChildFile ("Presets").getChildFile (f.getFileNameWithoutExtension() + kExt);
    if (! f.copyFileTo (dest)) return false;
    scan(); return true;
}

bool PresetManager::exportPreset (const PresetInfo& p, const File& dest) const { return p.file.copyFileTo (dest); }

bool PresetManager::remove (const PresetInfo& p)
{
    const bool ok = p.file.moveToTrash() || p.file.deleteFile();
    scan(); return ok;
}

void PresetManager::loadFavorites()
{
    favorites.clear();
    if (favFile().existsAsFile()) favorites.addLines (favFile().loadFileAsString());
}
void PresetManager::saveFavorites() const { favFile().replaceWithText (favorites.joinIntoString ("\n")); }
bool PresetManager::isFavorite (const File& f) const { return favorites.contains (f.getRelativePathFrom (root)); }

void PresetManager::setFavorite (const PresetInfo& p, bool fav)
{
    const auto rel = p.file.getRelativePathFrom (root);
    if (fav) favorites.addIfNotAlreadyThere (rel); else favorites.removeString (rel);
    saveFavorites(); scan();
}

void PresetManager::loadNext (int delta)
{
    if (list.empty()) return;
    int idx = -1;
    for (size_t i = 0; i < list.size(); ++i) if (list[i].name == currentName) idx = (int) i;
    const int n = (int) list.size();
    idx = idx < 0 ? (delta > 0 ? 0 : n - 1) : (idx + delta + n) % n;
    load (list[(size_t) idx].file);
}

bool PresetManager::copyAB() { (onB ? slotB : slotA) = apvts.copyState(); return true; }
void PresetManager::toggleAB()
{
    (onB ? slotB : slotA) = apvts.copyState();
    onB = ! onB;
    auto& target = onB ? slotB : slotA;
    if (target.isValid()) apvts.replaceState (target.createCopy());
}
}
