// Batuta - elementos de interface compartilhados (tema, icones, dialogos, painel MIDI)
#pragma once
#include <JuceHeader.h>
#include "../Engine/Engine.h"
#include "../Engine/Package.h"

namespace ui
{
using namespace juce;
inline String U (const char* utf8) { return String::fromUTF8 (utf8); }

namespace col
{
    const Colour bg       { 0xff14161a }, panel { 0xff1e2127 }, raised { 0xff2b2f37 }, line { 0xff3a3f49 };
    const Colour text     { 0xfff3f5f8 }, dim   { 0xffa3acb9 };
    const Colour accent   { 0xffffb020 }, play  { 0xff35d07f }, stop { 0xffff5c5c }, queued { 0xffffd84a }, link { 0xff4cc3ff }, cue { 0xffc58bff };
}

enum class Icon { play, stop, plus, minus, up, down, prev, next, loop, lock, metronome, copy };

inline Path iconPath (Icon i)
{
    Path p;
    switch (i)
    {
        case Icon::play:  p.addTriangle (0.15f, 0.05f, 0.95f, 0.5f, 0.15f, 0.95f); break;
        case Icon::stop:  p.addRectangle (0.12f, 0.12f, 0.76f, 0.76f); break;
        case Icon::plus:  p.addRectangle (0.42f, 0.1f, 0.16f, 0.8f); p.addRectangle (0.1f, 0.42f, 0.8f, 0.16f); break;
        case Icon::minus: p.addRectangle (0.1f, 0.42f, 0.8f, 0.16f); break;
        case Icon::up:    p.addTriangle (0.5f, 0.15f, 0.92f, 0.8f, 0.08f, 0.8f); break;
        case Icon::down:  p.addTriangle (0.5f, 0.85f, 0.92f, 0.2f, 0.08f, 0.2f); break;
        case Icon::prev:  p.addRectangle (0.08f, 0.1f, 0.14f, 0.8f); p.addTriangle (0.95f, 0.1f, 0.95f, 0.9f, 0.28f, 0.5f); break;
        case Icon::next:  p.addRectangle (0.78f, 0.1f, 0.14f, 0.8f); p.addTriangle (0.05f, 0.1f, 0.05f, 0.9f, 0.72f, 0.5f); break;
        case Icon::loop:  { Path a; a.addArc (0.14f, 0.14f, 0.72f, 0.72f, 0.7f, 5.7f, true); Path st; PathStrokeType (0.14f).createStrokedPath (st, a); p.addPath (st); }
                          p.addTriangle (0.62f, 0.0f, 0.62f, 0.44f, 0.9f, 0.2f); break;
        case Icon::lock:  p.addRoundedRectangle (0.15f, 0.45f, 0.7f, 0.5f, 0.08f);
                          { Path a; a.addArc (0.28f, 0.08f, 0.44f, 0.6f, -1.57f, 1.57f, true); Path st; PathStrokeType (0.12f).createStrokedPath (st, a); p.addPath (st); } break;
        case Icon::metronome: p.addQuadrilateral (0.36f, 0.05f, 0.64f, 0.05f, 0.85f, 0.95f, 0.15f, 0.95f);
                          { Path a; a.addLineSegment ({ 0.5f, 0.75f, 0.78f, 0.12f }, 0.09f); p.addPath (a); } break;
        case Icon::copy:  p.addRectangle (0.1f, 0.25f, 0.55f, 0.65f); p.addRectangle (0.35f, 0.08f, 0.55f, 0.12f); p.addRectangle (0.78f, 0.08f, 0.12f, 0.6f); break;
    }
    return p;
}

inline void drawIcon (Graphics& g, Icon i, Rectangle<float> r, Colour c)
{
    const float s = jmin (r.getWidth(), r.getHeight());
    auto sq = r.withSizeKeepingCentre (s, s);
    g.setColour (c);
    g.fillPath (iconPath (i), AffineTransform::scale (s, s).translated (sq.getX(), sq.getY()));
}

// Botao com icone padrao de DAW e (opcionalmente) texto
class IconButton : public Button
{
public:
    IconButton (Icon i, String label = {}, Colour c = col::text) : Button (label), icon (i), colour (c), text (std::move (label)) {}
    Icon icon; Colour colour; String text; Colour fill = col::raised; bool lit = false;
    void paintButton (Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (lit ? colour : (down ? fill.brighter (0.25f) : over ? fill.brighter (0.1f) : fill));
        g.fillRoundedRectangle (r, 6.0f);
        const Colour fg = lit ? col::bg : (isEnabled() ? colour : col::dim.darker());
        if (text.isEmpty()) drawIcon (g, icon, r.reduced (r.getHeight() * 0.27f), fg);
        else
        {
            auto ir = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.3f);
            drawIcon (g, icon, ir, fg);
            g.setColour (fg); g.setFont (FontOptions (jmin (16.0f, r.getHeight() * 0.42f), Font::bold));
            g.drawFittedText (text, r.toNearestInt().reduced (2, 0), Justification::centredLeft, 1);
        }
    }
};

