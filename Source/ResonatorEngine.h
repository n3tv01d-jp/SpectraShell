// Copyright (C) 2026 Spectra
//
// This file is part of SpectraShell. It is licensed under the GNU Affero
// General Public License, version 3 (AGPLv3). See the LICENSE file in the
// project root for the full license text.

#pragma once

// SpectraShell resonator engine.
//
// A bank of tuned feedback-comb resonators (one per pitch class per octave),
// each with an in-loop damping filter. Plain C++17 with no framework
// dependency so it can be unit-tested outside the plugin.
//
// Efficiency notes:
//  * Voices that are gated off and have rung out are skipped entirely.
//  * All transcendental math (pow/exp/atan) happens once per voice per
//    64-sample chunk; the per-sample loop is a handful of multiply-adds.
//  * Delay lines are power-of-two ring buffers (mask instead of modulo).

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace spectra
{
constexpr int kNumPitchClasses = 12;
constexpr int kMaxStack        = 4;   // octaves stacked per pitch class
constexpr int kNumSlots        = kNumPitchClasses * kMaxStack;
constexpr int kMaxChannels     = 2;
constexpr int kChunk           = 64;  // control-rate granularity (samples)

struct Params
{
    uint16_t gateMask   = 0;      // bit n = pitch class n resonates
    int   baseOctave    = 3;      // 3 => C3 (MIDI 48) is the lowest root
    int   stack         = 2;      // number of octaves stacked (1..4)
    float tuneCents     = 0.0f;   // global fine tune
    float widthCents    = 0.0f;   // L/R detune spread (total)
    float decaySec      = 1.5f;   // T60 while a note is held
    float releaseSec    = 0.4f;   // T60 after a note is released (<= decay)
    float brightness    = 0.65f;  // 0..1, in-loop damping
    bool  hollow        = false;  // false: all harmonics, true: odd only
    float lowCutHz      = 80.0f;  // high-pass on the signal feeding the resonators
    float mix           = 0.5f;   // dry/wet
    float outGainDb     = 0.0f;
};

class ResonatorEngine
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        fs = (float) sampleRate;
        channels = std::clamp (numChannels, 1, kMaxChannels);

        // Lowest possible pitch: C1 (32.7 Hz) minus 1 semitone of tune and
        // half the max width -> ~30 Hz. Leave margin for interpolation.
        const int needed = (int) std::ceil (fs / 25.0f) + 8;
        bufLen = 1;
        while (bufLen < needed)
            bufLen <<= 1;
        bufMask = bufLen - 1;

        for (auto& s : slots)
            for (auto& l : s.lines)
                l.buf.assign ((size_t) bufLen, 0.0f);

        reset();
    }

    void reset()
    {
        for (auto& s : slots)
        {
            for (auto& l : s.lines)
            {
                std::fill (l.buf.begin(), l.buf.end(), 0.0f);
                l.write = 0; l.lp = 0.0f; l.delay = 0.0f; l.fb = 0.0f;
            }
            s.inGain = 0.0f; s.active = false; s.quiet = 0;
        }
        for (auto& h : hp) h = {};
        voiceNorm = 1.0f;
        smMix = params.mix;
        smOut = dbToGain (params.outGainDb);
        meterState.fill (0.0f);
        for (auto& m : meters) m.store (0.0f, std::memory_order_relaxed);
        activeVoices.store (0, std::memory_order_relaxed);
    }

    void setParams (const Params& p) noexcept { params = p; }

    void process (float* const* io, int numChannels, int numSamples) noexcept
    {
        const int nCh = std::min ({ numChannels, channels, kMaxChannels });
        if (nCh <= 0 || bufLen == 0)
            return;

        std::array<float, kNumPitchClasses> blockPeak {};

        for (int pos = 0; pos < numSamples;)
        {
            const int len = std::min (kChunk, numSamples - pos);
            processChunk (io, nCh, pos, len, blockPeak);
            pos += len;
        }

        // Meters for the UI: peak hold with ~300 ms fall.
        const float fall = std::exp (-(float) numSamples / (0.3f * fs));
        int voices = 0;
        for (auto& s : slots) voices += s.active ? 1 : 0;
        for (size_t pc = 0; pc < (size_t) kNumPitchClasses; ++pc)
        {
            meterState[pc] = std::max (blockPeak[pc] * voiceNorm, meterState[pc] * fall);
            meters[pc].store (meterState[pc], std::memory_order_relaxed);
        }
        activeVoices.store (voices, std::memory_order_relaxed);
    }

    float getPitchClassLevel (int pc) const noexcept { return meters[(size_t) pc].load (std::memory_order_relaxed); }
    int   getActiveVoiceCount() const noexcept      { return activeVoices.load (std::memory_order_relaxed); }

