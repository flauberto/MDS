// MDS - sons sintetizados (click, one-shots embutidos) e banco de amostras
#pragma once
#include "Model.h"
#include <cmath>

namespace bt
{
using Sample = std::vector<float>;

// Indices no banco de amostras
enum SampleId { sClickAccent = 0, sClickBeat, sClickSub, sVoiceNum /*12*/ = 3, sVoiceCue = 15 /*8*/, sPad = 23 /*16*/, sCount = 39 };

struct SampleBank { Sample s[sCount]; };

namespace synth
{
    inline float rnd (uint32_t& st) { st = st * 1664525u + 1013904223u; return (float) ((st >> 8) & 0xffff) / 32768.0f - 1.0f; }

    inline Sample click (int sound, int kind /*0 acento, 1 tempo, 2 subdivisao*/, double sr)
    {
        static const double freqs[4][3] = { { 1760, 1175, 880 }, { 2200, 1650, 1100 }, { 845, 587, 440 }, { 988, 659, 494 } };
        const double f = freqs[juce::jlimit (0, 3, sound)][kind];
        const double len = (sound == 2 ? 0.09 : 0.045) * (kind == 2 ? 0.6 : 1.0);
        const int n = (int) (len * sr);
        Sample out ((size_t) n);
        uint32_t seed = 12345u;
        const float level = kind == 0 ? 0.9f : (kind == 1 ? 0.65f : 0.4f);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr, ph = juce::MathConstants<double>::twoPi * f * t;
            const double env = std::exp (-t / (len * 0.28)) * juce::jmin (1.0, i / (0.0005 * sr));
            double v = 0;
            switch (sound)
            {
                case 0: v = std::sin (ph); break;
                case 1: v = 0.7 * std::sin (ph) + 0.3 * std::sin (ph * 2.76) + 0.25 * rnd (seed) * std::exp (-t / 0.004); break;
                case 2: v = 0.5 * (std::sin (ph) > 0 ? 1.0 : -1.0) + 0.5 * (std::sin (ph * 1.48) > 0 ? 1.0 : -1.0); v *= 0.6; break;
                default: v = (std::sin (ph) > 0 ? 0.6 : -0.6); break;
            }
            out[(size_t) i] = (float) (v * env) * level;
        }
        return out;
    }

    inline Sample builtinPad (int which, double sr)
    {
        const double twoPi = juce::MathConstants<double>::twoPi;
        uint32_t seed = 777u + (uint32_t) which;
        auto make = [&] (double seconds, auto fn)
        {
            const int n = (int) (seconds * sr);
            Sample o ((size_t) n);
            for (int i = 0; i < n; ++i) o[(size_t) i] = (float) fn (i / sr, seconds);
            const int fade = juce::jmin (n, (int) (0.01 * sr));          // evita estalo no fim
            for (int i = 0; i < fade; ++i) o[(size_t) (n - 1 - i)] *= (float) i / (float) fade;
            return o;
        };
        double lp = 0, hpPrev = 0, phase = 0;
        switch (which)
        {
            case 0: // shaker
                return make (0.14, [&] (double t, double) { double x = rnd (seed); double h = x - hpPrev; hpPrev = x;
                                                           return 0.5 * h * std::exp (-t / 0.035) * juce::jmin (1.0, t / 0.008); });
            case 1: // palma
                return make (0.3, [&] (double t, double) { double x = rnd (seed); lp += 0.45 * (x - lp); double band = x - lp;
                                                          double e = 0; for (double d : { 0.0, 0.011, 0.023 }) if (t >= d) e += std::exp (-(t - d) / 0.006);
                                                          e += (t >= 0.03 ? std::exp (-(t - 0.03) / 0.07) : 0.0);
                                                          return 0.45 * band * e; });
            case 2: // sub drop
                return make (2.5, [&] (double t, double len) { double f = 30.0 + 70.0 * std::exp (-t / 0.5); phase += twoPi * f / sr;
                                                              return 0.9 * std::sin (phase) * std::exp (-t / 0.9) * juce::jmin (1.0, (len - t) / 0.2); });
            case 3: // riser
                return make (4.0, [&] (double t, double len) { double x = rnd (seed); double k = 0.02 + 0.6 * std::pow (t / len, 2.0); lp += k * (x - lp);
                                                              phase += twoPi * (200.0 + 1800.0 * std::pow (t / len, 2.0)) / sr;
                                                              return (0.45 * lp + 0.12 * std::sin (phase)) * std::pow (t / len, 1.5); });
            case 4: // impacto
                return make (3.0, [&] (double t, double) { double x = rnd (seed); lp += 0.08 * (x - lp); double f = 38.0 + 60.0 * std::exp (-t / 0.08); phase += twoPi * f / sr;
                                                          return 0.8 * std::sin (phase) * std::exp (-t / 0.7) + 0.5 * lp * std::exp (-t / 0.5); });
            case 5: // bumbo
                return make (0.45, [&] (double t, double) { double f = 48.0 + 110.0 * std::exp (-t / 0.03); phase += twoPi * f / sr;
                                                           return 0.95 * std::sin (phase) * std::exp (-t / 0.14); });
            case 6: // pandeiro
                return make (0.35, [&] (double t, double) { double x = rnd (seed); double h = x - hpPrev; hpPrev = x;
                                                           double jing = std::sin (twoPi * 6100 * t) * std::sin (twoPi * 8300 * t);
                                                           return (0.3 * h + 0.3 * jing) * std::exp (-t / 0.09); });
            default: // swell
                return make (5.0, [&] (double t, double len) { double x = rnd (seed); lp += 0.05 * (x - lp);
                                                              double env = std::sin (juce::MathConstants<double>::pi * t / len); env *= env;
                                                              return (0.5 * lp + 0.15 * std::sin (twoPi * 220 * t) + 0.1 * std::sin (twoPi * 330 * t)) * env; });
        }
    }

