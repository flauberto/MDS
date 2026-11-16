#include "Engine.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
#endif
#include <ableton/Link.hpp>

namespace bt
{
using namespace juce;

struct LinkHolder { ableton::Link link { 120.0 }; };

//======================================================================================
// Toca um arquivo de audio: os primeiros 2 s ficam na memoria (disparo imediato e sem
// falha), o restante vem do disco por uma thread de leitura em segundo plano.
class ClipPlayer
{
public:
    ClipPlayer (AudioFormatReader* r, const Clip& c, double sr, int block, TimeSliceThread* thread)
        : loop (c.loop), srcChannel (c.srcChannel), gain (Decibels::decibelsToGain (c.gainDb))
    {
        ratio = r->sampleRate / sr;
        length = (int64) ((double) r->lengthInSamples / ratio);
        const double fileRate = r->sampleRate;
        src.reset (new AudioFormatReaderSource (r, true));
        headLen = (int) jmin (length, (int64) (2.0 * sr));
        head.setSize (2, jmax (1, headLen));
        head.clear();
        if (headLen > 0)
        {
            ResamplingAudioSource tmp (src.get(), false, 2);
            tmp.setResamplingRatio (ratio);
            tmp.prepareToPlay (block, sr);
            AudioSourceChannelInfo info (&head, 0, headLen);
            tmp.getNextAudioBlock (info);
        }
        if (length > headLen)
        {
            streamStart = (int64) std::llround (headLen * ratio);
            if (thread != nullptr)
            {
                buf.reset (new BufferingAudioSource (src.get(), *thread, false, (int) (fileRate * 4.0), 2, false));
                input = buf.get();
            }
            else
                input = src.get();
            rs.reset (new ResamplingAudioSource (input, false, 2));
            rs->setResamplingRatio (ratio);
            rs->prepareToPlay (block, sr);
            input->setNextReadPosition (streamStart);
        }
    }

    void start()
    {
        pos = 0; done = length <= 0;
        if (rs != nullptr) { input->setNextReadPosition (streamStart); rs->flushBuffers(); }
    }

    void render (AudioBuffer<float>& dst, int off, int n)
    {
        while (n > 0)
        {
            if (done) { dst.clear (off, n); return; }
            int m = 0;
            if (pos < headLen)
            {
                m = (int) jmin ((int64) n, headLen - pos);
                dst.copyFrom (0, off, head, 0, (int) pos, m);
                dst.copyFrom (1, off, head, 1, (int) pos, m);
            }
            else if (pos < length && rs != nullptr)
            {
                m = (int) jmin ((int64) n, length - pos);
                AudioSourceChannelInfo info (&dst, off, m);
                rs->getNextAudioBlock (info);
            }
            else
            {
                if (loop) start(); else done = true;
                continue;
            }
            pos += m; off += m; n -= m;
        }
    }

