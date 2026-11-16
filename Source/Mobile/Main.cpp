// MDS (Android) - modulo de palco portatil: split stereo fixo, mixer tatil, pads, setlist
#include "../UI/Common.h"

namespace mob
{
using namespace juce; using namespace ui;

// O Android filtra pacotes multicast/broadcast do Wi-Fi para economizar bateria; o Ableton Link
// e a busca do computador pela rede dependem deles, entao o app segura um MulticastLock.
static void acquireMulticastLock()
{
   #if JUCE_ANDROID
    auto* env = getEnv();
    auto context = getAppContext();
    if (env == nullptr || context.get() == nullptr) return;
    jclass contextClass = env->GetObjectClass (context.get());
    jmethodID getSystemService = env->GetMethodID (contextClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring serviceName = env->NewStringUTF ("wifi");
    jobject wifi = env->CallObjectMethod (context.get(), getSystemService, serviceName);
    if (wifi != nullptr)
    {
        jclass wifiClass = env->GetObjectClass (wifi);
        jmethodID create = env->GetMethodID (wifiClass, "createMulticastLock", "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$MulticastLock;");
        jstring tag = env->NewStringUTF ("mds");
        jobject lock = env->CallObjectMethod (wifi, create, tag);
        if (lock != nullptr)
        {
            jclass lockClass = env->GetObjectClass (lock);
            env->CallVoidMethod (lock, env->GetMethodID (lockClass, "setReferenceCounted", "(Z)V"), (jboolean) JNI_FALSE);
            env->CallVoidMethod (lock, env->GetMethodID (lockClass, "acquire", "()V"));
            env->NewGlobalRef (lock);     // mantido enquanto o app existir
        }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
   #endif
}

struct MApp
{
    AudioDeviceManager dm;
    bt::Engine engine;
    File dataDir, projectFile;
    bool locked = true;                       // trava de palco: acoes criticas pedem toque duplo
    std::function<void()> refreshAll;
    std::function<void (String)> toast;
    std::function<void (String, Component*)> showOverlay;    // assume a posse do componente

    bt::Song& song() { return engine.song(); }
    void save() { projectFile.getParentDirectory().createDirectory(); engine.project.save (projectFile); dataDir.getChildFile ("atual.txt").replaceWithText (projectFile.getFullPathName()); }
    void changed (bool structural) { if (structural) engine.reloadSong(); else engine.syncParams(); save(); }
    void selectSong (int i) { engine.selectSong (i); if (refreshAll) refreshAll(); }

    // toque duplo para acoes criticas quando a trava esta ligada
    bool guard (const String& id)
    {
        if (! locked) return true;
        const auto now = Time::getMillisecondCounter();
        if (lastGuard == id && now - lastGuardTime < 900) { lastGuard.clear(); return true; }
        lastGuard = id; lastGuardTime = now;
        if (toast) toast ("Toque de novo para confirmar");
        return false;
    }
    void loadProject (const File& f)
    {
        bt::Project p;
        if (! bt::Project::load (f, p)) { if (toast) toast (U ("N\xc3\xa3o foi poss\xc3\xadvel abrir a setlist")); return; }
        projectFile = f; engine.setProject (p); save(); if (refreshAll) refreshAll();
    }
    void importPackFile (const File& pack)
    {
        File pj;
        auto r = bt::importPack (pack, dataDir.getChildFile ("biblioteca"), pj);
        if (! r.ok) { message ("Pacote", r.message); return; }
        loadProject (pj);
        if (toast) toast ("Setlist carregada: " + engine.project.name);
    }
private:
    String lastGuard; uint32 lastGuardTime = 0;
};

//======================================================================================
// Lista generica de linhas tocaveis (ajustes e editor de musica)
class ActionList : public Component, private ListBoxModel
{
public:
    struct Row { String title; std::function<String()> value; std::function<void()> tap; bool header = false; };
    std::vector<Row> rows;
    ActionList() { list.setModel (this); list.setRowHeight (54); addAndMakeVisible (list); }
    void refresh() { list.updateContent(); list.repaint(); }
    void resized() override { list.setBounds (getLocalBounds()); }
private:
    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int row, Graphics& g, int w, int h, bool) override
    {
        if (row < 0 || row >= (int) rows.size()) return;
        auto& r = rows[(size_t) row];
        if (r.header)
        {
            g.setColour (col::accent); g.setFont (FontOptions (13.0f, Font::bold));
            g.drawFittedText (r.title, 14, 0, w - 28, h - 4, Justification::bottomLeft, 2);
            return;
        }
        g.setColour (col::panel); g.fillRoundedRectangle (6.0f, 2.0f, w - 12.0f, h - 4.0f, ui::radius + 1.0f);
        const String v = r.value ? r.value() : String();
        g.setColour (col::text); g.setFont (FontOptions (17.0f, Font::bold));
        g.drawFittedText (r.title, 18, 0, v.isEmpty() ? w - 36 : w * 55 / 100, h, Justification::centredLeft, 2);
        g.setColour (col::accent); g.setFont (FontOptions (16.0f));
        g.drawFittedText (v, w * 55 / 100 + 18, 0, w * 45 / 100 - 36, h, Justification::centredRight, 2);
    }
    void listBoxItemClicked (int row, const MouseEvent&) override { if (row >= 0 && row < (int) rows.size() && rows[(size_t) row].tap) { rows[(size_t) row].tap(); list.repaint(); } }
    ListBox list;
};

//======================================================================================
class StageScreen : public Component
{
public:
    explicit StageScreen (MApp& a) : app (a)
    {
        for (Component* c : std::initializer_list<Component*> { &prev, &next, &play, &stop, &pad, &click, &lock }) addAndMakeVisible (c);
        play.colour = col::play; stop.colour = col::stop; click.colour = col::accent; lock.colour = col::queued;
        pad.setClickingTogglesState (true); pad.setColour (TextButton::buttonOnColourId, col::link);
        play.onClick = [this] { app.engine.play(); };
        stop.onClick = [this] { if (! app.engine.playing.load() || app.guard ("stop")) app.engine.stop(); };
        prev.onClick = [this] { step (-1); }; next.onClick = [this] { step (1); };
        pad.onClick = [this] { app.engine.setAutoPad (pad.getToggleState()); };
        click.onClick = [this] { app.engine.project.clickOn = ! app.engine.project.clickOn; app.changed (false); };
        lock.onClick = [this] { app.locked = ! app.locked; if (app.toast) app.toast (app.locked ? "Trava de palco ligada: Stop e troca de musica pedem toque duplo" : "Trava de palco desligada"); };
    }
    void step (int d)
    {
        auto& e = app.engine;
        const int i = jlimit (0, (int) e.project.songs.size() - 1, e.songIndex + d);
        if (i == e.songIndex) return;
        if (e.playing.load() && ! app.guard (d > 0 ? "next" : "prev")) return;
        app.selectSong (i);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        auto top = r.removeFromTop (64);
        prev.setBounds (top.removeFromLeft (64)); next.setBounds (top.removeFromRight (64)); titleArea = top.reduced (8, 0);
        auto bottom = r.removeFromBottom (jmin (150, r.getHeight() / 4));
        play.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).reduced (4)); stop.setBounds (bottom.reduced (4));
        r.removeFromBottom (8);
        auto toggles = r.removeFromBottom (64);
        const int tw = toggles.getWidth() / 4;
        pad.setBounds (toggles.removeFromLeft (tw * 2).reduced (4)); click.setBounds (toggles.removeFromLeft (tw).reduced (4)); lock.setBounds (toggles.reduced (4));
        r.removeFromBottom (8);
        display = r;
    }
    void paint (Graphics& g) override
    {
        auto& e = app.engine; auto& s = app.song();
        g.fillAll (col::bg);
        g.setColour (col::dim); g.setFont (FontOptions (13.0f, Font::bold));
        g.drawText (U ("M\xc3\x9aSICA ") + String (e.songIndex + 1) + " DE " + String ((int) e.project.songs.size()), titleArea.removeFromTop (20), Justification::centred);
        g.setColour (col::text); g.setFont (FontOptions (24.0f, Font::bold));
        g.drawFittedText (s.name, titleArea, Justification::centred, 2);
        titleArea = Rectangle<int> (prev.getRight() + 8, prev.getY(), next.getX() - prev.getRight() - 16, prev.getHeight());

        auto d = display;
        g.setColour (col::panel); g.fillRoundedRectangle (d.toFloat(), ui::radius + 3.0f);
        const bool playing = e.playing.load(); const int bar = e.uiBar.load(), beat = e.uiBeatInBar.load(), num = jlimit (1, 16, e.uiNum.load());
        auto leds = d.removeFromBottom (jmin (70, d.getHeight() / 4)).reduced (14, 10);
        const float lw = (float) leds.getWidth() / (float) num;
        for (int i = 0; i < num; ++i)
        {
            auto l = Rectangle<float> (leds.getX() + i * lw, (float) leds.getY(), lw, (float) leds.getHeight()).reduced (3.0f);
            g.setColour (playing && i == beat ? (i == 0 ? col::accent : col::play) : col::raised); g.fillRoundedRectangle (l, ui::radius + 1.0f);
        }
        auto info = d.removeFromBottom (30);
        g.setColour (col::dim); g.setFont (FontOptions (15.0f));
        String line = String (e.uiNum.load()) + "/" + String (e.uiDen.load());
        if (s.padKey >= 0) line << U ("   \xc2\xb7   Tom ") << bt::keyNames()[s.padKey];
        if (! s.scenes.empty()) line << U ("   \xc2\xb7   com VS");
        if (e.isLinkEnabled()) line << U ("   \xc2\xb7   Link ") << String (e.linkPeers());
        g.drawText (line, info, Justification::centred);
        auto left = d.removeFromLeft (d.getWidth() / 2);
        auto big = [&] (Rectangle<int> r, const String& label, const String& value, Colour c)
        {
            g.setColour (col::dim); g.setFont (FontOptions (13.0f, Font::bold)); g.drawText (label, r.removeFromTop (30).withTrimmedTop (12), Justification::centred);
            g.setColour (c); g.setFont (FontOptions (Font::getDefaultMonospacedFontName(), jmin (72.0f, r.getHeight() * 0.7f, r.getWidth() * 0.34f), Font::bold));
            g.drawText (value, r, Justification::centred);
        };
        big (left, "BPM", String (roundToInt (e.uiBpm.load())), col::accent);
        big (d, bar < 0 && playing ? "CONTAGEM" : "COMPASSO", playing ? (bar < 0 ? String (beat + 1) : String (bar + 1) + "." + String (beat + 1)) : U ("\xe2\x80\x94"), bar < 0 && playing ? col::queued : col::text);
    }
    void tick()
    {
        auto& e = app.engine;
        play.lit = e.playing.load(); click.lit = e.project.clickOn; lock.lit = app.locked;
        pad.setToggleState (e.autoPad.load(), dontSendNotification);
        pad.setButtonText (app.song().padKey >= 0 ? "Auto-PAD  " + bt::keyNames()[app.song().padKey] : "Auto-PAD  (sem tom)");
        repaint();
    }
private:
    MApp& app;
    IconButton prev { Icon::prev }, next { Icon::next }, play { Icon::play }, stop { Icon::stop }, click { Icon::metronome }, lock { Icon::lock };
    TextButton pad { "Auto-PAD" };
    Rectangle<int> titleArea, display;
};

