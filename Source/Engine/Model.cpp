#include "Model.h"
#include "SoundIndex.h"

namespace bt
{
using namespace juce;

const StringArray& keyNames()
{
    static StringArray a { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return a;
}
int numCues() { return kNumCues; }
int cueIndex (const String& id)
{
    for (int i = 0; i < kNumCues; ++i) if (id == kCues[i].id) return i;
    return -1;
}
String cueId (int i) { return i >= 0 && i < kNumCues ? String (kCues[i].id) : String(); }
String cueLabel (int i, int lang) { return i >= 0 && i < kNumCues ? String::fromUTF8 (lang == 1 ? kCues[i].en : kCues[i].pt) : String(); }
int cueGroup (int i) { return i >= 0 && i < kNumCues ? kCues[i].group : 0; }
StringArray clickKitNames() { StringArray a; for (int i = 0; i < kNumClickKits; ++i) a.add (kClickKits[i]); return a; }

// guias gravadas por versoes anteriores (nomes em portugues) -> identificadores atuais
static String migrateCue (const String& c)
{
    static const char* old[][2] = { { "Intro", "intro" }, { "Verso", "verse" }, { "Pre-refrao", "pre_chorus" }, { "Refrao", "chorus" },
                                    { "Ponte", "bridge" }, { "Solo", "solo" }, { "Interludio", "interlude" }, { "Final", "ending" } };
    for (auto& o : old) if (c == o[0]) return o[1];
    return c;
}

const StringArray& builtinPadNames()
{
    static StringArray a { "Shaker", "Palma", "Sub drop", "Riser", "Impacto", "Bumbo", "Pandeiro", "Swell" };
    return a;
}
String actionName (int action, int index)
{
    switch (action)
    {
        case actPlay:       return "Play";
        case actStop:       return "Stop";
        case actToggle:     return "Play/Stop";
        case actNextScene:  return String::fromUTF8 ("Pr\xc3\xb3xima cena");
        case actPrevScene:  return "Cena anterior";
        case actNextSong:   return String::fromUTF8 ("Pr\xc3\xb3xima m\xc3\xbasica");
        case actPrevSong:   return String::fromUTF8 ("M\xc3\xbasica anterior");
        case actScene:      return "Cena " + String (index + 1);
        case actPad:        return "Pad " + String (index + 1);
        case actAutoPad:    return "Auto-PAD liga/desliga";
        case actBusFader:   return StringArray { "Fader Click", "Fader Subdiv", "Fader Voz", "Fader Auto-PAD", "Fader One-shots" }[index];
        case actTrackFader: return "Fader trilha " + String (index + 1);
        case actClick:      return "Click liga/desliga";
        default:            return "?";
    }
}

Project::Project()
{
    for (int i = 0; i < numPads; ++i)
    {
        if (i < builtinPadNames().size()) { pads[i].builtin = i; pads[i].name = builtinPadNames()[i]; }
    }
    buses[busSub].gainDb = -6.0f;
    buses[busPad].gainDb = -6.0f;
}

static var obj() { return var (new DynamicObject()); }
static void set (var& v, const char* k, const var& x) { v.getDynamicObject()->setProperty (k, x); }
template <typename T> static T get (const var& v, const char* k, T def)
{
    if (auto* o = v.getDynamicObject()) if (o->hasProperty (k)) return (T) o->getProperty (k);
    return def;
}
static String getS (const var& v, const char* k, String def = {})
{
    if (auto* o = v.getDynamicObject()) if (o->hasProperty (k)) return o->getProperty (k).toString();
    return def;
}
static const Array<var>& arr (const var& v, const char* k)
{
    static Array<var> none;
    if (auto* o = v.getDynamicObject()) if (auto* a = o->getProperty (k).getArray()) return *a;
    return none;
}

var Project::toVar() const
{
    var p = obj();
    set (p, "format", 1); set (p, "name", name);
    set (p, "clickKit", clickKit); set (p, "favKits", var (favKits)); set (p, "voiceMode", voiceMode); set (p, "voiceLang", voiceLang); set (p, "clickOn", clickOn);

    Array<var> sa;
    for (auto& s : songs)
    {
        var sv = obj();
        set (sv, "name", s.name); set (sv, "countIn", s.countInBars); set (sv, "subdiv", s.subdiv); set (sv, "padKey", s.padKey); set (sv, "clickKit", s.clickKit);
        Array<var> ta;
        for (auto& b : s.tempo) { var t = obj(); set (t, "bars", b.bars); set (t, "num", b.num); set (t, "den", b.den); set (t, "bpm0", b.bpm0); set (t, "bpm1", b.bpm1); set (t, "pattern", b.pattern); ta.add (t); }
        set (sv, "tempo", ta);
        Array<var> tr;
        for (auto& t : s.tracks) { var x = obj(); set (x, "name", t.name); set (x, "cue", t.isCue); set (x, "outL", t.outL); set (x, "outR", t.outR); set (x, "gain", t.gainDb); set (x, "mute", t.mute); tr.add (x); }
        set (sv, "tracks", tr);
        Array<var> sc;
        for (size_t i = 0; i < s.scenes.size(); ++i)
        {
            auto& c = s.scenes[i];
            var x = obj(); set (x, "name", c.name); set (x, "bars", c.bars); set (x, "follow", c.follow); set (x, "cue", c.cue);
            Array<var> cl;
            for (size_t t = 0; t < s.tracks.size(); ++t)
            {
                Clip k; if (i < s.clips.size() && t < s.clips[i].size()) k = s.clips[i][t];
                var y = obj(); set (y, "file", k.file); set (y, "loop", k.loop); set (y, "ch", k.srcChannel); set (y, "gain", k.gainDb); cl.add (y);
            }
            set (x, "clips", cl); sc.add (x);
        }
        set (sv, "scenes", sc);
        sa.add (sv);
    }
    set (p, "songs", sa);

    Array<var> pa;
    for (auto& d : pads) { var x = obj(); set (x, "name", d.name); set (x, "builtin", d.builtin); set (x, "file", d.file); set (x, "gain", d.gainDb); pa.add (x); }
    set (p, "pads", pa);
    Array<var> ba;
    for (auto& b : buses) { var x = obj(); set (x, "gain", b.gainDb); set (x, "mute", b.mute); set (x, "outL", b.outL); set (x, "outR", b.outR); ba.add (x); }
    set (p, "buses", ba);
    Array<var> ma;
    for (auto& m : midi) { var x = obj(); set (x, "cc", m.isCC); set (x, "ch", m.channel); set (x, "num", m.number); set (x, "action", m.action); set (x, "index", m.index); ma.add (x); }
    set (p, "midi", ma);
    return p;
}

Project Project::fromVar (const var& p)
{
    Project r;
    r.name = getS (p, "name", "Setlist");
    r.clickKit = getS (p, "clickKit", "Classic"); if (! clickKitNames().contains (r.clickKit)) r.clickKit = "Classic";
    for (auto& f : arr (p, "favKits")) if (clickKitNames().contains (f.toString())) r.favKits.addIfNotAlreadyThere (f.toString());
    r.voiceMode = get (p, "voiceMode", 1); r.voiceLang = get (p, "voiceLang", 0); r.clickOn = get (p, "clickOn", true);
    r.songs.clear();
    for (auto& sv : arr (p, "songs"))
    {
        Song s; s.tempo.clear();
        s.name = getS (sv, "name", "Musica"); s.countInBars = get (sv, "countIn", 1); s.subdiv = jlimit (1, 4, get (sv, "subdiv", 1)); s.padKey = get (sv, "padKey", -1); s.clickKit = getS (sv, "clickKit"); if (! clickKitNames().contains (s.clickKit)) s.clickKit.clear();
        for (auto& t : arr (sv, "tempo"))
        {
            TempoBlock b; b.bars = jmax (1, get (t, "bars", 8)); b.num = jlimit (1, 32, get (t, "num", 4)); b.den = get (t, "den", 4);
            if (b.den != 2 && b.den != 4 && b.den != 8 && b.den != 16) b.den = 4;
            b.bpm0 = jlimit (20.0, 400.0, get (t, "bpm0", 120.0)); b.bpm1 = jlimit (20.0, 400.0, get (t, "bpm1", b.bpm0)); b.pattern = getS (t, "pattern").retainCharacters ("0123");
            s.tempo.push_back (b);
        }
        for (auto& x : arr (sv, "tracks"))
        {
            Track t; t.name = getS (x, "name"); t.isCue = get (x, "cue", false); t.outL = get (x, "outL", 0); t.outR = get (x, "outR", 1); t.gainDb = get (x, "gain", 0.0f); t.mute = get (x, "mute", false);
            s.tracks.push_back (t);
        }
        for (auto& x : arr (sv, "scenes"))
        {
            Scene c; c.name = getS (x, "name"); c.bars = jmax (1, get (x, "bars", 8)); c.follow = get (x, "follow", 0); c.cue = migrateCue (getS (x, "cue"));
            s.scenes.push_back (c);
            std::vector<Clip> row;
            for (auto& y : arr (x, "clips")) { Clip k; k.file = getS (y, "file"); k.loop = get (y, "loop", false); k.srcChannel = get (y, "ch", -1); k.gainDb = get (y, "gain", 0.0f); row.push_back (k); }
            s.clips.push_back (row);
        }
        s.normalise();
        r.songs.push_back (s);
    }
    if (r.songs.empty()) r.songs.push_back ({});
    int i = 0;
    for (auto& x : arr (p, "pads")) { if (i >= numPads) break; auto& d = r.pads[i++]; d.name = getS (x, "name"); d.builtin = get (x, "builtin", -1); d.file = getS (x, "file"); d.gainDb = get (x, "gain", 0.0f); }
    i = 0;
    for (auto& x : arr (p, "buses")) { if (i >= numBuses) break; auto& b = r.buses[i++]; b.gainDb = get (x, "gain", 0.0f); b.mute = get (x, "mute", false); b.outL = get (x, "outL", 0); b.outR = get (x, "outR", 1); }
    for (auto& x : arr (p, "midi")) { MidiBind m; m.isCC = get (x, "cc", false); m.channel = get (x, "ch", 1); m.number = get (x, "num", 0); m.action = get (x, "action", 0); m.index = get (x, "index", 0); r.midi.push_back (m); }
    return r;
}

bool Project::save (const File& f) const
{
    // caminhos de audio dentro da pasta do projeto sao gravados como relativos
    Project copy = *this;
    auto dir = f.getParentDirectory();
    auto rel = [&] (String& path)
    {
        if (path.isNotEmpty() && File::isAbsolutePath (path) && File (path).isAChildOf (dir))
            path = File (path).getRelativePathFrom (dir).replaceCharacter ('\\', '/');
    };
    for (auto& s : copy.songs) for (auto& row : s.clips) for (auto& c : row) rel (c.file);
    for (auto& d : copy.pads) rel (d.file);
    return f.replaceWithText (JSON::toString (copy.toVar()));
}

bool Project::load (const File& f, Project& out)
{
    var v;
    if (! f.existsAsFile() || JSON::parse (f.loadFileAsString(), v).failed() || ! v.isObject()) return false;
    out = fromVar (v);
    auto dir = f.getParentDirectory();
    auto abs = [&] (String& path) { if (path.isNotEmpty() && ! File::isAbsolutePath (path)) path = dir.getChildFile (path).getFullPathName(); };
    for (auto& s : out.songs) for (auto& row : s.clips) for (auto& c : row) abs (c.file);
    for (auto& d : out.pads) abs (d.file);
    return true;
}
}
