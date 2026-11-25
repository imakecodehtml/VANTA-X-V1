#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "Synthesis/Granular/SampleData.h"

namespace vanta
{
// Owns the granular source sample. Loading happens on the message thread; the audio thread only reads an
// immutable SampleData through an atomic pointer and acknowledges it, so no locks / allocation / frees ever
// happen on the audio thread.
class SampleBank
{
public:
    static constexpr double kMaxSeconds = 120.0;       // longer files are truncated

    SampleBank();

    // message thread ------------------------------------------------------------------------------
    bool loadFile (const juce::File& f, juce::String& error);   // true on success (error filled otherwise)
    void useBuiltIn();
    std::shared_ptr<const SampleData> current() const;          // safe for UI (shared ownership)
    juce::String currentPath() const;                           // empty = built-in texture
    juce::String getWildcard() const { return formats.getWildcardForAllFormats(); }
    bool wasTruncated() const { return truncated.load(); }

    // audio thread --------------------------------------------------------------------------------
    const SampleData* audioPointer() const noexcept { return pending.load (std::memory_order_acquire); }
    void acknowledge (const SampleData* s) noexcept { if (s != nullptr) acked.store (s->version, std::memory_order_release); }

private:
    void publish (std::shared_ptr<SampleData> s);

    mutable juce::SpinLock lock;
    std::shared_ptr<const SampleData> cur;
    std::vector<std::shared_ptr<const SampleData>> retired;
    std::atomic<const SampleData*> pending { nullptr };
    std::atomic<uint32_t> acked { 0 };
    std::atomic<bool> truncated { false };
    uint32_t nextVersion = 1;
    juce::AudioFormatManager formats;
};
}