    bool loop; int srcChannel; float gain;
    bool done = false;
    int64 pos = 0, length = 0;

private:
    double ratio = 1.0;
    int headLen = 0; int64 streamStart = 0;
    AudioBuffer<float> head;
    std::unique_ptr<AudioFormatReaderSource> src;
    std::unique_ptr<BufferingAudioSource> buf;
    std::unique_ptr<ResamplingAudioSource> rs;
    PositionableAudioSource* input = nullptr;
};

//======================================================================================
Engine::Engine (bool offlineMode) : offline (offlineMode)
{
    formats.registerBasicFormats();
    for (auto& a : uiTrackClip) a = -1;
    for (auto& a : uiTrackQueued) a = -2;
    for (auto& a : uiTrackPeak) a = 0.0f;
    for (auto& a : uiBusPeak) a = 0.0f;
    for (auto& a : uiOutPeak) a = 0.0f;
    for (auto& a : uiPadFlash) a = 0;
    if (! offline)
    {
        ioThread.reset (new TimeSliceThread ("MDS leitura de disco"));
        ioThread->startThread (Thread::Priority::high);
        link.reset (new LinkHolder());
        link->link.enableStartStopSync (true);
    }
    scratch.setSize (2, 8192); mono.setSize (1, 8192);
}

Engine::~Engine()
{
    if (link != nullptr) link->link.enable (false);
    { SpinLock::ScopedLockType l (lock); rt.players.clear(); }
    if (ioThread != nullptr) ioThread->stopThread (2000);
}

void Engine::setLinkEnabled (bool on) { linkEnabled = on; if (link != nullptr) link->link.enable (on); }
int Engine::linkPeers() const { return link != nullptr ? (int) link->link.numPeers() : 0; }

//======================================================================================
void Engine::setProject (const Project& p)
{
    stop();
    project = p;
    songIndex = 0;
    rebuildSamples();
    selectSong (0);
}

void Engine::selectSong (int index)
{
    songIndex = jlimit (0, (int) project.songs.size() - 1, index);
    reloadSong();
    push ({ cPadKey, song().padKey, 0 });
}

void Engine::prepare (double sr, int block)
{
    sampleRate = sr; blockSize = jmax (64, block);
    {
        SpinLock::ScopedLockType l (lock);
        scratch.setSize (2, jmax (8192, blockSize * 2)); mono.setSize (1, jmax (8192, blockSize * 2));
    }
    rebuildSamples();
    reloadSong();
}

void Engine::rebuildSamples()
{
    auto nb = std::make_unique<SampleBank>();
    if (! exportLinear)
    {
        for (int k = 0; k < 3; ++k) nb->s[sClickAccent + k] = synth::click (project.clickSound, k, sampleRate);
        WavAudioFormat wav;
        auto loadVoice = [&] (const String& resName) -> Sample
        {
            int size = 0;
            if (auto* data = BinaryData::getNamedResource (resName.toRawUTF8(), size))
            {
                std::unique_ptr<AudioFormatReader> r (wav.createReaderFor (new MemoryInputStream (data, (size_t) size, false), true));
                return synth::loadMono (r.get(), sampleRate);
            }
            return {};
        };
        const String lang = project.voiceLang == 1 ? "en" : "pt";
        for (int i = 0; i < 12; ++i) nb->s[sVoiceNum + i] = loadVoice (lang + "_" + String (i + 1) + "_wav");
        for (int i = 0; i < 8; ++i)  nb->s[sVoiceCue + i] = loadVoice (lang + "_cue_" + String (i) + "_wav");
        for (int i = 0; i < numPads; ++i)
        {
            auto& d = project.pads[i];
            if (d.file.isNotEmpty())
            {
                std::unique_ptr<AudioFormatReader> r (formats.createReaderFor (File (d.file)));
                nb->s[sPad + i] = synth::loadMono (r.get(), sampleRate);
            }
            else if (d.builtin >= 0)
                nb->s[sPad + i] = synth::builtinPad (d.builtin, sampleRate);
            const float g = Decibels::decibelsToGain (d.gainDb);
            for (auto& x : nb->s[sPad + i]) x *= g;
        }
    }
    SpinLock::ScopedLockType l (lock);
    for (auto& v : voices) v.data = nullptr;
    bank.swap (nb);
}

void Engine::reloadSong()
{
    auto& s = song();
    s.normalise();
    std::vector<std::vector<std::unique_ptr<ClipPlayer>>> players (s.scenes.size());
    for (size_t i = 0; i < s.scenes.size(); ++i)
    {
        players[i].resize (s.tracks.size());
        for (size_t t = 0; t < s.tracks.size(); ++t)
        {
            auto& c = s.clips[i][t];
            if (c.empty()) continue;
            if (auto* r = formats.createReaderFor (File (c.file)))
                players[i][t].reset (new ClipPlayer (r, c, sampleRate, blockSize, offline ? nullptr : ioThread.get()));
        }
    }
    {
        SpinLock::ScopedLockType l (lock);
        stopTransport();
        rt.players.swap (players);
        rt.tracks.assign (s.tracks.size(), {});
        rt.scenes.clear();
    }
    syncParams();
    // 'players' antigo e destruido aqui, fora do lock
}

void Engine::syncParams()
{
    auto& s = song();
    s.normalise();
    {
        SpinLock::ScopedLockType l (lock);
        if (rt.players.size() != s.scenes.size() || rt.tracks.size() != s.tracks.size()) { /* estrutura mudou */ }
        else
        {
            rt.tempo = s.tempo; rt.countIn = jlimit (0, 8, s.countInBars); rt.subdiv = jlimit (1, 4, s.subdiv);
            rt.scenes = s.scenes;
            rt.sceneCue.clear();
            for (auto& sc : s.scenes) rt.sceneCue.push_back (cueNames().indexOf (sc.cue));
            for (size_t t = 0; t < s.tracks.size(); ++t)
            {
                auto& tr = s.tracks[t]; auto& r = rt.tracks[t];
                r.gain = tr.mute ? 0.0f : Decibels::decibelsToGain (tr.gainDb);
                r.outL = forceSplit ? (tr.isCue ? 0 : 1) : tr.outL;
                r.outR = forceSplit ? -1 : tr.outR;
            }
            for (int b = 0; b < numBuses; ++b)
            {
                auto& bus = project.buses[b];
                rt.busGain[b] = bus.mute ? 0.0f : Decibels::decibelsToGain (bus.gainDb);
                rt.busL[b] = forceSplit ? (b <= busVoice ? 0 : 1) : bus.outL;
                rt.busR[b] = forceSplit ? -1 : bus.outR;
            }
            rt.clickOn = project.clickOn && ! exportLinear;
            rt.voiceMode = exportLinear ? 0 : project.voiceMode;
            for (size_t i = 0; i < s.scenes.size(); ++i)
                for (size_t t = 0; t < s.tracks.size(); ++t)
                    if (auto* p = rt.players[i][t].get()) { p->loop = s.clips[i][t].loop; p->gain = Decibels::decibelsToGain (s.clips[i][t].gainDb); }
            if (! tPlaying) seek (0.0);
            SpinLock::ScopedLockType ml (midiLock);
            midiBinds = project.midi;
            return;
        }
    }
    reloadSong();
}

//======================================================================================
void Engine::push (Cmd c)
{
    const ScopedLock sl (pushLock);
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) { fifoData[s1] = c; fifo.finishedWrite (1); }
}

