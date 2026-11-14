// Batuta (Windows) - paineis: faixa de mixer, editor de andamento, pads, click
#pragma once
#include "../UI/Common.h"

namespace desk
{
using namespace juce; using namespace ui;

struct App
{
    AudioDeviceManager dm;
    bt::Engine engine;
    File projectFile;
    bool dirty = false;
    std::function<void()> refreshAll;      // reconstruir a interface apos mudar de musica/estrutura
    bt::Song& song() { return engine.song(); }
    void changed (bool structural) { if (structural) engine.reloadSong(); else engine.syncParams(); dirty = true; }
};

//======================================================================================
class Strip : public Component
{
public:
    Strip (String t, Colour c) : fader (true, c), title (std::move (t)), colour (c)
    {
        addAndMakeVisible (fader); addAndMakeVisible (mute); addAndMakeVisible (out);
        mute.setClickingTogglesState (true);
        mute.setColour (TextButton::buttonOnColourId, col::stop);
        mute.setTooltip ("Mute");
        out.setTooltip (U ("Sa\xc3\xad" "da f\xc3\xadsica da interface de \xc3\xa1udio"));
        fader.onValueChange = [this] { repaint(); if (onChange) onChange(); };
        mute.onClick = [this] { if (onChange) onChange(); };
        out.onChange = [this] { if (onChange) onChange(); };
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        r.removeFromTop (20);
        out.setBounds (r.removeFromBottom (24));
        r.removeFromBottom (4);
        auto row = r.removeFromBottom (24);
        mute.setBounds (row.removeFromLeft (30));
        r.removeFromBottom (4);
        meterArea = r.removeFromRight (10);
        fader.setBounds (r.reduced (6, 0));
    }
    void paint (Graphics& g) override
    {
        g.setColour (col::panel); g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1), 5.0f);
        g.setColour (colour); g.fillRect (4, 3, getWidth() - 8, 2);
        g.setColour (col::text); g.setFont (FontOptions (13.0f, Font::bold));
        g.drawFittedText (title, 4, 6, getWidth() - 8, 16, Justification::centred, 1);
        g.setColour (col::dim); g.setFont (FontOptions (12.0f));
        g.drawText (String (fader.getValue(), 1) + " dB", 36, getHeight() - 56, getWidth() - 40, 24, Justification::centredRight);
        g.setColour (col::bg); g.fillRect (meterArea);
        const float db = Decibels::gainToDecibels (peak, -60.0f);
        const int hh = (int) (jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * (float) meterArea.getHeight());
        g.setColour (peak > 0.98f ? col::stop : col::play); g.fillRect (meterArea.withTop (meterArea.getBottom() - hh));
    }
    void setPeak (float p) { peak = jmax (p, peak * 0.85f); repaint (meterArea); }

    Fader fader; TextButton mute { "M" }; ComboBox out;
    String title; Colour colour; float peak = 0; Rectangle<int> meterArea;
    std::function<void()> onChange;
};

//======================================================================================
// Editor grafico do mapa de andamento: blocos com formula de compasso e rampas de BPM
class TempoEditor : public Component
{
public:
    explicit TempoEditor (App& a) : app (a)
    {
        auto setupNum = [this] (Slider& s, double lo, double hi, double step, const String& tip)
        {
            s.setSliderStyle (Slider::IncDecButtons); s.setRange (lo, hi, step); s.setTextBoxStyle (Slider::TextBoxLeft, false, 56, 24);
            s.setIncDecButtonsMode (Slider::incDecButtonsDraggable_Vertical); s.setTooltip (tip);
            s.onValueChange = [this] { fromControls(); }; addAndMakeVisible (s);
        };
        setupNum (bars, 1, 999, 1, U ("Dura\xc3\xa7\xc3\xa3o do bloco em compassos"));
        setupNum (num, 1, 32, 1, U ("Numerador da f\xc3\xb3rmula de compasso"));
        setupNum (b0, 20, 400, 0.5, U ("BPM no in\xc3\xad" "cio do bloco"));
        setupNum (b1, 20, 400, 0.5, U ("BPM no fim do bloco (diferente do in\xc3\xad" "cio = rampa)"));
        den.addItemList ({ "2", "4", "8", "16" }, 1); den.onChange = [this] { fromControls(); }; addAndMakeVisible (den);
        for (auto* b : { &add, &del }) addAndMakeVisible (*b);
        add.setTooltip (U ("Inserir bloco depois do selecionado")); del.setTooltip ("Excluir bloco selecionado");
        add.onClick = [this]
        {
            auto& t = app.song().tempo; sel = jlimit (0, (int) t.size() - 1, sel);
            bt::TempoBlock nb = t[(size_t) sel]; nb.bpm0 = nb.bpm1; nb.bars = 8;
            t.insert (t.begin() + sel + 1, nb); ++sel; app.changed (false); toControls();
        };
        del.onClick = [this]
        {
            auto& t = app.song().tempo; if (t.size() <= 1) return;
            t.erase (t.begin() + jlimit (0, (int) t.size() - 1, sel)); sel = jmax (0, sel - 1); app.changed (false); toControls();
        };
        toControls();
    }

