#include "PluginEditor.h"
#include "Synthesis/VirtualAnalog/VAOscillator.h"
#include <complex>

using namespace juce;
using namespace vui;

// ------------------------------------------------------------------ header pieces
struct HeaderStats : Component, Timer
{
    HeaderStats (VantaProcessor& p) : proc (p) { startTimerHz (20); hist.fill (0.f); }
    void timerCallback() override
    {
        hist[(size_t) pos] = jmax (proc.levelL.load(), proc.levelR.load()); pos = (pos + 1) % (int) hist.size(); repaint();
    }
    void paint (Graphics& g) override
    {
        g.setColour (dim()); g.setFont (font (13.5f));
        g.drawText ("CPU: " + String (proc.cpuLoad.load() * 100.f, 1) + "%", 0, 6, 120, 20, Justification::centredLeft, false);
        g.drawText ("Voices: " + String (proc.voiceCount.load()), 0, 26, 120, 20, Justification::centredLeft, false);
        const int n = (int) hist.size(); const float bw = 210.f / n;
        for (int i = 0; i < n; ++i)
        {
            const float v = hist[(size_t) ((pos + i) % n)];
            const float nrm = jlimit (0.f, 1.f, (Decibels::gainToDecibels (v, -60.f) + 60.f) / 60.f);
            const float h = 4.f + nrm * 34.f;
            g.setColour (Colours::white.withAlpha (0.35f + 0.6f * nrm));
            g.fillRect (130.f + i * bw, 44.f - h, bw - 2.f, h);
        }
    }
    VantaProcessor& proc; std::array<float, 40> hist; int pos = 0;
};

class PresetBar : public Component
{
public:
    PresetBar (VantaEditor& e, VantaProcessor& p) : ed (e), proc (p)
    {
        for (auto* b : { &prev, &next, &star, &folder }) addAndMakeVisible (b);
        prev.onClick = [this] { proc.presets.loadNext (-1); repaint(); };
        next.onClick = [this] { proc.presets.loadNext (1); repaint(); };
        star.onClick = [this] { toggleFav(); };
        folder.onClick = [this] { menu(); };
    }
    void resized() override
    {
        const int h = getHeight();
        prev.setBounds (6, 0, h, h); next.setBounds (6 + h, 0, h, h);
        folder.setBounds (getWidth() - h - 4, 0, h, h); star.setBounds (getWidth() - 2 * h - 8, 0, h, h);
    }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Colour (0xff060607)); g.fillRoundedRectangle (r, 8.f);
        g.setColour (border()); g.drawRoundedRectangle (r, 8.f, 1.f);
        const auto& pm = proc.presets;
        String t = pm.getCurrentName(); if (t.isEmpty()) t = pm.isEmpty() ? "Empty library - save or import a preset" : "Init";
        g.setColour (pm.getCurrentName().isEmpty() && pm.isEmpty() ? dim() : Colours::white); g.setFont (font (pm.isEmpty() ? 14.f : 18.f));
        g.drawText (t, getLocalBounds().withTrimmedLeft (getHeight() * 2 + 12).withTrimmedRight (getHeight() * 2 + 12), Justification::centred, false);
        star.on = false;
        for (const auto& p : pm.getAll()) if (p.name == pm.getCurrentName()) star.on = p.favorite;
    }
    void mouseDown (const MouseEvent& e) override { if (e.x > getHeight() * 2 + 10 && e.x < getWidth() - getHeight() * 2 - 10) menu(); }

private:
    void toggleFav()
    {
        for (const auto& p : proc.presets.getAll())
            if (p.name == proc.presets.getCurrentName()) { proc.presets.setFavorite (p, ! p.favorite); break; }
        repaint(); star.repaint();
    }
    void menu()
    {
        auto& pm = proc.presets; pm.scan();
        PopupMenu m; m.addItem (1, "Save preset...");
        m.addItem (2, "Import preset file...");
        m.addItem (3, "Open user library folder");
        m.addItem (4, pm.isB() ? "A/B: switch to A" : "A/B: store as A, switch to B");
        m.addItem (5, "Undo"); m.addItem (6, "Redo");
        m.addSeparator();
        const auto all = pm.getAll(); const auto cats = pm.getCategories();
        if (all.empty()) m.addItem (-1, "(no presets yet)", false, false);
        int id = 100;
        for (const auto& c : cats)
        {
            PopupMenu sub;
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i].category == c) sub.addItem (id + (int) i, all[i].name, true, all[i].name == pm.getCurrentName());
            m.addSubMenu (c, sub);
        }
        for (size_t i = 0; i < all.size(); ++i) if (all[i].category.isEmpty()) m.addItem (id + (int) i, all[i].name, true, all[i].name == pm.getCurrentName());
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this, all] (int r)
        {
            auto& pm2 = proc.presets;
            if (r == 1) saveDialog();
            else if (r == 2)
            {
                ed.chooser = std::make_unique<FileChooser> ("Import preset", File(), "*.vxp");
                ed.chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                                         [this] (const FileChooser& fc) { for (auto& f : fc.getResults()) proc.presets.importPreset (f); repaint(); });
            }
            else if (r == 3) pm2.getRootFolder().revealToUser();
            else if (r == 4) { pm2.toggleAB(); repaint(); }
            else if (r == 5) proc.undoManager.undo();
            else if (r == 6) proc.undoManager.redo();
            else if (r >= 100 && (size_t) (r - 100) < all.size()) { pm2.load (all[(size_t) (r - 100)].file); repaint(); }
        });
    }
    void saveDialog()
    {
        auto* w = new AlertWindow ("Save preset", "Name your preset (saved in your user library):", MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", proc.presets.getCurrentName(), "Name:");
        w->addTextEditor ("cat", "", "Category (optional):");
        w->addButton ("Save", 1, KeyPress (KeyPress::returnKey)); w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int r)
        { if (r == 1) { proc.presets.save (w->getTextEditorContents ("name"), w->getTextEditorContents ("cat").trim()); repaint(); } }), true);
    }
    VantaEditor& ed; VantaProcessor& proc;
    IconButton prev { IconButton::Left }, next { IconButton::Right }, star { IconButton::Star }, folder { IconButton::Folder };
};

