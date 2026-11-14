// Batuta (Windows) - central de producao e palco: sessao por cenas, mixer, roteamento
#include "Panels.h"

namespace desk
{
static const char* audioWildcard = "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3";
static const char* followGlyph[5] = { "", "\xe2\x86\x92", "\xe2\x86\xbb", "\xe2\x96\xa0", "\xe2\x87\xa4" };
static StringArray followNames() { return { "Nenhuma", U ("Pr\xc3\xb3xima cena"), "Repetir esta cena", "Parar", "Voltar para a primeira cena" }; }

//======================================================================================
// Grade de sessao: linhas = cenas, colunas = trilhas
class SessionView : public Component, public FileDragAndDropTarget
{
public:
    static constexpr int sceneW = 176, colW = 116, rowH = 38, headH = 30;
    explicit SessionView (App& a) : app (a) {}

    int idealWidth() const  { return sceneW + ((int) app.song().tracks.size() + 1) * colW + 8; }
    int idealHeight() const { return headH + ((int) app.song().scenes.size() + 1) * rowH + 8; }

    Rectangle<int> cellRect (int scene, int track) const { return { sceneW + track * colW, headH + scene * rowH, colW, rowH }; }
    Rectangle<int> sceneRect (int scene) const { return { 0, headH + scene * rowH, sceneW, rowH }; }

    void paint (Graphics& g) override
    {
        auto& s = app.song(); auto& e = app.engine;
        const int nT = (int) s.tracks.size(), nS = (int) s.scenes.size();
        const bool blink = (Time::getMillisecondCounter() / 220) % 2 == 0;
        g.fillAll (col::bg);

        g.setFont (FontOptions (12.0f, Font::bold)); g.setColour (col::dim);
        g.drawText ("CENAS", 10, 0, sceneW, headH, Justification::centredLeft);
        for (int t = 0; t < nT; ++t)
        {
            auto r = Rectangle<int> (sceneW + t * colW, 0, colW, headH).reduced (2);
            auto& tr = s.tracks[(size_t) t];
            g.setColour (tr.isCue ? col::cue.withAlpha (0.35f) : col::raised); g.fillRoundedRectangle (r.toFloat(), 4.0f);
            g.setColour (col::text); g.setFont (FontOptions (14.0f, Font::bold));
            g.drawFittedText (tr.name, r.reduced (6, 0).withTrimmedRight (tr.isCue ? 34 : 0), Justification::centredLeft, 1);
            if (tr.isCue) { g.setColour (col::cue); g.setFont (FontOptions (10.0f, Font::bold)); g.drawText ("GUIA", r.reduced (5, 0), Justification::centredRight); }
        }
        {
            auto r = Rectangle<int> (sceneW + nT * colW, 0, colW, headH).reduced (2);
            g.setColour (col::line); g.drawRoundedRectangle (r.toFloat(), 4.0f, 1.0f);
            drawIcon (g, Icon::plus, r.removeFromLeft (26).toFloat().reduced (8), col::dim);
            g.setColour (col::dim); g.setFont (FontOptions (13.0f)); g.drawText ("Trilha", r, Justification::centredLeft);
        }

        const int cur = e.uiScene.load(), queued = e.uiQueuedScene.load();
        for (int i = 0; i < nS; ++i)
        {
            auto& sc = s.scenes[(size_t) i];
            auto r = sceneRect (i).reduced (2);
            const bool isCur = i == cur, isQ = i == queued;
            g.setColour (isCur ? col::play.withAlpha (0.28f) : col::panel); g.fillRoundedRectangle (r.toFloat(), 4.0f);
            if (isCur && sc.bars > 0)
            {
                g.setColour (col::play.withAlpha (0.35f));
                g.fillRoundedRectangle (r.toFloat().withWidth (r.getWidth() * jlimit (0.0f, 1.0f, (float) (e.uiSceneBar.load() + 1) / (float) sc.bars)), 4.0f);
            }
            auto ic = r.removeFromLeft (rowH - 4).toFloat().reduced (10);
            drawIcon (g, Icon::play, ic, isQ ? (blink ? col::queued : col::dim) : (isCur ? col::play : col::text));
            auto info = r.removeFromRight (58);
            g.setColour (col::dim); g.setFont (FontOptions (12.0f));
            g.drawText (String (sc.bars) + " c " + U (followGlyph[jlimit (0, 4, sc.follow)]), info.reduced (4, 0), Justification::centredRight);
            g.setColour (col::text); g.setFont (FontOptions (15.0f, Font::bold));
            g.drawFittedText (sc.name, r.reduced (2, 0), Justification::centredLeft, 1);

            for (int t = 0; t < nT; ++t)
            {
                auto c = cellRect (i, t).reduced (2);
                auto& clip = s.clips[(size_t) i][(size_t) t];
                const bool playingHere = t < 64 && e.uiTrackClip[t].load() == i;
                const bool queuedHere = (t < 64 && e.uiTrackQueued[t].load() == i) || (isQ && ! clip.empty());
                if (clip.empty()) { g.setColour (col::panel.darker (0.25f)); g.fillRoundedRectangle (c.toFloat(), 4.0f); continue; }
                g.setColour (playingHere ? col::play : (s.tracks[(size_t) t].isCue ? col::cue.withAlpha (0.55f) : col::raised.brighter (0.12f)));
                g.fillRoundedRectangle (c.toFloat(), 4.0f);
                const Colour fg = playingHere ? col::bg : col::text;
                drawIcon (g, Icon::play, c.removeFromLeft (26).toFloat().reduced (8), queuedHere && ! playingHere ? (blink ? col::queued : col::dim) : fg);
                if (clip.loop) drawIcon (g, Icon::loop, c.removeFromRight (20).toFloat().reduced (4), fg);
                g.setColour (fg); g.setFont (FontOptions (13.0f));
                g.drawFittedText (File (clip.file).getFileNameWithoutExtension(), c.reduced (2, 0), Justification::centredLeft, 1);
            }
        }
        // linha final: + Cena e botoes de parada por trilha
        auto add = sceneRect (nS).reduced (2);
        g.setColour (col::line); g.drawRoundedRectangle (add.toFloat(), 4.0f, 1.0f);
        drawIcon (g, Icon::plus, add.removeFromLeft (rowH - 4).toFloat().reduced (11), col::dim);
        g.setColour (col::dim); g.setFont (FontOptions (13.0f)); g.drawText ("Cena", add, Justification::centredLeft);
        for (int t = 0; t < nT; ++t)
        {
            auto c = cellRect (nS, t).reduced (2);
            g.setColour (col::panel); g.fillRoundedRectangle (c.toFloat(), 4.0f);
            const bool active = t < 64 && e.uiTrackClip[t].load() >= 0;
            drawIcon (g, Icon::stop, c.toFloat().withSizeKeepingCentre (13, 13), active ? col::stop : col::dim);
        }
    }

