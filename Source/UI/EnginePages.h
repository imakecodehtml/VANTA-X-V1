#pragma once
// VANTA X - widgets used by the Granular and Additive pages (Stage 3).
// Every widget here reads / writes real APVTS parameters or real engine data (sample overview, grain ring, shaped spectrum).
#include <complex>
#include "../PluginProcessor.h"
#include "Widgets.h"

namespace vui
{
// ---------------------------------------------------------------- parameter handle for custom-drawn editors
struct ParamHandle
{
    RangedAudioParameter* p = nullptr;
    ParamHandle() = default;
    explicit ParamHandle (RangedAudioParameter* q) : p (q) {}
    float get() const { return p->convertFrom0to1 (p->getValue()); }
    float def() const { return p->convertFrom0to1 (p->getDefaultValue()); }
    void begin() { p->beginChangeGesture(); }
    void set (float real) { p->setValueNotifyingHost (p->convertTo0to1 (real)); }
    void end() { p->endChangeGesture(); }
};

// ---------------------------------------------------------------- simple text label (scales with its height)
class Caption : public Component
{
public:
    Caption (const String& t, Justification j = Justification::centred, bool dimmed = false, float frac = 0.62f)
        : text (t), just (j), dimmedText (dimmed), fr (frac) { setInterceptsMouseClicks (false, false); }
    void paint (Graphics& g) override
    {
        g.setColour (dimmedText ? dim() : white()); g.setFont (font ((float) getHeight() * fr));
        g.drawText (text, getLocalBounds(), just, false);
    }
private:
    String text; Justification just; bool dimmedText; float fr;
};

// ---------------------------------------------------------------- live text (polled), e.g. active grains
class LiveText : public Component, private Timer
{
public:
    LiveText (std::function<String()> f, Justification j = Justification::centredLeft, bool dimmed = true)
        : fn (std::move (f)), just (j), dimmedText (dimmed) { setInterceptsMouseClicks (false, false); startTimerHz (10); }
    void paint (Graphics& g) override
    {
        g.setColour (dimmedText ? dim() : white()); g.setFont (font ((float) getHeight() * 0.62f));
        g.drawText (fn(), getLocalBounds(), just, false);
    }
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    std::function<String()> fn; Justification just; bool dimmedText;
};

// ---------------------------------------------------------------- on/off switch bound to a 2-choice parameter
class VSwitch : public Component
{
public:
    VSwitch (APVTS& a, const String& id, const String& label)
        : name (label), param (a.getParameter (id)), att (*param, [this] (float v) { on = v > 0.5f; repaint(); }, nullptr)
    { att.sendInitialUpdate(); setMouseCursor (MouseCursor::PointingHandCursor); }
    void paint (Graphics& g) override
    {
        const float h = (float) getHeight(); const float tw = h * 1.8f;
        Rectangle<float> t (1.f, h * 0.12f, tw, h * 0.76f);
        g.setColour (Colour (0xff050506)); g.fillRoundedRectangle (t, t.getHeight() * 0.5f);
        g.setColour (on ? Colours::white.withAlpha (0.75f) : border()); g.drawRoundedRectangle (t.reduced (0.5f), t.getHeight() * 0.5f, 1.2f);
        const float kd = t.getHeight() - 4.f; const float kx = on ? t.getRight() - kd - 2.f : t.getX() + 2.f;
        g.setGradientFill (ColourGradient (on ? Colour (0xfff2f2f2) : Colour (0xff77777d), kx, t.getY(), on ? Colour (0xff9a9aa0) : Colour (0xff2a2a2e), kx, t.getBottom(), false));
        g.fillEllipse (kx, t.getY() + 2.f, kd, kd);
        g.setColour (on ? white() : dim()); g.setFont (font (h * 0.62f));
        g.drawText (name, getLocalBounds().withTrimmedLeft ((int) (tw + h * 0.5f)), Justification::centredLeft, false);
    }
    void mouseDown (const MouseEvent&) override { att.setValueAsCompleteGesture (on ? 0.f : 1.f); }
private:
    String name; RangedAudioParameter* param; ParameterAttachment att; bool on = false;
};

// ---------------------------------------------------------------- shared drawings
inline void drawAmpEnv (Graphics& g, Rectangle<float> r, float aT, float dT, float s, float rT)
{
    auto nrm = [] (float x) { return jlimit (0.f, 1.f, (std::log10 (std::max (0.0005f, x) * 1000.f) + 0.3f) / 4.3f); };
    const float a = 0.08f + 0.25f * nrm (aT), d = 0.08f + 0.25f * nrm (dT), rl = 0.08f + 0.25f * nrm (rT), hold = 0.18f;
    const float tot = a + d + hold + rl; const float W = r.getWidth() - 8, X0 = r.getX() + 4, yb = r.getBottom() - 4, yt = r.getY() + 6, H = yb - yt;
    const float xa = X0 + W * a / tot, xd = xa + W * d / tot, xs = xd + W * hold / tot, xr = xs + W * rl / tot;
    Path p; p.startNewSubPath (X0, yb); p.lineTo (xa, yt);
    for (int i = 1; i <= 24; ++i) { const float t = i / 24.f; const float y = 1.f + (s - 1.f) * (1.f - std::exp (-4.6f * t)); p.lineTo (xa + (xd - xa) * t, yb - y * H); }
    p.lineTo (xs, yb - s * H);
    for (int i = 1; i <= 24; ++i) { const float t = i / 24.f; const float y = s * std::exp (-4.6f * t) * (1.f - t); p.lineTo (xs + (xr - xs) * t, yb - y * H); }
    Path f (p); f.lineTo (xr, yb); f.closeSubPath();
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.14f), 0, yt, Colours::transparentWhite, 0, yb, false)); g.fillPath (f);
    glowStroke (g, p, 1.5f);
    g.setColour (Colours::white);
    const Point<float> pts[5] { Point<float> (X0, yb), Point<float> (xa, yt), Point<float> (xd, yb - s * H), Point<float> (xs, yb - s * H), Point<float> (xr, yb) };
    for (const auto& pt : pts) g.fillEllipse (pt.x - 3, pt.y - 3, 6, 6);
}

