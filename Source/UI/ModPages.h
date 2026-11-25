#pragma once
// VANTA X - Stage 4 widgets: tab strip, envelope editor, step editor, matrix row, routing view, macro cell, MIDI-learn panel.
// All of them read / write real APVTS parameters (or the processor's MIDI-map API); nothing here is decorative.
#include <map>
#include "EnginePages.h"

namespace vui
{
// ---------------------------------------------------------------- small tab strip (1..8, 1-16 / 17-32)
class TabStrip : public Component
{
public:
    explicit TabStrip (const StringArray& names) : items (names) {}
    std::function<void (int)> onChange; int sel = 0;
    void paint (Graphics& g) override
    {
        const float w = (float) getWidth() / (float) jmax (1, items.size());
        for (int i = 0; i < items.size(); ++i)
        {
            Rectangle<float> r ((float) i * w + 1.5f, 1.f, w - 3.f, (float) getHeight() - 2.f);
            g.setColour (i == sel ? Colour (0xff2a2a2e) : Colour (0xff111113)); g.fillRoundedRectangle (r, 4.f);
            g.setColour (i == sel ? Colours::white.withAlpha (0.75f) : border()); g.drawRoundedRectangle (r.reduced (0.5f), 4.f, 1.f);
            g.setColour (i == sel ? Colours::white : dim()); g.setFont (font ((float) getHeight() * 0.5f));
            g.drawText (items[i], r, Justification::centred, false);
        }
    }
    void mouseDown (const MouseEvent& e) override
    {
        const int i = jlimit (0, items.size() - 1, (int) (e.position.x / ((float) getWidth() / (float) items.size())));
        if (i != sel) { sel = i; if (onChange) onChange (i); repaint(); }
    }
private:
    StringArray items;
};

// ---------------------------------------------------------------- envelope editor: drag the handles, drag the line for the curve
class EnvEditor : public Component, private Timer
{
public:
    EnvEditor (VantaProcessor& p, int& selected) : proc (p), sel (selected) { startTimerHz (20); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colour (0xff040405)); g.fillRoundedRectangle (r, 5.f);
        g.setColour (border()); g.drawRoundedRectangle (r.reduced (0.5f), 5.f, 1.f);
        const Geo q = geo(); const float curve = prm ("curve").get(), s = prm ("s").get();
        g.setColour (Colours::white.withAlpha (0.06f));
        for (int i = 1; i < 4; ++i) g.drawHorizontalLine ((int) (q.yt + (q.yb - q.yt) * (float) i / 4.f), 4.f, (float) getWidth() - 4.f);
        Path p; p.startNewSubPath (q.x0, q.yb);
        const int N = 24;
        for (int i = 1; i <= N; ++i) { const float t = (float) i / N; p.lineTo (q.x0 + (q.xa - q.x0) * t, q.yb - vanta::mod::envShape (t, curve) * (q.yb - q.yt)); }
        p.lineTo (q.xh, q.yt);
        for (int i = 1; i <= N; ++i) { const float t = (float) i / N; p.lineTo (q.xh + (q.xd - q.xh) * t, q.yb - (1.f - (1.f - s) * vanta::mod::envShape (t, curve)) * (q.yb - q.yt)); }
        p.lineTo (q.xs, q.ys);
        for (int i = 1; i <= N; ++i) { const float t = (float) i / N; p.lineTo (q.xs + (q.xr - q.xs) * t, q.yb - s * (1.f - vanta::mod::envShape (t, curve)) * (q.yb - q.yt)); }
        Path f (p); f.lineTo (q.xr, q.yb); f.closeSubPath();
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.14f), 0, q.yt, Colours::transparentWhite, 0, q.yb, false)); g.fillPath (f);
        glowStroke (g, p, 1.5f);
        g.setColour (Colours::white);
        for (auto pt : { Point<float> (q.xa, q.yt), Point<float> (q.xh, q.yt), Point<float> (q.xd, q.ys), Point<float> (q.xr, q.yb) }) g.fillEllipse (pt.x - 4.f, pt.y - 4.f, 8.f, 8.f);
        g.setColour (dim()); g.setFont (font (11.f));
        g.drawText ("drag handles: A  H  D/S  R   -   drag the line: curve", Rectangle<float> (6, 2, (float) getWidth() - 12, 14), Justification::topLeft, false);
    }
    void mouseDown (const MouseEvent& e) override
    {
        const Geo q = geo(); mode = Curve;
        const Point<float> pts[4] { { q.xa, q.yt }, { q.xh, q.yt }, { q.xd, q.ys }, { q.xr, q.yb } };
        float best = 14.f; for (int i = 0; i < 4; ++i) { const float d = pts[i].getDistanceFrom (e.position); if (d < best) { best = d; mode = i; } }
        const char* fields[4] { "a", "h", "d", "r" };
        if (mode < 4) { h1 = prm (fields[mode]); start1 = h1.p->getValue(); h1.begin(); }
        if (mode == 2) { h2 = prm ("s"); start2 = h2.p->getValue(); h2.begin(); }
        if (mode == Curve) { h1 = prm ("curve"); start1 = h1.get(); h1.begin(); }
    }
    void mouseDrag (const MouseEvent& e) override
    {
        const float W = (float) jmax (1, getWidth()), H = (float) jmax (1, getHeight());
        if (mode < 4 && h1.p != nullptr)
        {
            h1.p->setValueNotifyingHost (jlimit (0.f, 1.f, start1 + (float) e.getDistanceFromDragStartX() / (W * 0.3f)));
            if (mode == 2 && h2.p != nullptr) h2.p->setValueNotifyingHost (jlimit (0.f, 1.f, start2 - (float) e.getDistanceFromDragStartY() / (H * 0.8f)));
        }
        else if (mode == Curve && h1.p != nullptr) h1.set (jlimit (-1.f, 1.f, start1 - (float) e.getDistanceFromDragStartY() / (H * 0.5f)));
    }
    void mouseUp (const MouseEvent&) override
    {
        if (mode >= 0) { if (h1.p) h1.end(); if (mode == 2 && h2.p) h2.end(); }
        h1 = {}; h2 = {}; mode = -1;
    }
    void mouseDoubleClick (const MouseEvent&) override { auto c = prm ("curve"); c.begin(); c.set (0.f); c.end(); }