void Engine::play()                         { push ({ cPlay, 0, 0 }); }
void Engine::stop()                         { push ({ cStop, 0, 0 }); }
void Engine::launchScene (int s)            { push ({ cScene, s, 0 }); }
void Engine::launchClip (int s, int t)      { push ({ cClip, s, t }); }
void Engine::stopTrack (int t)              { push ({ cStopTrack, t, 0 }); }
void Engine::triggerPad (int p)             { push ({ cPad, p, 0 }); }
void Engine::setAutoPad (bool on)           { autoPad = on; push ({ cPadOn, on ? 1 : 0, 0 }); }

void Engine::performAction (int action, int index, float value)
{
    switch (action)
    {
        case actPlay:      play(); break;
        case actStop:      stop(); break;
        case actToggle:    togglePlay(); break;
        case actScene:     launchScene (index); break;
        case actNextScene: launchScene (jmax (uiScene.load(), uiQueuedScene.load()) + 1); break;
        case actPrevScene: launchScene (jmax (0, uiScene.load() - 1)); break;
        case actPad:       triggerPad (index); break;
        case actAutoPad:   setAutoPad (! autoPad.load()); break;
        default:
            MessageManager::callAsync ([this, action, index, value] { if (onUiAction) onUiAction (action, index, value); });
            break;
    }
}