// ------------------------------------------------------------------ number formatters
static String f2 (double v) { return String (v, 2); }
static String fInt (double v) { return String ((int) std::round (v)); }
static String fHz (double v) { return v >= 1000.0 ? String (v / 1000.0, 2) + " kHz" : String (v, 0) + " Hz"; }
static String fTime (double s) { return s < 0.1 ? String (s * 1000.0, 2) + " ms" : String (s, 2) + " s"; }
static String fDb (double v) { return String (v, 1) + " dB"; }
static String fMs (double v) { return String (v, v < 10.0 ? 1 : 0) + " ms"; }
static String fDens (double v) { return String (v, v < 10.0 ? 2 : 1) + " Hz"; }
static String fSt (double v) { return (v > 0.005 ? "+" : String()) + String (v, 2) + " st"; }
static String fStI (double v) { const int i = (int) std::round (v); return (i > 0 ? "+" : String()) + String (i) + " st"; }
static String fSpeed (double v) { return String (v, 2) + "x"; }
static String fRate (double v) { return String (v, v < 10.0 ? 2 : 1) + " Hz"; }
static String fCt (double v) { return String (v, 0) + " ct"; }
static String fBal (double v) { return v < -0.005 ? "odd " + String (-v, 2) : (v > 0.005 ? "even " + String (v, 2) : String ("both")); }

// ------------------------------------------------------------------ editor
VantaEditor::VantaEditor (VantaProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);
    setResizable (true, true);
    setResizeLimits (728, 408, 2912, 1632);
    getConstrainer()->setFixedAspectRatio ((double) kW / kH);
    buildHeader();
    buildVA();
    buildFilterPanel();
    buildGranular();
    buildAdditive();
    buildMod();
    buildMatrix();
    // engine selector <-> "engine" parameter (presets / automation switch the page, clicking switches the engine)
    engineAtt = std::make_unique<ParameterAttachment> (*processor.apvts.getParameter ("engine"), [this] (float v)
    {
        const int e = (int) std::lround (v);
        engineSel->selected = e <= 2 ? 2 : jlimit (3, 4, e);       // Wavetable / FM are not implemented yet: the DSP falls back to Virtual Analog
        updateVisibility(); repaint();
    }, nullptr);
    engineSel->onChange = [this] (int i) { engineAtt->setValueAsCompleteGesture ((float) i); };
    setSize (1164, (int) (1164 * kH / kW));
    engineAtt->sendInitialUpdate();
    updateVisibility();
}

VantaEditor::~VantaEditor() { setLookAndFeel (nullptr); }

void VantaEditor::buildHeader()
{
    mk<PresetBar> (kNone, 425, 16, 490, 46, *this, processor);
    mk<HeaderStats> (kNone, 945, 12, 350, 52, processor);
    auto* st = mk<IconButton> (kNone, 1322, 16, 46, 46, IconButton::Settings);
    st->onClick = [this]
    {
        PopupMenu m; m.addItem (1, "Panic: all notes off"); m.addItem (2, "Open user library folder");
        m.showMenuAsync (PopupMenu::Options(), [this] (int r) { if (r == 1) processor.panic(); else if (r == 2) processor.presets.getRootFolder().revealToUser(); });
    };
    auto* pw = mk<IconButton> (kNone, 1390, 16, 46, 46, IconButton::Power);
    pw->onClick = [this] { processor.panic(); };

    nav = mk<NavTabs> (kNone, 12, 80, 1428, 42);
    nav->onChange = [this] (int i) { page = i; updateVisibility(); repaint(); };
    engineSel = mk<EngineSelect> (kShared, 12, 136, 185, 236);
    engineSel->implemented[3] = engineSel->implemented[4] = true;

    keyboard = mk<MidiKeyboardComponent> (kNone, 408, 667, 668, 132, processor.keyboardState, MidiKeyboardComponent::horizontalKeyboard);
    keyboard->setAvailableRange (36, 96); keyboard->setLowestVisibleKey (36); keyboard->setScrollButtonsVisible (false);
    keyboard->setKeyWidth (668.f / 36.f); keyboard->setMidiChannel (1);
    keyboard->setColour (MidiKeyboardComponent::whiteNoteColourId, Colour (0xffe9e9ea));
    keyboard->setColour (MidiKeyboardComponent::blackNoteColourId, Colour (0xff060607));
    keyboard->setColour (MidiKeyboardComponent::keySeparatorLineColourId, Colour (0xff222226));
    keyboard->setColour (MidiKeyboardComponent::mouseOverKeyOverlayColourId, Colours::white.withAlpha (0.12f));
    keyboard->setColour (MidiKeyboardComponent::keyDownOverlayColourId, Colour (0x889fe8e0));
    keyboard->setColour (MidiKeyboardComponent::shadowColourId, Colours::transparentBlack);
    keyboard->setColour (MidiKeyboardComponent::upDownButtonBackgroundColourId, Colours::transparentBlack);
}