class Look : public LookAndFeel_V4
{
public:
    Look()
    {
        setColour (ResizableWindow::backgroundColourId, col::bg);
        setColour (TextButton::buttonColourId, col::raised);
        setColour (TextButton::buttonOnColourId, col::accent);
        setColour (TextButton::textColourOffId, col::text);
        setColour (TextButton::textColourOnId, col::bg);
        setColour (ComboBox::backgroundColourId, col::raised);
        setColour (ComboBox::textColourId, col::text);
        setColour (ComboBox::outlineColourId, col::line);
        setColour (ComboBox::arrowColourId, col::dim);
        setColour (PopupMenu::backgroundColourId, col::panel);
        setColour (PopupMenu::textColourId, col::text);
        setColour (PopupMenu::highlightedBackgroundColourId, col::accent);
        setColour (PopupMenu::highlightedTextColourId, col::bg);
        setColour (Label::textColourId, col::text);
        setColour (ListBox::backgroundColourId, col::panel);
        setColour (TextEditor::backgroundColourId, col::bg);
        setColour (TextEditor::textColourId, col::text);
        setColour (TextEditor::outlineColourId, col::line);
        setColour (AlertWindow::backgroundColourId, col::panel);
        setColour (AlertWindow::textColourId, col::text);
        setColour (AlertWindow::outlineColourId, col::line);
        setColour (TabbedButtonBar::tabTextColourId, col::dim);
        setColour (TabbedButtonBar::frontTextColourId, col::text);
        setColour (TabbedComponent::backgroundColourId, col::panel);
        setColour (TabbedComponent::outlineColourId, col::line);
        setColour (ScrollBar::thumbColourId, col::line.brighter (0.3f));
        setColour (Slider::textBoxTextColourId, col::text);
        setColour (Slider::textBoxOutlineColourId, col::line);
        setColour (Slider::textBoxBackgroundColourId, col::bg);
        setColour (ToggleButton::textColourId, col::text);
        setColour (ToggleButton::tickColourId, col::accent);
        setColour (DocumentWindow::textColourId, col::text);
    }