    void toControls()
    {
        auto& t = app.song().tempo; sel = jlimit (0, (int) t.size() - 1, sel);
        auto& b = t[(size_t) sel];
        updating = true;
        bars.setValue (b.bars); num.setValue (b.num); b0.setValue (b.bpm0); b1.setValue (b.bpm1);
        den.setText (String (b.den), dontSendNotification);
        updating = false; repaint();
    }
    void fromControls()
    {
        if (updating) return;
        auto& b = app.song().tempo[(size_t) sel];
        b.bars = (int) bars.getValue(); b.num = (int) num.getValue(); b.den = jmax (2, den.getText().getIntValue());
        b.bpm0 = b0.getValue(); b.bpm1 = b1.getValue();
        app.changed (false); repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto row = r.removeFromBottom (28); graph = r.withTrimmedBottom (6);
        auto place = [&] (Component& c, int w) { c.setBounds (row.removeFromLeft (w)); row.removeFromLeft (6); };
        labelX[0] = row.getX(); row.removeFromLeft (78); place (bars, 110);
        labelX[1] = row.getX(); row.removeFromLeft (64); place (num, 100); place (den, 60);
        labelX[2] = row.getX(); row.removeFromLeft (76); place (b0, 120);
        labelX[3] = row.getX(); row.removeFromLeft (62); place (b1, 120);
        row.removeFromLeft (10); place (add, 130); place (del, 130);
        labelY = row.getY();
    }