void VantaEditor::buildVA()
{
    auto& A = processor.apvts;
    auto P = [&] (float x, float y, float w, float h, const String& t) { panels[kVA].push_back ({ { x, y, w, h }, t }); };
    auto knob = [&] (float cx, float top, const char* id, const char* name, std::function<String (double)> f, float w = 78.f, float h = 100.f)
    { mk<VKnob> (kVA, cx - w * 0.5f, top, w, h, A, id, name, f); };

    // ---- engine panel frames (positions taken from reference 03 – Virtual Analog)
    panel (kShared, 12, 136, 185, 236, "ENGINE SELECT");
    P (12, 376, 185, 80, "UNISON");
    P (212, 136, 568, 304, "VIRTUAL ANALOG ENGINE");
    P (796, 136, 292, 304, "OSC MIXER");
    P (1102, 136, 338, 304, "FILTER");
    P (12, 458, 270, 188, "SUB OSC");
    P (292, 458, 270, 188, "PULSE WIDTH");
    P (572, 458, 224, 188, "NOISE");
    P (806, 458, 204, 188, "PORTAMENTO");
    P (1020, 458, 420, 188, "AMP ENVELOPE");
    P (12, 654, 384, 150, "MODULATION");
    panel (kShared, 1084, 654, 130, 150, "");
    panel (kShared, 1224, 654, 216, 150, "MASTER");

    // ---- unison
    mk<VStepper> (kVA, 22, 400, 70, 22, A, "uni_voices");
    { auto* k = mk<VKnob> (kVA, 98, 392, 44, 62, A, "uni_detune", "Detune", f2, 62.f, 0.5f); (void) k; }
    { auto* k = mk<VKnob> (kVA, 146, 392, 44, 62, A, "uni_spread", "Spread", f2, 62.f, 0.5f); (void) k; }

    // ---- main oscillator
    mk<VCombo> (kVA, 640, 146, 130, 26, A, "va_wave");
    mk<FuncDisplay> (kVA, 222, 184, 553, 122, [this] (Graphics& g, Rectangle<float> r)
    {
        const int wave = (int) aval ("va_wave"); const float pw = aval ("va_pw");
        Path p; const int N = (int) r.getWidth();
        for (int i = 0; i <= N; ++i)
        {
            const float t = i / (float) N * 3.f; const float ph = t - std::floor (t);
            const float y = vanta::oscSample (wave, ph, 0.f, pw);
            const float px = r.getX() + i, py = r.getCentreY() - y * r.getHeight() * 0.42f;
            if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        glowStroke (g, p, 1.5f);
    });
    knob (265, 336, "va_oct", "Octave", fInt);
    knob (360, 336, "va_semi", "Semitone", fInt);
    knob (455, 336, "va_fine", "Fine", f2);
    knob (550, 336, "va_pw", "PW", f2);
    mk<VCombo> (kVA, 608, 366, 68, 28, A, "va_sync");
    { auto* l = mk<Label> (kVA, 600, 330, 84, 22); l->setText ("Sync", dontSendNotification); l->setJustificationType (Justification::centred); l->setColour (Label::textColourId, white()); l->setFont (font (13.5f)); }
    knob (736, 336, "va_drift", "Drift", f2);

    // ---- mixer
    const float mx[3] { 841, 908, 976 };
    const char* mids[3] { "va_mix1", "va_mix2", "va_mix3" }; const char* mn[3] { "Osc 1", "Osc 2", "Osc 3" };
    for (int i = 0; i < 3; ++i) mk<VFader> (kVA, mx[i] - 18, 180, 36, 250, A, mids[i], mn[i]);
    mk<VFader> (kVA, 1043 - 18, 180, 36, 250, A, "noise_level", "Noise");

    // ---- filter
    mk<VCombo> (kVA, 1304, 146, 124, 26, A, "flt_type");
    mk<FuncDisplay> (kVA, 1112, 184, 320, 126, [this] (Graphics& g, Rectangle<float> r)
    {
        const int type = (int) aval ("flt_type"); const float fc = aval ("flt_cutoff"), k = aval ("flt_res") * 3.9f;
        g.setColour (Colours::white.withAlpha (0.07f));
        for (int i = 1; i < 4; ++i) g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * i / 4), r.getX(), r.getRight());
        for (float f : { 100.f, 1000.f, 10000.f }) { const float x = r.getX() + r.getWidth() * std::log10 (f / 20.f) / 3.f; g.drawVerticalLine ((int) x, r.getY(), r.getBottom()); }
        Path line, fill; const int N = (int) r.getWidth();
        for (int i = 0; i <= N; ++i)
        {
            const float f = 20.f * std::pow (1000.f, i / (float) N);
            const std::complex<float> a = 1.f / std::complex<float> (1.f, f / fc), a2 = a * a, a4 = a2 * a2, den = 1.f + k * a4;
            std::complex<float> num;
            switch (type) { case 1: num = a2; break; case 2: num = 2.f * (a2 - a4); break; case 3: { auto o = 1.f - a; num = o * o * o * o; break; } default: num = a4; }
            const float db = 20.f * std::log10 (std::max (1e-5f, std::abs (num / den)));
            const float y = jmap (jlimit (-42.f, 18.f, db), 18.f, -42.f, r.getY(), r.getBottom());
            const float x = r.getX() + i;
            if (i == 0) { line.startNewSubPath (x, y); fill.startNewSubPath (x, r.getBottom()); fill.lineTo (x, y); } else { line.lineTo (x, y); fill.lineTo (x, y); }
        }
        fill.lineTo (r.getRight(), r.getBottom()); fill.closeSubPath();
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.16f), 0, r.getY(), Colours::transparentWhite, 0, r.getBottom(), false)); g.fillPath (fill);
        glowStroke (g, line, 1.5f);
    });
    knob (1163, 352, "flt_cutoff", "Cutoff", fHz, 90.f, 96.f);
    knob (1276, 352, "flt_res", "Resonance", f2, 90.f, 96.f);
    knob (1385, 352, "flt_drive", "Drive", f2, 90.f, 96.f);

    // ---- sub osc
    mk<FuncDisplay> (kVA, 24, 490, 116, 54, [this] (Graphics& g, Rectangle<float> r)
    {
        const int w = (int) aval ("sub_wave"); const int wv = w == 0 ? vanta::Sine : (w == 1 ? vanta::Square : vanta::Triangle);
        Path p; for (int i = 0; i <= (int) r.getWidth(); ++i) { const float t = i / r.getWidth() * 2.f; const float y = vanta::oscSample (wv, t - std::floor (t), 0.f, 0.5f);
            const float px = r.getX() + i, py = r.getCentreY() - y * r.getHeight() * 0.4f; if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py); }
        glowStroke (g, p, 1.3f);
    });
    mk<VCombo> (kVA, 152, 494, 118, 26, A, "sub_wave");
    knob (77, 552, "sub_level", "Level", f2);
    knob (190, 552, "sub_oct", "Octave", fInt);

    // ---- pulse width
    mk<FuncDisplay> (kVA, 304, 490, 246, 62, [this] (Graphics& g, Rectangle<float> r)
    {
        const float pw = aval ("va_pw"); Path p; const float hgt = r.getHeight() * 0.38f;
        for (int i = 0; i <= (int) r.getWidth(); ++i) { const float t = i / r.getWidth() * 3.f; const float y = (t - std::floor (t)) < pw ? 1.f : -1.f;
            const float px = r.getX() + i, py = r.getCentreY() - y * hgt; if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py); }
        glowStroke (g, p, 1.3f);
    });
    knob (428, 556, "va_pw", "Amount", f2, 90.f, 90.f);

    // ---- noise
    mk<VCombo> (kVA, 582, 488, 206, 26, A, "noise_type");
    mk<FuncDisplay> (kVA, 582, 520, 206, 42, [this] (Graphics& g, Rectangle<float> r)
    {
        const float col = aval ("noise_color"); Random rnd (7); float lp = 0.f; const float k = 0.1f + 0.9f * col;
        g.setColour (Colours::white.withAlpha (0.8f));
        for (int i = 0; i < (int) r.getWidth(); i += 2) { lp += (rnd.nextFloat() * 2.f - 1.f - lp) * k; const float h = std::fabs (lp) * r.getHeight() * 0.45f;
            g.drawLine (r.getX() + i, r.getCentreY() - h, r.getX() + i, r.getCentreY() + h, 1.2f); }
    });
    knob (640, 568, "noise_level", "Level", f2, 70.f, 84.f);
    knob (734, 568, "noise_color", "Color", f2, 70.f, 84.f);

    // ---- portamento
    mk<FuncDisplay> (kVA, 816, 490, 184, 62, [this] (Graphics& g, Rectangle<float> r)
    {
        const float c = aval ("porta_curve"); Path p; const int N = (int) r.getWidth();
        for (int i = 0; i <= N; ++i) { const float t = i / (float) N; const float prog = jmin (1.f, t / 0.7f);
            const float y = (1.f - c) * prog + c * (1.f - std::exp (-5.f * prog)) / (1.f - std::exp (-5.f));
            const float px = r.getX() + i, py = r.getBottom() - 4 - y * (r.getHeight() - 10); if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py); }
        glowStroke (g, p, 1.3f);
    });
    knob (860, 556, "porta_time", "Time", fTime, 80.f, 90.f);
    knob (956, 556, "porta_curve", "Curve", f2, 80.f, 90.f);

    // ---- amp envelope
    mk<FuncDisplay> (kVA, 1030, 488, 402, 78, [this] (Graphics& g, Rectangle<float> r)
    {
        auto nrm = [] (float s) { return jlimit (0.f, 1.f, (std::log10 (std::max (0.0005f, s) * 1000.f) + 0.3f) / 4.3f); };
        const float a = 0.08f + 0.25f * nrm (aval ("amp_a")), d = 0.08f + 0.25f * nrm (aval ("amp_d")), s = aval ("amp_s"), rl = 0.08f + 0.25f * nrm (aval ("amp_r")), hold = 0.18f;
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
    });
    knob (1088, 570, "amp_a", "Attack", fTime, 80.f, 82.f);
    knob (1186, 570, "amp_d", "Decay", fTime, 80.f, 82.f);
    knob (1284, 570, "amp_s", "Sustain", f2, 80.f, 82.f);
    knob (1384, 570, "amp_r", "Release", fTime, 80.f, 82.f);

    // ---- modulation placeholder (system arrives in Stage 2) + wheel + master
    {
        auto* wheel = mk<FuncDisplay> (kShared, 1094, 690, 110, 108, [this] (Graphics& g, Rectangle<float> r)
        {
            g.setColour (dim()); g.setFont (font (r.getHeight() * 0.11f)); g.drawText ("PITCH", r.withHeight (r.getHeight() * 0.15f), Justification::centred, false);
            const float top = r.getY() + r.getHeight() * 0.2f, bot = r.getBottom() - r.getHeight() * 0.04f, th = r.getHeight() * 0.12f;
            g.setColour (Colour (0xff1b1b1e)); g.fillRoundedRectangle (r.getCentreX() - r.getWidth() * 0.13f, top, r.getWidth() * 0.26f, bot - top, 5.f);
            const float cy = (top + bot) * 0.5f - processor.uiBend.load() * ((bot - top) * 0.5f - th * 0.5f);
            g.setColour (white()); g.fillRoundedRectangle (r.getCentreX() - r.getWidth() * 0.13f, cy - th * 0.5f, r.getWidth() * 0.26f, th, 3.f);
        });
        wheel->setMouseCursor (MouseCursor::UpDownResizeCursor);
        struct Wheel : MouseListener
        {
            VantaProcessor& p; Component& c; Wheel (VantaProcessor& pp, Component& cc) : p (pp), c (cc) {}
            void set (const MouseEvent& e)
            {
                const float top = c.getHeight() * 0.2f, bot = c.getHeight() * 0.96f, th = c.getHeight() * 0.12f;
                const float n = (e.position.y - (top + th * 0.5f)) / ((bot - top) - th);          // 0 top .. 1 bottom
                p.uiBend.store (jlimit (-1.f, 1.f, 1.f - 2.f * n));
            }
            void mouseDown (const MouseEvent& e) override { set (e); }
            void mouseDrag (const MouseEvent& e) override { set (e); }
            void mouseUp (const MouseEvent&) override { p.uiBend.store (0.f); }
        };
        struct Holder : Component { std::unique_ptr<Wheel> w; };
        auto h = std::make_unique<Holder>(); h->w = std::make_unique<Wheel> (processor, *wheel);
        wheel->addMouseListener (h->w.get(), false); owned.push_back (std::move (h));
    }
    mk<VKnob> (kShared, 1332 - 50.f, 668, 100.f, 112.f, A, "master_vol", "Volume", fDb);
    mk<LevelMeter> (kShared, 1400, 690, 34, 108, processor.levelL, processor.levelR);
}