    void mouseDown (const MouseEvent& ev) override
    {
        auto& s = app.song();
        const int nT = (int) s.tracks.size(), nS = (int) s.scenes.size();
        const auto p = ev.getPosition();
        const int row = p.y < headH ? -1 : (p.y - headH) / rowH;
        const int colI = p.x < sceneW ? -1 : (p.x - sceneW) / colW;
        const bool menu = ev.mods.isPopupMenu();

        if (row == -1 && colI >= 0)
        {
            if (colI == nT) addTrack();
            else if (colI < nT && (menu || ev.getNumberOfClicks() > 1)) trackMenu (colI);
        }
        else if (colI == -1 && row >= 0)
        {
            if (row == nS) addScene();
            else if (row < nS) { if (menu) sceneMenu (row); else app.engine.launchScene (row); }
        }
        else if (row >= 0 && colI >= 0 && colI < nT)
        {
            if (row == nS) app.engine.stopTrack (colI);
            else if (row < nS)
            {
                auto& clip = s.clips[(size_t) row][(size_t) colI];
                if (clip.empty()) chooseClip (row, colI);
                else if (menu) clipMenu (row, colI);
                else app.engine.launchClip (row, colI);
            }
        }
    }

    bool isInterestedInFileDrag (const StringArray&) override { return true; }
    void filesDropped (const StringArray& files, int x, int y) override
    {
        auto& s = app.song();
        int row = jmax (0, (y - headH) / rowH), colI = jmax (0, (x - sceneW) / colW);
        if (row >= (int) s.scenes.size()) { s.scenes.push_back ({}); s.scenes.back().name = "Cena " + String ((int) s.scenes.size()); row = (int) s.scenes.size() - 1; }
        StringArray sorted (files); sorted.sort (true);
        for (auto& f : sorted)
        {
            if (! File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) continue;
            if (colI >= (int) s.tracks.size()) { bt::Track t; t.name = File (f).getFileNameWithoutExtension(); nameCue (t); s.tracks.push_back (t); }
            s.normalise();
            s.clips[(size_t) row][(size_t) colI].file = f;
            ++colI;
        }
        app.changed (true); if (app.refreshAll) app.refreshAll();
    }

    static void nameCue (bt::Track& t)
    {
        const auto n = t.name.toLowerCase();
        if (n.contains ("click") || n.contains ("guia") || n.contains ("guide") || n.contains ("cue")) t.isCue = true;
    }