//======================================================================================
class MixerScreen : public Component
{
public:
    explicit MixerScreen (MApp& a) : app (a) { rebuild(); }
    struct Row { String name, side; Colour colour; std::unique_ptr<Fader> fader; std::unique_ptr<TextButton> mute; int bus = -1, track = -1; };
    void rebuild()
    {
        rows.clear();
        auto add = [this] (const String& name, const String& side, Colour c, int bus, int track, float db, bool muted)
        {
            auto r = std::make_unique<Row>(); r->name = name; r->side = side; r->colour = c; r->bus = bus; r->track = track;
            r->fader.reset (new Fader (false, c)); r->fader->setValue (db, dontSendNotification);
            r->mute.reset (new TextButton ("M")); r->mute->setClickingTogglesState (true); r->mute->setColour (TextButton::buttonOnColourId, col::stop);
            r->mute->setToggleState (muted, dontSendNotification);
            auto* raw = r.get();
            auto apply = [this, raw]
            {
                const float db2 = (float) raw->fader->getValue(); const bool m = raw->mute->getToggleState();
                if (raw->bus >= 0) { app.engine.project.buses[raw->bus].gainDb = db2; app.engine.project.buses[raw->bus].mute = m; }
                else if (raw->track < (int) app.song().tracks.size()) { app.song().tracks[(size_t) raw->track].gainDb = db2; app.song().tracks[(size_t) raw->track].mute = m; }
                app.engine.syncParams(); repaint();
            };
            r->fader->onValueChange = apply; r->mute->onClick = [this, apply] { apply(); app.save(); };
            r->fader->onDragEnd = [this] { app.save(); };
            addAndMakeVisible (*r->fader); addAndMakeVisible (*r->mute);
            rows.push_back (std::move (r));
        };
        auto& p = app.engine.project;
        const char* names[bt::numBuses] = { "Click", "Subdivis\xc3\xa3o", "Voz", "Auto-PAD", "One-shots" };
        for (int b = 0; b < bt::numBuses; ++b) add (U (names[b]), b <= bt::busVoice ? "L" : "R", b <= bt::busVoice ? col::accent : col::link, b, -1, p.buses[b].gainDb, p.buses[b].mute);
        auto& s = app.song();
        for (int t = 0; t < (int) s.tracks.size(); ++t) add (s.tracks[(size_t) t].name, s.tracks[(size_t) t].isCue ? "L" : "R", s.tracks[(size_t) t].isCue ? col::cue : col::play, -1, t, s.tracks[(size_t) t].gainDb, s.tracks[(size_t) t].mute);
        resized(); repaint();
    }
    void setBus (int b, float norm) { if (b >= 0 && b < (int) rows.size()) rows[(size_t) b]->fader->setFromNormalised (norm); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10); r.removeFromTop (34);
        const int h = jmin (96, r.getHeight() / jmax (1, (int) rows.size()));
        for (auto& row : rows)
        {
            auto a = r.removeFromTop (h).reduced (0, 3);
            a.removeFromTop (22);
            row->mute->setBounds (a.removeFromRight (54).reduced (2));
            row->fader->setBounds (a.reduced (4, 0));
        }
    }
    void paint (Graphics& g) override
    {
        g.fillAll (col::bg);
        g.setColour (col::dim); g.setFont (FontOptions (13.0f, Font::bold));
        g.drawText (U ("CANAL L = CLICK E GUIAS   \xc2\xb7   CANAL R = VS, PADS E EFEITOS"), getLocalBounds().reduced (10).removeFromTop (28), Justification::centredLeft);
        for (auto& row : rows)
        {
            auto a = row->fader->getBounds().withY (row->fader->getY() - 22).withHeight (22);
            g.setColour (row->colour); g.fillRoundedRectangle (Rectangle<float> ((float) a.getX(), (float) a.getY() + 3, 22.0f, 17.0f), 4.0f);
            g.setColour (col::bg); g.setFont (FontOptions (12.0f, Font::bold)); g.drawText (row->side, a.getX(), a.getY() + 3, 22, 17, Justification::centred);
            g.setColour (col::text); g.setFont (FontOptions (17.0f, Font::bold)); g.drawText (row->name, a.withTrimmedLeft (30), Justification::centredLeft);
            g.setColour (col::dim); g.setFont (FontOptions (14.0f)); g.drawText (String (row->fader->getValue(), 1) + " dB", a, Justification::centredRight);
        }
    }