void Engine::handleIncomingMidiMessage (MidiInput*, const MidiMessage& m)
{
    const bool isCC = m.isController(), isNote = m.isNoteOn();
    if (! isCC && ! isNote) return;
    const int num = isCC ? m.getControllerNumber() : m.getNoteNumber();
    const float value = isCC ? (float) m.getControllerValue() / 127.0f : 1.0f;

    if (midiLearning.load())
    {
        if (isCC && m.getControllerValue() == 0) return;
        MidiBind b; b.isCC = isCC; b.channel = m.getChannel(); b.number = num;
        midiLearning = false;
        MessageManager::callAsync ([this, b] { if (onMidiLearned) onMidiLearned (b); });
        return;
    }
    int action = -1, index = 0;
    {
        SpinLock::ScopedLockType l (midiLock);
        for (auto& b : midiBinds)
            if (b.isCC == isCC && b.number == num && b.channel == m.getChannel()) { action = b.action; index = b.index; break; }
    }
    if (action < 0) return;
    const bool continuous = action == actBusFader || action == actTrackFader;
    if (! continuous && isCC && value < 0.5f) return;      // pedal solto / botao liberado
    performAction (action, index, value);
}

//======================================================================================
void Engine::seek (double b)
{
    if (rt.tempo.empty()) rt.tempo.push_back ({});
    const auto& first = rt.tempo.front();
    if (b < 0)
    {
        blockIdx = 0; blockStart = 0; blockBeats = first.beats(); bpm0 = bpm1 = first.bpm0; pastEnd = false;
        sigNum = first.num; sigDen = first.den; barLen = first.barBeats();
        barNum = (int) std::floor (b / barLen + 1.0e-9); barStart = barNum * barLen;
    }
    else
    {
        double acc = 0; int bars = 0; bool found = false;
        for (size_t i = 0; i < rt.tempo.size(); ++i)
        {
            auto& t = rt.tempo[i];
            if (b < acc + t.beats() - 1.0e-9)
            {
                blockIdx = (int) i; blockStart = acc; blockBeats = t.beats(); bpm0 = t.bpm0; bpm1 = t.bpm1; pastEnd = false;
                sigNum = t.num; sigDen = t.den; barLen = t.barBeats();
                const int k = (int) std::floor ((b - acc) / barLen + 1.0e-9);
                barNum = bars + k; barStart = acc + k * barLen; found = true; break;
            }
            acc += t.beats(); bars += t.bars;
        }
        if (! found)
        {
            auto& t = rt.tempo.back();
            blockIdx = (int) rt.tempo.size() - 1; blockStart = acc; blockBeats = t.beats(); bpm0 = bpm1 = t.bpm1; pastEnd = true;
            sigNum = t.num; sigDen = t.den; barLen = t.barBeats();
            const int k = (int) std::floor ((b - acc) / barLen + 1.0e-9);
            barNum = bars + k; barStart = acc + k * barLen;
        }
    }
    tick = 0;
}

double Engine::bpmAt (double b) const
{
    if (b < 0 || pastEnd || blockBeats <= 0) return bpm0;
    return bpm0 + (bpm1 - bpm0) * jlimit (0.0, 1.0, (b - blockStart) / blockBeats);
}

void Engine::fire (int id, int bus, int delay, float gain)
{
    if (bank == nullptr || id < 0 || id >= sCount || bank->s[id].empty()) return;
    for (auto& v : voices)
        if (v.data == nullptr)
        {
            v.data = bank->s[id].data(); v.len = (int) bank->s[id].size(); v.pos = 0; v.delay = delay; v.bus = bus; v.gain = gain;
            return;
        }
}

void Engine::startTransport()
{
    if (rt.tempo.empty()) rt.tempo.push_back ({});
    beat = -rt.countIn * rt.tempo.front().barBeats();
    if (exportLinear) beat = 0;
    seek (beat);
    barStart = beat;   // garante alinhamento exato
    tPlaying = true; linkJustStarted = true;
    scene = -1; sceneBars = 0;
    if (barNum >= 0) barEvent();
}

void Engine::stopTransport()
{
    tPlaying = false;
    for (size_t t = 0; t < rt.tracks.size(); ++t)
    {
        auto& tr = rt.tracks[t];
        if (tr.active >= 0 && tr.active < (int) rt.players.size())
            if (auto* p = rt.players[(size_t) tr.active][t].get()) p->start();   // rearma para o proximo disparo
        tr.active = -1; tr.queued = -2;
    }
    scene = -1; sceneQueued = -1; sceneBars = 0;
    beat = 0; seek (0.0);
}