    void addTrack()
    {
        auto& s = app.song(); bt::Track t; t.name = "Trilha " + String ((int) s.tracks.size() + 1);
        s.tracks.push_back (t); app.changed (true); if (app.refreshAll) app.refreshAll();
    }
    void addScene()
    {
        auto& s = app.song(); bt::Scene sc; sc.name = "Cena " + String ((int) s.scenes.size() + 1);
        s.scenes.push_back (sc); app.changed (true); if (app.refreshAll) app.refreshAll();
    }

private:
    void chooseClip (int row, int colI)
    {
        chooser.reset (new FileChooser (U ("Escolha o \xc3\xa1udio do clipe"), {}, audioWildcard));
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this, row, colI] (const FileChooser& fc)
        {
            auto f = fc.getResult(); auto& s = app.song();
            if (! f.existsAsFile() || row >= (int) s.scenes.size() || colI >= (int) s.tracks.size()) return;
            s.clips[(size_t) row][(size_t) colI].file = f.getFullPathName();
            app.changed (true); repaint();
        });
    }
    void clipMenu (int row, int colI)
    {
        auto& clip = app.song().clips[(size_t) row][(size_t) colI];
        PopupMenu m;
        m.addItem (1, "Repetir em loop", true, clip.loop); m.addItem (2, U ("Volume do clipe\xe2\x80\xa6")); m.addItem (3, U ("Trocar arquivo\xe2\x80\xa6")); m.addSeparator(); m.addItem (4, "Remover clipe");
        m.showMenuAsync ({}, [this, row, colI] (int r)
        {
            auto& s = app.song(); if (row >= (int) s.scenes.size() || colI >= (int) s.tracks.size()) return;
            auto& c = s.clips[(size_t) row][(size_t) colI];
            if (r == 1) { c.loop = ! c.loop; app.changed (false); }
            else if (r == 2) prompt ("Volume do clipe (dB)", { { "dB (-24 a +12)", String (c.gainDb, 1), {} } }, [this, &c] (StringArray v) { c.gainDb = jlimit (-24.0f, 12.0f, v[0].replaceCharacter (',', '.').getFloatValue()); app.changed (false); });
            else if (r == 3) chooseClip (row, colI);
            else if (r == 4) { c = {}; app.changed (true); }
            repaint();
        });
    }
    void sceneMenu (int row)
    {
        PopupMenu m;
        m.addItem (1, U ("Editar cena\xe2\x80\xa6")); m.addItem (2, "Duplicar"); m.addItem (3, "Mover para cima", row > 0); m.addItem (4, "Mover para baixo", row + 1 < (int) app.song().scenes.size());
        m.addSeparator(); m.addItem (5, "Excluir cena");
        m.showMenuAsync ({}, [this, row] (int r)
        {
            auto& s = app.song(); if (row >= (int) s.scenes.size() || r == 0) return;
            if (r == 1)
            {
                auto& sc = s.scenes[(size_t) row];
                StringArray cues { "Nenhuma" }; cues.addArray (bt::cueNames());
                prompt ("Cena", { { "Nome", sc.name, {} }, { U ("Dura\xc3\xa7\xc3\xa3o (compassos)"), String (sc.bars), {} },
                                  { U ("Ao terminar a dura\xc3\xa7\xc3\xa3o (a\xc3\xa7\xc3\xa3o de seguimento)"), followNames()[sc.follow], followNames() },
                                  { "Guia de voz ao chamar a cena", sc.cue.isEmpty() ? "Nenhuma" : sc.cue, cues } },
                        [this, row] (StringArray v)
                        {
                            auto& s2 = app.song(); if (row >= (int) s2.scenes.size()) return;
                            auto& sc2 = s2.scenes[(size_t) row];
                            sc2.name = v[0]; sc2.bars = jlimit (1, 999, v[1].getIntValue()); sc2.follow = jmax (0, followNames().indexOf (v[2]));
                            sc2.cue = v[3] == "Nenhuma" ? String() : v[3];
                            app.changed (false); repaint();
                        });
                return;
            }
            if (r == 2) { s.scenes.insert (s.scenes.begin() + row + 1, s.scenes[(size_t) row]); s.clips.insert (s.clips.begin() + row + 1, s.clips[(size_t) row]); }
            else if (r == 3) { std::swap (s.scenes[(size_t) row], s.scenes[(size_t) row - 1]); std::swap (s.clips[(size_t) row], s.clips[(size_t) row - 1]); }
            else if (r == 4) { std::swap (s.scenes[(size_t) row], s.scenes[(size_t) row + 1]); std::swap (s.clips[(size_t) row], s.clips[(size_t) row + 1]); }
            else if (r == 5) { s.scenes.erase (s.scenes.begin() + row); s.clips.erase (s.clips.begin() + row); }
            app.changed (true); if (app.refreshAll) app.refreshAll();
        });
    }
    void trackMenu (int t)
    {
        PopupMenu m;
        m.addItem (1, U ("Renomear\xe2\x80\xa6")); m.addItem (2, U ("Trilha-guia (vai para o canal L no celular)"), true, app.song().tracks[(size_t) t].isCue);
        m.addItem (3, "Mover para a esquerda", t > 0); m.addItem (4, "Mover para a direita", t + 1 < (int) app.song().tracks.size());
        m.addSeparator(); m.addItem (5, "Excluir trilha");
        m.showMenuAsync ({}, [this, t] (int r)
        {
            auto& s = app.song(); if (t >= (int) s.tracks.size() || r == 0) return;
            auto swapT = [&s] (int a, int b) { std::swap (s.tracks[(size_t) a], s.tracks[(size_t) b]); for (auto& row : s.clips) std::swap (row[(size_t) a], row[(size_t) b]); };
            if (r == 1) { prompt ("Nome da trilha", { { "Nome", s.tracks[(size_t) t].name, {} } }, [this, t] (StringArray v) { auto& s2 = app.song(); if (t < (int) s2.tracks.size()) { s2.tracks[(size_t) t].name = v[0]; app.dirty = true; if (app.refreshAll) app.refreshAll(); } }); return; }
            if (r == 2) { s.tracks[(size_t) t].isCue = ! s.tracks[(size_t) t].isCue; app.changed (false); repaint(); return; }
            if (r == 3) swapT (t, t - 1); else if (r == 4) swapT (t, t + 1);
            else if (r == 5) { s.tracks.erase (s.tracks.begin() + t); for (auto& row : s.clips) row.erase (row.begin() + t); }
            app.changed (true); if (app.refreshAll) app.refreshAll();
        });
    }
    App& app;
    std::unique_ptr<FileChooser> chooser;
};