private:
    enum { Curve = 4 };
    struct Geo { float x0, xa, xh, xd, xs, xr, yt, yb, ys; };
    ParamHandle prm (const char* f) const { return ParamHandle (proc.apvts.getParameter (vanta::mod::envId (sel + 1, f).c_str())); }
    Geo geo() const
    {
        const float wa = 0.06f + 0.24f * prm ("a").p->getValue(), wh = 0.02f + 0.2f * prm ("h").p->getValue();
        const float wd = 0.06f + 0.24f * prm ("d").p->getValue(), wr = 0.06f + 0.24f * prm ("r").p->getValue(), ws = 0.14f;
        const float tot = wa + wh + wd + ws + wr, W = (float) getWidth() - 20.f; Geo q;
        q.x0 = 10.f; q.xa = q.x0 + W * wa / tot; q.xh = q.xa + W * wh / tot; q.xd = q.xh + W * wd / tot; q.xs = q.xd + W * ws / tot; q.xr = q.xs + W * wr / tot;
        q.yt = 22.f; q.yb = (float) getHeight() - 8.f; q.ys = q.yb - prm ("s").get() * (q.yb - q.yt);
        return q;
    }
    void timerCallback() override { if (isShowing()) repaint(); }
    VantaProcessor& proc; int& sel; int mode = -1; ParamHandle h1, h2; float start1 = 0.f, start2 = 0.f;
};