// ------------------------------------------------------------------ shared Filter panel (Granular + Additive voices use the same ladder filter + amp envelope as VA)
void VantaEditor::buildFilterPanel()
{
    auto& A = processor.apvts;
    panel (kGA, 12, 654, 384, 150, "FILTER");
    mk<VCombo> (kGA, 268, 662, 116, 24, A, "flt_type");
    mk<FuncDisplay> (kGA, 22, 692, 150, 104, [this] (Graphics& g, Rectangle<float> r)
    { drawFilterResponse (g, r, (int) aval ("flt_type"), aval ("flt_cutoff"), aval ("flt_res")); });
    const float cx[3] { 212, 284, 356 }; const char* ids[3] { "flt_cutoff", "flt_res", "flt_drive" }; const char* nm[3] { "Cutoff", "Res", "Drive" };
    for (int i = 0; i < 3; ++i)
        mk<VKnob> (kGA, cx[i] - 36.f, 694, 72, 102, A, ids[i], nm[i], i == 0 ? std::function<String (double)> (fHz) : std::function<String (double)> (f2));
}

// ------------------------------------------------------------------ GRANULAR page
void VantaEditor::buildGranular()
{
    auto& A = processor.apvts;
    auto knob = [&] (float cx, float top, const char* id, const char* name, std::function<String (double)> f, float w = 110.f, float h = 100.f)
    { mk<VKnob> (kGran, cx - w * 0.5f, top, w, h, A, id, name, f); };
    auto cap = [&] (float x, float y, float w, float h, const String& t, Justification j = Justification::centredLeft)
    { mk<Caption> (kGran, x, y, w, h, t, j, true); };

    panel (kGran, 212, 136, 836, 304, "GRANULAR ENGINE");
    panel (kGran, 1062, 136, 378, 304, "AMP ENVELOPE");
    panel (kGran, 12, 376, 185, 80, "ROOT NOTE");
    panel (kGran, 12, 458, 360, 188, "REGION & PLAYBACK");
    panel (kGran, 384, 458, 380, 188, "GRAIN CONTROLS");
    panel (kGran, 776, 458, 300, 188, "GRAIN SHAPE");
    panel (kGran, 1088, 458, 352, 188, "VOICE");

    // root note
    mk<VStepper> (kGran, 22, 404, 90, 24, A, "gr_root");
    mk<LiveText> (kGran, 118, 404, 70, 24, [this] { return MidiMessage::getMidiNoteName (jlimit (0, 127, (int) aval ("gr_root")), true, true, 3); }, Justification::centredLeft, false);
    cap (22, 430, 165, 18, "MIDI note played at original pitch");

    // sample + display + cloud
    cap (540, 144, 64, 28, "Sample", Justification::centredRight);
    mk<SampleSelector> (kGran, 610, 144, 400, 28, processor);
    mk<SampleView> (kGran, 222, 182, 596, 132, processor);
    mk<GrainCloud> (kGran, 826, 182, 212, 132, processor);
    const float cxs[6] { 290, 426, 562, 698, 834, 970 };
    knob (cxs[0], 326, "gr_size", "Grain Size", fMs, 120, 108);
    knob (cxs[1], 326, "gr_density", "Density", fDens, 120, 108);
    knob (cxs[2], 326, "gr_pos", "Position", f2, 120, 108);
    knob (cxs[3], 326, "gr_spray", "Pos Random", f2, 120, 108);
    knob (cxs[4], 326, "gr_pitch", "Pitch", fSt, 120, 108);
    knob (cxs[5], 326, "gr_spread", "Stereo Spread", f2, 120, 108);

    // amp envelope
    mk<FuncDisplay> (kGran, 1072, 180, 358, 118, [this] (Graphics& g, Rectangle<float> r)
    { drawAmpEnv (g, r, aval ("amp_a"), aval ("amp_d"), aval ("amp_s"), aval ("amp_r")); });
    const float ex[4] { 1117, 1206, 1296, 1385 }; const char* eid[4] { "amp_a", "amp_d", "amp_s", "amp_r" }; const char* en[4] { "Attack", "Decay", "Sustain", "Release" };
    for (int i = 0; i < 4; ++i) knob (ex[i], 328, eid[i], en[i], i == 2 ? std::function<String (double)> (f2) : std::function<String (double)> (fTime), 84, 106);

    // region & playback
    knob (72, 490, "gr_start", "Start", f2, 110, 100);
    knob (192, 490, "gr_end", "End", f2, 110, 100);
    knob (312, 490, "gr_speed", "Scan Speed", fSpeed, 110, 100);
    cap (24, 602, 84, 28, "Direction");
    mk<VCombo> (kGran, 108, 602, 150, 28, A, "gr_dir");

    // grain controls
    knob (447, 490, "gr_sizerand", "Size Random", f2, 120, 100);
    knob (574, 490, "gr_pitchrand", "Pitch Random", f2, 120, 100);
    knob (700, 490, "gr_texture", "Texture", f2, 120, 100);
    mk<LiveText> (kGran, 396, 608, 356, 24, [this] { return "Active grains: " + String (processor.grainCount.load()) + "   Voices: " + String (processor.voiceCount.load()); }, Justification::centred);

    // grain shape
    mk<VCombo> (kGran, 936, 464, 132, 24, A, "gr_window");
    mk<FuncDisplay> (kGran, 788, 496, 276, 66, [this] (Graphics& g, Rectangle<float> r)
    {
        const int type = (int) aval ("gr_window"); const float w = aval ("gr_width"), sh = aval ("gr_shape");
        Path line, fill; const int N = jmax (2, (int) r.getWidth());
        for (int i = 0; i <= N; ++i)
        {
            const float v = vanta::grainwin::value (type, (float) i / (float) N, w, sh);
            const float x = r.getX() + r.getWidth() * (float) i / (float) N, y = r.getBottom() - 4.f - v * (r.getHeight() - 10.f);
            if (i == 0) { line.startNewSubPath (x, y); fill.startNewSubPath (x, r.getBottom()); fill.lineTo (x, y); } else { line.lineTo (x, y); fill.lineTo (x, y); }
        }
        fill.lineTo (r.getRight(), r.getBottom()); fill.closeSubPath();
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.16f), 0, r.getY(), Colours::transparentWhite, 0, r.getBottom(), false)); g.fillPath (fill);
        glowStroke (g, line, 1.4f);
    });
    knob (862, 566, "gr_width", "Width", f2, 100, 78);
    knob (990, 566, "gr_shape", "Shape", f2, 100, 78);

    // voice
    cap (1100, 494, 110, 24, "Polyphony");
    mk<VStepper> (kGran, 1214, 494, 90, 24, A, "poly");
    cap (1100, 528, 110, 24, "Voice mode");
    mk<VCombo> (kGran, 1214, 526, 110, 28, A, "voice_mode");
    cap (1100, 562, 110, 24, "Bend range");
    mk<VStepper> (kGran, 1214, 562, 90, 24, A, "bend_range");
    knob (1385, 490, "porta_time", "Glide", fTime, 86, 100);
}