inline void drawFilterResponse (Graphics& g, Rectangle<float> r, int type, float fc, float res)
{
    const float k = res * 3.9f;
    g.setColour (Colours::white.withAlpha (0.07f));
    for (int i = 1; i < 4; ++i) g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * i / 4), r.getX(), r.getRight());
    Path line, fill; const int N = jmax (2, (int) r.getWidth());
    for (int i = 0; i <= N; ++i)
    {
        const float f = 20.f * std::pow (1000.f, i / (float) N);
        const std::complex<float> a = 1.f / std::complex<float> (1.f, f / fc), a2 = a * a, a4 = a2 * a2, den = 1.f + k * a4;
        std::complex<float> num;
        switch (type) { case 1: num = a2; break; case 2: num = 2.f * (a2 - a4); break; case 3: { auto o = 1.f - a; num = o * o * o * o; break; } default: num = a4; }
        const float db = 20.f * std::log10 (std::max (1e-5f, std::abs (num / den)));
        const float y = jmap (jlimit (-42.f, 18.f, db), 18.f, -42.f, r.getY(), r.getBottom());
        const float x = r.getX() + r.getWidth() * (float) i / (float) N;
        if (i == 0) { line.startNewSubPath (x, y); fill.startNewSubPath (x, r.getBottom()); fill.lineTo (x, y); } else { line.lineTo (x, y); fill.lineTo (x, y); }
    }
    fill.lineTo (r.getRight(), r.getBottom()); fill.closeSubPath();
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.16f), 0, r.getY(), Colours::transparentWhite, 0, r.getBottom(), false)); g.fillPath (fill);
    glowStroke (g, line, 1.5f);
}

// ---------------------------------------------------------------- loading with user feedback (message thread)
inline void loadSampleWithFeedback (VantaProcessor& proc, const File& f)
{
    String err;
    if (! proc.loadSample (f, err))
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Could not load sample", err);
}