private:
    MApp& app; std::vector<std::unique_ptr<Row>> rows;
};

//======================================================================================
class PadsScreen : public Component
{
public:
    static constexpr int count = 12;
    explicit PadsScreen (MApp& a) : app (a)
    {
        addAndMakeVisible (edit); edit.setClickingTogglesState (true);
        edit.onClick = [this] { repaint(); };
    }
    void resized() override { auto r = getLocalBounds().reduced (10); edit.setBounds (r.removeFromTop (44).removeFromRight (150)); r.removeFromTop (8); grid = r; }
    Rectangle<int> cell (int i) const { const int w = grid.getWidth() / 3, h = grid.getHeight() / 4; return Rectangle<int> (grid.getX() + (i % 3) * w, grid.getY() + (i / 3) * h, w, h).reduced (5); }
    void paint (Graphics& g) override
    {
        g.fillAll (col::bg);
        g.setColour (col::dim); g.setFont (FontOptions (13.0f, Font::bold));
        g.drawText (edit.getToggleState() ? String ("TOQUE EM UM PAD PARA TROCAR O SOM") : U ("ONE-SHOTS  \xc2\xb7  CANAL R"), 12, 10, getWidth() - 170, 44, Justification::centredLeft);
        for (int i = 0; i < count; ++i)
        {
            auto& d = app.engine.project.pads[i];
            const bool has = d.file.isNotEmpty() || d.builtin >= 0;
            const bool flash = Time::getMillisecondCounter() - flashTime[i] < 150;
            auto c = cell (i).toFloat();
            g.setColour (flash ? col::accent : (has ? col::raised : col::panel)); g.fillRoundedRectangle (c, ui::radius + 3.0f);
            g.setColour (edit.getToggleState() ? col::queued : col::line); g.drawRoundedRectangle (c, 10.0f, edit.getToggleState() ? 2.0f : 1.0f);
            g.setColour (flash ? col::bg : col::dim); g.setFont (FontOptions (13.0f)); g.drawText (String (i + 1), c.reduced (10, 8), Justification::topLeft);
            g.setColour (flash ? col::bg : (has ? col::text : col::dim)); g.setFont (FontOptions (19.0f, Font::bold));
            g.drawFittedText (has ? d.name : "vazio", c.toNearestInt().reduced (6), Justification::centred, 2);
        }
    }
    void mouseDown (const MouseEvent& e) override
    {
        for (int i = 0; i < count; ++i)
        {
            if (! cell (i).contains (e.getPosition())) continue;
            if (edit.getToggleState()) { menu (i); return; }
            app.engine.triggerPad (i); flashTime[i] = Time::getMillisecondCounter(); repaint();
        }
    }
    void tick()
    {
        for (int i = 0; i < count; ++i) { const int f = app.engine.uiPadFlash[i].load(); if (f != lastFlash[i]) { lastFlash[i] = f; flashTime[i] = Time::getMillisecondCounter(); } }
        repaint();
    }
private:
    void menu (int i)
    {
        PopupMenu m;
        for (int b = 0; b < bt::builtinPadNames().size(); ++b) m.addItem (100 + b, bt::builtinPadNames()[b]);
        m.addSeparator(); m.addItem (1, U ("Arquivo de \xc3\xa1udio do aparelho\xe2\x80\xa6")); m.addItem (2, "Deixar vazio");
        m.showMenuAsync (PopupMenu::Options().withMinimumWidth (240).withStandardItemHeight (48), [this, i] (int r)
        {
            auto& d = app.engine.project.pads[i];
            auto done = [this] { app.engine.rebuildSamples(); app.save(); repaint(); };
            if (r >= 100) { d = {}; d.builtin = r - 100; d.name = bt::builtinPadNames()[r - 100]; done(); }
            else if (r == 2) { d = {}; done(); }
            else if (r == 1)
            {
                chooser.reset (new FileChooser (U ("Escolha um \xc3\xa1udio curto"), {}, "*"));
                chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this, i, done] (const FileChooser& fc)
                {
                    auto url = fc.getURLResult(); if (url.isEmpty()) return;
                    auto name = url.getFileName().isNotEmpty() ? URL::removeEscapeChars (url.getFileName()) : String ("pad.wav");
                    auto dest = app.dataDir.getChildFile ("pads").getChildFile (String (i + 1) + "_" + File::createLegalFileName (name));
                    dest.getParentDirectory().createDirectory(); dest.deleteFile();
                    if (auto in = url.createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress)))
                    {
                        FileOutputStream out (dest);
                        if (out.openedOk()) out.writeFromInputStream (*in, 20 * 1024 * 1024);
                    }
                    if (dest.getSize() == 0) return;
                    auto& d2 = app.engine.project.pads[i]; d2 = {}; d2.file = dest.getFullPathName(); d2.name = dest.getFileNameWithoutExtension().fromFirstOccurrenceOf ("_", false, false);
                    done();
                });
            }
        });
    }
    MApp& app; TextButton edit { "Editar pads" }; Rectangle<int> grid;
    uint32 flashTime[count] {}; int lastFlash[count] {};
    std::unique_ptr<FileChooser> chooser;
};