    void paint (Graphics& g) override
    {
        g.fillAll (col::panel);
        g.setColour (col::dim); g.setFont (FontOptions (13.0f));
        const char* names[4] = { "Compassos", "F\xc3\xb3rmula", "BPM in\xc3\xad" "cio", "BPM fim" };
        for (int i = 0; i < 4; ++i) g.drawText (U (names[i]), labelX[i], labelY, 76, 28, Justification::centredLeft);

        auto& t = app.song().tempo;
        lo = 400; hi = 20; totalBars = 0;
        for (auto& b : t) { lo = jmin (lo, b.bpm0, b.bpm1); hi = jmax (hi, b.bpm0, b.bpm1); totalBars += b.bars; }
        lo = jmax (20.0, std::floor (lo / 10.0) * 10.0 - 20.0); hi = jmin (400.0, std::ceil (hi / 10.0) * 10.0 + 20.0);
        const int shownBars = totalBars + 4;
        auto gr = graph.toFloat().withTrimmedLeft (40).withTrimmedTop (18);
        plot = gr; barW = gr.getWidth() / (float) shownBars;
        g.setColour (col::bg); g.fillRect (gr);

        g.setFont (FontOptions (11.0f));
        for (double v = lo; v <= hi + 0.1; v += (hi - lo > 120 ? 40 : 20))
        {
            const float y = yFor (v);
            g.setColour (col::line.withAlpha (0.6f)); g.drawHorizontalLine ((int) y, gr.getX(), gr.getRight());
            g.setColour (col::dim); g.drawText (String ((int) v), graph.getX(), (int) y - 7, 36, 14, Justification::centredRight);
        }
        const int step = shownBars > 120 ? 16 : shownBars > 60 ? 8 : shownBars > 30 ? 4 : 1;
        for (int b = 0; b <= shownBars; ++b)
        {
            const float x = gr.getX() + b * barW;
            if (b % step == 0)
            {
                g.setColour (col::line.withAlpha (0.5f)); g.drawVerticalLine ((int) x, gr.getY(), gr.getBottom());
                if (b < shownBars) { g.setColour (col::dim); g.drawText (String (b + 1), (int) x + 2, (int) gr.getBottom() - 13, 30, 12, Justification::left); }
            }
        }
        int bar = 0; handles.clear();
        for (size_t i = 0; i < t.size(); ++i)
        {
            auto& b = t[i];
            const float x0 = gr.getX() + bar * barW, x1 = x0 + b.bars * barW;
            const bool s = (int) i == sel;
            if (s) { g.setColour (col::accent.withAlpha (0.10f)); g.fillRect (x0, gr.getY(), x1 - x0, gr.getHeight()); }
            g.setColour (s ? col::accent : col::dim); g.drawVerticalLine ((int) x0, gr.getY() - 16, gr.getBottom());
            g.setFont (FontOptions (13.0f, Font::bold));
            g.drawText (String (b.num) + "/" + String (b.den), (int) x0 + 4, (int) gr.getY() - 17, 60, 15, Justification::left);
            g.setColour (s ? col::accent : col::text);
            g.drawLine (x0, yFor (b.bpm0), x1, yFor (b.bpm1), 2.5f);
            handles.push_back ({ (int) i, false, { x0 + 6, yFor (b.bpm0) } });
            handles.push_back ({ (int) i, true,  { x1 - 6, yFor (b.bpm1) } });
            bar += b.bars;
        }
        // depois do ultimo bloco o andamento permanece
        g.setColour (col::dim);
        const float dashes[2] = { 5.0f, 4.0f };
        g.drawDashedLine ({ gr.getX() + bar * barW, yFor (t.back().bpm1), gr.getRight(), yFor (t.back().bpm1) }, dashes, 2, 1.5f);
        for (auto& h : handles)
        {
            g.setColour (h.block == sel ? col::accent : col::text); g.fillEllipse (Rectangle<float> (11, 11).withCentre (h.pos));
            g.setColour (col::bg); g.drawEllipse (Rectangle<float> (11, 11).withCentre (h.pos), 1.5f);
        }
        if (app.engine.playing.load())
        {
            const float x = gr.getX() + (float) playBars * barW;
            if (x >= gr.getX() && x <= gr.getRight()) { g.setColour (col::play); g.fillRect (x - 1.0f, gr.getY(), 2.0f, gr.getHeight()); }
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        drag = -1;
        for (size_t i = 0; i < handles.size(); ++i)
            if (handles[i].pos.getDistanceFrom (e.position) < 12.0f) { drag = (int) i; sel = handles[i].block; }
        if (drag < 0 && plot.contains (e.position))
        {
            int bar = (int) ((e.position.x - plot.getX()) / barW), acc = 0;
            auto& t = app.song().tempo;
            for (size_t i = 0; i < t.size(); ++i) { if (bar < acc + t[i].bars) { sel = (int) i; break; } acc += t[i].bars; }
        }
        toControls();
    }
    void mouseDrag (const MouseEvent& e) override
    {
        if (drag < 0) return;
        auto& h = handles[(size_t) drag];
        auto& b = app.song().tempo[(size_t) h.block];
        const double v = jlimit (20.0, 400.0, std::round (lo + (hi - lo) * (1.0 - (e.position.y - plot.getY()) / plot.getHeight())));
        const bool flat = b.bpm0 == b.bpm1 && ! e.mods.isShiftDown() && ! h.end;
        if (h.end) b.bpm1 = v; else { b.bpm0 = v; if (flat) b.bpm1 = v; }
        app.changed (false); toControls();
    }
    void setPlayhead (double barsPos) { if (std::abs (barsPos - playBars) > 0.01) { playBars = barsPos; repaint(); } }

private:
    float yFor (double bpm) const { return plot.getBottom() - (float) ((bpm - lo) / (hi - lo)) * plot.getHeight(); }
    struct Handle { int block; bool end; Point<float> pos; };
    App& app;
    Slider bars, num, b0, b1; ComboBox den;
    IconButton add { Icon::plus, "Novo bloco" }, del { Icon::minus, "Excluir bloco" };
    Rectangle<int> graph; Rectangle<float> plot; float barW = 10;
    std::vector<Handle> handles;
    int sel = 0, drag = -1, totalBars = 0, labelX[4] {}, labelY = 0;
    double lo = 60, hi = 180, playBars = 0;
    bool updating = false;
};

//======================================================================================
class PadsPanel : public Component
{
public:
    explicit PadsPanel (App& a) : app (a)
    {
        addAndMakeVisible (autoPad); addAndMakeVisible (key);
        autoPad.setClickingTogglesState (true); autoPad.setColour (TextButton::buttonOnColourId, col::link);
        autoPad.onClick = [this] { app.engine.setAutoPad (autoPad.getToggleState()); };
        key.addItem (U ("\xe2\x80\x94 sem pad"), 1);
        for (int i = 0; i < 12; ++i) key.addItem (bt::keyNames()[i], i + 2);
        key.setTooltip (U ("Tonalidade do Auto-PAD desta m\xc3\xbasica"));
        key.onChange = [this] { app.song().padKey = key.getSelectedId() - 2; app.engine.selectSong (app.engine.songIndex); app.dirty = true; };
        refresh();
    }
    void refresh() { key.setSelectedId (app.song().padKey + 2, dontSendNotification); autoPad.setToggleState (app.engine.autoPad.load(), dontSendNotification); repaint(); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto left = r.removeFromLeft (170);
        autoPad.setBounds (left.removeFromTop (44)); left.removeFromTop (8); key.setBounds (left.removeFromTop (30));
        r.removeFromLeft (12); grid = r;
    }
    Rectangle<int> cell (int i) const { const int w = grid.getWidth() / 8, h = grid.getHeight() / 2; return Rectangle<int> (grid.getX() + (i % 8) * w, grid.getY() + (i / 8) * h, w, h).reduced (3); }
    void paint (Graphics& g) override
    {
        g.fillAll (col::panel);
        for (int i = 0; i < bt::numPads; ++i)
        {
            auto& d = app.engine.project.pads[i];
            const bool has = d.file.isNotEmpty() || d.builtin >= 0;
            auto c = cell (i).toFloat();
            const bool flash = Time::getMillisecondCounter() - flashTime[i] < 140;
            g.setColour (flash ? col::accent : (has ? col::raised : col::bg)); g.fillRoundedRectangle (c, 6.0f);
            g.setColour (col::line); g.drawRoundedRectangle (c, 6.0f, 1.0f);
            g.setColour (flash ? col::bg : col::dim); g.setFont (FontOptions (11.0f)); g.drawText (String (i + 1), c.reduced (6, 4), Justification::topLeft);
            g.setColour (flash ? col::bg : (has ? col::text : col::dim)); g.setFont (FontOptions (14.0f, Font::bold));
            g.drawFittedText (has ? d.name : "vazio", c.toNearestInt().reduced (4), Justification::centred, 2);
        }
    }
    void mouseDown (const MouseEvent& e) override
    {
        for (int i = 0; i < bt::numPads; ++i)
        {
            if (! cell (i).contains (e.getPosition())) continue;
            if (e.mods.isPopupMenu()) { menu (i); return; }
            app.engine.triggerPad (i); flashTime[i] = Time::getMillisecondCounter(); repaint();
        }
    }
    void tick()
    {
        for (int i = 0; i < bt::numPads; ++i)
        {
            const int f = app.engine.uiPadFlash[i].load();
            if (f != lastFlash[i]) { lastFlash[i] = f; flashTime[i] = Time::getMillisecondCounter(); }
        }
        autoPad.setToggleState (app.engine.autoPad.load(), dontSendNotification);
        repaint (grid);
    }
private:
    void menu (int i)
    {
        PopupMenu m, built;
        for (int b = 0; b < bt::builtinPadNames().size(); ++b) built.addItem (100 + b, bt::builtinPadNames()[b]);
        m.addItem (1, U ("Carregar arquivo de \xc3\xa1udio\xe2\x80\xa6")); m.addSubMenu ("Som embutido", built); m.addItem (2, U ("Renomear\xe2\x80\xa6")); m.addItem (3, U ("Volume\xe2\x80\xa6")); m.addSeparator(); m.addItem (4, "Limpar");
        m.showMenuAsync ({}, [this, i] (int r)
        {
            auto& d = app.engine.project.pads[i];
            auto done = [this] { app.engine.rebuildSamples(); app.dirty = true; repaint(); };
            if (r >= 100) { d.builtin = r - 100; d.file.clear(); d.name = bt::builtinPadNames()[r - 100]; done(); }
            else if (r == 4) { d = {}; done(); }
            else if (r == 2) prompt ("Nome do pad", { { "Nome", d.name, {} } }, [&d, done] (StringArray v) { d.name = v[0]; done(); });
            else if (r == 3) prompt ("Volume do pad (dB)", { { "dB (-24 a +6)", String (d.gainDb, 1), {} } }, [&d, done] (StringArray v) { d.gainDb = jlimit (-24.0f, 6.0f, v[0].replaceCharacter (',', '.').getFloatValue()); done(); });
            else if (r == 1)
            {
                chooser.reset (new FileChooser (U ("Escolha um \xc3\xa1udio curto"), {}, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3"));
                chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [&d, done] (const FileChooser& fc)
                {
                    auto f = fc.getResult(); if (! f.existsAsFile()) return;
                    d.file = f.getFullPathName(); d.builtin = -1; d.name = f.getFileNameWithoutExtension(); done();
                });
            }
        });
    }
    App& app;
    TextButton autoPad { "Auto-PAD" }; ComboBox key;
    Rectangle<int> grid; uint32 flashTime[bt::numPads] {}; int lastFlash[bt::numPads] {};
    std::unique_ptr<FileChooser> chooser;
};

//======================================================================================
class ClickPanel : public Component
{
public:
    explicit ClickPanel (App& a) : app (a)
    {
        auto setup = [this] (ComboBox& c, StringArray items, std::function<void (int)> set)
        {
            c.addItemList (items, 1); addAndMakeVisible (c);
            c.onChange = [this, &c, set] { set (c.getSelectedItemIndex()); };
        };
        setup (sound, bt::clickSoundNames(), [this] (int i) { app.engine.project.clickSound = i; app.engine.rebuildSamples(); app.dirty = true; });
        setup (subdiv, { U ("Sem subdivis\xc3\xa3o"), "Colcheias (2)", "Tercinas (3)", "Semicolcheias (4)" }, [this] (int i) { app.song().subdiv = i + 1; app.changed (false); });
        setup (voice, { "Desligada", U ("S\xc3\xb3 na contagem inicial"), "Sempre (conta todos os compassos)" }, [this] (int i) { app.engine.project.voiceMode = i; app.changed (false); });
        setup (lang, { U ("Portugu\xc3\xaas"), U ("Ingl\xc3\xaas") }, [this] (int i) { app.engine.project.voiceLang = i; app.engine.rebuildSamples(); app.dirty = true; });
        setup (countIn, { "Sem contagem", "1 compasso", "2 compassos", "4 compassos" }, [this] (int i) { static const int v[4] = { 0, 1, 2, 4 }; app.song().countInBars = v[i]; app.changed (false); });
        refresh();
    }
    void refresh()
    {
        auto& p = app.engine.project; auto& s = app.song();
        sound.setSelectedItemIndex (p.clickSound, dontSendNotification); subdiv.setSelectedItemIndex (s.subdiv - 1, dontSendNotification);
        voice.setSelectedItemIndex (p.voiceMode, dontSendNotification); lang.setSelectedItemIndex (p.voiceLang, dontSendNotification);
        countIn.setSelectedItemIndex (s.countInBars >= 4 ? 3 : s.countInBars, dontSendNotification);
    }
    void resized() override
    {
        ComboBox* c[5] = { &sound, &subdiv, &countIn, &voice, &lang };
        for (int i = 0; i < 5; ++i) c[i]->setBounds (220 + (i / 3) * 520, 16 + (i % 3) * 40, 260, 30);
    }
    void paint (Graphics& g) override
    {
        g.fillAll (col::panel); g.setColour (col::text); g.setFont (FontOptions (14.0f));
        const char* n[5] = { "Timbre do click", "Subdivis\xc3\xa3o (desta m\xc3\xbasica)", "Contagem inicial (desta m\xc3\xbasica)", "Voz de contagem", "Idioma da voz" };
        for (int i = 0; i < 5; ++i) g.drawText (U (n[i]), 12 + (i / 3) * 520, 16 + (i % 3) * 40, 204, 30, Justification::centredLeft);
    }
private:
    App& app; ComboBox sound, subdiv, voice, lang, countIn;
};
}
