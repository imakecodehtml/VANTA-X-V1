#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Theme.h"

namespace vui
{
using APVTS = AudioProcessorValueTreeState;

// ---------- Knob with name + value readout ----------
class VKnob : public Component
{
public:
    VKnob (APVTS& a, const String& id, const String& label, std::function<String (double)> f, float designH = 100.f, float knobFrac = 0.56f)
        : name (label), fmt (std::move (f)), dh (designH), kf (knobFrac)
    {
        s.setSliderStyle (Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        s.setMouseDragSensitivity (220);
        addAndMakeVisible (s);
        att = std::make_unique<APVTS::SliderAttachment> (a, id, s);
        s.onValueChange = [this] { repaint(); };
    }
    void resized() override { const float d = jmin ((float) getWidth(), getHeight() * kf); s.setBounds (Rectangle<float> ((getWidth() - d) * 0.5f, 0.f, d, d).toNearestInt()); }
    void paint (Graphics& g) override
    {
        const float sc = getHeight() / dh;
        g.setColour (white()); g.setFont (font (13.5f * sc));
        g.drawText (name, 0, (int) (getHeight() * kf + 4 * sc), getWidth(), (int) (17 * sc), Justification::centred, false);
        g.setFont (font (15.f * sc)); g.setColour (Colours::white);
        g.drawText (fmt ? fmt (s.getValue()) : String (s.getValue(), 2), 0, (int) (getHeight() * kf + 24 * sc), getWidth(), (int) (19 * sc), Justification::centred, false);
    }
    Slider s;
private:
    String name; std::function<String (double)> fmt; float dh, kf;
    std::unique_ptr<APVTS::SliderAttachment> att;
};

// ---------- Vertical fader (mixer) ----------
class VFader : public Component
{
public:
    VFader (APVTS& a, const String& id, const String& label) : name (label)
    {
        s.setSliderStyle (Slider::LinearVertical); s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (s); att = std::make_unique<APVTS::SliderAttachment> (a, id, s);
        s.onValueChange = [this] { repaint(); };
    }
    void resized() override { s.setBounds (0, 0, getWidth(), getHeight() - 52); }
    void paint (Graphics& g) override
    {
        g.setColour (white()); g.setFont (font (14.f));
        g.drawText (name, 0, getHeight() - 48, getWidth(), 18, Justification::centred, false);
        g.setFont (font (15.5f)); g.drawText (String (s.getValue(), 2), 0, getHeight() - 24, getWidth(), 20, Justification::centred, false);
    }
    Slider s;
private:
    String name; std::unique_ptr<APVTS::SliderAttachment> att;
};

// ---------- Combo bound to a choice parameter ----------
class VCombo : public Component
{
public:
    VCombo (APVTS& a, const String& id)
    {
        if (auto* cp = dynamic_cast<AudioParameterChoice*> (a.getParameter (id))) cb.addItemList (cp->choices, 1);
        addAndMakeVisible (cb); att = std::make_unique<APVTS::ComboBoxAttachment> (a, id, cb);
    }
    void resized() override { cb.setBounds (getLocalBounds()); }
    ComboBox cb;
private:
    std::unique_ptr<APVTS::ComboBoxAttachment> att;
};

// ---------- Integer stepper:  <  4  > ----------
class VStepper : public Component
{
public:
    VStepper (APVTS& a, const String& id) : param (a.getParameter (id)), att (*param, [this] (float) { repaint(); }, nullptr) { att.sendInitialUpdate(); }
    void paint (Graphics& g) override
    {
        const float h = (float) getHeight(); const int v = (int) param->convertFrom0to1 (param->getValue());
        g.setColour (white()); g.setFont (font (h * 0.7f));
        g.drawText (String (v), getLocalBounds(), Justification::centred, false);
        Path l, r; l.startNewSubPath (h * 0.55f, h * 0.25f); l.lineTo (h * 0.3f, h * 0.5f); l.lineTo (h * 0.55f, h * 0.75f);
        r.startNewSubPath (getWidth() - h * 0.55f, h * 0.25f); r.lineTo (getWidth() - h * 0.3f, h * 0.5f); r.lineTo (getWidth() - h * 0.55f, h * 0.75f);
        g.strokePath (l, PathStrokeType (1.6f)); g.strokePath (r, PathStrokeType (1.6f));
    }
    void mouseDown (const MouseEvent& e) override
    {
        const int v = (int) param->convertFrom0to1 (param->getValue()); const int d = e.x < getWidth() / 2 ? -1 : 1;
        const auto range = param->getNormalisableRange();
        att.setValueAsCompleteGesture (jlimit (range.start, range.end, (float) (v + d)));
    }
private:
    RangedAudioParameter* param; ParameterAttachment att;
};

// ---------- Generic drawn display (waveforms, curves, envelopes) ----------
class FuncDisplay : public Component, private Timer
{
public:
    FuncDisplay (std::function<void (Graphics&, Rectangle<float>)> f, bool animated = true) : fn (std::move (f)) { if (animated) startTimerHz (24); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colour (0xff040405)); g.fillRoundedRectangle (r, 5.f);
        g.setColour (border()); g.drawRoundedRectangle (r.reduced (0.5f), 5.f, 1.f);
        Graphics::ScopedSaveState ss (g); g.reduceClipRegion (getLocalBounds().reduced (1));
        fn (g, r.reduced (4.f));
    }
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    std::function<void (Graphics&, Rectangle<float>)> fn;
};

// ---------- Stereo level meter ----------
class LevelMeter : public Component, private Timer
{
public:
    LevelMeter (std::atomic<float>& l, std::atomic<float>& r) : L (l), R (r) { startTimerHz (30); }
    void paint (Graphics& g) override
    {
        const int segs = 22; const float w = getWidth() * 0.38f, gap = getWidth() * 0.08f, sh = (float) getHeight() / segs;
        float vals[2] { L.load(), R.load() };
        for (int c = 0; c < 2; ++c)
        {
            const float n = jlimit (0.f, 1.f, (Decibels::gainToDecibels (vals[c], -60.f) + 60.f) / 60.f);
            const float x = gap + c * (w + gap);
            for (int i = 0; i < segs; ++i)
            {
                const bool lit = (float) (segs - 1 - i) / segs < n;
                g.setColour (lit ? meterCol().withAlpha (0.9f) : Colour (0xff1c1c1f));
                g.fillRect (x, i * sh + 1, w, sh - 2);
            }
        }
    }
private:
    void timerCallback() override { repaint(); }
    std::atomic<float>& L; std::atomic<float>& R;
};

// ---------- Main navigation tabs ----------
class NavTabs : public Component
{
public:
    StringArray labels { "MAIN", "OSC", "MOD", "FILTER", "FX", "MATRIX", "BROWSER" };
    int selected = 1; std::function<void (int)> onChange;
    void paint (Graphics& g) override
    {
        const float w = getWidth() / (float) labels.size();
        for (int i = 0; i < labels.size(); ++i)
        {
            auto r = Rectangle<float> (i * w + 2, 2, w - 4, getHeight() - 4.f);
            const bool sel = i == selected;
            g.setGradientFill (ColourGradient (sel ? Colour (0xff26262a) : Colour (0xff131315), r.getX(), r.getY(), Colour (0xff070708), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, 9.f);
            g.setColour (sel ? Colours::white.withAlpha (0.35f) : border()); g.drawRoundedRectangle (r.reduced (0.5f), 9.f, 1.f);
            if (sel) { g.setColour (Colours::white.withAlpha (0.9f)); g.fillRoundedRectangle (r.getX() + r.getWidth() * 0.2f, r.getY(), r.getWidth() * 0.6f, 2.f, 1.f); }
            g.setColour (sel ? Colours::white : Colour (0xffcfcfd2)); g.setFont (font (15.f));
            g.drawText (labels[i], r, Justification::centred, false);
        }
    }
    void mouseDown (const MouseEvent& e) override
    { const int i = jlimit (0, labels.size() - 1, (int) (e.x / (getWidth() / (float) labels.size()))); if (i != selected) { selected = i; repaint(); if (onChange) onChange (i); } }
};

// ---------- Engine selector ----------
class EngineSelect : public Component
{
public:
    StringArray names { "Wavetable", "FM", "Virtual Analog", "Granular", "Additive" };
    bool implemented[5] { false, false, true, false, false };
    int selected = 2; std::function<void (int)> onChange;