//======================================================================================
// Setlist + editor de musica com programacao do metronomo em blocos
class SongsScreen : public Component
{
public:
    explicit SongsScreen (MApp& a) : app (a)
    {
        addAndMakeVisible (list);
        for (auto* b : { &add, &del, &up, &down }) addAndMakeVisible (*b);
        add.onClick = [this] { auto& songs = app.engine.project.songs; bt::Song s; s.name = U ("M\xc3\xbasica ") + String ((int) songs.size() + 1); songs.push_back (s); app.selectSong ((int) songs.size() - 1); app.save(); };
        del.onClick = [this]
        {
            auto& songs = app.engine.project.songs; if (songs.size() <= 1) return;
            confirm (U ("Excluir m\xc3\xbasica"), U ("Excluir \"") + app.song().name + "\"?", [this] { auto& sg = app.engine.project.songs; const int i = app.engine.songIndex; sg.erase (sg.begin() + i); app.selectSong (jmin (i, (int) sg.size() - 1)); app.save(); });
        };
        up.onClick = [this] { move (-1); }; down.onClick = [this] { move (1); };
        rebuild();
    }
    void move (int d)
    {
        auto& songs = app.engine.project.songs; const int i = app.engine.songIndex, j = i + d;
        if (j < 0 || j >= (int) songs.size() || app.engine.playing.load()) return;
        std::swap (songs[(size_t) i], songs[(size_t) j]); app.selectSong (j); app.save();
    }
    static String blockText (const bt::TempoBlock& b)
    {
        String s = String (b.bars) + (b.bars == 1 ? " compasso" : " compassos") + U (" \xc2\xb7 ") + String (b.num) + "/" + String (b.den) + U (" \xc2\xb7 ");
        if (std::abs (b.bpm0 - b.bpm1) < 0.01) s << String (b.bpm0, b.bpm0 == std::floor (b.bpm0) ? 0 : 1) << " BPM";
        else s << String (roundToInt (b.bpm0)) << U (" \xe2\x86\x92 ") << String (roundToInt (b.bpm1)) << " BPM";
        return s;
    }
    void rebuild()
    {
        auto& rows = list.rows; rows.clear();
        auto& songs = app.engine.project.songs;
        rows.push_back ({ "Setlist: " + app.engine.project.name, {}, {}, true });
        for (int i = 0; i < (int) songs.size(); ++i)
            rows.push_back ({ String (i + 1) + ".  " + songs[(size_t) i].name, [this, i] { return i == app.engine.songIndex ? String ("selecionada") : String(); },
                              [this, i] { if (i != app.engine.songIndex) { if (app.engine.playing.load() && ! app.guard ("song" + String (i))) return; app.selectSong (i); } } });

        rows.push_back ({ U ("M\xc3\xbasica selecionada"), {}, {}, true });
        rows.push_back ({ "Nome", [this] { return app.song().name; }, [this] { prompt (U ("Nome da m\xc3\xbasica"), { { "Nome", app.song().name, {} } }, [this] (StringArray v) { app.song().name = v[0]; app.save(); rebuild(); }); } });
        rows.push_back ({ "Tom do Auto-PAD", [this] { return app.song().padKey < 0 ? String ("sem pad") : bt::keyNames()[app.song().padKey]; }, [this]
        {
            PopupMenu m; m.addItem (1, "Sem pad"); for (int k = 0; k < 12; ++k) m.addItem (k + 2, bt::keyNames()[k]);
            m.showMenuAsync (PopupMenu::Options().withStandardItemHeight (44).withMinimumWidth (200), [this] (int r) { if (r == 0) return; app.song().padKey = r - 2; app.save(); app.engine.selectSong (app.engine.songIndex); list.refresh(); });
        } });
        rows.push_back ({ "Contagem inicial", [this] { const int c = app.song().countInBars; return c == 0 ? String ("sem contagem") : String (c) + (c == 1 ? " compasso" : " compassos"); },
                          [this] { static const int v[4] = { 0, 1, 2, 4 }; int i = 0; for (int k = 0; k < 4; ++k) if (v[k] == app.song().countInBars) i = k; app.song().countInBars = v[(i + 1) % 4]; app.changed (false); } });
        rows.push_back ({ U ("Subdivis\xc3\xa3o do click"), [this] { return StringArray { "nenhuma", "colcheias", "tercinas", "semicolcheias" }[app.song().subdiv - 1]; },
                          [this] { app.song().subdiv = app.song().subdiv % 4 + 1; app.changed (false); } });

        rows.push_back ({ U ("Metr\xc3\xb4nomo em blocos (toque para editar)"), {}, {}, true });
        auto& tempo = app.song().tempo;
        for (int i = 0; i < (int) tempo.size(); ++i)
        {
            rows.push_back ({ String (i + 1) + ".  " + blockText (tempo[(size_t) i]), {}, [this, i] { editBlock (i); } });
            rows.push_back ({ U ("      Padr\xc3\xa3o do click"), [this, i] { return patternText (app.song().tempo[(size_t) i]); }, [this, i] { editPattern (i); } });
        }
        rows.push_back ({ U ("+  Adicionar bloco"), {}, [this] { auto& t = app.song().tempo; auto b = t.back(); b.bpm0 = b.bpm1; t.push_back (b); app.changed (false); rebuild(); editBlock ((int) t.size() - 1); } });
        if (tempo.size() > 1) rows.push_back ({ U ("\xe2\x88\x92  Remover o \xc3\xbaltimo bloco"), {}, [this] { auto& t = app.song().tempo; if (t.size() > 1) { t.pop_back(); app.changed (false); rebuild(); } } });
        rows.push_back ({ U ("Depois do \xc3\xbaltimo bloco o andamento final continua at\xc3\xa9 voc\xc3\xaa parar."), {}, {}, true });
        list.refresh();
    }
    static String patternText (const bt::TempoBlock& b)
    {
        String s; const char* g[4] = { "\xc2\xb7", "o", "O", "\xe2\x97\x8f" };
        for (auto c : b.fullPattern()) s << U (g[jlimit (0, 3, (int) (c - '0'))]) << " ";
        return s.trim();
    }
    struct PatternEditor : Component
    {
        PatternEditor() { addAndMakeVisible (strip); }
        void resized() override { strip.setBounds (getLocalBounds().reduced (14).withTrimmedTop (70).withHeight (jmin (150, getHeight() / 3))); }
        void paint (Graphics& g) override
        {
            g.fillAll (col::bg); g.setColour (col::dim); g.setFont (FontOptions (15.0f));
            g.drawFittedText (U ("Toque em cada tempo do compasso para alternar: forte \xe2\x86\x92 normal \xe2\x86\x92 fraco \xe2\x86\x92 mudo."), getLocalBounds().reduced (14).removeFromTop (60), Justification::centredLeft, 3);
        }
        PatternStrip strip;
    };
    void editPattern (int i)
    {
        auto* ed = new PatternEditor();
        ed->strip.setPattern (app.song().tempo[(size_t) i].fullPattern());
        ed->strip.onChange = [this, i] (String p) { auto& t = app.song().tempo; if (i < (int) t.size()) { t[(size_t) i].pattern = p; app.changed (false); list.refresh(); } };
        if (app.showOverlay) app.showOverlay (U ("Padr\xc3\xa3o do click \xe2\x80\x94 bloco ") + String (i + 1), ed);
    }
    void editBlock (int i)
    {
        auto& b = app.song().tempo[(size_t) i];
        prompt ("Bloco " + String (i + 1), { { "Quantos compassos", String (b.bars), {} }, { U ("F\xc3\xb3rmula de compasso (ex.: 4/4, 7/8)"), String (b.num) + "/" + String (b.den), {} },
                                             { U ("BPM no in\xc3\xad" "cio"), String (b.bpm0), {} }, { U ("BPM no fim (igual ao in\xc3\xad" "cio = andamento fixo)"), String (b.bpm1), {} } },
                [this, i] (StringArray v)
                {
                    auto& t = app.song().tempo; if (i >= (int) t.size()) return;
                    auto& x = t[(size_t) i];
                    x.bars = jlimit (1, 999, v[0].getIntValue());
                    const int n = v[1].upToFirstOccurrenceOf ("/", false, false).getIntValue(), dn = v[1].fromFirstOccurrenceOf ("/", false, false).getIntValue();
                    if (n >= 1 && n <= 32 && (dn == 2 || dn == 4 || dn == 8 || dn == 16)) { x.num = n; x.den = dn; }
                    x.bpm0 = jlimit (20.0, 400.0, v[2].replaceCharacter (',', '.').getDoubleValue());
                    x.bpm1 = jlimit (20.0, 400.0, v[3].replaceCharacter (',', '.').getDoubleValue());
                    x.pattern = x.fullPattern().substring (0, x.num);
                    app.changed (false); rebuild();
                });
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        auto row = r.removeFromBottom (56);
        const int w = row.getWidth() / 4;
        for (auto* b : { &add, &del, &up, &down }) b->setBounds (row.removeFromLeft (w).reduced (3));
        list.setBounds (r);
    }
    void paint (Graphics& g) override { g.fillAll (col::bg); }
private:
    MApp& app; ActionList list;
    IconButton add { Icon::plus, "Nova" }, del { Icon::minus, "Excluir" }, up { Icon::up, "Subir" }, down { Icon::down, "Descer" };
};

