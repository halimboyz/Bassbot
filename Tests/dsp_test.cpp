#include "BassDsp.h"
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { std::printf ("FAIL: %s\n", msg); ++failures; } } while (0)

// 1 s of a 110 Hz saw through the DSP, 512-sample blocks like a host.
static std::vector<float> render (BassParams p)
{
    BassDsp d;
    d.prepare (48000.0, 1);
    std::vector<float> b (48000);
    for (size_t i = 0; i < b.size(); ++i)
        b[i] = 0.5f * (2.f * std::fmod (110.f * (float) i / 48000.f, 1.f) - 1.f);
    for (int o = 0; o < (int) b.size(); o += 512)
    {
        float* ch[] = { b.data() + o };
        d.process (ch, 1, juce::jmin (512, (int) b.size() - o), p);
    }
    return b;
}

static double rms (const std::vector<float>& b)
{
    double e = 0;
    for (float v : b) e += (double) v * v;
    return std::sqrt (e / (double) b.size());
}

// energy below ~80 Hz (two one-pole low-passes), ignoring the first 0.25 s
static double lowEnergy (const std::vector<float>& b)
{
    const float k = 1.f - std::exp (-2.f * juce::MathConstants<float>::pi * 80.f / 48000.f);
    float y1 = 0, y2 = 0;
    double e = 0;
    for (size_t i = 0; i < b.size(); ++i)
    {
        y1 += k * (b[i] - y1);
        y2 += k * (y1 - y2);
        if (i > 12000) e += (double) y2 * y2;
    }
    return e;
}

static bool sane (const std::vector<float>& b)
{
    for (float v : b) if (! std::isfinite (v) || std::abs (v) > 1.0001f) return false;
    return true;
}

int main()
{
    const BassParams def { 6.f, 0.4f, 0.45f, 0.6f, 0.7f, 0.f };
    const auto a = render (def);
    std::printf ("default: rms=%.4f lowEnergy=%.4f\n", rms (a), lowEnergy (a));
    CHECK (sane (a), "default output finite and within +-1");
    CHECK (rms (a) > 0.01, "default output is not silent");

    BassParams noSub = def, fullSub = def;
    noSub.sub = 0.f; fullSub.sub = 1.f;
    const double lo0 = lowEnergy (render (noSub)), lo1 = lowEnergy (render (fullSub));
    std::printf ("sub=0 low=%.4f  sub=1 low=%.4f\n", lo0, lo1);
    CHECK (lo1 > 2.0 * lo0, "SUB adds low-frequency energy");

    for (float v : { 0.f, 1.f })
    {
        const BassParams ext { v > 0 ? 24.f : -12.f, v, v, v, v, v > 0 ? 6.f : -24.f };
        CHECK (sane (render (ext)), "extreme params stay finite and bounded");
    }

    std::printf (failures ? "FAILED\n" : "ALL OK\n");
    return failures != 0;
}
