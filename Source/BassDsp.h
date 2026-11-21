#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

struct BassParams { float inDb, vowel, grit, crunch, sub, outDb; };

// Chain: IN -> VOWEL (two formant band-passes) -> GRIT (tanh) -> CRUNCH (squash) -> + SUB -> OUT (tanh limit)
class BassDsp
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        using T = juce::dsp::StateVariableTPTFilterType;
        const juce::dsp::ProcessSpec spec { sampleRate, 4096u, (juce::uint32) numChannels };

        for (auto* f : { &bp1, &bp2 })       { f->prepare (spec); f->setType (T::bandpass); f->setResonance (kQ); }
        for (auto* f : { &detLp1, &detLp2 }) { f->prepare (spec); f->setType (T::lowpass);  f->setCutoffFrequency (250.f); }
        for (auto* f : { &subLp1, &subLp2 }) { f->prepare (spec); f->setType (T::lowpass);  f->setCutoffFrequency (150.f); }
        for (auto* s : { &gIn, &gVowel, &gGrit, &gCrunch, &gSub, &gOut }) s->reset (sampleRate, 0.02);

        const auto coef = [sampleRate] (float seconds) { return 1.f - std::exp (-1.f / (seconds * (float) sampleRate)); };
        atkC = coef (0.002f); relC = coef (0.08f);
        atkF = coef (0.005f); relF = coef (0.05f); relIn = coef (0.1f);

        st = {};
        primed = false;
    }

    // io: one pointer per channel, processed in place. Up to 2 channels.
    void process (float* const* io, int numCh, int n, const BassParams& p)
    {
        numCh = juce::jmin (numCh, 2);
        gIn.setTargetValue (juce::Decibels::decibelsToGain (p.inDb));
        gVowel.setTargetValue (p.vowel);
        gGrit.setTargetValue (p.grit);
        gCrunch.setTargetValue (p.crunch);
        gSub.setTargetValue (p.sub);
        gOut.setTargetValue (juce::Decibels::decibelsToGain (p.outDb));

        if (! primed)
        {
            for (auto* s : { &gIn, &gVowel, &gGrit, &gCrunch, &gSub, &gOut }) s->setCurrentAndTargetValue (s->getTargetValue());
            primed = true;
        }

        for (int i = 0; i < n; ++i)
        {
            const float g = gIn.getNextValue(), vow = gVowel.getNextValue(), grit = gGrit.getNextValue();
            const float crunch = gCrunch.getNextValue(), sub = gSub.getNextValue(), og = gOut.getNextValue();

            // VOWEL 0..1 sweeps A-E-I-O-U
            const float pos = vow * 4.f;
            const int k = juce::jmin ((int) pos, 3);
            const float fr = pos - (float) k;
            bp1.setCutoffFrequency (juce::jmap (fr, kVowel[k][0], kVowel[k + 1][0]));
            bp2.setCutoffFrequency (juce::jmap (fr, kVowel[k][1], kVowel[k + 1][1]));

            const float drive = 1.f + grit * 30.f;
            const float expo = crunch * 0.9f;

            for (int c = 0; c < numCh; ++c)
            {
                auto& s = st[(size_t) c];
                const float x = io[c][i] * g;

                const float v = (bp1.processSample (c, x) + 0.7f * bp2.processSample (c, x)) * kBpGain;
                // shortcut: no oversampling, GRIT aliases at high settings, upgrade to juce::dsp::Oversampling
                const float d = std::tanh (v * drive) * 0.7f;

                // shortcut: single-band squash, OTT is 3-band, upgrade with juce::dsp::LinkwitzRileyFilter
                follow (s.envC, std::abs (d), atkC, relC);
                const float y = d * juce::jlimit (0.1f, 30.f, std::pow (kTarget / (s.envC + 1e-4f), expo));

                // shortcut: octave-divider sub, monophonic input only, upgrade to a real pitch tracker
                const float f0 = detLp2.processSample (c, detLp1.processSample (c, x));
                follow (s.envF, std::abs (f0), atkF, relF);
                const float h = 0.1f * s.envF;
                if (! s.armed && f0 < -h)      s.armed = true;
                else if (s.armed && f0 > h)  { s.armed = false; s.flip = -s.flip; }
                follow (s.envIn, std::abs (x), atkF, relIn);
                const float subOut = subLp2.processSample (c, subLp1.processSample (c, s.flip))
                                     * juce::jmin (s.envIn * 4.f, 1.f) * sub * 0.6f;

                const float out = std::tanh ((y + subOut) * og);
                io[c][i] = out;
                peak = juce::jmax (peak, std::abs (out));
            }
        }
    }

    float takePeak() { const float p = peak; peak = 0.f; return p; }

private:
    static constexpr float kQ = 6.f, kBpGain = 0.5f, kTarget = 0.18f;
    static constexpr float kVowel[5][2] = { { 800, 1150 }, { 400, 1600 }, { 350, 2300 }, { 450, 800 }, { 325, 700 } }; // A E I O U (F1, F2 Hz)

    struct Ch { float envC = 0, envF = 0, envIn = 0, flip = 1; bool armed = false; };

    static void follow (float& e, float a, float atk, float rel) { e += (a > e ? atk : rel) * (a - e); }

    juce::dsp::StateVariableTPTFilter<float> bp1, bp2, detLp1, detLp2, subLp1, subLp2;
    juce::SmoothedValue<float> gIn, gVowel, gGrit, gCrunch, gSub, gOut;
    std::array<Ch, 2> st {};
    float atkC = 0, relC = 0, atkF = 0, relF = 0, relIn = 0, peak = 0;
    bool primed = false;
};