//======================================================================================
class SettingsScreen : public Component
{
public:
    SettingsScreen (MApp& a, MidiAutoConnect& mc) : app (a), midiConnect (mc)
    {
        addAndMakeVisible (list);
        auto& rows = list.rows; auto& e = app.engine;
        rows.push_back ({ "Sincronismo", {}, {}, true });
        rows.push_back ({ "Ableton Link", [&e] { return e.isLinkEnabled() ? "ligado  (" + String (e.linkPeers()) + " aparelho(s))" : String ("desligado"); }, [&e] { e.setLinkEnabled (! e.isLinkEnabled()); } });
        rows.push_back ({ "Quem dita o andamento", [&e] { return e.linkFollow.load() ? String ("o outro aparelho (seguir)") : String ("este celular"); }, [&e] { e.setLinkFollow (! e.linkFollow.load()); } });
        rows.push_back ({ U ("Metr\xc3\xb4nomo"), {}, {}, true });
        rows.push_back ({ "Timbre do click", [&e] { return bt::clickSoundNames()[e.project.clickSound]; }, [this, &e] { e.project.clickSound = (e.project.clickSound + 1) % 4; e.rebuildSamples(); app.save(); } });
        rows.push_back ({ "Voz de contagem", [&e] { return StringArray { "desligada", U ("s\xc3\xb3 na contagem inicial"), "sempre" }[e.project.voiceMode]; }, [this, &e] { e.project.voiceMode = (e.project.voiceMode + 1) % 3; app.changed (false); } });
        rows.push_back ({ "Idioma da voz", [&e] { return e.project.voiceLang == 1 ? U ("ingl\xc3\xaas") : U ("portugu\xc3\xaas"); }, [this, &e] { e.project.voiceLang = 1 - e.project.voiceLang; e.rebuildSamples(); app.save(); } });
        rows.push_back ({ U ("Pedais e controladores"), {}, {}, true });
        rows.push_back ({ U ("Parear pedal Bluetooth MIDI\xe2\x80\xa6"), {}, []
        {
            RuntimePermissions::request (RuntimePermissions::bluetoothMidi, [] (bool ok)
            {
                if (ok) BluetoothMidiDevicePairingDialogue::open();
                else message ("Bluetooth", U ("Permita o acesso ao Bluetooth para parear o pedal."));
            });
        } });
        rows.push_back ({ "Controladores conectados", [this] { return midiConnect.names.isEmpty() ? String ("nenhum") : midiConnect.names.joinIntoString (", "); }, {} });
        rows.push_back ({ U ("Mapear bot\xc3\xb5" "es (MIDI Learn)\xe2\x80\xa6"), {}, [this] { if (onOpenMidi) onOpenMidi(); } });
        rows.push_back ({ "Setlists", {}, {}, true });
        rows.push_back ({ "Receber por Wi-Fi", [] { return "IP deste celular: " + bt::localIpAddress(); }, {} });
        rows.push_back ({ U ("Abrir pacote (.mdspack) do aparelho ou da nuvem\xe2\x80\xa6"), {}, [this]
        {
            chooser.reset (new FileChooser ("Escolha o pacote .mdspack", {}, "*"));
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
            {
                auto url = fc.getURLResult(); if (url.isEmpty()) return;
                auto tmp = app.dataDir.getChildFile ("recebidos").getChildFile ("importado.mdspack");
                tmp.getParentDirectory().createDirectory(); tmp.deleteFile();
                String name = URL::removeEscapeChars (url.getFileName());
                if (auto in = url.createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress)))
                {
                    FileOutputStream out (tmp);
                    if (out.openedOk()) out.writeFromInputStream (*in, -1);
                }
                if (name.endsWithIgnoreCase (".mdspack")) { auto named = tmp.getSiblingFile (File::createLegalFileName (name)); named.deleteFile(); if (tmp.moveFileTo (named)) tmp = named; }
                app.importPackFile (tmp);
            });
        } });
        rows.push_back ({ U ("Nova setlist vazia neste celular"), {}, [this]
        {
            confirm ("Nova setlist", U ("Criar uma setlist vazia? A setlist atual continua guardada no aparelho."), [this]
            {
                auto f = app.dataDir.getChildFile ("local").getChildFile ("setlist_" + String (Time::currentTimeMillis()) + ".json");
                bt::Project p; p.name = "Setlist do celular"; for (int b = 0; b < bt::numBuses; ++b) { p.buses[b].outL = b <= bt::busVoice ? 0 : 1; p.buses[b].outR = -1; }
                f.getParentDirectory().createDirectory(); p.save (f); app.loadProject (f);
            });
        } });
        rows.push_back ({ U ("MDS 0.2  \xc2\xb7  software livre (GPLv3)"), {}, {}, true });
        list.refresh();
    }
    void resized() override { list.setBounds (getLocalBounds().reduced (6)); }
    void paint (Graphics& g) override { g.fillAll (col::bg); }
    void tick() { if (isShowing() && ++n % 15 == 0) list.repaint(); }
    std::function<void()> onOpenMidi;