    void drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        Colour c = b.getToggleState() ? b.findColour (TextButton::buttonOnColourId) : b.findColour (TextButton::buttonColourId);
        if (down) c = c.brighter (0.2f); else if (over) c = c.brighter (0.08f);
        g.setColour (c); g.fillRoundedRectangle (r, 6.0f);
    }
    Font getTextButtonFont (TextButton&, int h) override { return FontOptions (jmin (17.0f, h * 0.48f), Font::bold); }

    void drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle style, Slider& s) override
    {
        const bool vert = style == Slider::LinearVertical;
        auto area = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float thick = jmin (10.0f, (vert ? w : h) * 0.22f);
        auto track = vert ? area.withSizeKeepingCentre (thick, area.getHeight()) : area.withSizeKeepingCentre (area.getWidth(), thick);
        g.setColour (col::bg); g.fillRoundedRectangle (track, thick * 0.5f);
        g.setColour (col::line); g.drawRoundedRectangle (track, thick * 0.5f, 1.0f);
        auto fillC = s.findColour (Slider::trackColourId);
        g.setColour (s.isEnabled() ? fillC : col::line);
        if (vert) g.fillRoundedRectangle (track.withTop (pos), thick * 0.5f); else g.fillRoundedRectangle (track.withRight (pos), thick * 0.5f);
        // marca de 0 dB
        const float zero = (float) s.getPositionOfValue (0.0);
        g.setColour (col::dim);
        if (vert) g.fillRect (area.getX(), zero - 0.5f, area.getWidth() * 0.18f, 1.0f); else g.fillRect (zero - 0.5f, area.getY(), 1.0f, area.getHeight() * 0.18f);
        // knob largo (tatil)
        const float kl = vert ? jmin (26.0f, h * 0.18f) : jmin (38.0f, w * 0.12f);
        auto knob = vert ? Rectangle<float> (area.getX() + 2, pos - kl * 0.5f, area.getWidth() - 4, kl)
                         : Rectangle<float> (pos - kl * 0.5f, area.getY() + 2, kl, area.getHeight() - 4);
        g.setColour (col::text); g.fillRoundedRectangle (knob, 4.0f);
        g.setColour (col::bg);
        if (vert) g.fillRect (knob.getX() + 3, knob.getCentreY() - 1.0f, knob.getWidth() - 6, 2.0f);
        else g.fillRect (knob.getCentreX() - 1.0f, knob.getY() + 3, 2.0f, knob.getHeight() - 6);
    }
    int getSliderThumbRadius (Slider&) override { return 14; }
    Font getComboBoxFont (ComboBox& c) override { return FontOptions (jmin (16.0f, c.getHeight() * 0.55f)); }
    Font getPopupMenuFont() override { return FontOptions (scale * 16.0f); }
    Font getAlertWindowMessageFont() override { return FontOptions (scale * 15.0f); }
    Font getAlertWindowFont() override { return FontOptions (scale * 15.0f); }
    Font getAlertWindowTitleFont() override { return FontOptions (scale * 18.0f, Font::bold); }
    int getTabButtonBestWidth (TabBarButton& b, int depth) override { return jmax (90, LookAndFeel_V4::getTabButtonBestWidth (b, depth)); }
    float scale = 1.0f;
};

// Fader em dB (-60 .. +6)
class Fader : public Slider
{
public:
    Fader (bool vertical, Colour c = col::accent)
    {
        setSliderStyle (vertical ? LinearVertical : LinearHorizontal);
        setTextBoxStyle (NoTextBox, true, 0, 0);
        setRange (-60.0, 6.0, 0.1);
        setSkewFactorFromMidPoint (-12.0);
        setDoubleClickReturnValue (true, 0.0);
        setColour (trackColourId, c);
    }
    void setFromNormalised (float v) { setValue (proportionOfLengthToValue (jlimit (0.0, 1.0, (double) v)), sendNotificationSync); }
};

struct Field { String label, value; StringArray choices; };

// Dialogo assincrono com campos de texto e listas
inline void prompt (const String& title, std::vector<Field> fields, std::function<void (StringArray)> done)
{
    auto* w = new AlertWindow (title, {}, MessageBoxIconType::NoIcon);
    for (size_t i = 0; i < fields.size(); ++i)
    {
        const String id = "f" + String ((int) i);
        if (fields[i].choices.isEmpty()) w->addTextEditor (id, fields[i].value, fields[i].label);
        else { w->addComboBox (id, fields[i].choices, fields[i].label); w->getComboBoxComponent (id)->setSelectedItemIndex (jmax (0, fields[i].choices.indexOf (fields[i].value))); }
    }
    w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancelar", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([w, fields, done] (int r)
    {
        if (r != 1) return;
        StringArray out;
        for (size_t i = 0; i < fields.size(); ++i)
        {
            const String id = "f" + String ((int) i);
            out.add (fields[i].choices.isEmpty() ? w->getTextEditorContents (id) : w->getComboBoxComponent (id)->getText());
        }
        done (out);
    }), true);
}