void Engine::applyScene (int s)
{
    if (s < 0 || s >= (int) rt.players.size()) return;
    for (size_t t = 0; t < rt.tracks.size(); ++t)
    {
        auto& tr = rt.tracks[t];
        auto* np = rt.players[(size_t) s][t].get();
        if (tr.active >= 0 && tr.active != s)
            if (auto* old = rt.players[(size_t) tr.active][t].get()) old->start();
        if (np != nullptr) { np->start(); tr.active = s; } else tr.active = -1;
        tr.queued = -2;
    }
    scene = s; sceneBars = 0; sceneQueued = -1;
}

// Chamado no inicio de cada compasso (barNum >= 0)
void Engine::barEvent()
{
    if (barNum < 0) return;
    const int count = (int) rt.scenes.size();
    if (scene >= 0 && scene < count)
    {
        ++sceneBars;
        const auto& sc = rt.scenes[(size_t) scene];
        const int f = exportLinear ? (int) followNext : sc.follow;
        if (sceneQueued < 0 && f != followNone)
        {
            int target = -1;
            if (f == followNext) target = scene + 1 < count ? scene + 1 : -1;
            else if (f == followAgain) target = scene;
            else if (f == followFirst) target = 0;
            if (sceneBars >= sc.bars)
            {
                if (target < 0) { stopTransport(); return; }
                sceneQueued = target;
            }
            else if (sceneBars == sc.bars - 1 && target >= 0 && target != scene && rt.voiceMode != 0)
                fire (sVoiceCue + rt.sceneCue[(size_t) target], busVoice, 0);
        }
    }
    if (sceneQueued >= 0) applyScene (sceneQueued);
    for (size_t t = 0; t < rt.tracks.size(); ++t)
    {
        auto& tr = rt.tracks[t];
        if (tr.queued == -2) continue;
        if (tr.active >= 0) if (auto* old = rt.players[(size_t) tr.active][t].get()) old->start();
        tr.active = -1;
        if (tr.queued >= 0 && tr.queued < (int) rt.players.size())
            if (auto* p = rt.players[(size_t) tr.queued][t].get()) { p->start(); tr.active = tr.queued; }
        tr.queued = -2;
    }
}

void Engine::handle (const Cmd& c)
{
    const int count = (int) rt.scenes.size();
    switch (c.type)
    {
        case cPlay:
            if (! tPlaying) { if (sceneQueued < 0 && count > 0) sceneQueued = 0; startTransport(); }
            break;
        case cStop: stopTransport(); break;
        case cScene:
            if (c.a >= 0 && c.a < count)
            {
                sceneQueued = c.a;
                if (! tPlaying) startTransport();
                else if (rt.voiceMode != 0 && c.a != scene) fire (sVoiceCue + rt.sceneCue[(size_t) c.a], busVoice, 0);
            }
            break;
        case cClip:
            if (c.b >= 0 && c.b < (int) rt.tracks.size() && c.a >= 0 && c.a < count)
            {
                rt.tracks[(size_t) c.b].queued = c.a;
                if (! tPlaying) startTransport();
            }
            break;
        case cStopTrack:
            if (c.a >= 0 && c.a < (int) rt.tracks.size()) rt.tracks[(size_t) c.a].queued = -1;
            break;
        case cPad:
            if (c.a >= 0 && c.a < numPads) { fire (sPad + c.a, busShots, 0); uiPadFlash[c.a]++; }
            break;
        case cPadOn:
        case cPadKey:
        {
            if (c.type == cPadOn) padOn = c.a != 0; else padKey = c.a;
            auto& cur = drones[droneActive];
            if (padOn && padKey >= 0)
            {
                if (cur.key != padKey || cur.target <= 0.0f)
                {
                    cur.target = 0.0f;
                    if (cur.key != padKey) droneActive ^= 1;
                    drones[droneActive].setKey (padKey);
                    drones[droneActive].target = 1.0f;
                }
            }
            else { drones[0].target = 0.0f; drones[1].target = 0.0f; }
            break;
        }
        default: break;
    }
}