// ---------------------------------------------------------------- sample selector (name + menu + folder button)
class SampleSelector : public Component, private Timer
{
public:
    explicit SampleSelector (VantaProcessor& p) : proc (p) { startTimerHz (4); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Colour (0xff101012)); g.fillRoundedRectangle (r, 5.f);
        g.setColour (border()); g.drawRoundedRectangle (r, 5.f, 1.f);
        String t = "-";
        if (auto s = proc.samples.current()) t = String (s->name);
        const String missing = proc.getSampleMissingName();
        if (missing.isNotEmpty()) t = "Missing: " + missing + "  (built-in used)";
        g.setColour (missing.isNotEmpty() ? dim() : white()); g.setFont (font ((float) getHeight() * 0.5f));
        g.drawText (t, getLocalBounds().withTrimmedLeft (10).withTrimmedRight (getHeight() + 22), Justification::centredLeft, true);
        Path p; const float cx = (float) getWidth() - (float) getHeight() - 12.f, cy = getHeight() * 0.5f;
        p.startNewSubPath (cx - 4, cy - 2); p.lineTo (cx, cy + 2); p.lineTo (cx + 4, cy - 2);
        g.setColour (white()); g.strokePath (p, PathStrokeType (1.6f));
        const float s = (float) getHeight() * 0.5f; const float fx = (float) getWidth() - (float) getHeight() * 0.5f - s * 0.5f - 2.f, fy = (float) getHeight() * 0.5f - s * 0.3f;
        g.drawRoundedRectangle (fx, fy + s * 0.1f, s, s * 0.6f, 2.f, 1.5f);
        g.drawLine (fx, fy + s * 0.1f, fx + s * 0.35f, fy + s * 0.1f, 1.5f);
    }
    void mouseDown (const MouseEvent& e) override
    {
        if (e.x > getWidth() - getHeight() - 2) chooseFile(); else menu();
    }
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    void chooseFile()
    {
        chooser = std::make_unique<FileChooser> ("Load a sample for the granular engine", proc.presets.getSamplesFolder(), proc.samples.getWildcard());
        Component::SafePointer<SampleSelector> sp (this);
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [sp] (const FileChooser& fc)
        {
            if (sp == nullptr) return;
            const auto f = fc.getResult();
            if (f != File()) loadSampleWithFeedback (sp->proc, f);
            sp->repaint();
        });
    }
    void menu()
    {
        auto files = std::make_shared<Array<File>> (proc.presets.getSamplesFolder().findChildFiles (File::findFiles, true, proc.samples.getWildcard()));
        while (files->size() > 200) files->removeLast();
        PopupMenu m; m.addItem (1, "Load sample file...");
        m.addItem (2, "Use built-in texture");
        m.addItem (3, "Open user sample folder");
        m.addSeparator();
        if (files->isEmpty()) m.addItem (-1, "(no files in the user Samples folder)", false, false);
        const String cur = proc.samples.currentPath();
        for (int i = 0; i < files->size(); ++i) m.addItem (100 + i, (*files)[i].getFileName(), true, (*files)[i].getFullPathName() == cur);
        Component::SafePointer<SampleSelector> sp (this);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [sp, files] (int r)
        {
            if (sp == nullptr) return;
            if (r == 1) sp->chooseFile();
            else if (r == 2) sp->proc.useBuiltInSample();
            else if (r == 3) sp->proc.presets.getSamplesFolder().revealToUser();
            else if (r >= 100 && r - 100 < files->size()) loadSampleWithFeedback (sp->proc, (*files)[r - 100]);
            sp->repaint();
        });
    }
    VantaProcessor& proc; std::unique_ptr<FileChooser> chooser;
};

