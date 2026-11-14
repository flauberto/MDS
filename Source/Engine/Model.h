// Batuta - modelo de dados do projeto (setlist, musicas, cenas, trilhas)
#pragma once
#include <JuceHeader.h>
#include <vector>

namespace bt
{
using juce::String;

struct TempoBlock
{
    int bars = 8, num = 4, den = 4;
    double bpm0 = 120.0, bpm1 = 120.0;   // rampa linear de bpm0 ate bpm1 ao longo do bloco
    double barBeats() const { return num * 4.0 / den; }
    double beats() const { return bars * barBeats(); }
};

struct Clip
{
    String file;
    bool loop = false;
    int srcChannel = -1;      // -1 = arquivo inteiro, 0 = so canal L, 1 = so canal R
    float gainDb = 0.0f;
    bool empty() const { return file.isEmpty(); }
};

enum Follow { followNone = 0, followNext, followAgain, followStop, followFirst };

struct Scene
{
    String name = "Cena";
    int bars = 8;             // duracao usada pela acao de seguimento
    int follow = followNone;
    String cue;               // guia de voz falada ao enfileirar a cena ("" = nenhuma)
};

struct Track
{
    String name = "Trilha";
    bool isCue = false;       // trilha-guia (vai para o canal L no celular)
    int outL = 0, outR = 1;   // outR = -1 => mono em outL
    float gainDb = 0.0f;
    bool mute = false;
};

struct Song
{
    String name = "Nova musica";
    std::vector<TempoBlock> tempo { TempoBlock() };
    int countInBars = 1;
    int subdiv = 1;           // 1 = sem subdivisao, 2 = colcheias, 3 = tercinas, 4 = semicolcheias
    int padKey = -1;          // -1 = sem Auto-PAD, 0..11 = Do..Si
    std::vector<Track> tracks;
    std::vector<Scene> scenes;
    std::vector<std::vector<Clip>> clips;   // [cena][trilha]

    void normalise()
    {
        if (tempo.empty()) tempo.push_back ({});
        clips.resize (scenes.size());
        for (auto& row : clips) row.resize (tracks.size());
    }
};

enum BusId { busClick = 0, busSub, busVoice, busPad, busShots, numBuses };
struct Bus { float gainDb = 0.0f; bool mute = false; int outL = 0, outR = 1; };

struct PadDef { String name; int builtin = -1; String file; float gainDb = 0.0f; };
constexpr int numPads = 16;

// Acoes mapeaveis por MIDI
enum Action { actPlay = 0, actStop, actToggle, actNextScene, actPrevScene, actNextSong, actPrevSong,
              actScene, actPad, actAutoPad, actBusFader, actTrackFader, actClick, numActions };

struct MidiBind { bool isCC = false; int channel = 1, number = 0; int action = 0, index = 0; };

struct Project
{
    String name = "Setlist";
    std::vector<Song> songs { Song() };
    PadDef pads[numPads];
    Bus buses[numBuses];
    std::vector<MidiBind> midi;
    int clickSound = 0;
    int voiceMode = 1;        // 0 = sem voz, 1 = so na contagem inicial, 2 = sempre
    int voiceLang = 0;        // 0 = portugues, 1 = ingles
    bool clickOn = true;

    Project();
    juce::var toVar() const;
    static Project fromVar (const juce::var&);
    bool save (const juce::File&) const;
    static bool load (const juce::File&, Project& out);
};

const juce::StringArray& keyNames();
const juce::StringArray& cueNames();        // nomes das guias de voz embutidas
const juce::StringArray& builtinPadNames();
const juce::StringArray& clickSoundNames();
String actionName (int action, int index);
}