inline void message (const String& title, const String& text)
{
    AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::NoIcon).withTitle (title).withMessage (text).withButton ("OK"), nullptr);
}

inline void confirm (const String& title, const String& text, std::function<void()> yes)
{
    AlertWindow::showAsync (MessageBoxOptions().withIconType (MessageBoxIconType::NoIcon).withTitle (title).withMessage (text)
                                .withButton ("Sim").withButton (U ("N\xc3\xa3o")),
                            [yes] (int r) { if (r == 1) yes(); });
}

inline String describeBind (const bt::MidiBind& b)
{
    return (b.isCC ? "CC " : "Nota ") + String (b.number) + "  canal " + String (b.channel);
}

// Lista de acoes com MIDI Learn (usada no Windows e no Android)
class MidiPanel : public Component, private ListBoxModel
{
public:
    MidiPanel (bt::Engine& e, std::function<void()> changed, bool mobile, int rowH = 34) : engine (e), onChanged (std::move (changed))
    {
        auto add = [this] (int a, int i) { rows.push_back ({ a, i }); };
        add (bt::actToggle, 0); add (bt::actPlay, 0); add (bt::actStop, 0);
        add (bt::actNextSong, 0); add (bt::actPrevSong, 0); add (bt::actNextScene, 0); add (bt::actPrevScene, 0);
        add (bt::actAutoPad, 0); add (bt::actClick, 0);
        for (int i = 0; i < (mobile ? 12 : 16); ++i) add (bt::actPad, i);
        for (int i = 0; i < 8; ++i) add (bt::actScene, i);
        for (int i = 0; i < bt::numBuses; ++i) add (bt::actBusFader, i);
        if (! mobile) for (int i = 0; i < 8; ++i) add (bt::actTrackFader, i);
        list.setModel (this); list.setRowHeight (rowH);
        addAndMakeVisible (list);
        engine.onMidiLearned = [this] (bt::MidiBind b)
        {
            if (learningRow < 0) return;
            auto& midi = engine.project.midi;
            midi.erase (std::remove_if (midi.begin(), midi.end(), [&] (auto& m) { return (m.isCC == b.isCC && m.number == b.number && m.channel == b.channel)
                                                                                         || (m.action == rows[(size_t) learningRow].first && m.index == rows[(size_t) learningRow].second); }), midi.end());
            b.action = rows[(size_t) learningRow].first; b.index = rows[(size_t) learningRow].second;
            midi.push_back (b);
            learningRow = -1; engine.syncParams(); list.repaint(); if (onChanged) onChanged();
        };
    }
    ~MidiPanel() override { engine.onMidiLearned = nullptr; engine.midiLearning = false; }
    void resized() override { list.setBounds (getLocalBounds()); }
    void refresh() { list.updateContent(); list.repaint(); }

private:
    int getNumRows() override { return (int) rows.size(); }
    const bt::MidiBind* find (int row) const
    {
        for (auto& m : engine.project.midi) if (m.action == rows[(size_t) row].first && m.index == rows[(size_t) row].second) return &m;
        return nullptr;
    }
    void paintListBoxItem (int row, Graphics& g, int w, int h, bool) override
    {
        if (row < 0 || row >= (int) rows.size()) return;
        g.setColour (row % 2 ? col::panel : col::panel.brighter (0.04f)); g.fillRect (0, 0, w, h);
        const bool learning = row == learningRow;
        g.setFont (FontOptions (h * 0.45f));
        g.setColour (col::text); g.drawText (bt::actionName (rows[(size_t) row].first, rows[(size_t) row].second), 10, 0, w / 2, h, Justification::centredLeft);
        auto* b = find (row);
        g.setColour (learning ? col::queued : (b ? col::accent : col::dim));
        g.drawText (learning ? U ("toque o controle MIDI\xe2\x80\xa6") : (b ? describeBind (*b) : U ("\xe2\x80\x94")), w / 2, 0, w / 2 - 80, h, Justification::centredLeft);
        g.setColour (col::dim); g.setFont (FontOptions (h * 0.36f));
        g.drawText (b && ! learning ? "limpar" : "aprender", w - 80, 0, 72, h, Justification::centredRight);
    }
    void listBoxItemClicked (int row, const MouseEvent& e) override
    {
        if (e.x > list.getWidth() - 90 && find (row) != nullptr && row != learningRow)
        {
            auto& midi = engine.project.midi;
            midi.erase (std::remove_if (midi.begin(), midi.end(), [&] (auto& m) { return m.action == rows[(size_t) row].first && m.index == rows[(size_t) row].second; }), midi.end());
            engine.syncParams(); if (onChanged) onChanged();
        }
        else if (learningRow == row) { learningRow = -1; engine.midiLearning = false; }
        else { learningRow = row; engine.midiLearning = true; }
        list.repaint();
    }
    bt::Engine& engine;
    std::function<void()> onChanged;
    std::vector<std::pair<int, int>> rows;
    ListBox list;
    int learningRow = -1;
};