// ------------------------------------------------------------------ ADDITIVE page
void VantaEditor::buildAdditive()
{
    auto& A = processor.apvts;
    auto knob = [&] (float cx, float top, const char* id, const char* name, std::function<String (double)> f, float w = 110.f, float h = 100.f)
    { mk<VKnob> (kAdd, cx - w * 0.5f, top, w, h, A, id, name, f); };
    auto cap = [&] (float x, float y, float w, float h, const String& t, Justification j = Justification::centredLeft)
    { mk<Caption> (kAdd, x, y, w, h, t, j, true); };

    panel (kAdd, 212, 136, 712, 304, "ADDITIVE ENGINE");
    panel (kAdd, 938, 136, 318, 304, "HARMONIC EDITOR");
    panel (kAdd, 1270, 136, 170, 304, "TONE SHAPE");
    panel (kAdd, 12, 376, 185, 80, "HARMONICS");
    panel (kAdd, 12, 458, 330, 188, "AMP ENVELOPE");
    panel (kAdd, 352, 458, 300, 188, "SPECTRAL FILTER");
    panel (kAdd, 662, 458, 300, 188, "OUTPUT SPECTRUM");
    panel (kAdd, 972, 458, 468, 188, "ADDITIVE CONTROLS");

    mk<VStepper> (kAdd, 60, 404, 80, 24, A, "add_count");
    cap (22, 430, 165, 18, "active harmonics (1-32)", Justification::centred);

    // spectrum editor (UI view mode: Amplitude / Phase / Output)
    cap (470, 144, 90, 26, "Morph to", Justification::centredRight);
    mk<VCombo> (kAdd, 564, 144, 118, 26, A, "add_target");
    auto* view = mk<ComboBox> (kAdd, 700, 144, 150, 26);
    view->addItemList ({ "Edit amplitude", "Edit phase", "Output spectrum" }, 1);
    view->setSelectedId (1, dontSendNotification);
    view->onChange = [this, view] { harm.mode = jlimit (0, 2, view->getSelectedId() - 1); harm.changed(); };
    mk<HarmonicSpectrum> (kAdd, 222, 180, 692, 134, processor, harm);
    const float cxs[5] { 291, 430, 568, 707, 845 };
    knob (cxs[0], 326, "add_level", "Harmonic Level", f2, 124, 108);
    knob (cxs[1], 326, "add_shift", "Harmonic Shift", f2, 124, 108);
    knob (cxs[2], 326, "add_morph", "Spectral Morph", f2, 124, 108);
    knob (cxs[3], 326, "add_tone", "Tone", f2, 124, 108);
    knob (cxs[4], 326, "add_inharm", "Inharmonicity", f2, 124, 108);

    // per-harmonic sliders
    mk<HarmonicBank> (kAdd, 946, 168, 302, 266, processor, harm);

    // tone shaping
    knob (1313, 166, "add_balance", "Odd/Even", fBal, 82, 126);
    knob (1397, 166, "add_rolloff", "Rolloff", f2, 82, 126);
    knob (1313, 300, "add_semi", "Fundamental", fStI, 82, 126);
    knob (1397, 300, "add_fine", "Fine", fCt, 82, 126);

    // amp envelope
    mk<FuncDisplay> (kAdd, 22, 490, 310, 64, [this] (Graphics& g, Rectangle<float> r)
    { drawAmpEnv (g, r, aval ("amp_a"), aval ("amp_d"), aval ("amp_s"), aval ("amp_r")); });
    const float ex[4] { 53, 136, 218, 301 }; const char* eid[4] { "amp_a", "amp_d", "amp_s", "amp_r" }; const char* en[4] { "Attack", "Decay", "Sustain", "Release" };
    for (int i = 0; i < 4; ++i) knob (ex[i], 556, eid[i], en[i], i == 2 ? std::function<String (double)> (f2) : std::function<String (double)> (fTime), 80, 88);

    // spectral filter
    mk<VSwitch> (kAdd, 364, 484, 130, 22, A, "add_sf_on", "Enable");
    mk<FuncDisplay> (kAdd, 362, 512, 280, 52, [this] (Graphics& g, Rectangle<float> r)
    {
        const bool on = aval ("add_sf_on") > 0.5f; const float pos = aval ("add_sf_pos"), wd = aval ("add_sf_width"), gn = aval ("add_sf_gain");
        g.setColour (Colours::white.withAlpha (0.07f)); g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * 0.5f), r.getX(), r.getRight());
        Path line; const int N = jmax (2, (int) r.getWidth());
        for (int i = 0; i <= N; ++i)
        {
            const float h = std::pow (32.f, (float) i / (float) N);                 // harmonics 1..32, log axis
            const float gain = on ? vanta::additive::spectralFilterGain (pos, wd, gn, jlimit (1, 32, (int) std::lround (h))) : 1.f;
            const float db = jlimit (-18.f, 12.f, 20.f * std::log10 (jmax (1e-3f, gain)));
            const float x = r.getX() + r.getWidth() * (float) i / (float) N, y = jmap (db, 12.f, -18.f, r.getY() + 3.f, r.getBottom() - 3.f);
            if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
        }
        if (on) glowStroke (g, line, 1.3f); else { g.setColour (dim()); g.strokePath (line, PathStrokeType (1.2f)); }
    });
    knob (402, 568, "add_sf_pos", "Position", f2, 90, 76);
    knob (502, 568, "add_sf_width", "Width", f2, 90, 76);
    knob (602, 568, "add_sf_gain", "Gain", f2, 90, 76);

    // output spectrum (the real, shaped harmonic levels that the oscillators receive; dB scale)
    mk<FuncDisplay> (kAdd, 672, 490, 280, 146, [this] (Graphics& g, Rectangle<float> r)
    {
        vanta::AdditiveParams ap; processor.loadAdditiveParams (ap);
        float peak = 1e-6f; for (int k = 0; k < vanta::kAddHarm; ++k) peak = jmax (peak, ap.shaped[k]);
        g.setColour (Colours::white.withAlpha (0.07f));
        for (int i = 1; i < 4; ++i) g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * i / 4), r.getX(), r.getRight());
        const float slot = r.getWidth() / (float) vanta::kAddHarm;
        for (int k = 0; k < vanta::kAddHarm; ++k)
        {
            const float db = Decibels::gainToDecibels (ap.shaped[k] / peak, -60.f);
            const float v = jlimit (0.f, 1.f, (db + 60.f) / 60.f) * (r.getHeight() - 4.f);
            g.setColour (Colours::white.withAlpha (0.35f + 0.6f * v / r.getHeight()));
            g.fillRect (r.getX() + (float) k * slot + slot * 0.2f, r.getBottom() - 2.f - v, slot * 0.6f, v);
        }
        g.setColour (dim()); g.setFont (font (11.f));
        g.drawText ("0 dB", Rectangle<float> (r.getX() + 3, r.getY() + 1, 40, 12), Justification::topLeft, false);
        g.drawText ("-60 dB", Rectangle<float> (r.getX() + 3, r.getBottom() - 14, 50, 12), Justification::bottomLeft, false);
    });

    // additive controls
    mk<VSwitch> (kAdd, 984, 488, 210, 24, A, "add_henv", "Harmonic Envelopes");
    mk<VSwitch> (kAdd, 984, 520, 210, 24, A, "add_uni", "Unison Voices");
    knob (1020, 552, "add_hdecay", "H. Decay", fTime, 80, 90);
    knob (1100, 552, "add_spread", "Spread", f2, 80, 90);
    knob (1180, 552, "add_detune", "Detune", f2, 80, 90);
    cap (1214, 488, 100, 24, "Polyphony");
    mk<VStepper> (kAdd, 1318, 488, 90, 24, A, "poly");
    cap (1214, 520, 100, 24, "Mode");
    mk<VCombo> (kAdd, 1318, 518, 110, 28, A, "voice_mode");
    cap (1214, 556, 50, 24, "Bend");
    mk<VStepper> (kAdd, 1264, 556, 70, 24, A, "bend_range");
    knob (1388, 552, "porta_time", "Glide", fTime, 86, 90);
    mk<LiveText> (kAdd, 1214, 606, 220, 22, [this] { return "Voices: " + String (processor.voiceCount.load()); }, Justification::centredLeft);
}

