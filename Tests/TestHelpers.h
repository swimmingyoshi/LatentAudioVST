#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <vector>

namespace TestHelpers
{
    inline juce::File dataDir() { return juce::File (LATENTAUDIO_TEST_DATA); }
    inline juce::File tinyModelDir() { return dataDir().getChildFile ("tiny_model"); }

    constexpr int kTinyLatentDim = 8;
    constexpr int kTinySamples = 2205;

    // Expected output of the tiny model: tanh(z @ W), W[i,j] = 0.5 sin(0.37 i + 0.011 j)
    inline std::vector<float> tinyModelExpected (const std::vector<float>& z)
    {
        std::vector<float> out (kTinySamples);
        for (int j = 0; j < kTinySamples; ++j)
        {
            double acc = 0.0;
            for (int i = 0; i < kTinyLatentDim; ++i)
                acc += z[(size_t) i] * 0.5 * std::sin (0.37 * i + 0.011 * j);
            out[(size_t) j] = (float) std::tanh (acc);
        }
        return out;
    }

    inline std::vector<float> sine (double freq, double sampleRate, int numSamples, float amp = 0.5f)
    {
        std::vector<float> s ((size_t) numSamples);
        for (int i = 0; i < numSamples; ++i)
            s[(size_t) i] = amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sampleRate);
        return s;
    }

    // Frequency from rising zero crossings over [from, to).
    inline double measureFrequency (const std::vector<float>& x, int from, int to, double sampleRate)
    {
        int first = -1, last = -1, count = 0;
        for (int i = std::max (1, from); i < to && i < (int) x.size(); ++i)
        {
            if (x[(size_t) i - 1] < 0.0f && x[(size_t) i] >= 0.0f)
            {
                if (first < 0) first = i;
                last = i;
                ++count;
            }
        }
        return count > 1 ? (count - 1) * sampleRate / (last - first) : 0.0;
    }

    inline float rms (const std::vector<float>& x, int from, int to)
    {
        double acc = 0.0;
        int n = 0;
        for (int i = std::max (0, from); i < to && i < (int) x.size(); ++i, ++n)
            acc += (double) x[(size_t) i] * x[(size_t) i];
        return n > 0 ? (float) std::sqrt (acc / n) : 0.0f;
    }

    inline float peak (const std::vector<float>& x, int from = 0, int to = -1)
    {
        if (to < 0) to = (int) x.size();
        float p = 0.0f;
        for (int i = std::max (0, from); i < to && i < (int) x.size(); ++i)
            p = std::max (p, std::abs (x[(size_t) i]));
        return p;
    }
}
