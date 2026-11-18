#include "TestHelpers.h"
#include "LatentDecoder.h"

using namespace TestHelpers;

class DecoderTests : public juce::UnitTest
{
public:
    DecoderTests() : juce::UnitTest ("LatentDecoder", "LatentAudio") {}

    void runTest() override
    {
        const auto modelFile = tinyModelDir().getChildFile ("latent_decoder.onnx");

        beginTest ("loads the tiny model");
        LatentDecoder dec;
        auto res = dec.loadModel (modelFile, kTinyLatentDim);
        expect (res.ok, res.error);
        expect (dec.isModelLoaded());
        expectEquals (dec.getModelLatentDim(), kTinyLatentDim);
        expect (dec.getActiveProvider() != "None");
        logMessage ("    provider: " + dec.getActiveProvider());

        beginTest ("decode matches tanh(z W) exactly");
        std::vector<float> z (kTinyLatentDim, 0.0f);
        z[0] = 1.0f; z[3] = -0.5f;
        std::vector<float> out;
        expect (dec.decode (z, out));
        expectEquals ((int) out.size(), kTinySamples);
        const auto expected = tinyModelExpected (z);
        float maxErr = 0.0f;
        for (size_t i = 0; i < out.size(); ++i)
            maxErr = std::max (maxErr, std::abs (out[i] - expected[i]));
        expectLessThan (maxErr, 1.0e-5f);

        beginTest ("wrong latent size is rejected, not crashed");
        std::vector<float> bad (3, 0.0f);
        expect (! dec.decode (bad, out));

        beginTest ("latent size mismatch is refused and keeps the current model");
        res = dec.loadModel (modelFile, 16);
        expect (! res.ok);
        expect (res.error.contains ("latent size"), res.error);
        expect (dec.isModelLoaded());

        beginTest ("missing file gives an error");
        res = dec.loadModel (modelFile.getSiblingFile ("nope.onnx"));
        expect (! res.ok && res.error.isNotEmpty());
        expect (dec.isModelLoaded());

        beginTest ("instances share one session; unload frees it");
        {
            LatentDecoder other;
            expect (other.loadModel (modelFile).ok);
            expectEquals (LatentDecoder::getCacheSize(), 1);
            other.unload();
            expectEquals (LatentDecoder::getCacheSize(), 1); // `dec` still uses it
        }
        dec.unload();
        expect (! dec.isModelLoaded());
        expectEquals (LatentDecoder::getCacheSize(), 0);
        expect (! dec.decode (z, out));

        beginTest ("decoding while the model is swapped and unloaded does not crash");
        expect (dec.loadModel (modelFile).ok);
        std::atomic<bool> stop { false };
        std::atomic<int> ok { 0 }, wrongSize { 0 };
        std::vector<std::thread> workers;
        for (int t = 0; t < 4; ++t)
            workers.emplace_back ([&]
            {
                std::vector<float> o;
                while (! stop.load())
                {
                    if (dec.decode (z, o))
                    {
                        ++ok;
                        if ((int) o.size() != kTinySamples) ++wrongSize;
                    }
                }
            });
        for (int i = 0; i < 40; ++i)
        {
            if (i % 3 == 0) dec.unload();
            else expect (dec.loadModel (modelFile).ok);
            juce::Thread::sleep (2);
        }
        stop = true;
        for (auto& w : workers) w.join();
        expect (ok.load() > 0);
        expectEquals (wrongSize.load(), 0);
        dec.unload();
    }
};

static DecoderTests decoderTests;