private:
    struct Line
    {
        std::vector<float> buf;
        int   write = 0;
        float lp    = 0.0f;   // damping filter state (also the voice output)
        float delay = 0.0f;   // current delay, samples (smoothed)
        float fb    = 0.0f;   // current feedback gain (smoothed)
    };

    struct Slot
    {
        std::array<Line, kMaxChannels> lines;
        float inGain = 0.0f;
        bool  active = false;
        int   quiet  = 0;     // samples spent below the silence threshold
    };

    struct HighPass   // TPT state-variable filter, Butterworth Q
    {
        float ic1 = 0.0f, ic2 = 0.0f;
    };

    static float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

    // Soft ceiling used inside the feedback loop: transparent up to |t|,
    // then bends smoothly toward t+1. Guarantees the loop can never blow up.
    static inline float softCeil (float v, float t) noexcept
    {
        const float a = std::fabs (v);
        if (a <= t) return v;
        const float e = a - t;
        return std::copysign (t + e / (1.0f + e), v);
    }

    // Phase delay (samples) of the one-pole lowpass a/(1-(1-a)z^-1) at w rad/sample.
    static float onePolePhaseDelay (float a, float w) noexcept
    {
        const float b = 1.0f - a;
        return std::atan2 (b * std::sin (w), 1.0f - b * std::cos (w)) / w;
    }

    void processChunk (float* const* io, int nCh, int offset, int len,
                       std::array<float, kNumPitchClasses>& blockPeak) noexcept
    {
        const Params& p = params;

        // --- Prepare exciter (high-passed input) and keep the dry signal ----
        {
            const float g  = std::tan (3.14159265f * std::clamp (p.lowCutHz, 10.0f, 0.45f * fs) / fs);
            const float k  = 1.41421356f;
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float a3 = g * a2;

            for (int c = 0; c < nCh; ++c)
            {
                const float* in = io[c] + offset;
                auto& st = hp[(size_t) c];
                for (int i = 0; i < len; ++i)
                {
                    const float x  = in[i];
                    dry[c][i] = x;
                    const float v3 = x - st.ic2;
                    const float v1 = a1 * st.ic1 + a2 * v3;
                    const float v2 = st.ic2 + a2 * st.ic1 + a3 * v3;
                    st.ic1 = 2.0f * v1 - st.ic1;
                    st.ic2 = 2.0f * v2 - st.ic2;
                    exc[c][i] = x - k * v1 - v2;
                }
                std::fill (wet[c], wet[c] + len, 0.0f);
            }
        }

        // --- Shared per-chunk coefficients -----------------------------------
        const float chunkSec = (float) len / fs;
        const float aSlow  = 1.0f - std::exp (-chunkSec / 0.030f); // pitch / feedback glide
        const float aGate  = 1.0f - std::exp (-chunkSec / 0.006f); // input gate
        const float aLevel = 1.0f - std::exp (-chunkSec / 0.050f); // voice normalisation, mix

        // Brightness -> damping cutoff 300 Hz .. ~38 kHz (exponential).
        const float cutoff = 300.0f * std::exp2 (std::clamp (p.brightness, 0.0f, 1.0f) * 7.0f);
        const float damp   = std::clamp (1.0f - std::exp (-6.2831853f * cutoff / fs), 0.01f, 1.0f);

        const float holdT60 = std::max (p.decaySec, 0.01f);
        const float relT60  = std::max (std::min (p.releaseSec, p.decaySec), 0.01f);
        const float sgn     = p.hollow ? -1.0f : 1.0f;
        const float periodScale = p.hollow ? 0.5f : 1.0f;
        const int   stack   = std::clamp (p.stack, 1, kMaxStack);
        const int   rootMidi = 12 * (std::clamp (p.baseOctave, 0, 8) + 1);
        const float maxDelay = (float) bufLen - 8.0f;

        int gatedCount = 0;

        for (int s = 0; s < kNumSlots; ++s)
        {
            Slot& slot = slots[(size_t) s];
            const int pc  = s / kMaxStack;
            const int oct = s % kMaxStack;

            const bool wanted = ((p.gateMask >> pc) & 1u) != 0 && oct < stack;
            if (! wanted && ! slot.active)
                continue;

            const float midi = (float) (rootMidi + pc + 12 * oct);

            // Per-channel tuning.
            float targetDelay[kMaxChannels] {};
            float loopLen[kMaxChannels] {};
            bool valid = true;
            for (int c = 0; c < nCh; ++c)
            {
                float cents = p.tuneCents;
                if (nCh == 2) cents += (c == 0 ? -0.5f : 0.5f) * p.widthCents;
                const float f  = 440.0f * std::exp2 ((midi - 69.0f) / 12.0f + cents / 1200.0f);
                const float Lt = fs / f * periodScale;
                const float w  = 6.2831853f * f / fs;
                const float L  = Lt - onePolePhaseDelay (damp, w);
                if (f > 0.45f * fs || L < 4.0f || L > maxDelay) valid = false;
                loopLen[c]     = Lt;
                targetDelay[c] = std::clamp (L, 4.0f, maxDelay);
            }

            const bool gate = wanted && valid;
            if (gate) ++gatedCount;

            if (! slot.active)
            {
                if (! gate) continue;
                slot.active = true;
                slot.quiet  = 0;
                slot.inGain = 0.0f;
                for (int c = 0; c < kMaxChannels; ++c)
                {
                    auto& l = slot.lines[(size_t) c];
                    std::fill (l.buf.begin(), l.buf.end(), 0.0f);
                    l.lp = 0.0f; l.write = 0;
                    l.delay = targetDelay[std::min (c, nCh - 1)];
                    l.fb = 0.0f;
                }
            }

            // Input normalisation: sqrt(1-g^2) keeps broadband energy roughly
            // constant regardless of decay time.
            const float gHold  = std::pow (10.0f, -3.0f * loopLen[0] / (holdT60 * fs));
            const float igTarget = gate ? std::sqrt (std::max (1.0f - gHold * gHold, 0.0f)) : 0.0f;
            const float ig0 = slot.inGain;
            const float ig1 = ig0 + (igTarget - ig0) * aGate;
            const float igStep = (ig1 - ig0) / (float) len;
            slot.inGain = (std::fabs (ig1) < 1e-7f) ? 0.0f : ig1;

            float peak = 0.0f;

            for (int c = 0; c < nCh; ++c)
            {
                Line& l = slot.lines[(size_t) c];
                const float T60 = gate ? holdT60 : relT60;
                const float gT  = std::min (std::pow (10.0f, -3.0f * loopLen[c] / (T60 * fs)), 0.99995f);

                const float d0 = l.delay;
                const float d1 = d0 + (targetDelay[c] - d0) * aSlow;
                const float dStep = (d1 - d0) / (float) len;
                const float g0 = l.fb;
                const float g1 = g0 + (gT - g0) * aSlow;
                const float gStep = (g1 - g0) / (float) len;

                float* buf = l.buf.data();
                const int mask = bufMask;
                const float bufLenF = (float) bufLen;
                int   w  = l.write;
                float lp = l.lp;
                const float* x = exc[c];
                float* out = wet[c];

                for (int i = 0; i < len; ++i)
                {
                    const float d  = d0 + dStep * (float) i;
                    const float fb = g0 + gStep * (float) i;
                    const float ig = ig0 + igStep * (float) i;

                    float rp = (float) w - d;
                    if (rp < 0.0f) rp += bufLenF;
                    const int   i1 = (int) rp;
                    const float fr = rp - (float) i1;

                    const float xm1 = buf[(i1 - 1) & mask];
                    const float x0  = buf[i1 & mask];
                    const float x1  = buf[(i1 + 1) & mask];
                    const float x2  = buf[(i1 + 2) & mask];

                    // 4-point cubic Hermite interpolation
                    const float c1 = 0.5f * (x1 - xm1);
                    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
                    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                    const float y  = ((c3 * fr + c2) * fr + c1) * fr + x0;

                    lp += damp * (y - lp);

                    buf[w] = softCeil (x[i] * ig + sgn * fb * lp, 2.0f);
                    w = (w + 1) & mask;

                    out[i] += lp;
                    peak = std::max (peak, std::fabs (lp));
                }

                l.write = w;
                l.lp    = (std::fabs (lp) < 1e-20f) ? 0.0f : lp;
                l.delay = d1;
                l.fb    = g1;
            }

            blockPeak[(size_t) pc] = std::max (blockPeak[(size_t) pc], peak);

            // Retire voices that are gated off and silent.
            if (! gate && slot.inGain <= 0.0f && peak < 1e-5f)
            {
                slot.quiet += len;
                if (slot.quiet > (int) (0.05f * fs))
                    slot.active = false;
            }
            else
            {
                slot.quiet = 0;
            }
        }

        // --- Mix --------------------------------------------------------------
        const float normTarget = 1.0f / std::sqrt ((float) std::max (gatedCount, 1));
        const float n0 = voiceNorm;
        voiceNorm += (normTarget - voiceNorm) * aLevel;

        const float m0 = smMix;
        smMix += (std::clamp (p.mix, 0.0f, 1.0f) - smMix) * aLevel;
        const float o0 = smOut;
        smOut += (dbToGain (p.outGainDb) - smOut) * aLevel;

        const float inv = 1.0f / (float) len;
        const float nStep = (voiceNorm - n0) * inv;
        const float mStep = (smMix - m0) * inv;
        const float oStep = (smOut - o0) * inv;

        for (int c = 0; c < nCh; ++c)
        {
            float* o = io[c] + offset;
            for (int i = 0; i < len; ++i)
            {
                const float fi = (float) i;
                const float mx = m0 + mStep * fi;
                const float wv = softCeil (wet[c][i] * (n0 + nStep * fi), 1.0f);
                o[i] = (dry[c][i] * (1.0f - mx) + wv * mx) * (o0 + oStep * fi);
            }
        }
    }

    Params params;
    float fs = 48000.0f;
    int channels = 2;
    int bufLen = 0, bufMask = 0;

    std::array<Slot, kNumSlots> slots;
    std::array<HighPass, kMaxChannels> hp {};

    float exc[kMaxChannels][kChunk] {};
    float dry[kMaxChannels][kChunk] {};
    float wet[kMaxChannels][kChunk] {};

    float voiceNorm = 1.0f, smMix = 0.5f, smOut = 1.0f;

    std::array<float, kNumPitchClasses> meterState {};
    std::array<std::atomic<float>, kNumPitchClasses> meters {};
    std::atomic<int> activeVoices { 0 };
};

} // namespace spectra