// ---------------------------------------------------------------- granular sample view
// Real waveform (peak overview of the loaded sample), draggable start / end / position handles, spray band, live grain markers.
class SampleView : public Component, public FileDragAndDropTarget, private Timer
{
public:
    explicit SampleView (VantaProcessor& p)
        : proc (p), pStart (p.apvts.getParameter ("gr_start")), pEnd (p.apvts.getParameter ("gr_end")),
          pPos (p.apvts.getParameter ("gr_pos")), pSpray (p.apvts.getParameter ("gr_spray"))
    { startTimerHz (30); setMouseCursor (MouseCursor::LeftRightResizeCursor); }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colour (0xff040405)); g.fillRoundedRectangle (r, 5.f);
        g.setColour (border()); g.drawRoundedRectangle (r.reduced (0.5f), 5.f, 1.f);
        const float W = r.getWidth(), H = r.getHeight(), cy = H * 0.5f;
        float st = pStart.get(), en = pEnd.get(); if (st > en) std::swap (st, en);
        const auto s = proc.samples.current();
        if (s != nullptr && ! s->overview.empty())
        {
            const int N = vanta::SampleData::kOverview; float peak = 0.05f;
            for (float v : s->overview) peak = jmax (peak, std::fabs (v));
            const float gain = 0.46f * H / peak;
            for (int x = 0; x < (int) W - 2; ++x)
            {
                const int i0 = jlimit (0, N - 1, (int) ((float) x / W * (float) N)), i1 = jlimit (i0 + 1, N, (int) ((float) (x + 1) / W * (float) N));
                float lo = 0.f, hi = 0.f;
                for (int i = i0; i < i1; ++i) { lo = jmin (lo, s->overview[(size_t) i * 2]); hi = jmax (hi, s->overview[(size_t) i * 2 + 1]); }
                const float nx = (float) x / W; const bool inside = nx >= st && nx <= en;
                g.setColour (white().withAlpha (inside ? 0.88f : 0.22f));
                g.fillRect ((float) x + 1.f, cy - hi * gain - 0.5f, 1.f, jmax (1.f, (hi - lo) * gain + 1.f));
            }
        }
        g.setColour (Colours::white.withAlpha (0.08f)); g.drawHorizontalLine ((int) cy, 1.f, W - 1.f);

        // position + spray band (position is relative to the sample region)
        const float u = st + pPos.get() * (en - st), half = 0.5f * pSpray.get() * (en - st);
        g.setColour (Colours::white.withAlpha (0.10f));
        g.fillRect (jmax (0.f, (u - half) * W), 0.f, jmin (W, (u + half) * W) - jmax (0.f, (u - half) * W), H);
        g.setColour (Colours::white); g.fillRect (u * W - 0.75f, 0.f, 1.5f, H);
        Path tri; tri.addTriangle (u * W - 5, 0, u * W + 5, 0, u * W, 7); g.fillPath (tri);

        // region handles
        for (float hx : { st * W, en * W })
        {
            g.setColour (Colours::white.withAlpha (0.9f)); g.fillRect (hx - 0.5f, 0.f, 1.f, H);
            g.fillRect (hx - 3.f, H - 8.f, 6.f, 8.f);
        }

        // live grains
        if (proc.grainCount.load() > 0)
        {
            const uint32_t c = proc.grainViz.counter.load (std::memory_order_relaxed);
            for (int k = 0; k < 32 && c > 0; ++k)
            {
                const int i = (int) ((c - 1u - (uint32_t) k) & (uint32_t) (vanta::GrainViz::kN - 1));
                if (proc.grainViz.size[i].load (std::memory_order_relaxed) <= 0.f) continue;
                const float x = proc.grainViz.pos[i].load (std::memory_order_relaxed) * W;
                g.setColour (Colours::white.withAlpha (0.75f * (1.f - (float) k / 32.f)));
                g.fillRect (x - 1.f, H - 14.f, 2.f, 6.f);
            }
        }

        // info
        String info;
        if (s != nullptr)
            info = String (s->name) + "   " + String (s->sampleRate / 1000.0, 1) + " kHz  " + (s->channels > 1 ? "stereo" : "mono") + "  "
                   + String ((double) s->frames / s->sampleRate, 2) + " s" + (proc.samples.wasTruncated() ? "  (truncated to 120 s)" : "");
        g.setColour (dim()); g.setFont (font (11.5f));
        g.drawText (info, Rectangle<float> (8, 4, W - 16, 16), Justification::topLeft, true);
        if (proc.getSampleMissingName().isNotEmpty())
        {
            g.setColour (white()); g.drawText ("File not found: " + proc.getSampleMissingName() + " - using the built-in texture", Rectangle<float> (8, H - 22, W - 16, 16), Justification::bottomLeft, true);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        const float W = (float) getWidth(); float st = pStart.get(), en = pEnd.get(); if (st > en) std::swap (st, en);
        const float x = e.position.x, ds = std::fabs (x - st * W), de = std::fabs (x - en * W), up = std::fabs (x - (st + pPos.get() * (en - st)) * W);
        const float tol = 7.f;
        if (up <= tol && up <= ds && up <= de) drag = Pos;
        else if (ds <= tol && ds <= de) drag = Start;
        else if (de <= tol) drag = End;
        else drag = Pos;
        if (drag == Start) pStart.begin(); else if (drag == End) pEnd.begin(); else pPos.begin();
        mouseDrag (e);
    }
    void mouseDrag (const MouseEvent& e) override
    {
        const float n = jlimit (0.f, 1.f, e.position.x / (float) jmax (1, getWidth()));
        float st = pStart.get(), en = pEnd.get(); if (st > en) std::swap (st, en);
        if (drag == Start) pStart.set (jmin (n, en - 0.005f));
        else if (drag == End) pEnd.set (jmax (n, st + 0.005f));
        else if (drag == Pos) pPos.set (jlimit (0.f, 1.f, (n - st) / jmax (0.001f, en - st)));
    }
    void mouseUp (const MouseEvent&) override
    {
        if (drag == Start) pStart.end(); else if (drag == End) pEnd.end(); else if (drag == Pos) pPos.end();
        drag = None;
    }
    void mouseDoubleClick (const MouseEvent&) override       // reset the region to the whole sample
    {
        pStart.begin(); pStart.set (0.f); pStart.end(); pEnd.begin(); pEnd.set (1.f); pEnd.end();
    }

    bool isInterestedInFileDrag (const StringArray& files) override
    {
        const auto wc = proc.samples.getWildcard().toLowerCase();
        for (auto& f : files) if (wc.contains ("*" + File (f).getFileExtension().toLowerCase())) return true;
        return false;
    }
    void filesDropped (const StringArray& files, int, int) override
    {
        if (files.isEmpty()) return;
        loadSampleWithFeedback (proc, File (files[0]));
    }
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    enum Drag { None, Start, End, Pos };
    VantaProcessor& proc; ParamHandle pStart, pEnd, pPos, pSpray; Drag drag = None;
};