    static void icon (Graphics& g, Rectangle<float> r, int kind, Colour c)
    {
        Path p; const int N = 60;
        for (int i = 0; i <= N; ++i)
        {
            const float t = i / (float) N; float y = 0.f;
            switch (kind)
            {
                case 0: y = std::sin (t * 12.57f) * (0.4f + 0.6f * t); break;
                case 1: y = std::sin (t * 12.57f + 2.f * std::sin (t * 25.1f)); break;
                case 2: y = std::fmod (t * 3.f, 1.f) * 2.f - 1.f; break;
                case 3: y = std::sin (t * 40.f) * std::sin (t * 3.14f); break;
                default: y = (std::sin (t * 25.1f) + std::sin (t * 50.2f) * 0.6f + std::sin (t * 75.4f) * 0.4f) * 0.4f * std::sin (t * 3.14f); break;
            }
            const float px = r.getX() + t * r.getWidth(), py = r.getCentreY() - y * r.getHeight() * 0.42f;
            if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        g.setColour (c); g.strokePath (p, PathStrokeType (1.4f));
    }
    void paint (Graphics& g) override
    {
        const float rh = getHeight() / 5.f;
        for (int i = 0; i < 5; ++i)
        {
            auto r = Rectangle<float> (4, i * rh + 2, getWidth() - 8.f, rh - 4);
            const bool sel = i == selected;
            if (sel)
            {
                g.setColour (Colour (0xff1e1e22)); g.fillRoundedRectangle (r, 6.f);
                g.setColour (Colours::white.withAlpha (0.8f)); g.drawRoundedRectangle (r.reduced (0.5f), 6.f, 1.2f);
            }
            const auto col = implemented[i] ? (sel ? Colours::white : Colour (0xffc8c8cc)) : Colour (0xff55555a);
            icon (g, Rectangle<float> (r.getX() + 8, r.getY() + 4, 36, r.getHeight() - 8), i, col);
            g.setColour (col); g.setFont (font (15.f));
            g.drawText (names[i] + (implemented[i] ? "" : "  (soon)"), r.withTrimmedLeft (54), Justification::centredLeft, false);
        }
    }
    void mouseDown (const MouseEvent& e) override
    { const int i = jlimit (0, 4, (int) (e.y / (getHeight() / 5.f))); if (implemented[i] && i != selected) { selected = i; repaint(); if (onChange) onChange (i); } }
};

// ---------- Small icon button ----------
class IconButton : public Component
{
public:
    enum Kind { Settings, Power, Star, Folder, Left, Right };
    IconButton (Kind k) : kind (k) {}
    std::function<void()> onClick; bool on = false;
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (getWidth() * 0.22f); const auto c = r.getCentre();
        g.setColour (on ? Colours::white : Colour (0xffd0d0d4));
        Path p;
        switch (kind)
        {
            case Settings: for (int i = 0; i < 8; ++i) { const float a = i * MathConstants<float>::pi / 4.f; g.drawLine (c.x + std::cos (a) * r.getWidth() * 0.38f, c.y + std::sin (a) * r.getWidth() * 0.38f, c.x + std::cos (a) * r.getWidth() * 0.55f, c.y + std::sin (a) * r.getWidth() * 0.55f, 3.f); }
                           g.drawEllipse (c.x - r.getWidth() * 0.38f, c.y - r.getWidth() * 0.38f, r.getWidth() * 0.76f, r.getWidth() * 0.76f, 2.f);
                           g.drawEllipse (c.x - r.getWidth() * 0.14f, c.y - r.getWidth() * 0.14f, r.getWidth() * 0.28f, r.getWidth() * 0.28f, 2.f); break;
            case Power:    p.addCentredArc (c.x, c.y, r.getWidth() * 0.5f, r.getWidth() * 0.5f, 0, 0.6f, MathConstants<float>::twoPi - 0.6f, true);
                           g.strokePath (p, PathStrokeType (2.2f)); g.drawLine (c.x, c.y - r.getWidth() * 0.62f, c.x, c.y - r.getWidth() * 0.05f, 2.2f); break;
            case Star:     p.addStar (c, 5, r.getWidth() * 0.22f, r.getWidth() * 0.55f, 0.f); if (on) g.fillPath (p); else g.strokePath (p, PathStrokeType (1.6f)); break;
            case Folder:   g.drawRoundedRectangle (r.getX(), r.getY() + r.getHeight() * 0.2f, r.getWidth(), r.getHeight() * 0.65f, 2.f, 1.6f);
                           g.drawLine (r.getX(), r.getY() + r.getHeight() * 0.2f, r.getX() + r.getWidth() * 0.35f, r.getY() + r.getHeight() * 0.2f, 1.6f); break;
            case Left:     p.startNewSubPath (c.x + r.getWidth() * 0.2f, r.getY()); p.lineTo (c.x - r.getWidth() * 0.2f, c.y); p.lineTo (c.x + r.getWidth() * 0.2f, r.getBottom()); g.strokePath (p, PathStrokeType (2.f)); break;
            case Right:    p.startNewSubPath (c.x - r.getWidth() * 0.2f, r.getY()); p.lineTo (c.x + r.getWidth() * 0.2f, c.y); p.lineTo (c.x - r.getWidth() * 0.2f, r.getBottom()); g.strokePath (p, PathStrokeType (2.f)); break;
        }
    }
    void mouseDown (const MouseEvent&) override { if (onClick) onClick(); }
private:
    Kind kind;
};
} // namespace vui