//======================================================================================
class SetlistPanel : public Component, private ListBoxModel
{
public:
    explicit SetlistPanel (App& a) : app (a)
    {
        list.setModel (this); list.setRowHeight (34); addAndMakeVisible (list);
        for (auto* b : { &add, &dup, &del, &up, &down }) addAndMakeVisible (*b);
        add.setTooltip (U ("Nova m\xc3\xbasica")); dup.setTooltip (U ("Duplicar m\xc3\xbasica")); del.setTooltip (U ("Excluir m\xc3\xbasica")); up.setTooltip ("Subir na setlist"); down.setTooltip ("Descer na setlist");
        auto& songs = app.engine.project.songs;
        add.onClick = [this, &songs] { bt::Song s; s.name = U ("M\xc3\xbasica ") + String ((int) songs.size() + 1); songs.push_back (s); select ((int) songs.size() - 1); };
        dup.onClick = [this, &songs] { const int i = app.engine.songIndex; auto c = songs[(size_t) i]; c.name << U (" (c\xc3\xb3pia)"); songs.insert (songs.begin() + i + 1, c); select (i + 1); };
        del.onClick = [this, &songs]
        {
            if (songs.size() <= 1) return;
            confirm (U ("Excluir m\xc3\xbasica"), U ("Excluir \"") + app.song().name + U ("\" da setlist?"),
                     [this, &songs] { const int i = app.engine.songIndex; songs.erase (songs.begin() + i); select (jmin (i, (int) songs.size() - 1)); });
        };
        up.onClick = [this, &songs] { const int i = app.engine.songIndex; if (i > 0) { std::swap (songs[(size_t) i], songs[(size_t) i - 1]); select (i - 1); } };
        down.onClick = [this, &songs] { const int i = app.engine.songIndex; if (i + 1 < (int) songs.size()) { std::swap (songs[(size_t) i], songs[(size_t) i + 1]); select (i + 1); } };
    }
    void select (int i) { app.dirty = true; app.engine.selectSong (i); if (app.refreshAll) app.refreshAll(); }
    void refresh() { list.updateContent(); list.selectRow (app.engine.songIndex, false, true); list.repaint(); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (6); r.removeFromTop (26);
        auto row = r.removeFromBottom (34);
        for (auto* b : { &add, &dup, &del, &up, &down }) { b->setBounds (row.removeFromLeft (row.getHeight() + 2).reduced (1)); }
        r.removeFromBottom (4); list.setBounds (r);
    }
    void paint (Graphics& g) override
    {
        g.fillAll (col::panel); g.setColour (col::dim); g.setFont (FontOptions (12.0f, Font::bold));
        g.drawText ("SETLIST", 12, 6, getWidth(), 20, Justification::centredLeft);
    }
private:
    int getNumRows() override { return (int) app.engine.project.songs.size(); }
    void paintListBoxItem (int row, Graphics& g, int w, int h, bool) override
    {
        auto& songs = app.engine.project.songs; if (row < 0 || row >= (int) songs.size()) return;
        const bool cur = row == app.engine.songIndex;
        g.setColour (cur ? col::accent : col::panel); g.fillRoundedRectangle (1.0f, 1.0f, w - 2.0f, h - 2.0f, 4.0f);
        g.setColour (cur ? col::bg : col::dim); g.setFont (FontOptions (13.0f)); g.drawText (String (row + 1), 6, 0, 22, h, Justification::centredLeft);
        g.setColour (cur ? col::bg : col::text); g.setFont (FontOptions (15.0f, Font::bold)); g.drawFittedText (songs[(size_t) row].name, 30, 0, w - 36, h, Justification::centredLeft, 1);
    }
    void listBoxItemClicked (int row, const MouseEvent&) override { if (row != app.engine.songIndex) { app.engine.selectSong (row); if (app.refreshAll) app.refreshAll(); } }
    void listBoxItemDoubleClicked (int row, const MouseEvent&) override
    {
        prompt (U ("Nome da m\xc3\xbasica"), { { "Nome", app.engine.project.songs[(size_t) row].name, {} } },
                [this, row] (StringArray v) { auto& songs = app.engine.project.songs; if (row < (int) songs.size()) { songs[(size_t) row].name = v[0]; app.dirty = true; refresh(); } });
    }
    App& app; ListBox list;
    IconButton add { Icon::plus }, dup { Icon::copy }, del { Icon::minus }, up { Icon::up }, down { Icon::down };
};