private:
    MApp& app; MidiAutoConnect& midiConnect; ActionList list; int n = 0;
    std::unique_ptr<FileChooser> chooser;
};

//======================================================================================
class Root : public Component, private Timer
{
public:
    explicit Root (MApp& a) : app (a), midiConnect (a.dm, a.engine), stage (a), mixer (a), pads (a), songs (a), settings (a, midiConnect)
    {
        look.scale = 1.15f;
        setLookAndFeel (&look); LookAndFeel::setDefaultLookAndFeel (&look);
        Component* screens[5] = { &stage, &mixer, &pads, &songs, &settings };
        const char* names[5] = { "Palco", "Mixer", "Pads", "M\xc3\xbasicas", "Ajustes" };
        for (int i = 0; i < 5; ++i)
        {
            addChildComponent (screens[i]);
            auto* b = tabs.add (new TextButton (U (names[i])));
            b->setRadioGroupId (1); b->setClickingTogglesState (true);
            b->onClick = [this, i] { show (i); };
            addAndMakeVisible (b);
        }
        addChildComponent (overlay);
        app.showOverlay = [this] (String title, Component* c)
        {
            overlayContent.reset (c);
            overlay.title = title; overlay.panel = c; overlay.addAndMakeVisible (*c);
            overlay.setVisible (true); overlay.toFront (false); resized(); overlay.resized(); overlay.repaint();
        };
        settings.onOpenMidi = [this] { app.showOverlay ("Mapear controles MIDI", new MidiPanel (app.engine, [this] { app.save(); }, true, 52)); };
        overlay.close.onClick = [this] { closeOverlay(); };
        app.refreshAll = [this] { mixer.rebuild(); songs.rebuild(); repaint(); };
        app.toast = [this] (String s) { toastText = s; toastTime = Time::getMillisecondCounter(); repaint(); };
        app.engine.onUiAction = [this] (int action, int index, float v)
        {
            if (action == bt::actNextSong) stage.step (1); else if (action == bt::actPrevSong) stage.step (-1);
            else if (action == bt::actClick) { app.engine.project.clickOn = ! app.engine.project.clickOn; app.changed (false); }
            else if (action == bt::actBusFader) mixer.setBus (index, v);
        };
        tabs[0]->setToggleState (true, dontSendNotification); show (0);
        startTimerHz (30);
        setSize (400, 800);
    }
    ~Root() override { app.refreshAll = nullptr; app.toast = nullptr; app.showOverlay = nullptr; app.engine.onUiAction = nullptr; LookAndFeel::setDefaultLookAndFeel (nullptr); setLookAndFeel (nullptr); }