// ---------------------------------------------------------------- step modulator editor (16 bars, bipolar)
class StepEditor : public Component, private Timer
{
public:
    explicit StepEditor (VantaProcessor& p) : proc (p)
    {
        for (int i = 0; i < vanta::mod::kSteps; ++i) hp[i] = ParamHandle (p.apvts.getParameter (vanta::mod::stepValId (i).c_str()));
        startTimerHz (20);
    }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colour (0xff040405)); g.fillRoundedRectangle (r, 5.f);
        g.setColour (border()); g.drawRoundedRectangle (r.reduced (0.5f), 5.f, 1.f);
        const float W = r.getWidth(), H = r.getHeight(), top = 6.f, bot = H - 6.f, mid = (top + bot) * 0.5f, slot = W / (float) vanta::mod::kSteps;
        const int count = (int) proc.apvts.getRawParameterValue (vanta::mod::stepId ("count").c_str())->load();
        g.setColour (Colours::white.withAlpha (0.18f)); g.drawHorizontalLine ((int) mid, 2.f, W - 2.f);
        for (int i = 0; i < vanta::mod::kSteps; ++i)
        {
            const float v = hp[i].get(); const bool live = i < count;
            const float x = (float) i * slot + slot * 0.12f, w = slot * 0.76f, y = mid - v * (mid - top);
            Rectangle<float> bar (x, jmin (y, mid), w, jmax (1.5f, std::fabs (mid - y)));
            g.setGradientFill (ColourGradient (Colours::white.withAlpha (live ? 0.9f : 0.2f), 0.f, bar.getY(), Colours::white.withAlpha (live ? 0.35f : 0.06f), 0.f, bar.getBottom(), false));
            g.fillRoundedRectangle (bar, 2.f);
            g.setColour (live ? white() : dim()); g.setFont (font (10.5f));
            g.drawText (String (i + 1), Rectangle<float> ((float) i * slot, bot - 1.f, slot, 8.f), Justification::centred, false);
        }
    }
    void mouseDown (const MouseEvent& e) override { if (e.mods.isPopupMenu()) { menu(); return; } lastK = -1; edit (e); }
    void mouseDrag (const MouseEvent& e) override { if (! e.mods.isPopupMenu()) edit (e); }
    void mouseUp (const MouseEvent&) override { for (int i = 0; i < vanta::mod::kSteps; ++i) if (touched[i]) { hp[i].end(); touched[i] = false; } lastK = -1; }
    void mouseDoubleClick (const MouseEvent& e) override { const int k = stepAt (e.position.x); hp[k].begin(); hp[k].set (0.f); hp[k].end(); }
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    int stepAt (float x) const { return jlimit (0, vanta::mod::kSteps - 1, (int) (x / ((float) getWidth() / (float) vanta::mod::kSteps))); }
    void edit (const MouseEvent& e)
    {
        const float H = (float) getHeight(), top = 6.f, bot = H - 6.f;
        float v = 1.f - 2.f * (e.position.y - top) / (bot - top); v = jlimit (-1.f, 1.f, v); if (std::fabs (v) < 0.04f) v = 0.f;   // snap to centre
        const int k = stepAt (e.position.x); const int from = lastK < 0 ? k : lastK, step = k >= from ? 1 : -1;
        for (int j = from;; j += step)
        {
            const float t = from == k ? 1.f : (float) (j - from) / (float) (k - from);
            if (! touched[j]) { hp[j].begin(); touched[j] = true; }
            hp[j].set (lastK < 0 ? v : lastV + (v - lastV) * t);
            if (j == k) break;
        }
        lastK = k; lastV = v;
    }
    void menu()
    {
        PopupMenu m; m.addItem (1, "Zero"); m.addItem (2, "Ramp up"); m.addItem (3, "Ramp down"); m.addItem (4, "Triangle"); m.addItem (5, "Alternate +/-"); m.addItem (6, "Randomise");
        Component::SafePointer<StepEditor> sp (this);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [sp] (int r) { if (sp != nullptr && r != 0) sp->pattern (r); });
    }
    void pattern (int id)
    {
        Random rnd; const int n = vanta::mod::kSteps;
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) i / (float) (n - 1); float v = 0.f;
            switch (id) { case 2: v = t * 2.f - 1.f; break; case 3: v = 1.f - t * 2.f; break; case 4: v = t < 0.5f ? t * 4.f - 1.f : 3.f - t * 4.f; break;
                          case 5: v = (i & 1) ? -1.f : 1.f; break; case 6: v = rnd.nextFloat() * 2.f - 1.f; break; default: v = 0.f; }
            hp[i].begin(); hp[i].set (v); hp[i].end();
        }
    }
    VantaProcessor& proc; ParamHandle hp[vanta::mod::kSteps]; bool touched[vanta::mod::kSteps] {}; int lastK = -1; float lastV = 0.f;
};