//======================================================================================
void Engine::addTo (float* const* out, int numOut, const float* src, int start, int n, float gain, int ch)
{
    if (ch < 0 || ch >= numOut || gain == 0.0f) return;
    FloatVectorOperations::addWithMultiply (out[ch] + start, src, gain, n);
}

void Engine::renderTracks (float* const* out, int numOut, int start, int end)
{
    const int n = end - start;
    if (n <= 0) return;
    for (size_t t = 0; t < rt.tracks.size(); ++t)
    {
        auto& tr = rt.tracks[t];
        if (tr.active < 0 || tr.active >= (int) rt.players.size()) continue;
        auto* p = rt.players[(size_t) tr.active][t].get();
        if (p == nullptr) { tr.active = -1; continue; }
        p->render (scratch, 0, n);
        if (p->done) { p->start(); tr.active = -1; }
        const float g = tr.gain * p->gain;
        const float* l = scratch.getReadPointer (0); const float* r = scratch.getReadPointer (1);
        if (t < 64) uiTrackPeak[t] = jmax (uiTrackPeak[t].load(), g * scratch.getMagnitude (0, n));
        if (p->srcChannel >= 0)
        {
            const float* s = p->srcChannel == 0 ? l : r;
            addTo (out, numOut, s, start, n, g, tr.outL);
            addTo (out, numOut, s, start, n, g, tr.outR);
        }
        else if (tr.outR >= 0)
        {
            addTo (out, numOut, l, start, n, g, tr.outL);
            addTo (out, numOut, r, start, n, g, tr.outR);
        }
        else
        {
            addTo (out, numOut, l, start, n, g * 0.5f, tr.outL);
            addTo (out, numOut, r, start, n, g * 0.5f, tr.outL);
        }
    }
}