// ------------------------------------------------------------------ MOD page: LFO editor, envelope editor, step modulator, macros
void VantaEditor::buildMod()
{
    using namespace vanta::mod;
    auto& A = processor.apvts;
    auto cap = [&] (float x, float y, float w, float h, const String& t, Justification j = Justification::centredLeft)
    { mk<Caption> (kMod, x, y, w, h, t, j, true); };

    panel (kMod, 12, 136, 700, 250, "LFO");
    panel (kMod, 720, 136, 720, 250, "ENVELOPE");
    panel (kMod, 12, 394, 700, 252, "STEP MODULATOR");
    panel (kMod, 720, 394, 720, 252, "MACROS");

    // ---- LFO (8 independent LFOs, one control set each; the selector shows one set at a time)
    auto* lt = mk<TabStrip> (kMod, 452, 140, 252, 24, StringArray { "1", "2", "3", "4", "5", "6", "7", "8" });
    lt->onChange = [this] (int i) { lfoSel = i; updateVisibility(); repaint(); };
    mk<FuncDisplay> (kMod, 22, 172, 300, 118, [this] (Graphics& g, Rectangle<float> r)
    {
        const int k = lfoSel + 1; const int wave = (int) aval (lfoId (k, "wave").c_str()); const float ph = aval (lfoId (k, "phase").c_str()), depth = aval (lfoId (k, "depth").c_str());
        const bool bip = aval (lfoId (k, "bipolar").c_str()) > 0.5f;
        g.setColour (Colours::white.withAlpha (0.07f)); g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        Path line; const int N = jmax (2, (int) r.getWidth());
        for (int i = 0; i <= N; ++i)
        {
            float v = lfoShape (wave, 2.0 * i / (double) N + ph, 4242u); if (! bip) v = v * 0.5f + 0.5f; v *= depth;
            const float y = bip ? r.getCentreY() - v * (r.getHeight() * 0.45f) : r.getBottom() - 4.f - v * (r.getHeight() - 10.f);
            const float x = r.getX() + r.getWidth() * (float) i / (float) N;
            if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
        }
        glowStroke (g, line, 1.6f);
    });
    for (int k = 0; k < kLfos; ++k)
    {
        auto reg = [&] (Component* c) { lfoSet[k].push_back (c); };
        const int n = k + 1;
        reg (mk<Caption> (kNone, 332, 174, 44, 26, "Wave", Justification::centredLeft, true));
        reg (mk<VCombo> (kNone, 378, 174, 130, 26, A, lfoId (n, "wave").c_str()));
        reg (mk<Caption> (kNone, 516, 174, 56, 26, "Retrig", Justification::centredLeft, true));
        reg (mk<VCombo> (kNone, 574, 174, 88, 26, A, lfoId (n, "retrig").c_str()));
        reg (mk<Caption> (kNone, 332, 208, 44, 26, "Div", Justification::centredLeft, true));
        reg (mk<VCombo> (kNone, 378, 208, 94, 26, A, lfoId (n, "div").c_str()));
        reg (mk<VSwitch> (kNone, 486, 210, 130, 22, A, lfoId (n, "sync").c_str(), "Tempo Sync"));
        reg (mk<VSwitch> (kNone, 332, 244, 130, 22, A, lfoId (n, "bipolar").c_str(), "Bipolar"));
        const float cx[4] { 107, 277, 447, 617 };
        reg (mk<VKnob> (kNone, cx[0] - 60, 294, 120, 90, A, lfoId (n, "rate").c_str(), "Rate", fRate));
        reg (mk<VKnob> (kNone, cx[1] - 60, 294, 120, 90, A, lfoId (n, "phase").c_str(), "Phase", f2));
        reg (mk<VKnob> (kNone, cx[2] - 60, 294, 120, 90, A, lfoId (n, "fade").c_str(), "Fade-in", fTime));
        reg (mk<VKnob> (kNone, cx[3] - 60, 294, 120, 90, A, lfoId (n, "depth").c_str(), "Depth", f2));
    }

    // ---- Envelope editor (8 envelopes)
    auto* et = mk<TabStrip> (kMod, 1170, 140, 262, 24, StringArray { "1", "2", "3", "4", "5", "6", "7", "8" });
    et->onChange = [this] (int i) { envSel = i; updateVisibility(); repaint(); };
    mk<EnvEditor> (kMod, 730, 172, 420, 118, processor, envSel);
    for (int k = 0; k < kEnvs; ++k)
    {
        auto reg = [&] (Component* c) { envSet[k].push_back (c); };
        const int n = k + 1;
        reg (mk<VSwitch> (kNone, 1164, 176, 120, 22, A, envId (n, "loop").c_str(), "Loop"));
        reg (mk<VSwitch> (kNone, 1164, 208, 120, 22, A, envId (n, "bipolar").c_str(), "Bipolar"));
        reg (mk<Caption> (kNone, 1164, 240, 54, 26, "Retrig", Justification::centredLeft, true));
        reg (mk<VCombo> (kNone, 1220, 240, 130, 26, A, envId (n, "retrig").c_str()));
        const char* f[7] { "a", "h", "d", "s", "r", "curve", "vel" }; const char* nm[7] { "Attack", "Hold", "Decay", "Sustain", "Release", "Curve", "Velocity" };
        for (int i = 0; i < 7; ++i)
            reg (mk<VKnob> (kNone, 730.f + 100.f * (float) i + 2.f, 294, 96, 90, A, envId (n, f[i]).c_str(), nm[i], (i == 3 || i == 5 || i == 6) ? std::function<String (double)> (f2) : std::function<String (double)> (fTime)));
    }

    // ---- Step modulator
    mk<StepEditor> (kMod, 22, 430, 680, 108, processor);
    cap (22, 544, 50, 24, "Steps"); mk<VStepper> (kMod, 72, 544, 80, 24, A, stepId ("count").c_str());
    cap (166, 544, 34, 24, "Dir"); mk<VCombo> (kMod, 202, 544, 112, 24, A, stepId ("dir").c_str());
    cap (326, 544, 50, 24, "Retrig"); mk<VCombo> (kMod, 378, 544, 86, 24, A, stepId ("retrig").c_str());
    mk<VSwitch> (kMod, 480, 546, 100, 22, A, stepId ("sync").c_str(), "Sync");
    mk<VSwitch> (kMod, 590, 546, 112, 22, A, stepId ("bipolar").c_str(), "Bipolar");
    cap (22, 576, 40, 24, "Div"); mk<VCombo> (kMod, 62, 576, 90, 24, A, stepId ("div").c_str());
    mk<VKnob> (kMod, 190, 566, 120, 78, A, stepId ("rate").c_str(), "Rate", fRate);
    mk<VKnob> (kMod, 330, 566, 120, 78, A, stepId ("smooth").c_str(), "Smooth", f2);
    cap (470, 576, 232, 62, "Step Mod is a source in the Matrix. Right-click the bars for patterns, double-click a bar to reset.", Justification::topLeft);

    // ---- Macros (8): double-click a name to rename it; every macro is host-automatable and can drive any destinations from the Matrix
    for (int i = 0; i < kMacros; ++i)
        mk<MacroCell> (kMod, 730.f + 178.f * (float) (i % 4), 428.f + 108.f * (float) (i / 4), 170, 100, processor, i);
    cap (1000, 400, 430, 22, "double-click a name to rename   |   assign in MATRIX", Justification::centredRight);
}