// ---------------------------------------------------------------- bipolar amount bar (matrix)
class AmountBar : public Component
{
public:
    AmountBar (APVTS& a, const String& id) : h (a.getParameter (id)), att (*h.p, [this] (float) { repaint(); }, nullptr) { att.sendInitialUpdate(); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f); const float v = h.get(), cx = r.getCentreX();
        g.setColour (Colour (0xff050506)); g.fillRoundedRectangle (r, 4.f);
        g.setColour (border()); g.drawRoundedRectangle (r, 4.f, 1.f);
        const float x = cx + v * (r.getWidth() * 0.5f - 2.f);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.85f), jmin (cx, x), 0.f, Colours::white.withAlpha (0.35f), jmax (cx, x), 0.f, false));
        g.fillRect (jmin (cx, x), r.getY() + 3.f, jmax (1.5f, std::fabs (x - cx)), r.getHeight() - 6.f);
        g.setColour (Colours::white.withAlpha (0.5f)); g.fillRect (cx - 0.5f, r.getY() + 1.f, 1.f, r.getHeight() - 2.f);
        g.setColour (white()); g.setFont (font ((float) getHeight() * 0.58f));
        g.drawText ((v > 0.0005f ? "+" : String()) + String (v, 2), getLocalBounds().withTrimmedRight (6), Justification::centredRight, false);
    }
    void mouseDown (const MouseEvent& e) override { h.begin(); mouseDrag (e); }
    void mouseDrag (const MouseEvent& e) override
    {
        float v = jlimit (-1.f, 1.f, ((e.position.x / (float) jmax (1, getWidth())) * 2.f - 1.f) * 1.04f);
        v = std::round (v * (e.mods.isShiftDown() ? 100.f : 50.f)) / (e.mods.isShiftDown() ? 100.f : 50.f); if (std::fabs (v) < 0.02f) v = 0.f;
        h.set (v);
    }
    void mouseUp (const MouseEvent&) override { h.end(); }
    void mouseDoubleClick (const MouseEvent&) override { h.begin(); h.set (0.f); h.end(); }
private:
    ParamHandle h; ParameterAttachment att;
};

// ---------------------------------------------------------------- one matrix row: [on] [source] [destination] [amount]
class MatrixRow : public Component
{
public:
    MatrixRow (APVTS& a, int slot)
        : on (a, vanta::mod::slotId (slot, "on").c_str(), String()), src (a, vanta::mod::slotId (slot, "src").c_str()),
          dst (a, vanta::mod::slotId (slot, "dst").c_str()), amt (a, vanta::mod::slotId (slot, "amt").c_str()), index (slot)
    { addAndMakeVisible (on); addAndMakeVisible (src); addAndMakeVisible (dst); addAndMakeVisible (amt); }
    void resized() override
    {
        const int h = getHeight() - 4;
        on.setBounds (30, 2, 56, h); src.setBounds (94, 2, 190, h); dst.setBounds (292, 2, 230, h); amt.setBounds (530, 2, getWidth() - 534, h);
    }
    void paint (Graphics& g) override
    {
        g.setColour (dim()); g.setFont (font ((float) getHeight() * 0.5f));
        g.drawText (String (index), 0, 0, 26, getHeight(), Justification::centredRight, false);
    }
private:
    VSwitch on; VCombo src, dst; AmountBar amt; int index;
};