//======================================================================================
class MainComponent : public Component, private Timer
{
public:
    explicit MainComponent (App& a) : app (a), session (a), setlist (a), tempo (a), pads (a), click (a),
                                      midi (a.engine, [&a] { a.dirty = true; }, false), midiConnect (a.dm, a.engine)
    {
        setLookAndFeel (&look);
        setWantsKeyboardFocus (true);
        for (Component* c : std::initializer_list<Component*> { &fileB, &playB, &stopB, &clickB, &linkB, &followB, &audioB, &exportB, &setlist, &viewport, &tabs, &status })
            addAndMakeVisible (c);
        playB.colour = col::play; stopB.colour = col::stop; clickB.colour = col::accent;
        playB.setTooltip (U ("Tocar (barra de espa\xc3\xa7o)")); stopB.setTooltip ("Parar"); clickB.setTooltip (U ("Metr\xc3\xb4nomo ligado/desligado"));
        linkB.setTooltip (U ("Ableton Link: sincroniza andamento e play/stop com outros aparelhos na mesma rede"));
        followB.setTooltip (U ("Seguir: este computador obedece o andamento do Link (o celular dita). Desligado: este computador dita."));
        linkB.setClickingTogglesState (true); followB.setClickingTogglesState (true);
        linkB.setColour (TextButton::buttonOnColourId, col::link); followB.setColour (TextButton::buttonOnColourId, col::link);
        playB.onClick = [this] { app.engine.play(); };
        stopB.onClick = [this] { app.engine.stop(); };
        clickB.onClick = [this] { app.engine.project.clickOn = ! app.engine.project.clickOn; app.changed (false); };
        linkB.onClick = [this] { app.engine.setLinkEnabled (linkB.getToggleState()); };
        followB.onClick = [this] { app.engine.setLinkFollow (followB.getToggleState()); };
        audioB.onClick = [this] { showAudioSettings(); };
        exportB.onClick = [this] { exportMenu(); };
        fileB.onClick = [this] { fileMenu(); };
        status.setColour (Label::textColourId, col::dim); status.setJustificationType (Justification::centredRight);

        viewport.setViewedComponent (&session, false);
        viewport.setScrollBarsShown (true, true);
        tabs.setTabBarDepth (30);
        tabs.addTab ("Andamento", col::panel, &tempo, false);
        tabs.addTab ("Pads e Auto-PAD", col::panel, &pads, false);
        tabs.addTab ("Click e voz", col::panel, &click, false);
        tabs.addTab ("MIDI", col::panel, &midi, false);

        const char* busNames[bt::numBuses] = { "Click", "Subdiv.", "Voz", "Auto-PAD", "One-shots" };
        for (int b = 0; b < bt::numBuses; ++b)
        {
            auto* s = busStrips.add (new Strip (busNames[b], b <= bt::busVoice ? col::accent : col::link));
            addAndMakeVisible (s);
            s->onChange = [this, s, b] { if (building) return; auto& bus = app.engine.project.buses[b]; bus.gainDb = (float) s->fader.getValue(); bus.mute = s->mute.getToggleState(); readOutputs (s->out, bus.outL, bus.outR); app.changed (false); };
        }
        app.refreshAll = [this] { rebuild(); };
        app.engine.onUiAction = [this] (int action, int index, float v) { uiAction (action, index, v); };
        rebuild();
        startTimerHz (30);
        setSize (1360, 820);
    }
    ~MainComponent() override { app.refreshAll = nullptr; app.engine.onUiAction = nullptr; setLookAndFeel (nullptr); }

    void rebuild()
    {
        building = true;
        auto& s = app.song(); auto& e = app.engine;
        const int nOut = jmax (2, e.numOutputs.load());
        trackStrips.clear();
        for (int t = 0; t < (int) s.tracks.size(); ++t)
        {
            auto& tr = s.tracks[(size_t) t];
            auto* st = trackStrips.add (new Strip (tr.name, tr.isCue ? col::cue : col::play));
            addAndMakeVisible (st);
            st->fader.setValue (tr.gainDb, dontSendNotification); st->mute.setToggleState (tr.mute, dontSendNotification);
            fillOutputs (st->out, nOut, tr.outL, tr.outR);
            st->onChange = [this, st, t] { if (building) return; auto& s2 = app.song(); if (t >= (int) s2.tracks.size()) return; auto& x = s2.tracks[(size_t) t]; x.gainDb = (float) st->fader.getValue(); x.mute = st->mute.getToggleState(); readOutputs (st->out, x.outL, x.outR); app.changed (false); };
        }
        for (int b = 0; b < bt::numBuses; ++b)
        {
            auto& bus = e.project.buses[b]; auto* st = busStrips[b];
            st->fader.setValue (bus.gainDb, dontSendNotification); st->mute.setToggleState (bus.mute, dontSendNotification);
            fillOutputs (st->out, nOut, bus.outL, bus.outR);
        }
        session.setSize (jmax (session.idealWidth(), viewport.getWidth() - 12), jmax (session.idealHeight(), viewport.getHeight() - 12));
        setlist.refresh(); tempo.toControls(); pads.refresh(); click.refresh(); midi.refresh();
        building = false; shownOuts = nOut;
        resized(); repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (56).reduced (8, 8);
        fileB.setBounds (top.removeFromLeft (96)); top.removeFromLeft (14);
        playB.setBounds (top.removeFromLeft (52)); top.removeFromLeft (4); stopB.setBounds (top.removeFromLeft (52)); top.removeFromLeft (14);
        display = top.removeFromLeft (330); top.removeFromLeft (14);
        clickB.setBounds (top.removeFromLeft (44)); top.removeFromLeft (14);
        linkB.setBounds (top.removeFromLeft (84)); top.removeFromLeft (4); followB.setBounds (top.removeFromLeft (84)); top.removeFromLeft (14);
        exportB.setBounds (top.removeFromRight (190)); top.removeFromRight (6); audioB.setBounds (top.removeFromRight (130)); top.removeFromRight (8);
        status.setBounds (top);

        tabs.setBounds (r.removeFromBottom (230));
        setlist.setBounds (r.removeFromLeft (190));
        auto buses = r.removeFromRight (bt::numBuses * 78 + 12).reduced (4);
        auto mixRow = r.removeFromBottom (214);
        buses.removeFromTop (22);
        for (auto* s : busStrips) s->setBounds (buses.removeFromLeft (78).removeFromBottom (214 + 0).reduced (1));
        viewport.setBounds (r.reduced (4));
        session.setSize (jmax (session.idealWidth(), viewport.getWidth() - 12), jmax (session.idealHeight(), viewport.getHeight() - 12));
        mixArea = mixRow;
        layoutStrips();
    }
    void layoutStrips()
    {
        const int x0 = viewport.getX() + SessionView::sceneW - viewport.getViewPositionX();
        for (int t = 0; t < trackStrips.size(); ++t)
        {
            auto b = Rectangle<int> (x0 + t * SessionView::colW, mixArea.getY(), SessionView::colW, mixArea.getHeight()).reduced (2, 1);
            trackStrips[t]->setBounds (b);
            trackStrips[t]->setVisible (b.getX() >= viewport.getX() && b.getRight() <= viewport.getRight() + 4);
        }
    }