// ------------------------------------------------------------------ MATRIX page: 32-slot matrix, live routing diagram, MIDI learn
void VantaEditor::buildMatrix()
{
    panel (kMat, 12, 136, 860, 510, "MODULATION MATRIX");
    panel (kMat, 884, 136, 556, 260, "ROUTING");
    panel (kMat, 884, 404, 556, 242, "MIDI LEARN");
    auto* tabs = mk<TabStrip> (kMat, 700, 140, 160, 24, StringArray { "1-16", "17-32" });
    tabs->onChange = [this] (int i) { matPage = i; updateVisibility(); repaint(); };
    mk<Caption> (kMat, 36, 168, 56, 18, "ON", Justification::centred, true);
    mk<Caption> (kMat, 94, 168, 190, 18, "SOURCE", Justification::centred, true);
    mk<Caption> (kMat, 292, 168, 230, 18, "DESTINATION", Justification::centred, true);
    mk<Caption> (kMat, 552, 168, 300, 18, "AMOUNT  (-1 .. +1, shift = fine, double-click = 0)", Justification::centredLeft, true);
    for (int i = 0; i < vanta::mod::kSlots; ++i)
        mk<MatrixRow> (i < 16 ? (int) kMatA : (int) kMatB, 22, 190.f + 28.f * (float) (i % 16), 840, 28, processor.apvts, i + 1);
    mk<RoutingView> (kMat, 894, 170, 536, 218, processor);
    mk<MidiLearnPanel> (kMat, 894, 438, 536, 200, processor);
}