    bool closeOverlay()
    {
        if (! overlay.isVisible()) return false;
        overlay.setVisible (false); overlay.panel = nullptr; overlayContent.reset(); return true;
    }
    void show (int i)
    {
        Component* screens[5] = { &stage, &mixer, &pads, &songs, &settings };
        for (int k = 0; k < 5; ++k) screens[k]->setVisible (k == i);
        if (i == 1) mixer.rebuild(); if (i == 3) songs.rebuild();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        if (auto* d = Desktop::getInstance().getDisplays().getPrimaryDisplay()) { r.removeFromTop (d->safeAreaInsets.getTop()); r.removeFromBottom (d->safeAreaInsets.getBottom()); }
        auto bar = r.removeFromBottom (62);
        const int w = bar.getWidth() / 5;
        for (auto* b : tabs) b->setBounds (bar.removeFromLeft (w).reduced (2, 4));
        for (Component* c : std::initializer_list<Component*> { &stage, &mixer, &pads, &songs, &settings, &overlay }) c->setBounds (r);
    }
    void paint (Graphics& g) override { g.fillAll (col::bg); }
    void paintOverChildren (Graphics& g) override
    {
        if (toastText.isEmpty() || Time::getMillisecondCounter() - toastTime > 2600) return;
        auto r = getLocalBounds().reduced (16).withTrimmedTop (84).removeFromTop (60);
        g.setColour (col::queued); g.fillRoundedRectangle (r.toFloat(), ui::radius + 3.0f);
        g.setColour (col::bg); g.setFont (FontOptions (17.0f, Font::bold)); g.drawFittedText (toastText, r.reduced (10, 4), Justification::centred, 2);
    }
private:
    struct Overlay : Component
    {
        Overlay() { addAndMakeVisible (close); }
        void paint (Graphics& g) override { g.fillAll (col::bg); g.setColour (col::text); g.setFont (FontOptions (18.0f, Font::bold)); g.drawFittedText (title, 14, 0, getWidth() - 140, 56, Justification::centredLeft, 2); }
        void resized() override { auto r = getLocalBounds(); close.setBounds (r.removeFromTop (56).removeFromRight (120).reduced (6)); if (panel != nullptr) panel->setBounds (r); }
        TextButton close { "Concluir" }; Component* panel = nullptr; String title;
    };
    void timerCallback() override
    {
        if (stage.isVisible()) stage.tick();
        if (pads.isVisible()) pads.tick();
        settings.tick();
        if (toastText.isNotEmpty() && Time::getMillisecondCounter() - toastTime > 2600) { toastText.clear(); repaint(); }
    }
    MApp& app; Look look; MidiAutoConnect midiConnect;
    StageScreen stage; MixerScreen mixer; PadsScreen pads; SongsScreen songs; SettingsScreen settings;
    OwnedArray<TextButton> tabs; Overlay overlay; std::unique_ptr<Component> overlayContent;
    String toastText; uint32 toastTime = 0;
};