// Mantem todas as entradas MIDI (USB e Bluetooth) abertas e ligadas ao motor
class MidiAutoConnect : private Timer
{
public:
    MidiAutoConnect (AudioDeviceManager& d, bt::Engine& e) : dm (d), engine (e) { dm.addMidiInputDeviceCallback ({}, &engine); startTimer (2000); timerCallback(); }
    ~MidiAutoConnect() override { dm.removeMidiInputDeviceCallback ({}, &engine); }
    StringArray names;
private:
    void timerCallback() override
    {
        names.clear();
        for (auto& d : MidiInput::getAvailableDevices())
        {
            names.add (d.name);
            if (! dm.isMidiInputDeviceEnabled (d.identifier)) dm.setMidiInputDeviceEnabled (d.identifier, true);
        }
    }
    AudioDeviceManager& dm; bt::Engine& engine;
};

// Sem nenhum dispositivo de audio (ex.: maquina de testes), mantem o motor rodando em tempo
// real, sem som, para que transporte e interface continuem funcionando.
class FakeAudio : private HighResolutionTimer
{
public:
    explicit FakeAudio (bt::Engine& e) : engine (e) { buf.setSize (8, 480); engine.numOutputs = 8; startTimer (10); }
    ~FakeAudio() override { stopTimer(); }
private:
    void hiResTimerCallback() override { engine.process (buf.getArrayOfWritePointers(), 8, 480); }
    bt::Engine& engine; AudioBuffer<float> buf;
};

inline String outputName (int outL, int outR) { return outR >= 0 ? String (outL + 1) + "/" + String (outR + 1) : String (outL + 1); }

// Preenche um ComboBox com as saidas fisicas: pares estereo e canais mono
inline void fillOutputs (ComboBox& c, int numOut, int outL, int outR)
{
    c.clear (dontSendNotification);
    int id = 1, sel = 0;
    for (int i = 0; i + 1 < numOut; i += 2) { c.addItem (String (i + 1) + "/" + String (i + 2), 1000 + i); if (outL == i && outR == i + 1) sel = 1000 + i; ++id; }
    c.addSeparator();
    for (int i = 0; i < numOut; ++i) { c.addItem ("Mono " + String (i + 1), 2000 + i); if (outL == i && outR < 0) sel = 2000 + i; }
    if (sel != 0) c.setSelectedId (sel, dontSendNotification);
    else c.setText (outputName (outL, outR) + " (indisp.)", dontSendNotification);
}
inline void readOutputs (const ComboBox& c, int& outL, int& outR)
{
    const int id = c.getSelectedId();
    if (id >= 2000) { outL = id - 2000; outR = -1; } else if (id >= 1000) { outL = id - 1000; outR = outL + 1; }
}
}