void Engine::process (float* const* out, int numOut, int n)
{
    for (int c = 0; c < numOut; ++c) FloatVectorOperations::clear (out[c], n);
    SpinLock::ScopedTryLockType l (lock);
    if (! l.isLocked() || n <= 0) return;
    if (n > scratch.getNumSamples()) { scratch.setSize (2, n, false, false, true); mono.setSize (1, n, false, false, true); }

    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) handle (fifoData[s1 + i]);
        for (int i = 0; i < n2; ++i) handle (fifoData[s2 + i]);
        fifo.finishedRead (n1 + n2);
    }

    double linkTempo = 0.0;
    if (link != nullptr && linkEnabled.load())
    {
        auto st = link->link.captureAudioSessionState();
        const auto now = link->link.clock().micros();
        if (linkFollow.load())
        {
            const bool lp = st.isPlaying();
            if (lp != linkWasPlaying)
            {
                linkWasPlaying = lp;
                if (lp && ! tPlaying) { if (sceneQueued < 0 && ! rt.scenes.empty()) sceneQueued = 0; startTransport(); }
                else if (! lp && tPlaying) stopTransport();
            }
            linkTempo = st.tempo();
        }
        else
        {
            if (tPlaying) st.setTempo (bpmAt (beat), now);
            if (st.isPlaying() != tPlaying) st.setIsPlaying (tPlaying, now);
            linkWasPlaying = tPlaying;
        }
        if (tPlaying && link->link.numPeers() > 0)
        {
            const double target = st.phaseAtTime (now, 1.0);
            double err = target - (beat - std::floor (beat));
            if (err > 0.5) err -= 1.0; else if (err < -0.5) err += 1.0;
            beat += linkJustStarted ? err : err * 0.05;
        }
        link->link.commitAudioSessionState (st);
    }
    linkJustStarted = false;

    int chunkStart = 0;
    double bpm = linkTempo > 0 ? linkTempo : bpmAt (beat);
    if (tPlaying)
    {
        for (int i = 0; i < n; ++i)
        {
            if (beat >= barStart + barLen - 1.0e-9)
            {
                renderTracks (out, numOut, chunkStart, i); chunkStart = i;
                seek (barStart + barLen + 1.0e-6);
                barEvent();
                if (! tPlaying) break;
            }
            const double subLen = (4.0 / sigDen) / rt.subdiv;
            if (tick < sigNum * rt.subdiv && beat >= barStart + tick * subLen - 1.0e-9)
            {
                if (tick % rt.subdiv == 0)
                {
                    const int pulse = tick / rt.subdiv;
                    if (rt.clickOn)
                    {
                        const int lvl = rt.tempo[(size_t) jlimit (0, (int) rt.tempo.size() - 1, blockIdx)].level (pulse);
                        if (lvl > 0) fire (lvl == 3 ? sClickAccent : sClickBeat, busClick, i, lvl == 1 ? 0.4f : 1.0f);
                    }
                    if ((rt.voiceMode == 2 || (rt.voiceMode == 1 && barNum < 0)) && pulse < 12) fire (sVoiceNum + pulse, busVoice, i);
                    uiBeatInBar = pulse;
                }
                else if (rt.clickOn) fire (sClickSub, busSub, i);
                ++tick;
            }
            bpm = linkTempo > 0 ? linkTempo : bpmAt (beat);
            beat += bpm / (60.0 * sampleRate);
        }
    }
    if (tPlaying) renderTracks (out, numOut, chunkStart, n);

    // click, vozes e one-shots
    for (auto& v : voices)
    {
        if (v.data == nullptr) continue;
        const int start = jmin (v.delay, n); v.delay -= start;
        const int m = jmin (n - start, v.len - v.pos);
        if (m > 0)
        {
            const float g = v.gain * rt.busGain[v.bus];
            addTo (out, numOut, v.data + v.pos, start, m, g, rt.busL[v.bus]);
            addTo (out, numOut, v.data + v.pos, start, m, g, rt.busR[v.bus]);
            uiBusPeak[v.bus] = jmax (uiBusPeak[v.bus].load(), g * FloatVectorOperations::findMinAndMax (v.data + v.pos, m).getEnd());
            v.pos += m;
        }
        if (v.pos >= v.len) v.data = nullptr;
    }

    // Auto-PAD
    if (! drones[0].silent() || ! drones[1].silent())
    {
        mono.clear (0, n);
        for (auto& d : drones) if (! d.silent()) d.render (mono.getWritePointer (0), n, sampleRate, 4.0f);
        const float g = rt.busGain[busPad];
        addTo (out, numOut, mono.getReadPointer (0), 0, n, g, rt.busL[busPad]);
        addTo (out, numOut, mono.getReadPointer (0), 0, n, g, rt.busR[busPad]);
        uiBusPeak[busPad] = jmax (uiBusPeak[busPad].load(), g * mono.getMagnitude (0, n));
    }

    for (int c = 0; c < jmin (numOut, 32); ++c)
    {
        auto r = FloatVectorOperations::findMinAndMax (out[c], n);
        uiOutPeak[c] = jmax (uiOutPeak[c].load(), jmax (r.getEnd(), -r.getStart()));
        FloatVectorOperations::clip (out[c], out[c], -1.0f, 1.0f, n);
    }

    playing = tPlaying;
    uiBeat = beat; uiBpm = bpm; uiBar = barNum; uiNum = sigNum; uiDen = sigDen;
    uiScene = scene; uiQueuedScene = sceneQueued; uiSceneBar = sceneBars;
    for (size_t t = 0; t < jmin ((size_t) 64, rt.tracks.size()); ++t) { uiTrackClip[t] = rt.tracks[t].active; uiTrackQueued[t] = rt.tracks[t].queued; }
}

void Engine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n,
                                               const AudioIODeviceCallbackContext&)
{
    ScopedNoDenormals nd;
    process (out, numOut, n);
}

void Engine::audioDeviceAboutToStart (AudioIODevice* d)
{
    const double sr = d->getCurrentSampleRate(); const int bs = d->getCurrentBufferSizeSamples();
    numOutputs = d->getActiveOutputChannels().countNumberOfSetBits();
    auto doIt = [this, sr, bs] { prepare (sr, bs); };
    if (MessageManager::getInstance()->isThisTheMessageThread()) doIt(); else MessageManager::callAsync (doIt);
}
}