    inline Sample resample (const float* in, int n, double srIn, double srOut)
    {
        if (n <= 0) return {};
        const double ratio = srIn / srOut;
        const int outN = (int) (n / ratio);
        Sample o ((size_t) juce::jmax (0, outN));
        for (int i = 0; i < outN; ++i)
        {
            const double p = i * ratio; const int a = (int) p; const float fr = (float) (p - a);
            const float x0 = in[juce::jmin (a, n - 1)], x1 = in[juce::jmin (a + 1, n - 1)];
            o[(size_t) i] = x0 + fr * (x1 - x0);
        }
        return o;
    }

    // Le um arquivo/bloco de audio inteiro como mono, na taxa pedida (limitado a maxSeconds)
    inline Sample loadMono (juce::AudioFormatReader* r, double srOut, double maxSeconds = 30.0)
    {
        if (r == nullptr || r->sampleRate <= 0) return {};
        const int n = (int) juce::jmin ((juce::int64) (maxSeconds * r->sampleRate), r->lengthInSamples);
        juce::AudioBuffer<float> b (2, n);
        r->read (&b, 0, n, 0, true, true);
        if (r->numChannels > 1) { b.addFrom (0, 0, b, 1, 0, n); b.applyGain (0, 0, n, 0.5f); }
        return resample (b.getReadPointer (0), n, r->sampleRate, srOut);
    }
}

// Drone/ambiente continuo por tonalidade (Auto-PAD): fundamental + quinta, sem terca
struct Drone
{
    double phase[8] {};
    double freq[8] {};
    float amp[8] {};
    double lfo = 0;
    float gain = 0, target = 0;
    int key = -1;

    void setKey (int k)
    {
        key = k;
        const double root = 65.406 * std::pow (2.0, k / 12.0);   // C2
        const double mult[8] = { 1.0, 1.003, 1.4983, 2.0, 2.004, 2.9966, 4.0, 0.5 };
        const float  amps[8] = { 0.30f, 0.22f, 0.20f, 0.22f, 0.16f, 0.10f, 0.07f, 0.16f };
        for (int i = 0; i < 8; ++i) { freq[i] = root * mult[i]; amp[i] = amps[i]; }
    }
    bool silent() const { return gain < 1.0e-5f && target <= 0.0f; }

    void render (float* dst, int n, double sr, float fadeSeconds)
    {
        const float step = 1.0f / (fadeSeconds * (float) sr);
        const double twoPi = juce::MathConstants<double>::twoPi;
        for (int i = 0; i < n; ++i)
        {
            if (gain < target) gain = juce::jmin (target, gain + step); else if (gain > target) gain = juce::jmax (target, gain - step);
            lfo += twoPi * 0.11 / sr; if (lfo > twoPi) lfo -= twoPi;
            float v = 0;
            for (int h = 0; h < 8; ++h)
            {
                phase[h] += twoPi * freq[h] / sr; if (phase[h] > twoPi) phase[h] -= twoPi;
                v += amp[h] * (float) std::sin (phase[h] + (h & 1 ? 0.6 * std::sin (lfo) : 0.0));
            }
            dst[i] += v * gain * gain * 0.5f * (0.85f + 0.15f * (float) std::sin (lfo * 1.7));
        }
    }
};
}
