// Copyright (C) 2026 Spectra
//
// This file is part of SpectraShell. It is licensed under the GNU Affero
// General Public License, version 3 (AGPLv3). See the LICENSE file in the
// project root for the full license text.

// Standalone checks for the resonator engine: tuning accuracy, stability,
// output levels and CPU cost. Build with -DSPECTRASHELL_BUILD_TESTS=ON
// -DSPECTRASHELL_PLUGIN=OFF.

#include "ResonatorEngine.h"

#include <chrono>
#include <cstdio>
#include <random>

using spectra::Params;
using spectra::ResonatorEngine;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); } } while (0)

static float rms (const std::vector<float>& v, size_t from)
{
    double s = 0; for (size_t i = from; i < v.size(); ++i) s += (double) v[i] * v[i];
    return (float) std::sqrt (s / (double) (v.size() - from));
}

// Run engine on a mono-duplicated signal; returns left channel.
static std::vector<float> run (ResonatorEngine& e, const std::vector<float>& in, int block = 512)
{
    std::vector<float> L (in), R (in);
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - pos);
        float* ch[2] = { L.data() + pos, R.data() + pos };
        e.process (ch, 2, n);
    }
    for (float v : L) if (! std::isfinite (v)) { ++failures; std::printf ("FAIL: non-finite output\n"); break; }
    return L;
}

// Estimate the fundamental via normalised autocorrelation (robust to harmonics).
static float estimateFreq (const std::vector<float>& v, size_t from, size_t to, float fs)
{
    const int minLag = (int) (fs / 1000.0f), maxLag = (int) (fs / 50.0f);
    const size_t n = to - from - (size_t) maxLag - 2;
    std::vector<double> r ((size_t) maxLag + 2, 0.0);
    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag)
    {
        double s = 0, e0 = 0, e1 = 0;
        for (size_t i = from; i < from + n; ++i)
        { s += (double) v[i] * v[i + (size_t) lag]; e0 += (double) v[i] * v[i]; e1 += (double) v[i + (size_t) lag] * v[i + (size_t) lag]; }
        r[(size_t) lag] = s / std::sqrt (e0 * e1 + 1e-30);
    }
    // Largest lag whose correlation is within 0.5% of the best = the true period
    // (shorter lags at sub-multiples score lower when harmonics are present).
    int best = minLag;
    for (int lag = minLag; lag <= maxLag; ++lag) if (r[(size_t) lag] > r[(size_t) best]) best = lag;
    for (int lag = minLag; lag <= maxLag; ++lag)
        if (r[(size_t) lag] > r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1] && r[(size_t) lag] > r[(size_t) best] - 0.005) { best = lag; break; }
    const double a = r[(size_t) best - 1], b = r[(size_t) best], c = r[(size_t) best + 1];
    const double off = 0.5 * (a - c) / (a - 2 * b + c);
    return (float) (fs / (best + off));
}

