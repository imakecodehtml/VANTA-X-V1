#include "SampleBank.h"
using namespace juce;

namespace vanta
{
SampleBank::SampleBank() { formats.registerBasicFormats(); useBuiltIn(); }

void SampleBank::publish (std::shared_ptr<SampleData> s)
{
    const SpinLock::ScopedLockType sl (lock);
    s->version = nextVersion++;
    std::shared_ptr<const SampleData> n = s;
    if (cur != nullptr) retired.push_back (cur);
    cur = n;
    pending.store (cur.get(), std::memory_order_release);
    // free buffers the audio thread has certainly moved away from (it acknowledged a newer version)
    const uint32_t a = acked.load (std::memory_order_acquire);
    retired.erase (std::remove_if (retired.begin(), retired.end(), [a] (const std::shared_ptr<const SampleData>& r) { return r->version < a; }), retired.end());
    while (retired.size() > 6) retired.erase (retired.begin());        // audio is not running: nothing can still be reading these
}

void SampleBank::useBuiltIn()
{
    truncated.store (false);
    publish (std::make_shared<SampleData> (SampleData::makeBuiltIn (48000.0)));
}

bool SampleBank::loadFile (const File& f, String& error)
{
    if (! f.existsAsFile()) { error = "File not found: " + f.getFileName(); return false; }
    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples < 16)
    { error = "Unsupported or empty audio file: " + f.getFileName(); return false; }

    const int64 maxFrames = (int64) (kMaxSeconds * reader->sampleRate);
    const int frames = (int) std::min<int64> (reader->lengthInSamples, maxFrames);
    const int chans = (int) std::min<unsigned> (reader->numChannels, 2u);
    AudioBuffer<float> buf (std::max (1, chans), frames);
    if (! reader->read (&buf, 0, frames, 0, true, chans > 1))
    { error = "Could not read audio data from " + f.getFileName(); return false; }

    auto s = std::make_shared<SampleData>();
    s->channels = chans; s->sampleRate = reader->sampleRate; s->name = f.getFileName().toStdString(); s->path = f.getFullPathName().toStdString();
    s->ch[0].assign (buf.getReadPointer (0), buf.getReadPointer (0) + frames);
    if (chans > 1) s->ch[1].assign (buf.getReadPointer (1), buf.getReadPointer (1) + frames);
    // sanitise: a NaN/Inf in a decoded file must never reach the audio path
    for (int c = 0; c < (chans > 1 ? 2 : 1); ++c)
        for (auto& v : s->ch[c]) if (! std::isfinite (v)) v = 0.f;
    s->finalise();
    truncated.store (reader->lengthInSamples > maxFrames);
    publish (s);
    return true;
}

std::shared_ptr<const SampleData> SampleBank::current() const { const SpinLock::ScopedLockType sl (lock); return cur; }
String SampleBank::currentPath() const { const SpinLock::ScopedLockType sl (lock); return cur != nullptr ? String (cur->path) : String(); }
}