void VantaEditor::updateVisibility()
{
    const bool osc = (page == 1); const int eng = engineSel != nullptr ? engineSel->selected : 2;
    auto setAll = [] (std::vector<Component*>& v, bool b) { for (auto* c : v) c->setVisible (b); };
    setAll (groups[kShared], osc);
    setAll (groups[kVA],   osc && eng == 2);
    setAll (groups[kGran], osc && eng == 3);
    setAll (groups[kAdd],  osc && eng == 4);
    setAll (groups[kGA],   osc && (eng == 3 || eng == 4));
    setAll (groups[kMod],  page == 2);
    for (int k = 0; k < 8; ++k) { setAll (lfoSet[k], page == 2 && lfoSel == k); setAll (envSet[k], page == 2 && envSel == k); }
    setAll (groups[kMat],  page == 5);
    setAll (groups[kMatA], page == 5 && matPage == 0);
    setAll (groups[kMatB], page == 5 && matPage == 1);
    keyboard->setVisible (true);
}

void VantaEditor::resized()
{
    scale = getWidth() / kW;
    for (auto& p : layout)
        p.c->setBounds (Rectangle<float> (p.r.getX() * scale, p.r.getY() * scale, p.r.getWidth() * scale, p.r.getHeight() * scale).toNearestInt());
    if (keyboard != nullptr) keyboard->setKeyWidth (keyboard->getWidth() / 36.f);
}

void VantaEditor::paint (Graphics& g)
{
    g.fillAll (Colours::black);
    Graphics::ScopedSaveState ss (g);
    g.addTransform (AffineTransform::scale (scale));

    drawPanel (g, { 6, 6, 1444, 68 }, {});
    g.setColour (Colours::white); g.setFont (font (44.f, false, 0.22f));
    g.drawText ("VANTA X", Rectangle<float> (24, 6, 340, 46), Justification::centredLeft, false);
    g.setColour (dim()); g.setFont (font (10.5f, false, 0.32f));
    g.drawText ("ADVANCED DIGITAL SYNTHESIZER", Rectangle<float> (28, 50, 320, 16), Justification::centredLeft, false);

    if (page == 1)
    {
        const int eng = engineSel->selected;
        auto draw = [&] (int grp) { for (const auto& p : panels[grp]) drawPanel (g, p.r, p.title); };
        draw (kShared);
        if (eng == 2)
        {
            draw (kVA);
            g.setColour (dim()); g.setFont (font (12.5f));
            g.drawText ("Modulation: see the MOD and MATRIX tabs", Rectangle<float> (12, 700, 384, 40), Justification::centred, false);
            g.setColour (white()); g.setFont (font (12.f));
            g.drawText ("Voices", Rectangle<float> (24, 380 + 4, 70, 14), Justification::centredLeft, false);
        }
        else if (eng == 3) { draw (kGran); draw (kGA); }
        else if (eng == 4) { draw (kAdd);  draw (kGA); }
    }
    else if (page == 2 || page == 5)
    {
        for (const auto& p : panels[page == 2 ? kMod : kMat]) drawPanel (g, p.r, p.title);
        drawPanel (g, { 12, 654, 1428, 150 }, {});
    }
    else
    {
        static const char* names[] { "MAIN", "OSC", "MOD", "FILTER", "FX", "MATRIX", "BROWSER" };
        static const char* stage[] { "a later stage", "", "", "a later stage", "a later stage", "", "a later stage" };
        drawPanel (g, { 12, 136, 1428, 510 }, names[page]);
        g.setColour (dim()); g.setFont (font (16.f));
        g.drawText (String (names[page]) + " page - scheduled for " + stage[page] + " (see PROGRESS.md)", Rectangle<float> (12, 136, 1428, 510), Justification::centred, false);
        drawPanel (g, { 12, 654, 1428, 150 }, {});
    }
}