// ---------------------------------------------------------------- live routing diagram (sources -> destinations of every enabled slot)
class RoutingView : public Component, private Timer
{
public:
    explicit RoutingView (VantaProcessor& p) : proc (p) { startTimerHz (10); }
    void paint (Graphics& g) override
    {
        using namespace vanta::mod;
        auto r = getLocalBounds().toFloat();
        g.setColour (Colour (0xff040405)); g.fillRoundedRectangle (r, 5.f);
        g.setColour (border()); g.drawRoundedRectangle (r.reduced (0.5f), 5.f, 1.f);
        struct C { int src, dst; float amt; }; std::vector<C> cons; std::vector<int> ss, dd;
        for (int i = 1; i <= kSlots; ++i)
        {
            auto v = [&] (const char* f) { return proc.apvts.getRawParameterValue (slotId (i, f).c_str())->load(); };
            const int s = (int) v ("src"), d = (int) v ("dst"); const float a = v ("amt");
            if (v ("on") < 0.5f || s <= 0 || d <= 0 || a == 0.f) continue;
            cons.push_back ({ s, d, a });
            if (std::find (ss.begin(), ss.end(), s) == ss.end()) ss.push_back (s);
            if (std::find (dd.begin(), dd.end(), d) == dd.end()) dd.push_back (d);
        }
        std::sort (ss.begin(), ss.end()); std::sort (dd.begin(), dd.end());
        if (cons.empty())
        {
            g.setColour (dim()); g.setFont (font (14.f));
            g.drawText ("No active connections. Choose a source, a destination and an amount in the matrix.", r.reduced (14.f), Justification::centred, true); return;
        }
        const float nodeW = r.getWidth() * 0.3f, pad = 8.f;
        auto place = [&] (const std::vector<int>& v, float x, std::vector<Point<float>>& out)
        {
            const float rh = jmin (24.f, (r.getHeight() - 2 * pad) / (float) jmax (1, (int) v.size())); const float gap = (r.getHeight() - 2 * pad) / (float) jmax (1, (int) v.size());
            for (size_t i = 0; i < v.size(); ++i) out.push_back ({ x, pad + gap * ((float) i + 0.5f) });
            return rh;
        };
        std::vector<Point<float>> sp, dp;
        const float rhS = place (ss, pad, sp), rhD = place (dd, r.getWidth() - pad - nodeW, dp);
        for (const auto& c : cons)
        {
            const auto a = sp[(size_t) (std::find (ss.begin(), ss.end(), c.src) - ss.begin())] + Point<float> (nodeW, 0.f);
            const auto b = dp[(size_t) (std::find (dd.begin(), dd.end(), c.dst) - dd.begin())];
            Path p; p.startNewSubPath (a); p.cubicTo (a.x + 60.f, a.y, b.x - 60.f, b.y, b.x, b.y);
            g.setColour (Colours::white.withAlpha (0.25f + 0.6f * std::fabs (c.amt)));
            if (c.amt >= 0.f) g.strokePath (p, PathStrokeType (1.f + 2.5f * std::fabs (c.amt)));
            else { Path dashed; const float dl[] { 6.f, 4.f }; PathStrokeType (1.f + 2.5f * std::fabs (c.amt)).createDashedStroke (dashed, p, dl, 2); g.fillPath (dashed); }
        }
        auto node = [&] (Point<float> pt, float h, const char* t)
        {
            Rectangle<float> b (pt.x, pt.y - h * 0.5f + 1.f, nodeW, h - 2.f);
            g.setColour (Colour (0xff1c1c1f)); g.fillRoundedRectangle (b, 4.f); g.setColour (Colours::white.withAlpha (0.6f)); g.drawRoundedRectangle (b.reduced (0.5f), 4.f, 1.f);
            g.setColour (white()); g.setFont (font (jmin (13.f, h * 0.6f))); g.drawText (t, b.reduced (4.f, 0.f), Justification::centred, true);
        };
        for (size_t i = 0; i < ss.size(); ++i) node (sp[i], rhS, kSrcNames[ss[i]]);
        for (size_t i = 0; i < dd.size(); ++i) node (dp[i], rhD, kDstNames[dd[i]]);
        g.setColour (dim()); g.setFont (font (10.5f));
        g.drawText ("solid = positive   dashed = negative   thickness = amount", Rectangle<float> (0, r.getHeight() - 14.f, r.getWidth(), 12.f), Justification::centred, false);
    }
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    VantaProcessor& proc;
};