    void paint (Graphics& g) override
    {
        g.fillAll (col::bg);
        g.setColour (col::panel); g.fillRect (0, 0, getWidth(), 56);
        // visor de transporte
        auto d = display;
        g.setColour (col::bg); g.fillRoundedRectangle (d.toFloat(), 6.0f);
        auto& e = app.engine;
        const bool playing = e.playing.load(); const int bar = e.uiBar.load();
        auto cellD = [&] (int w, const String& label, const String& value, Colour c)
        {
            auto x = d.removeFromLeft (w);
            g.setColour (col::dim); g.setFont (FontOptions (10.0f, Font::bold)); g.drawText (label, x.removeFromTop (14).reduced (8, 0), Justification::bottomLeft);
            g.setColour (c); g.setFont (FontOptions (Font::getDefaultMonospacedFontName(), 21.0f, Font::bold)); g.drawText (value, x.reduced (8, 0), Justification::centredLeft);
        };
        cellD (92, "BPM", String (e.uiBpm.load(), 1), col::accent);
        cellD (70, U ("F\xc3\x93RMULA"), String (e.uiNum.load()) + "/" + String (e.uiDen.load()), col::text);
        cellD (96, "COMPASSO", playing ? (bar < 0 ? "-" + String (-bar) : String (bar + 1)) + "." + String (e.uiBeatInBar.load() + 1) : U ("\xe2\x80\x94"), bar < 0 && playing ? col::queued : col::text);
        const int num = jlimit (1, 16, e.uiNum.load());
        for (int i = 0; i < num; ++i)
        {
            auto led = Rectangle<float> ((float) d.getX() + 4 + i * (jmin (14.0f, 66.0f / num)), (float) d.getCentreY() - 5, jmin (10.0f, 60.0f / num), 10.0f);
            const bool on = playing && i == e.uiBeatInBar.load();
            g.setColour (on ? (i == 0 ? col::accent : col::play) : col::line); g.fillEllipse (led);
        }
        g.setColour (col::dim); g.setFont (FontOptions (12.0f, Font::bold));
        g.drawText ("TRILHAS", mixArea.getX() + 8, mixArea.getY() + 4, 170, 16, Justification::left);
        g.setFont (FontOptions (11.0f));
        g.drawFittedText (U ("Fader, mute e sa\xc3\xad" "da f\xc3\xadsica de cada trilha. Arraste arquivos de \xc3\xa1udio para a grade para criar clipes."), mixArea.getX() + 8, mixArea.getY() + 24, 170, 90, Justification::topLeft, 6);
        auto bl = busStrips[0]->getBounds();
        g.setFont (FontOptions (12.0f, Font::bold)); g.drawText (U ("CLICK, VOZ E EFEITOS"), bl.getX() + 4, bl.getY() - 20, 300, 16, Justification::left);
    }

    bool keyPressed (const KeyPress& k) override
    {
        if (k == KeyPress::spaceKey) { app.engine.togglePlay(); return true; }
        return false;
    }

    bool save (bool as)
    {
        if (as || app.projectFile == File())
        {
            chooser.reset (new FileChooser ("Salvar setlist", app.projectFile, "*.batuta"));
            chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting, [this] (const FileChooser& fc)
            {
                auto f = fc.getResult(); if (f == File()) return;
                app.projectFile = f.withFileExtension ("batuta"); save (false);
            });
            return false;
        }
        app.engine.project.name = app.projectFile.getFileNameWithoutExtension();
        if (app.engine.project.save (app.projectFile)) { app.dirty = false; setStatus ("Salvo: " + app.projectFile.getFileName()); return true; }
        message ("Erro", U ("N\xc3\xa3o foi poss\xc3\xadvel salvar o arquivo."));
        return false;
    }
    void open (const File& f)
    {
        bt::Project p;
        if (! bt::Project::load (f, p)) { message ("Erro", U ("N\xc3\xa3o foi poss\xc3\xadvel abrir ") + f.getFileName()); return; }
        app.engine.setProject (p); app.projectFile = f; app.dirty = false; rebuild();
        setStatus ("Aberto: " + f.getFileName());
    }
    void setStatus (const String& s) { status.setText (s, dontSendNotification); }