//======================================================================================
class MobileWindow : public DocumentWindow
{
public:
    explicit MobileWindow (MApp& a) : DocumentWindow ("MDS", col::bg, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        root = new Root (a);
        setContentOwned (root, true);
       #if JUCE_ANDROID || JUCE_IOS
        setFullScreen (true);
       #else
        setResizable (true, true); centreWithSize (getWidth(), getHeight());
       #endif
        setVisible (true);
    }
    void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    Root* root = nullptr;
};

class MDSMobileApp : public JUCEApplication
{
public:
    const String getApplicationName() override { return "MDS"; }
    const String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const String&) override
    {
        app.reset (new MApp());
        app->engine.forceSplit = true;
        app->dataDir = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("MDS");
        app->dataDir.createDirectory();
        acquireMulticastLock();
        Desktop::getInstance().setScreenSaverEnabled (false);          // a tela nunca apaga
        Desktop::getInstance().setOrientationsEnabled (Desktop::upright);

        app->dm.initialise (0, 2, nullptr, true);
        app->dm.addAudioCallback (&app->engine);
        if (app->dm.getCurrentAudioDevice() == nullptr) { app->engine.prepare (48000.0, 512); fake.reset (new ui::FakeAudio (app->engine)); }

        File last (app->dataDir.getChildFile ("atual.txt").loadFileAsString().trim());
        bt::Project p;
        if (last.existsAsFile() && bt::Project::load (last, p)) app->projectFile = last;
        else
        {
            p = bt::Project(); p.name = "Setlist do celular";
            app->projectFile = app->dataDir.getChildFile ("local").getChildFile ("setlist.json");
        }
        app->engine.setProject (p);
        window.reset (new MobileWindow (*app));

        receiver.reset (new bt::PackReceiver (SystemStats::getDeviceDescription().isNotEmpty() ? SystemStats::getDeviceDescription() : String ("Celular"), app->dataDir.getChildFile ("recebidos")));
        receiver->onReceived = [this] (File f) { if (app != nullptr) app->importPackFile (f); };
        receiver->onProgress = [this] (float pr) { if (app != nullptr && app->toast) app->toast ("Recebendo setlist: " + String ((int) (pr * 100)) + "%"); };
    }
    void shutdown() override
    {
        receiver = nullptr; fake = nullptr;
        if (app != nullptr) { app->save(); window = nullptr; app->dm.removeAudioCallback (&app->engine); app->dm.closeAudioDevice(); }
        app = nullptr;
    }
    // Botao "voltar" do Android e fechar o app: exigem toque duplo
    bool backButtonPressed() override
    {
        if (window == nullptr || ! window->root->closeOverlay()) requestQuit();
        return true;
    }
    void systemRequestedQuit() override { requestQuit(); }
    void requestQuit()
    {
        const auto now = Time::getMillisecondCounter();
        if (now - lastQuit < 1500) { quit(); return; }
        lastQuit = now;
        if (app != nullptr && app->toast) app->toast ("Pressione de novo para sair do MDS");
    }
    void suspended() override { if (app != nullptr) app->save(); }
private:
    std::unique_ptr<MApp> app;
    std::unique_ptr<MobileWindow> window;
    std::unique_ptr<bt::PackReceiver> receiver;
    std::unique_ptr<ui::FakeAudio> fake;
    uint32 lastQuit = 0;
};
}

START_JUCE_APPLICATION (mob::MDSMobileApp)