int main()
{
    const float fs = 48000.0f;
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);

    // 1. Tuning: single note A (pc 9), octave 3 -> A3 = 220 Hz, impulse, dark loop.
    for (bool hollow : { false, true })
    for (float bright : { 0.2f, 0.65f, 1.0f })
    {
        ResonatorEngine e; e.prepare (fs, 2);
        Params p; p.gateMask = 1u << 9; p.stack = 1; p.baseOctave = 3; p.decaySec = 5.0f;
        p.mix = 1.0f; p.widthCents = 0.0f; p.lowCutHz = 20.0f; p.brightness = bright; p.hollow = hollow;
        e.setParams (p);
        std::vector<float> in ((size_t) fs, 0.0f);
        for (int i = 2000; i < 2400; ++i) in[(size_t) i] = uni (rng) * 0.5f; // short noise burst
        auto out = run (e, in);
        // Low brightness leaves mostly the fundamental; measure it.
        const float f = estimateFreq (out, 12000, out.size(), fs);
        const float cents = 1200.0f * std::log2 (f / 220.0f);
        std::printf ("tuning hollow=%d bright=%.2f: %.2f Hz (%+.1f ct)\n", hollow, bright, f, cents);
        CHECK (std::fabs (cents) < 5.0f, "tuning off by %.1f cents", cents);
    }

    // 2. Stability / level: full chromatic, max stack, max decay, loud noise.
    {
        ResonatorEngine e; e.prepare (fs, 2);
        Params p; p.gateMask = 0x0FFF; p.stack = 4; p.baseOctave = 1; p.decaySec = 10.0f;
        p.releaseSec = 10.0f; p.brightness = 1.0f; p.mix = 1.0f; p.widthCents = 50.0f;
        e.setParams (p);
        std::vector<float> in ((size_t) (fs * 4));
        for (auto& v : in) v = uni (rng);
        auto out = run (e, in, 333);
        float peak = 0; for (float v : out) peak = std::max (peak, std::fabs (v));
        std::printf ("stress: peak %.3f rms %.3f voices %d\n", peak, rms (out, out.size() / 2), e.getActiveVoiceCount());
        CHECK (peak <= 2.0f, "peak too high %.3f", peak);
    }

    // 3. Pure sine exactly on resonance with long decay (worst case for buildup).
    {
        ResonatorEngine e; e.prepare (fs, 2);
        Params p; p.gateMask = 1u << 9; p.stack = 1; p.baseOctave = 3; p.decaySec = 10.0f; p.mix = 1.0f;
        p.brightness = 1.0f; p.widthCents = 0.0f;
        e.setParams (p);
        std::vector<float> in ((size_t) (fs * 6));
        for (size_t i = 0; i < in.size(); ++i) in[i] = 0.5f * std::sin (6.2831853f * 220.0f * (float) i / fs);
        auto out = run (e, in);
        float peak = 0; for (float v : out) peak = std::max (peak, std::fabs (v));
        std::printf ("on-resonance sine: peak %.3f\n", peak);
        CHECK (peak <= 2.0f, "peak too high %.3f", peak);
    }

    // 4. Broadband level: wet RMS vs input RMS for a typical setting.
    {
        ResonatorEngine e; e.prepare (fs, 2);
        Params p; p.mix = 1.0f; p.gateMask = (1u << 0) | (1u << 3) | (1u << 7);
        e.setParams (p);
        std::vector<float> in ((size_t) (fs * 3));
        for (auto& v : in) v = uni (rng) * 0.3f;
        auto out = run (e, in);
        const float ratioDb = 20.0f * std::log10 (rms (out, out.size() / 2) / rms (in, in.size() / 2));
        std::printf ("noise wet/dry level: %+.1f dB\n", ratioDb);
        CHECK (ratioDb > -18.0f && ratioDb < 6.0f, "wet level %.1f dB out of range", ratioDb);
    }

    // 5. Voice retirement: gate off -> voices go idle after release.
    {
        ResonatorEngine e; e.prepare (fs, 2);
        Params p; p.gateMask = 0x0FFF; p.stack = 4; p.releaseSec = 0.2f; p.mix = 1.0f;
        e.setParams (p);
        std::vector<float> in ((size_t) fs);
        for (auto& v : in) v = uni (rng) * 0.3f;
        run (e, in);
        const int before = e.getActiveVoiceCount();
        p.gateMask = 0; e.setParams (p);
        std::vector<float> silence ((size_t) (fs * 2), 0.0f);
        run (e, silence);
        std::printf ("voices: %d active -> %d after release\n", before, e.getActiveVoiceCount());
        CHECK (before == 48 && e.getActiveVoiceCount() == 0, "voice retirement failed");
    }

    // 6. CPU: 48 voices stereo, 10 s of audio.
    {
        ResonatorEngine e; e.prepare (fs, 2);
        Params p; p.gateMask = 0x0FFF; p.stack = 4; p.decaySec = 5.0f; p.mix = 0.5f;
        e.setParams (p);
        std::vector<float> in ((size_t) (fs * 10));
        for (auto& v : in) v = uni (rng) * 0.3f;
        const auto t0 = std::chrono::steady_clock::now();
        run (e, in, 256);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("cpu: 48 voices stereo, 10 s audio in %.3f s (%.2f%% of one core)\n", sec, sec / 10.0 * 100.0);
        // 3 voices (typical triad, stack 1)
        p.gateMask = 0x89; p.stack = 1; e.setParams (p); e.reset();
        const auto t1 = std::chrono::steady_clock::now();
        run (e, in, 256);
        const double sec2 = std::chrono::duration<double> (std::chrono::steady_clock::now() - t1).count();
        std::printf ("cpu: triad stack 1, 10 s audio in %.3f s (%.2f%% of one core)\n", sec2, sec2 / 10.0 * 100.0);
    }

    std::printf (failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