private:
    void timerCallback() override
    {
        auto& e = app.engine;
        playB.lit = e.playing.load(); clickB.lit = e.project.clickOn;
        playB.repaint(); clickB.repaint();
        linkB.setButtonText (e.isLinkEnabled() ? "Link " + String (e.linkPeers()) : "Link");
        repaint (display); session.repaint();
        for (int t = 0; t < trackStrips.size() && t < 64; ++t) trackStrips[t]->setPeak (e.uiTrackPeak[t].exchange (0.0f));
        for (int b = 0; b < bt::numBuses; ++b) busStrips[b]->setPeak (e.uiBusPeak[b].exchange (0.0f));
        layoutStrips();
        pads.tick();
        tempo.setPlayhead (e.playing.load() ? jmax (0.0, (double) e.uiBar.load() + (double) e.uiBeatInBar.load() / jmax (1, e.uiNum.load())) : 0.0);
        if (e.numOutputs.load() != shownOuts) rebuild();
        if (auto* w = dynamic_cast<DocumentWindow*> (getTopLevelComponent()))
            w->setName ("Batuta - " + (app.projectFile == File() ? U ("setlist sem t\xc3\xadtulo") : app.projectFile.getFileNameWithoutExtension()) + (app.dirty ? " *" : ""));
    }

    void uiAction (int action, int index, float v)
    {
        auto& e = app.engine; auto& p = e.project;
        if (action == bt::actNextSong || action == bt::actPrevSong)
        {
            const int i = jlimit (0, (int) p.songs.size() - 1, e.songIndex + (action == bt::actNextSong ? 1 : -1));
            if (i != e.songIndex) { e.selectSong (i); rebuild(); }
        }
        else if (action == bt::actClick) { p.clickOn = ! p.clickOn; app.changed (false); }
        else if (action == bt::actBusFader && index < bt::numBuses) busStrips[index]->fader.setFromNormalised (v);
        else if (action == bt::actTrackFader && index < trackStrips.size()) trackStrips[index]->fader.setFromNormalised (v);
    }

    void fileMenu()
    {
        PopupMenu m;
        m.addItem (1, "Nova setlist"); m.addItem (2, U ("Abrir\xe2\x80\xa6")); m.addItem (3, "Salvar"); m.addItem (4, U ("Salvar como\xe2\x80\xa6"));
        m.addSeparator(); m.addItem (5, U ("Importar stems (multitrack)\xe2\x80\xa6"));
        m.addSeparator(); m.addItem (6, "Sobre o Batuta");
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (fileB), [this] (int r)
        {
            if (r == 1) { auto go = [this] { app.engine.setProject (bt::Project()); app.projectFile = File(); app.dirty = false; rebuild(); };
                          if (app.dirty) confirm ("Nova setlist", U ("Descartar as altera\xc3\xa7\xc3\xb5" "es n\xc3\xa3o salvas?"), go); else go(); }
            else if (r == 2)
            {
                chooser.reset (new FileChooser ("Abrir setlist", {}, "*.batuta"));
                chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc) { if (fc.getResult().existsAsFile()) open (fc.getResult()); });
            }
            else if (r == 3) save (false);
            else if (r == 4) save (true);
            else if (r == 5)
            {
                chooser.reset (new FileChooser (U ("Escolha todos os stems da m\xc3\xbasica"), {}, audioWildcard));
                chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems, [this] (const FileChooser& fc)
                {
                    StringArray files; for (auto& f : fc.getResults()) files.add (f.getFullPathName());
                    if (files.isEmpty()) return;
                    auto& s = app.song();
                    const int row = (int) s.scenes.size();   // cria uma cena nova com uma trilha por arquivo
                    session.filesDropped (files, SessionView::sceneW + 1, SessionView::headH + row * SessionView::rowH + 1);
                });
            }
            else if (r == 6) message ("Batuta 0.1", U ("DAW de dire\xc3\xa7\xc3\xa3o musical.\nSoftware livre (GPLv3). Usa JUCE, Ableton Link e, no Windows, o ASIO SDK da Steinberg.\nASIO \xc3\xa9 marca registrada da Steinberg Media Technologies GmbH."));
        });
    }

    void showAudioSettings()
    {
        auto* sel = new AudioDeviceSelectorComponent (app.dm, 0, 0, 2, 64, true, false, false, false);
        sel->setSize (560, 520);
        DialogWindow::LaunchOptions o;
        o.content.setOwned (sel); o.dialogTitle = U ("\xc3\x81udio e MIDI"); o.dialogBackgroundColour = col::panel;
        o.escapeKeyTriggersCloseButton = true; o.useNativeTitleBar = true; o.resizable = false;
        o.launchAsync();
    }

    void exportMenu()
    {
        PopupMenu m;
        m.addItem (1, U ("Salvar pacote em arquivo\xe2\x80\xa6 (para pendrive, Drive, Dropbox)")); m.addItem (2, U ("Enviar por Wi-Fi para o celular\xe2\x80\xa6"));
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (exportB), [this] (int r)
        {
            if (r == 1)
            {
                chooser.reset (new FileChooser ("Salvar pacote para o celular", File::getSpecialLocation (File::userDocumentsDirectory).getChildFile (app.engine.project.name + ".batpack"), "*.batpack"));
                chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting, [this] (const FileChooser& fc)
                {
                    if (fc.getResult() != File()) runExport (fc.getResult().withFileExtension ("batpack"), {});
                });
            }
            else if (r == 2)
            {
                setStatus (U ("Procurando celulares na rede\xe2\x80\xa6"));
                Component::SafePointer<MainComponent> safe (this);
                Thread::launch ([safe]
                {
                    auto peers = bt::discoverPeers();
                    MessageManager::callAsync ([safe, peers] { if (safe != nullptr) safe->choosePeer (peers); });
                });
            }
        });
    }
    void choosePeer (std::vector<bt::Peer> peers)
    {
        setStatus (peers.empty() ? "Nenhum celular encontrado" : String ((int) peers.size()) + " celular(es) encontrado(s)");
        PopupMenu m;
        for (size_t i = 0; i < peers.size(); ++i) m.addItem (10 + (int) i, peers[i].name + "  (" + peers[i].ip + ")");
        if (! peers.empty()) m.addSeparator();
        m.addItem (1, U ("Digitar o IP do celular\xe2\x80\xa6"));
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (exportB), [this, peers] (int r)
        {
            auto tmp = File::getSpecialLocation (File::tempDirectory).getChildFile (File::createLegalFileName (app.engine.project.name) + ".batpack");
            if (r >= 10) runExport (tmp, peers[(size_t) (r - 10)].ip);
            else if (r == 1) prompt ("IP do celular", { { U ("Endere\xc3\xa7o mostrado em Ajustes no app do celular"), "192.168.", {} } }, [this, tmp] (StringArray v) { runExport (tmp, v[0].trim()); });
        });
    }
    void runExport (File dest, String ip)
    {
        if (exporting) return;
        exporting = true; exportB.setEnabled (false);
        auto project = app.engine.project;
        Component::SafePointer<MainComponent> safe (this);
        Thread::launch ([safe, project, dest, ip]
        {
            auto say = [safe] (String s) { MessageManager::callAsync ([safe, s] { if (safe != nullptr) safe->setStatus (s); }); };
            auto res = bt::exportPack (project, dest, [say] (float p, String s) { say (String ((int) (p * 100)) + "%  " + s); });
            if (res.ok && ip.isNotEmpty())
            {
                res = bt::sendPack (ip, dest, [say] (float p) { say ("Enviando " + String ((int) (p * 100)) + "%"); });
                dest.deleteFile();
            }
            MessageManager::callAsync ([safe, res, ip, dest]
            {
                if (safe == nullptr) return;
                safe->exporting = false; safe->exportB.setEnabled (true);
                if (res.ok) { safe->setStatus (ip.isNotEmpty() ? "Enviado para " + ip : "Pacote salvo: " + dest.getFileName()); }
                else { safe->setStatus ("Falha ao exportar"); message ("Exportar para o celular", res.message); }
            });
        });
    }

    App& app; Look look; TooltipWindow tooltips { this, 600 };
    SessionView session; SetlistPanel setlist; Viewport viewport;
    TempoEditor tempo; PadsPanel pads; ClickPanel click; MidiPanel midi; MidiAutoConnect midiConnect;
    TabbedComponent tabs { TabbedButtonBar::TabsAtTop };
    OwnedArray<Strip> trackStrips, busStrips;
    TextButton fileB { "Arquivo" }, linkB { "Link" }, followB { "Seguir" }, audioB { U ("\xc3\x81udio/MIDI") };
    IconButton playB { Icon::play }, stopB { Icon::stop }, clickB { Icon::metronome }, exportB { Icon::next, "Exportar p/ celular" };
    Label status;
    Rectangle<int> display, mixArea;
    std::unique_ptr<FileChooser> chooser;
    bool building = false, exporting = false; int shownOuts = 2;
};

