#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace vui
{
using namespace juce;
inline Font font (float h, bool bold = false, float kern = 0.f)
{ return Font (FontOptions ("Segoe UI", h, bold ? Font::bold : Font::plain).withKerningFactor (kern)); }

inline Colour white()   { return Colour (0xfff2f2f2); }
inline Colour dim()     { return Colour (0xff9a9aa0); }
inline Colour panelHi() { return Colour (0xff17171a); }
inline Colour panelLo() { return Colour (0xff09090a); }
inline Colour border()  { return Colour (0xff34343a); }
inline Colour meterCol(){ return Colour (0xff9fe8e0); }

inline void drawPanel (Graphics& g, Rectangle<float> r, const String& title, float titleSize = 15.5f)
{
    g.setGradientFill (ColourGradient (panelHi(), r.getX(), r.getY(), panelLo(), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 9.f);
    g.setColour (Colours::black); g.drawRoundedRectangle (r.expanded (0.5f), 9.f, 1.f);
    g.setColour (border()); g.drawRoundedRectangle (r.reduced (0.5f), 9.f, 1.f);
    g.setColour (Colours::white.withAlpha (0.10f));
    g.drawLine (r.getX() + 10, r.getY() + 1.5f, r.getRight() - 10, r.getY() + 1.5f, 1.f);
    if (title.isNotEmpty())
    {
        g.setColour (white()); g.setFont (font (titleSize));
        g.drawText (title, Rectangle<float> (r.getX() + 12, r.getY() + 8, r.getWidth() - 24, 22), Justification::centredLeft, false);
    }
}

inline void glowStroke (Graphics& g, const Path& p, float w = 1.6f)
{
    g.setColour (Colours::white.withAlpha (0.10f)); g.strokePath (p, PathStrokeType (w * 4.f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colours::white.withAlpha (0.28f)); g.strokePath (p, PathStrokeType (w * 2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (white());                          g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
}

class VantaLNF : public LookAndFeel_V4
{
public:
    VantaLNF()
    {
        setColour (ComboBox::backgroundColourId, Colour (0xff101012));
        setColour (ComboBox::outlineColourId, border());
        setColour (ComboBox::textColourId, white());
        setColour (ComboBox::arrowColourId, white());
        setColour (PopupMenu::backgroundColourId, Colour (0xff0c0c0e));
        setColour (PopupMenu::textColourId, white());
        setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xff2a2a2e));
        setColour (PopupMenu::highlightedTextColourId, Colours::white);
        setColour (AlertWindow::backgroundColourId, Colour (0xff0c0c0e));
        setColour (AlertWindow::textColourId, white());
        setColour (TextEditor::backgroundColourId, Colour (0xff17171a));
        setColour (TextEditor::textColourId, white());
        setColour (TextButton::buttonColourId, Colour (0xff1c1c20));
        setColour (TextButton::textColourOffId, white());
    }

    void drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider&) override
    {
        auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.f);
        const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f; const auto c = b.getCentre();
        const float ang = a0 + pos * (a1 - a0);
        Path track; track.addCentredArc (c.x, c.y, r - 1, r - 1, 0, a0, a1, true);
        g.setColour (Colour (0xff3a3a3f)); g.strokePath (track, PathStrokeType (2.f));
        Path val; val.addCentredArc (c.x, c.y, r - 1, r - 1, 0, a0, ang, true);
        g.setColour (Colours::white.withAlpha (0.18f)); g.strokePath (val, PathStrokeType (5.f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (white()); g.strokePath (val, PathStrokeType (2.4f, PathStrokeType::curved, PathStrokeType::rounded));
        const float br = r * 0.74f;
        g.setGradientFill (ColourGradient (Colour (0xff45454b), c.x - br * 0.5f, c.y - br * 0.7f, Colour (0xff09090a), c.x + br * 0.5f, c.y + br * 0.9f, false));
        g.fillEllipse (c.x - br, c.y - br, br * 2, br * 2);
        g.setColour (Colour (0xff62626a)); g.drawEllipse (c.x - br, c.y - br, br * 2, br * 2, 1.f);
        const float s = std::sin (ang), co = -std::cos (ang);
        g.setColour (white());
        g.drawLine (c.x + s * br * 0.25f, c.y + co * br * 0.25f, c.x + s * br * 0.88f, c.y + co * br * 0.88f, 2.2f);
    }

    void drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle, Slider&) override
    {
        const float cx = x + w * 0.5f;
        g.setColour (Colour (0xff050506)); g.fillRoundedRectangle (cx - 6, (float) y, 12, (float) h, 3.f);
        g.setColour (border()); g.drawRoundedRectangle (cx - 6, (float) y, 12, (float) h, 3.f, 1.f);
        g.setColour (Colours::white.withAlpha (0.55f)); g.fillRect (cx - 1.f, (float) y + 3, 2.f, (float) h - 6);
        Rectangle<float> t (cx - 14, pos - 8, 28, 16);
        g.setGradientFill (ColourGradient (Colour (0xffd8d8dc), t.getX(), t.getY(), Colour (0xff5a5a60), t.getX(), t.getBottom(), false));
        g.fillRoundedRectangle (t, 3.f);
        g.setColour (Colours::black.withAlpha (0.7f)); g.fillRect (t.getX() + 3, t.getCentreY() - 0.5f, t.getWidth() - 6, 1.f);
    }

    void drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& cb) override
    {
        auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (cb.findColour (ComboBox::backgroundColourId)); g.fillRoundedRectangle (r, 5.f);
        g.setColour (border()); g.drawRoundedRectangle (r, 5.f, 1.f);
        Path p; const float cx = w - 14.f, cy = h * 0.5f;
        p.startNewSubPath (cx - 4, cy - 2); p.lineTo (cx, cy + 2); p.lineTo (cx + 4, cy - 2);
        g.setColour (white()); g.strokePath (p, PathStrokeType (1.6f));
    }
    Font getComboBoxFont (ComboBox& cb) override { return font (jmax (9.f, cb.getHeight() * 0.5f)); }
    void positionComboBoxText (ComboBox& cb, Label& l) override { l.setBounds (8, 1, cb.getWidth() - 26, cb.getHeight() - 2); l.setFont (getComboBoxFont (cb)); }
    Font getPopupMenuFont() override { return font (15.f); }
};
} // namespace vui