// ---------------------------------------------------------------- grain cloud (live: x = position in the sample, y = pitch, size = grain length)
class GrainCloud : public Component, private Timer
{
public:
    explicit GrainCloud (VantaProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colour (0xff040405)); g.fillRoundedRectangle (r, 5.f);
        g.setColour (border()); g.drawRoundedRectangle (r.reduced (0.5f), 5.f, 1.f);
        g.setColour (Colours::white.withAlpha (0.07f));
        for (int i = 1; i < 4; ++i) g.drawHorizontalLine ((int) (r.getHeight() * i / 4), 1.f, r.getWidth() - 1.f);
        g.drawHorizontalLine ((int) (r.getHeight() * 0.5f), 1.f, r.getWidth() - 1.f);
        g.setColour (dim()); g.setFont (font (11.f));
        g.drawText ("GRAIN CLOUD", Rectangle<float> (6, 3, r.getWidth() - 12, 14), Justification::topLeft, false);
        g.drawText ("position  >", Rectangle<float> (6, r.getHeight() - 17, r.getWidth() - 12, 14), Justification::bottomRight, false);
        if (proc.grainCount.load() <= 0) return;
        const uint32_t c = proc.grainViz.counter.load (std::memory_order_relaxed);
        for (int k = 0; k < vanta::GrainViz::kN && c > 0; ++k)
        {
            const int i = (int) ((c - 1u - (uint32_t) k) & (uint32_t) (vanta::GrainViz::kN - 1));
            const float sz = proc.grainViz.size[i].load (std::memory_order_relaxed);
            if (sz <= 0.f) continue;
            const float x = proc.grainViz.pos[i].load (std::memory_order_relaxed) * (r.getWidth() - 12.f) + 6.f;
            const float semis = jlimit (-36.f, 36.f, proc.grainViz.pitch[i].load (std::memory_order_relaxed));
            const float y = r.getHeight() * 0.5f - semis / 36.f * (r.getHeight() * 0.42f);
            const float rad = jmap (jlimit (0.002f, 0.5f, sz), 0.002f, 0.5f, 1.5f, 5.5f);
            g.setColour (Colours::white.withAlpha (0.85f * (1.f - (float) k / (float) vanta::GrainViz::kN)));
            g.fillEllipse (x - rad, y - rad, rad * 2.f, rad * 2.f);
        }
    }
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    VantaProcessor& proc;
};