// ---------------------------------------------------------------- macro knob with a renaming label
class MacroCell : public Component, private Timer
{
public:
    MacroCell (VantaProcessor& p, int idx)
        : proc (p), index (idx), knob (p.apvts, vanta::mod::macroId (idx + 1).c_str(), String(), [] (double v) { return String (v, 2); }, 100.f, 0.56f)
    {
        addAndMakeVisible (knob); addAndMakeVisible (lab);
        lab.setText (proc.getMacroName (idx), dontSendNotification); lab.setEditable (false, true, false); lab.setJustificationType (Justification::centred);
        lab.setColour (Label::textColourId, white()); lab.setColour (Label::textWhenEditingColourId, Colours::white);
        lab.setColour (Label::backgroundWhenEditingColourId, Colour (0xff101012)); lab.setColour (Label::outlineWhenEditingColourId, border());
        lab.onTextChange = [this] { proc.setMacroName (index, lab.getText()); };
        startTimerHz (2);
    }
    void resized() override
    {
        knob.setBounds (getLocalBounds()); const float sc = (float) getHeight() / 100.f;
        lab.setBounds (0, (int) ((float) getHeight() * 0.56f + 3.f * sc), getWidth(), (int) (19.f * sc)); lab.setFont (font (13.5f * sc));
    }
private:
    void timerCallback() override
    {
        if (! lab.isBeingEdited() && lab.getText() != proc.getMacroName (index)) lab.setText (proc.getMacroName (index), dontSendNotification);
    }
    VantaProcessor& proc; int index; VKnob knob; Label lab;
};

