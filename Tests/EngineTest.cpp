// Teste offline do motor: tempo map, click, cenas, follow actions, exportacao
#include <JuceHeader.h>
#include "Engine/Package.h"
using namespace juce; using namespace bt;

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { ++fails; std::cout << "FALHOU: " << msg << std::endl; } else std::cout << "ok: " << msg << std::endl; } while (0)

static File makeTone (const File& dir, const String& name, double freq, double seconds, double sr = 48000.0)
{
    auto f = dir.getChildFile (name); f.deleteFile();
    WavAudioFormat wav; std::unique_ptr<OutputStream> os (f.createOutputStream());
    auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (16));
    AudioBuffer<float> b (2, (int) (seconds * sr));
    for (int i = 0; i < b.getNumSamples(); ++i) { float v = 0.5f * (float) std::sin (MathConstants<double>::twoPi * freq * i / sr); b.setSample (0, i, v); b.setSample (1, i, v); }
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return f;
}

// devolve os instantes (s) em que o canal passa de silencio para som
static std::vector<double> onsets (const AudioBuffer<float>& b, int ch, double sr, float thr = 0.05f, double minGap = 0.05)
{
    std::vector<double> r; double last = -1;
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (std::abs (b.getSample (ch, i)) > thr) { double t = i / sr; if (last < 0 || t - last > minGap) r.push_back (t); last = t; }
    return r;
}