// ---------------------------------------------------------------- additive: shared editor state (selected harmonic, display/edit mode)
struct HarmonicEdit
{
    int selected = 0;     // 0..31
    int mode = 0;         // 0 amplitude, 1 phase, 2 output (read-only analyser view)
    std::vector<std::function<void()>> listeners;
    void changed() { for (auto& f : listeners) f(); }
};

// Click / drag on bars to edit individual harmonics (amplitude or phase). Double-click resets, right-click = patterns.
class HarmonicSpectrum : public Component, private Timer
{
public:
    HarmonicSpectrum (VantaProcessor& p, HarmonicEdit& h) : proc (p), he (h)
    {
        for (int k = 0; k < vanta::kAddHarm; ++k)
        {
            hp[0][k] = ParamHandle (p.apvts.getParameter (vanta::addHarmId ('h', k).c_str()));
            hp[1][k] = ParamHandle (p.apvts.getParameter (vanta::addHarmId ('p', k).c_str()));
        }
        startTimerHz (30);
    }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colour (0xff040405)); g.fillRoundedRectangle (r, 5.f);
        g.setColour (border()); g.drawRoundedRectangle (r.reduced (0.5f), 5.f, 1.f);
        const float W = r.getWidth(), H = r.getHeight(), labelH = jmax (12.f, H * 0.11f), top = 6.f, plotH = H - labelH - top - 2.f, bottom = top + plotH;
        g.setColour (Colours::white.withAlpha (0.06f));
        for (int i = 0; i <= 4; ++i) g.drawHorizontalLine ((int) (top + plotH * i / 4.f), 1.f, W - 1.f);
        vanta::AdditiveParams ap; proc.loadAdditiveParams (ap);
        const float slot = W / (float) vanta::kAddHarm;
        float peak = 1e-6f; for (int k = 0; k < vanta::kAddHarm; ++k) peak = jmax (peak, ap.shaped[k]);
        for (int k = 0; k < vanta::kAddHarm; ++k)
        {
            float v = he.mode == 0 ? ap.amp[k] : (he.mode == 1 ? ap.phase[k] : ap.shaped[k] / peak);
            v = jlimit (0.f, 1.f, v);
            const bool muted = (k + 1) > ap.count; const bool sel = k == he.selected;
            const float bw = slot * 0.62f, x = (float) k * slot + (slot - bw) * 0.5f, h = v * plotH;
            Rectangle<float> bar (x, bottom - h, bw, h);
            g.setGradientFill (ColourGradient (Colours::white.withAlpha (muted ? 0.22f : 0.92f), 0.f, bar.getY(), Colours::white.withAlpha (muted ? 0.05f : 0.30f), 0.f, bottom, false));
            g.fillRoundedRectangle (bar, 1.5f);
            if (he.mode != 2) { g.setColour (Colours::white.withAlpha (muted ? 0.3f : 1.f)); g.fillRect (x - 1.f, bottom - h - 1.f, bw + 2.f, 2.f); }
            if (sel) { g.setColour (Colours::white); g.drawRoundedRectangle (x - 2.f, top - 2.f, bw + 4.f, plotH + 4.f, 2.f, 1.f); }
            if (k == 0 || (k + 1) % 4 == 0 || sel)
            {
                g.setColour (sel ? Colours::white : dim()); g.setFont (font (labelH * 0.82f));
                g.drawText (String (k + 1), Rectangle<float> (x - slot, bottom + 2.f, bw + 2.f * slot, labelH), Justification::centred, false);
            }
        }
    }
    void mouseDown (const MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { patternMenu(); return; }
        lastK = -1;
        edit (e);
    }
    void mouseDrag (const MouseEvent& e) override { if (! e.mods.isPopupMenu()) edit (e); }
    void mouseUp (const MouseEvent&) override
    {
        for (int m = 0; m < 2; ++m) for (int k = 0; k < vanta::kAddHarm; ++k) if (touched[m][k]) { hp[m][k].end(); touched[m][k] = false; }
        lastK = -1;
    }
    void mouseDoubleClick (const MouseEvent& e) override
    {
        if (he.mode == 2) return;
        const int k = harmAt (e.position.x), m = he.mode == 1 ? 1 : 0;
        hp[m][k].begin(); hp[m][k].set (hp[m][k].def()); hp[m][k].end();
    }
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    int harmAt (float x) const { return jlimit (0, vanta::kAddHarm - 1, (int) (x / ((float) getWidth() / (float) vanta::kAddHarm))); }
    float valueAt (float y) const
    {
        const float H = (float) getHeight(), labelH = jmax (12.f, H * 0.11f), top = 6.f, plotH = H - labelH - top - 2.f;
        return jlimit (0.f, 1.f, 1.f - (y - top) / plotH);
    }
    void edit (const MouseEvent& e)
    {
        const int k = harmAt (e.position.x);
        he.selected = k; he.changed();
        if (he.mode == 2) return;                              // output view is read-only (selecting still works)
        const int m = he.mode == 1 ? 1 : 0; const float v = valueAt (e.position.y);
        const int from = lastK < 0 ? k : lastK, to = k, step = to >= from ? 1 : -1;
        for (int j = from;; j += step)                         // fill harmonics skipped by a fast drag
        {
            const float t = from == to ? 1.f : (float) (j - from) / (float) (to - from);
            const float vv = lastK < 0 ? v : lastV + (v - lastV) * t;
            if (! touched[m][j]) { hp[m][j].begin(); touched[m][j] = true; }
            hp[m][j].set (vv);
            if (j == to) break;
        }
        lastK = k; lastV = v;
    }
    void patternMenu()
    {
        if (he.mode == 2) return;
        PopupMenu m;
        if (he.mode == 0)
        { m.addItem (1, "Saw (1/n)"); m.addItem (2, "Square (odd 1/n)"); m.addItem (3, "Triangle (odd 1/n^2)"); m.addItem (4, "All harmonics full"); m.addItem (5, "Clear all"); m.addItem (6, "Randomise"); }
        else
        { m.addItem (7, "All phases 0"); m.addItem (8, "Randomise phases"); }
        Component::SafePointer<HarmonicSpectrum> sp (this);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [sp] (int r) { if (sp != nullptr && r != 0) sp->applyPattern (r); });
    }
    void applyPattern (int id)
    {
        Random rnd;
        const int m = id >= 7 ? 1 : 0;
        for (int k = 0; k < vanta::kAddHarm; ++k)
        {
            const int h = k + 1; const bool odd = (h & 1) != 0; float v = 0.f;
            switch (id)
            {
                case 1: v = 1.f / (float) h; break;
                case 2: v = odd ? 1.f / (float) h : 0.f; break;
                case 3: v = odd ? 1.f / (float) (h * h) : 0.f; break;
                case 4: v = 1.f; break;
                case 5: v = 0.f; break;
                case 6: v = rnd.nextFloat(); break;
                case 7: v = 0.f; break;
                default: v = rnd.nextFloat(); break;
            }
            hp[m][k].begin(); hp[m][k].set (v); hp[m][k].end();
        }
    }
    VantaProcessor& proc; HarmonicEdit& he; ParamHandle hp[2][vanta::kAddHarm];
    bool touched[2][vanta::kAddHarm] {}; int lastK = -1; float lastV = 0.f;
};