//======================================================================================
class MainWindow : public DocumentWindow
{
public:
    MainWindow (App& a) : DocumentWindow ("Batuta", col::bg, DocumentWindow::allButtons), app (a)
    {
        setUsingNativeTitleBar (true);
        main = new MainComponent (a);
        setContentOwned (main, true);
        setResizable (true, true); setResizeLimits (1100, 700, 10000, 10000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }
    void closeButtonPressed() override
    {
        if (app.dirty) confirm ("Sair", U ("H\xc3\xa1 altera\xc3\xa7\xc3\xb5" "es n\xc3\xa3o salvas. Sair mesmo assim?"), [] { JUCEApplication::getInstance()->quit(); });
        else JUCEApplication::getInstance()->quit();
    }
    MainComponent* main = nullptr;
private:
    App& app;
};

class BatutaApp : public JUCEApplication
{
public:
    const String getApplicationName() override { return "Batuta"; }
    const String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const String& cmd) override
    {
        PropertiesFile::Options o; o.applicationName = "Batuta"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support"; o.folderName = "Batuta";
        props.setStorageParameters (o);
        app.reset (new App());
        auto saved = props.getUserSettings()->getXmlValue ("audio");
        app->dm.initialise (0, 64, saved.get(), true);
        app->dm.addAudioCallback (&app->engine);
        if (app->dm.getCurrentAudioDevice() == nullptr) { app->engine.prepare (48000.0, 512); fake.reset (new ui::FakeAudio (app->engine)); }
        window.reset (new MainWindow (*app));

        File f (cmd.unquoted());
        if (! f.existsAsFile()) f = File (props.getUserSettings()->getValue ("lastProject"));
        if (f.existsAsFile() && f.hasFileExtension ("batuta")) window->main->open (f);
        if (fake != nullptr) window->main->setStatus (U ("Sem dispositivo de \xc3\xa1udio: abra \xc3\x81udio/MIDI"));
    }
    void shutdown() override
    {
        if (app != nullptr)
        {
            if (auto xml = app->dm.createStateXml()) props.getUserSettings()->setValue ("audio", xml.get());
            props.getUserSettings()->setValue ("lastProject", app->projectFile.getFullPathName());
            props.saveIfNeeded();
            window = nullptr; fake = nullptr;
            app->dm.removeAudioCallback (&app->engine);
            app->dm.closeAudioDevice();
        }
        app = nullptr;
    }
    void systemRequestedQuit() override { if (window != nullptr) window->closeButtonPressed(); else quit(); }
private:
    ApplicationProperties props;
    std::unique_ptr<App> app;
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<ui::FakeAudio> fake;
};
}

START_JUCE_APPLICATION (desk::BatutaApp)