// ---------------------------------------------------------------- MIDI learn panel
class MidiLearnPanel : public Component, private Timer
{
public:
    explicit MidiLearnPanel (VantaProcessor& p) : proc (p) { startTimerHz (10); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colour (0xff040405)); g.fillRoundedRectangle (r, 5.f);
        g.setColour (border()); g.drawRoundedRectangle (r.reduced (0.5f), 5.f, 1.f);
        const auto bl = btnLearn(), bc = btnCancel(), ba = btnClear();
        auto button = [&] (Rectangle<float> b, const String& t, bool lit)
        {
            g.setColour (lit ? Colour (0xff2e2e32) : Colour (0xff141416)); g.fillRoundedRectangle (b, 4.f);
            g.setColour (lit ? Colours::white.withAlpha (0.8f) : border()); g.drawRoundedRectangle (b.reduced (0.5f), 4.f, 1.f);
            g.setColour (white()); g.setFont (font (13.f)); g.drawText (t, b, Justification::centred, false);
        };
        const int lp = proc.getLearningParam();
        button (bl, "Learn parameter...", lp >= 0); button (bc, "Cancel", false); button (ba, "Clear all", false);
        g.setColour (lp >= 0 ? Colours::white : dim()); g.setFont (font (12.5f));
        String status = "Last CC received: " + (proc.getLastCC() >= 0 ? String (proc.getLastCC()) : String ("-"));
        if (lp >= 0) status = "Move a MIDI controller now to assign:  " + proc.getAllParams()[(size_t) lp]->getName (40);
        g.drawText (status, Rectangle<float> (8.f, 36.f, r.getWidth() - 16.f, 18.f), Justification::centredLeft, true);
        const auto maps = proc.getMidiMappings();
        scrollMax = jmax (0, (int) maps.size() - visibleRows());
        scroll = jlimit (0, scrollMax, scroll);
        if (maps.empty())
        { g.setColour (dim()); g.drawText ("No MIDI mappings. Press 'Learn parameter...', choose a parameter, then move a knob on your controller.", Rectangle<float> (10.f, 60.f, r.getWidth() - 20.f, 60.f), Justification::centredTop, true); return; }
        for (int i = 0; i < visibleRows() && i + scroll < (int) maps.size(); ++i)
        {
            const auto& m = maps[(size_t) (i + scroll)]; const auto row = rowRect (i);
            g.setColour (i % 2 ? Colour (0xff0b0b0d) : Colour (0xff111113)); g.fillRect (row);
            g.setColour (white()); g.setFont (font (13.f));
            g.drawText ("CC " + String (m.cc), row.withWidth (60.f).translated (8.f, 0.f), Justification::centredLeft, false);
            g.drawText (proc.getAllParams()[(size_t) m.paramIndex]->getName (36), row.withTrimmedLeft (74.f).withTrimmedRight (30.f), Justification::centredLeft, true);
            g.setColour (dim()); g.drawText ("x", removeRect (i), Justification::centred, false);
        }
    }
    void mouseDown (const MouseEvent& e) override
    {
        const auto p = e.position;
        if (btnLearn().contains (p)) { learnMenu(); return; }
        if (btnCancel().contains (p)) { proc.cancelMidiLearn(); return; }
        if (btnClear().contains (p)) { proc.clearMidiMappings(); return; }
        const auto maps = proc.getMidiMappings();
        for (int i = 0; i < visibleRows() && i + scroll < (int) maps.size(); ++i)
            if (removeRect (i).contains (p)) { proc.removeMidiMapping (maps[(size_t) (i + scroll)].cc); return; }
    }
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& d) override { scroll = jlimit (0, scrollMax, scroll - (d.deltaY > 0 ? 1 : -1)); repaint(); }
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    int visibleRows() const { return jmax (1, (getHeight() - 62) / 22); }
    Rectangle<float> btnLearn() const  { return { 8.f, 6.f, 150.f, 26.f }; }
    Rectangle<float> btnCancel() const { return { 166.f, 6.f, 80.f, 26.f }; }
    Rectangle<float> btnClear() const  { return { 254.f, 6.f, 90.f, 26.f }; }
    Rectangle<float> rowRect (int i) const { return { 4.f, 58.f + 22.f * (float) i, (float) getWidth() - 8.f, 22.f }; }
    Rectangle<float> removeRect (int i) const { auto r = rowRect (i); return r.removeFromRight (28.f); }
    static String groupOf (const String& id)
    {
        struct G { const char* prefix; const char* name; };
        static const G gs[] { { "mac", "Macros" }, { "master", "Master" }, { "flt", "Filter" }, { "amp", "Amp Envelope" }, { "porta", "Glide" }, { "uni", "Unison" },
                              { "va", "Virtual Analog" }, { "sub", "Sub / Noise" }, { "noise", "Sub / Noise" }, { "gr", "Granular" }, { "add", "Additive" },
                              { "lfo", "LFOs" }, { "env", "Envelopes" }, { "stp", "Step Modulator" }, { "mm", "Matrix" } };
        for (const auto& g : gs) if (id.startsWith (g.prefix)) return g.name;
        return "Other";
    }
    void learnMenu()
    {
        std::map<String, PopupMenu> groups; const auto& ps = proc.getAllParams();
        for (size_t i = 0; i < ps.size(); ++i) groups[groupOf (ps[i]->paramID)].addItem ((int) i + 1, ps[i]->getName (40));
        PopupMenu m;
        auto add = [&] (const String& n) { auto it = groups.find (n); if (it != groups.end()) { m.addSubMenu (n, it->second); groups.erase (it); } };
        for (auto* n : { "Macros", "Filter", "Amp Envelope", "Master" }) add (n);
        for (auto it = groups.begin(); it != groups.end();) { m.addSubMenu (it->first, it->second); it = groups.erase (it); }
        Component::SafePointer<MidiLearnPanel> sp (this);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [sp] (int r) { if (sp != nullptr && r > 0) sp->proc.beginMidiLearn (r - 1); });
    }
    VantaProcessor& proc; int scroll = 0, scrollMax = 0;
};
} // namespace vui