static AudioBuffer<float> run (Engine& e, int channels, double seconds, double sr, std::function<void (int)> atBlock = {})
{
    const int block = 256; const int total = (int) (seconds * sr);
    AudioBuffer<float> out (channels, total), tmp (channels, block);
    for (int pos = 0, k = 0; pos + block <= total; pos += block, ++k)
    {
        if (atBlock) atBlock (k);
        e.process (tmp.getArrayOfWritePointers(), channels, block);
        for (int c = 0; c < channels; ++c) out.copyFrom (c, pos, tmp, c, 0, block);
    }
    return out;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    if (argc == 4 && String (argv[1]) == "--export")      // MDSTest --export setlist.batuta saida.mdspack
    {
        Project p; if (! Project::load (File (argv[2]), p)) { std::cout << "nao abriu" << std::endl; return 1; }
        auto r = exportPack (p, File (argv[3]), [] (float f, String s) { std::cout << (int) (f * 100) << "% " << s << std::endl; });
        std::cout << (r.ok ? "ok " : "erro ") << r.message << std::endl; return r.ok ? 0 : 1;
    }
    if (argc == 4 && String (argv[1]) == "--import")      // MDSTest --import pacote.mdspack pastaBiblioteca
    {
        File pj; auto r = importPack (File (argv[2]), File (argv[3]), pj);
        std::cout << (r.ok ? pj.getFullPathName() : r.message) << std::endl; return r.ok ? 0 : 1;
    }
    const double sr = 48000.0;
    auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("mds_test"); dir.createDirectory();

    // ---------- 1. click com andamento fixo, mudanca de formula e rampa ----------
    {
        Engine e (true);
        Project p; p.voiceMode = 0;
        auto& s = p.songs[0]; s.countInBars = 0;
        s.tempo = { { 2, 4, 4, 120, 120 }, { 1, 7, 8, 120, 120 }, { 2, 4, 4, 120, 240 } };
        p.buses[busClick].outL = 0; p.buses[busClick].outR = -1;
        e.project = p; e.sampleRate = sr; e.prepare (sr, 256);
        e.play();
        auto out = run (e, 2, 9.0, sr);
        auto on = onsets (out, 0, sr);
        CHECK (on.size() >= 19, "click gerou pulsos (" + String ((int) on.size()) + ")");
        bool fixedOk = true; for (int i = 0; i < 8; ++i) fixedOk = fixedOk && std::abs (on[(size_t) i] - 0.5 * i) < 0.002;
        CHECK (fixedOk, "8 pulsos a 120 BPM caem a cada 0,5 s");
        bool sevenOk = true; for (int i = 0; i < 7; ++i) sevenOk = sevenOk && std::abs (on[(size_t) (8 + i)] - (4.0 + 0.25 * i)) < 0.002;
        CHECK (sevenOk, "compasso 7/8 gera 7 pulsos de colcheia");
        CHECK (std::abs (on[15] - 5.75) < 0.002, "volta ao 4/4 no tempo certo (5,75 s)");
        // rampa 120->240 em 8 tempos: t(x) = 60*L/(b1-b0) * ln(bpm(x)/b0)
        auto tr = [] (double x) { return 60.0 * 8.0 / 120.0 * std::log ((120.0 + 15.0 * x) / 120.0); };
        bool rampOk = true; for (int i = 1; i < 4; ++i) rampOk = rampOk && std::abs (on[(size_t) (15 + i)] - (5.75 + tr (i))) < 0.003;
        CHECK (rampOk, "accelerando 120->240 segue a curva teorica");
    }

    // ---------- 1b. padrao de click customizado ----------
    {
        Engine e (true);
        Project p; p.voiceMode = 0;
        auto& s = p.songs[0]; s.countInBars = 0;
        s.tempo = { { 4, 4, 4, 120, 120 } }; s.tempo[0].pattern = "3012";     // forte, mudo, fraco, normal
        e.project = p; e.sampleRate = sr; e.prepare (sr, 256);
        e.play();
        auto out = run (e, 2, 2.0, sr);
        auto peak = [&] (double t) { return out.getMagnitude (0, (int) (t * sr), (int) (0.05 * sr)); };
        CHECK (peak (0.0) > 0.5f && peak (0.5) < 0.001f, "padrao: tempo 1 forte, tempo 2 mudo");
        CHECK (peak (1.0) > 0.1f && peak (1.0) < 0.6f * peak (1.5), "padrao: tempo 3 fraco, mais baixo que o tempo 4 normal");
    }

    // ---------- 2. contagem inicial, cenas e acao de seguimento ----------
    auto a = makeTone (dir, "a.wav", 440, 10.0), b = makeTone (dir, "b.wav", 880, 10.0, 44100.0), g = makeTone (dir, "guia.wav", 300, 10.0);
    Project p; p.voiceMode = 0; p.clickOn = false;
    {
        auto& s = p.songs[0]; s.name = "Teste"; s.countInBars = 1;
        s.tempo = { { 8, 4, 4, 120, 120 } };
        s.tracks = { { "VS", false, 0, -1 }, { "Guia", true, 1, -1 } };
        s.scenes = { { "Intro", 1, followNext, "" }, { "Verso", 2, followStop, "" } };
        s.normalise();
        s.clips[0][0].file = a.getFullPathName(); s.clips[1][0].file = b.getFullPathName(); s.clips[1][1].file = g.getFullPathName();
    }
    {
        Engine e (true);
        e.project = p; e.sampleRate = sr; e.prepare (sr, 256);
        e.play();
        auto out = run (e, 2, 9.0, sr);
        auto on0 = onsets (out, 0, sr, 0.05f, 0.5), on1 = onsets (out, 1, sr, 0.05f, 0.5);
        CHECK (! on0.empty() && std::abs (on0[0] - 2.0) < 0.002, "cena 1 comeca apos 1 compasso de contagem (2,0 s)");
        CHECK (! on1.empty() && std::abs (on1[0] - 4.0) < 0.002, "follow action: cena 2 entra apos 1 compasso (4,0 s)");
        const int endSample = (int) (8.0 * sr);
        CHECK (out.getMagnitude (0, endSample + 2000, 20000) < 0.001f, "acao 'parar' encerra tudo apos 2 compassos (8,0 s)");
        CHECK (! e.playing.load(), "transporte parado ao final");
        // frequencia do trecho da cena 2 (arquivo 44,1 kHz reamostrado): 880 Hz => ~1760 cruzamentos/s
        int zc = 0; for (int i = (int) (5.0 * sr); i < (int) (6.0 * sr); ++i) if ((out.getSample (0, i) > 0) != (out.getSample (0, i + 1) > 0)) ++zc;
        CHECK (std::abs (zc - 1760) < 6, "reamostragem 44,1k->48k preserva a afinacao (" + String (zc) + " cruzamentos)");
        // continuidade na juncao memoria->disco (2 s depois do inicio do clipe): sem estalo
        float maxJump = 0; for (int i = (int) (5.9 * sr); i < (int) (6.1 * sr); ++i) maxJump = jmax (maxJump, std::abs (out.getSample (0, i + 1) - out.getSample (0, i)));
        CHECK (maxJump < 0.08f, "sem estalo na juncao memoria/disco (salto max " + String (maxJump, 4) + ")");
    }

    // ---------- 3. disparo manual de cena quantizado ao compasso ----------
    {
        Engine e (true);
        auto q = p; q.songs[0].scenes[0].follow = followNone; q.songs[0].countInBars = 0;
        e.project = q; e.sampleRate = sr; e.prepare (sr, 256);
        e.play();
        auto out = run (e, 2, 6.0, sr, [&] (int k) { if (k == (int) (2.7 * sr / 256)) e.launchScene (1); });
        auto on1 = onsets (out, 1, sr, 0.05f, 0.5);
        CHECK (! on1.empty() && std::abs (on1[0] - 4.0) < 0.002, "cena disparada em 2,7 s entra no proximo compasso (4,0 s)");
    }

    // ---------- 4. exportar para o celular e reimportar ----------
    {
        auto pack = dir.getChildFile ("teste.mdspack");
        auto r = exportPack (p, pack, {});
        CHECK (r.ok && pack.getSize() > 1000, "pacote exportado (" + String (pack.getSize() / 1024) + " kB)");
        File pj; auto lib = dir.getChildFile ("lib");
        auto ir = importPack (pack, lib, pj);
        Project m; const bool loaded = ir.ok && Project::load (pj, m);
        CHECK (loaded, "pacote importado e lido");
        if (loaded)
        {
            auto& s = m.songs[0];
            CHECK (s.tracks.size() == 2 && s.scenes.size() == 1 && s.scenes[0].bars == 3, "musica convertida em 1 cena de 3 compassos e 2 trilhas");
            AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (File (s.clips[0][0].file)));
            CHECK (rd != nullptr && rd->numChannels == 2, "audio OGG split stereo presente");
            if (rd != nullptr)
            {
                AudioBuffer<float> buf (2, (int) rd->lengthInSamples); rd->read (&buf, 0, buf.getNumSamples(), 0, true, true);
                const int n1 = (int) (1.0 * rd->sampleRate), n3 = (int) (3.0 * rd->sampleRate);
                CHECK (buf.getMagnitude (1, n1 / 2, n1 / 4) > 0.1f && buf.getMagnitude (0, n1 / 2, n1 / 4) < 0.01f, "trecho 1: VS no canal R, canal L em silencio");
                CHECK (buf.getMagnitude (0, n3, n1 / 4) > 0.1f && buf.getMagnitude (1, n3, n1 / 4) > 0.1f, "trecho 2: guia no canal L e VS no canal R");
                CHECK (std::abs (rd->lengthInSamples / rd->sampleRate - 6.0) < 0.2, "duracao total de 3 compassos (6 s)");
            }
            // toca o pacote com o motor em modo celular (split forcado)
            Engine e (true); e.forceSplit = true; m.voiceMode = 0; m.clickOn = true;
            e.project = m; e.sampleRate = sr; e.prepare (sr, 256);
            e.play();
            auto out = run (e, 2, 5.0, sr);
            auto clicks = onsets (out, 0, sr, 0.3f, 0.2);
            CHECK (clicks.size() >= 4 && std::abs (clicks[0]) < 0.002, "celular: click no canal L desde a contagem");
            CHECK (out.getMagnitude (1, (int) (2.5 * sr), 4800) > 0.1f && out.getMagnitude (1, (int) (1.0 * sr), 4800) < 0.001f, "celular: VS no canal R apos a contagem");
        }
    }

    // ---------- 5. one-shots e Auto-PAD ----------
    {
        Engine e (true); e.forceSplit = true;
        Project q; q.songs[0].padKey = 9;
        e.project = q; e.sampleRate = sr; e.prepare (sr, 256); e.selectSong (0);
        e.setAutoPad (true); e.triggerPad (5);
        auto out = run (e, 2, 6.0, sr);
        CHECK (out.getMagnitude (1, 0, 4800) > 0.2f, "one-shot (bumbo) dispara no canal R");
        CHECK (out.getMagnitude (1, (int) (5.0 * sr), 4800) > 0.02f, "Auto-PAD soando no canal R");
        CHECK (out.getMagnitude (0, 0, out.getNumSamples()) < 1.0e-6f, "nada vaza para o canal L");
        int zc = 0; // fundamental La = 110 Hz dominante
        (void) zc;
    }

    std::cout << (fails == 0 ? "TODOS OS TESTES PASSARAM" : String (fails) + " TESTE(S) FALHARAM") << std::endl;
    dir.deleteRecursively();
    return fails;
}
