// MDS - motor de audio compartilhado entre Windows e Android
#pragma once
#include "Model.h"
#include "Sounds.h"
#include <atomic>
#include <memory>

namespace bt
{
class ClipPlayer;
struct LinkHolder;

class Engine : public juce::AudioIODeviceCallback,
               public juce::MidiInputCallback
{
public:
    explicit Engine (bool offlineMode = false);
    ~Engine() override;

    //============================== thread de mensagens ==============================
    Project project;
    int songIndex = 0;
    bool forceSplit = false;      // Android: L = click/sub/voz/guias, R = VS/pads/one-shots
    bool exportLinear = false;    // render offline: cenas em ordem, sem click

    Song& song() { return project.songs[(size_t) juce::jlimit (0, (int) project.songs.size() - 1, songIndex)]; }
    void setProject (const Project&);
    void selectSong (int index);          // para o transporte e carrega os clipes
    void reloadSong();                    // apos mudanca estrutural (clipes, trilhas, cenas)
    void syncParams();                    // apos mudanca leve (ganhos, saidas, andamento, cenas)
    void rebuildSamples();                // apos mudar som do click, idioma ou pads
    void prepare (double sampleRate, int blockSize);

    void play();
    void stop();
    void togglePlay() { if (playing.load()) stop(); else play(); }
    void launchScene (int scene);
    void launchClip (int scene, int track);
    void stopTrack (int track);
    void triggerPad (int pad);
    void setAutoPad (bool on);
    void speakCue (int cueIndex);         // fala uma guia agora
    void previewClick();                  // toca forte + normal do timbre atual
    void performAction (int action, int index, float value);   // usado pelo MIDI e pela interface

    void setLinkEnabled (bool);
    void setLinkFollow (bool f) { linkFollow = f; }
    bool isLinkEnabled() const { return linkEnabled.load(); }
    int linkPeers() const;

    std::atomic<bool> midiLearning { false };
    std::function<void (MidiBind)> onMidiLearned;                 // chamado na thread de mensagens
    std::function<void (int action, int index, float value)> onUiAction;  // acoes que mexem no modelo

    //============================== estado para a interface ==========================
    std::atomic<bool> playing { false }, autoPad { false }, linkFollow { false };
    std::atomic<double> uiBeat { 0 }, uiBpm { 120 };
    std::atomic<int> uiBar { 0 }, uiBeatInBar { 0 }, uiNum { 4 }, uiDen { 4 };
    std::atomic<int> uiScene { -1 }, uiQueuedScene { -1 }, uiSceneBar { 0 };
    std::atomic<int> uiTrackClip[64];      // cena tocando em cada trilha (-1 = parada)
    std::atomic<int> uiTrackQueued[64];    // cena enfileirada (-2 = nada, -1 = parar)
    std::atomic<float> uiTrackPeak[64], uiBusPeak[numBuses], uiOutPeak[32];
    std::atomic<int> uiPadFlash[numPads];
    std::atomic<int> numOutputs { 2 };
    double sampleRate = 48000.0;

    //============================== audio ============================================
    void process (float* const* out, int numOut, int numSamples);
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

private:
    struct Cmd { int type = 0, a = 0, b = 0; };
    enum { cPlay = 1, cStop, cScene, cClip, cStopTrack, cPad, cPadOn, cPadKey, cCue, cPreview };
    void push (Cmd);
    void handle (const Cmd&);

    struct TrackRT { float gain = 1; int outL = 0, outR = 1; int active = -1, queued = -2; };
    struct RT
    {
        std::vector<TempoBlock> tempo;
        int countIn = 1, subdiv = 1;
        std::vector<TrackRT> tracks;
        std::vector<Scene> scenes;
        std::vector<int> sceneCue;
        std::vector<std::vector<std::unique_ptr<ClipPlayer>>> players;
        float busGain[numBuses] {}; int busL[numBuses] {}, busR[numBuses] {};
        bool clickOn = true; int voiceMode = 1;
    };
    struct Voice { const float* data = nullptr; int len = 0, pos = 0, delay = 0, bus = 0; float gain = 1; };

    void seek (double beat);
    double bpmAt (double beat) const;
    void startTransport();
    void stopTransport();
    void barEvent();
    void applyScene (int scene);
    void fire (int sampleId, int bus, int delay, float gain = 1.0f);
    void renderTracks (float* const* out, int numOut, int start, int end);
    void addTo (float* const* out, int numOut, const float* src, int start, int n, float gain, int ch);

    const bool offline;
    juce::SpinLock lock;
    RT rt;
    std::unique_ptr<SampleBank> bank;
    std::vector<MidiBind> midiBinds;
    juce::SpinLock midiLock;

    juce::AbstractFifo fifo { 256 };
    Cmd fifoData[256];
    juce::CriticalSection pushLock;

    // estado da thread de audio
    bool tPlaying = false;
    double beat = 0;
    int blockIdx = 0; double blockStart = 0, blockBeats = 0, bpm0 = 120, bpm1 = 120;
    bool pastEnd = false;
    double barStart = 0, barLen = 4; int barNum = 0, sigNum = 4, sigDen = 4, tick = 0;
    int scene = -1, sceneQueued = -1, sceneBars = 0;
    Voice voices[32];
    Drone drones[2]; int droneActive = 0; bool padOn = false; int padKey = -1;
    juce::AudioBuffer<float> scratch, mono;
    int blockSize = 512;
    bool linkWasPlaying = false, linkJustStarted = false;

    std::unique_ptr<LinkHolder> link;
    std::atomic<bool> linkEnabled { false };
    std::unique_ptr<juce::TimeSliceThread> ioThread;
    juce::AudioFormatManager formats;
    String loadedKit; int loadedLang = -1; double loadedRate = 0;
    std::vector<Sample> cueCache, numCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};
}
