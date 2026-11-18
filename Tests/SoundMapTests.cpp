#include "TestHelpers.h"
#include "SoundMap.h"

using namespace TestHelpers;

class SoundMapTests : public juce::UnitTest
{
public:
    SoundMapTests() : juce::UnitTest ("SoundMap", "LatentAudio") {}

    void runTest() override
    {
        juce::String err;

        beginTest ("parses the tiny map");
        auto map = SoundMapData::load (tinyModelDir().getChildFile ("sound_map.json"), err);
        expect (map != nullptr, err);
        if (map == nullptr) return;
        expectEquals ((int) map->points.size(), 3);
        expectEquals (map->latentDim, kTinyLatentDim);
        expectEquals (map->sampleRate, 44100);
        expectEquals (map->minX, -1.0f);
        expectEquals (map->maxX, 1.0f);
        expectEquals (map->minY, -1.0f);
        expectEquals (map->maxY, 1.0f);
        expectEquals (map->points[0].label, juce::String ("a"));

        beginTest ("on a point: returns that point's latent unblended");
        auto z = map->interpolate (-1.0f, -1.0f);
        expectEquals ((int) z.size(), kTinyLatentDim);
        expectEquals (z[0], 1.0f);
        expectEquals (z[1], 0.0f);

        beginTest ("between points: IDW weights sum to one, symmetric at the midpoint");
        z = map->interpolate (0.0f, -1.0f);
        expectWithinAbsoluteError (z[0] + z[1] + z[2], 1.0f, 1.0e-5f);
        expectWithinAbsoluteError (z[0], z[1], 1.0e-5f);
        expect (z[2] > 0.0f && z[2] < z[0]);

        beginTest ("nearest point ordering");
        auto idx = map->nearestIndices (0.9f, -0.9f, 3);
        expectEquals ((int) idx.size(), 3);
        expectEquals (idx[0], 1);
        expectEquals (map->nearestIndex (0.0f, 0.9f), 2);

        beginTest ("rejects invalid maps with a reason");
        expect (SoundMapData::parse ("{ not json", err) == nullptr && err.isNotEmpty());
        expect (SoundMapData::parse ("{\"latent_dim\": 0, \"points\": []}", err) == nullptr);
        expect (SoundMapData::parse ("{\"latent_dim\": 2, \"points\": [{\"x\":0,\"y\":0,\"latent\":[1,2,3]}]}", err) == nullptr);
        expect (err.contains ("no usable points"), err);
        expect (SoundMapData::parse ("{\"latent_dim\": 2, \"sample_rate\": 5, \"points\": [{\"x\":0,\"y\":0,\"latent\":[1,2]}]}", err) == nullptr);

        beginTest ("skips bad points but keeps good ones");
        auto mixed = SoundMapData::parse (
            "{\"latent_dim\": 2, \"points\": [{\"x\":0,\"y\":0,\"latent\":[1,2]}, {\"x\":1,\"y\":1,\"latent\":[1]}]}", err);
        expect (mixed != nullptr && mixed->points.size() == 1);

        beginTest ("single point still gets a usable viewport");
        expect (mixed->rangeX() > 0.5f && mixed->rangeY() > 0.5f);

        beginTest ("failed load leaves the current map untouched");
        SoundMap holder;
        expect (holder.loadFromFile (tinyModelDir().getChildFile ("sound_map.json")));
        expect (! holder.loadFromFile (juce::File::getCurrentWorkingDirectory().getChildFile ("missing.json"), &err));
        expect (holder.isLoaded());
        expectEquals ((int) holder.snapshot()->points.size(), 3);

        beginTest ("snapshots stay valid after the map is replaced");
        auto snap = holder.snapshot();
        holder.clear();
        expect (! holder.isLoaded());
        expectEquals ((int) snap->points.size(), 3);
    }
};

static SoundMapTests soundMapTests;