// ---------------------------------------------------------------- bank of 8 vertical sliders for the selected group of harmonics
class HarmonicBank : public Component
{
public:
    HarmonicBank (VantaProcessor& p, HarmonicEdit& h) : proc (p), he (h)
    {
        for (int i = 0; i < 8; ++i)
        {
            auto& s = sl[i];
            s.setSliderStyle (Slider::LinearVertical); s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
            s.onDragStart = [this, i] { he.selected = page * 8 + i; he.changed(); };
            s.onValueChange = [this] { repaint(); };
            addAndMakeVisible (s);
        }
        he.listeners.push_back ([this] { sync(); });
        sync (true);
    }
    void resized() override
    {
        const float slot = (float) getWidth() / 8.f;
        for (int i = 0; i < 8; ++i) sl[i].setBounds ((int) (i * slot + (slot - 30.f) * 0.5f), headH, 30, jmax (20, getHeight() - headH - footH));
    }
    void paint (Graphics& g) override
    {
        const float W = (float) getWidth(), slot = W / 8.f, segW = W / 4.f;
        const bool phase = he.mode == 1;
        static const char* names[] { "1-8", "9-16", "17-24", "25-32" };
        for (int i = 0; i < 4; ++i)
        {
            Rectangle<float> r ((float) i * segW + 2.f, 0.f, segW - 4.f, (float) headH - 4.f);
            g.setColour (i == page ? Colour (0xff26262a) : Colour (0xff111113)); g.fillRoundedRectangle (r, 4.f);
            g.setColour (i == page ? Colours::white.withAlpha (0.7f) : border()); g.drawRoundedRectangle (r.reduced (0.5f), 4.f, 1.f);
            g.setColour (i == page ? Colours::white : dim()); g.setFont (font ((float) headH * 0.5f));
            g.drawText (names[i], r, Justification::centred, false);
        }
        const int sel = he.selected - page * 8;
        if (sel >= 0 && sel < 8)
        { g.setColour (Colours::white.withAlpha (0.07f)); g.fillRoundedRectangle ((float) sel * slot + 1.f, (float) headH, slot - 2.f, (float) (getHeight() - headH), 4.f); }
        g.setFont (font ((float) footH * 0.34f));
        for (int i = 0; i < 8; ++i)
        {
            g.setColour (i == sel ? Colours::white : dim());
            g.drawText (String (page * 8 + i + 1), Rectangle<float> ((float) i * slot, (float) (getHeight() - footH + 2), slot, (float) footH * 0.4f), Justification::centred, false);
        }
        const int k = jlimit (0, 31, he.selected);
        const auto* prm = proc.apvts.getParameter (vanta::addHarmId (phase ? 'p' : 'h', k).c_str());
        const float v = prm != nullptr ? prm->convertFrom0to1 (prm->getValue()) : 0.f;
        g.setColour (white()); g.setFont (font ((float) footH * 0.38f));
        g.drawText ("Harmonic " + String (k + 1) + (phase ? "  phase " : "  level ") + String (v, 2) + (phase ? " turn" : ""),
                    Rectangle<float> (0.f, (float) (getHeight() - footH * 0.5f), W, (float) footH * 0.5f), Justification::centred, false);
    }
    void mouseDown (const MouseEvent& e) override
    {
        if (e.y < headH) { page = jlimit (0, 3, (int) (e.x / ((float) getWidth() / 4.f))); he.selected = page * 8 + he.selected % 8; he.changed(); }
    }
private:
    void sync (bool force = false)
    {
        const int wantPage = he.selected / 8; const char kind = he.mode == 1 ? 'p' : 'h';
        if (! force && wantPage == page && kind == curKind) { repaint(); return; }
        page = wantPage; curKind = kind;
        for (int i = 0; i < 8; ++i)
        {
            att[i].reset();
            if (auto* prm = proc.apvts.getParameter (vanta::addHarmId (kind, page * 8 + i).c_str()))
                att[i] = std::make_unique<SliderParameterAttachment> (*prm, sl[i], nullptr);
        }
        repaint();
    }
    static constexpr int headH = 24, footH = 44;
    VantaProcessor& proc; HarmonicEdit& he; Slider sl[8]; std::unique_ptr<SliderParameterAttachment> att[8]; int page = 0; char curKind = 'h';
};
} // namespace vui
