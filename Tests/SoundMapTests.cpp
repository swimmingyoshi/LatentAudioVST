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

        beginTest ("blend 0 is the map's own interpolation");
        expect (map->interpolateBlend (0.2f, -0.4f, 0.0f) == map->interpolate (0.2f, -0.4f));
        expect (map->interpolateBlend (-1.0f, -1.0f, 0.0f) == map->points[0].latent);

        beginTest ("blend > 0 mixes other sounds in, even on a point, more as it grows");
        {
            const auto lo = map->interpolateBlend (-1.0f, -1.0f, 0.2f);
            const auto hi = map->interpolateBlend (-1.0f, -1.0f, 1.0f);
            expect (lo[1] > 0.0f && lo[2] > 0.0f);
            expect (hi[1] > lo[1] && hi[2] > lo[2]);
            expect (lo[0] > lo[1]); // the point under the cursor still leads
            for (float v : hi)
                expect (std::isfinite (v));
        }

        beginTest ("blend info: shares sum to one, largest first, count grows with blend");
        {
            const auto narrow = map->blendInfo (-0.9f, -0.9f, 0.0f, 10);
            const auto wide = map->blendInfo (-0.9f, -0.9f, 1.0f, 10);
            float sum = 0.0f;
            for (auto& [i, s] : wide.top)
                sum += s;
            expectWithinAbsoluteError (sum, 1.0f, 1.0e-4f);
            expectEquals (narrow.top.front().first, 0);
            expect (wide.top.front().second >= wide.top.back().second);
            expect (wide.effectiveCount > narrow.effectiveCount);
        }

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

        beginTest ("pitch block: index, scale, per-point notes");
        auto pitched = SoundMapData::load (tinyPitchedDir().getChildFile ("sound_map.json"), err);
        expect (pitched != nullptr, err);
        if (pitched != nullptr)
        {
            expect (pitched->hasPitch());
            expectEquals (pitched->pitchIndex, kTinyLatentDim);
            expectEquals (pitched->latentDim, kTinyLatentDim + 1);
            expectEquals (pitched->points[1].midi, 64.0f);
            expectWithinAbsoluteError (pitched->midiToLatent (72.0f), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (pitched->latentToMidi (pitched->midiToLatent (55.5f)), 55.5f, 1.0e-4f);
        }
        expect (! map->hasPitch(), "maps without a pitch block are not pitched");
        auto badPitch = SoundMapData::parse (
            "{\"latent_dim\": 2, \"pitch\": {\"index\": 5, \"offset\": 60, \"scale\": 24}, \"points\": [{\"x\":0,\"y\":0,\"latent\":[1,2]}]}", err);
        expect (badPitch != nullptr && ! badPitch->hasPitch(), "out-of-range pitch index is ignored");

        beginTest ("sky map: field grid (f16) drives interpolation; sky grid and star info parse");
        {
            // 2x2 grid, dim 2: corners (row = y) [1,0] [0.5,0] / [-2,0] [0,0]
            const uint16_t halves[] = { 0x3C00, 0, 0x3800, 0, 0xC000, 0, 0, 0 };
            juce::MemoryBlock mb (halves, sizeof (halves));
            const juce::String field = "{\"res\":2,\"dim\":2,\"min_x\":-1,\"max_x\":1,\"min_y\":-1,\"max_y\":1,"
                                       "\"format\":\"f16\",\"data\":\"" + juce::Base64::toBase64 (mb.getData(), mb.getSize()) + "\"}";
            const float sky32[] = { 0.0f, 0.2f, 1.0f, 0.4f, 0.5f, 0.6f, 0.25f, 0.8f };
            juce::MemoryBlock skyMb (sky32, sizeof (sky32));
            const juce::String sky = "{\"res\":2,\"dim\":2,\"min_x\":-1,\"max_x\":1,\"min_y\":-1,\"max_y\":1,"
                                     "\"format\":\"f32\",\"data\":\"" + juce::Base64::toBase64 (skyMb.getData(), skyMb.getSize()) + "\"}";
            const juce::String json = "{\"latent_dim\":2,\"field\":" + field + ",\"sky\":" + sky + ","
                "\"points\":[{\"x\":-1,\"y\":-1,\"latent\":[9,9],\"brightness\":0.7,\"length\":0.2},"
                "{\"x\":1,\"y\":1,\"latent\":[3,3]}]}";
            auto m = SoundMapData::parse (json, err);
            expect (m != nullptr, err);
            if (m != nullptr)
            {
                expect (m->hasField());
                expect (m->sky.isValid());
                expectEquals (m->points[0].brightness, 0.7f);
                expect (std::isnan (m->points[1].brightness));
                // exactly on a sound: that sound, unblended
                expectEquals (m->interpolate (-1.0f, -1.0f)[0], 9.0f);
                // elsewhere: the field (bicubic, clamped): corner, edge midpoint, centre
                expectWithinAbsoluteError (m->interpolate (1.0f, -1.0f)[0], 0.5f, 1.0e-5f);
                expectWithinAbsoluteError (m->interpolate (0.0f, -1.0f)[0], 0.75f, 1.0e-5f);
                expectWithinAbsoluteError (m->interpolate (0.0f, 0.0f)[0], (1.0f + 0.5f - 2.0f + 0.0f) / 4.0f, 1.0e-5f);
                expectWithinAbsoluteError (m->interpolate (5.0f, -5.0f)[0], 0.5f, 1.0e-5f);   // outside: edge value
                float v[2];
                m->sky.sample (-1.0f, 1.0f, v);
                expectWithinAbsoluteError (v[0], 0.5f, 1.0e-6f);
                expectWithinAbsoluteError (v[1], 0.6f, 1.0e-6f);
            }
            // a broken or mismatched field is ignored; the map still loads (IDW)
            auto bad = SoundMapData::parse ("{\"latent_dim\":3,\"field\":" + field + ",\"points\":[{\"x\":0,\"y\":0,\"latent\":[1,2,3]}]}", err);
            expect (bad != nullptr && ! bad->hasField());
            // Exactly what Python's base64.b64encode writes for float32 [1, 2]
            auto py = SoundMapData::parse ("{\"latent_dim\":1,\"field\":{\"res\":2,\"dim\":1,\"format\":\"f32\","
                "\"data\":\"AACAPwAAAEAAAIA/AAAAQA==\"},\"points\":[{\"x\":0,\"y\":0,\"latent\":[1]}]}", err);
            expect (py != nullptr && py->hasField(), "standard base64 from Python");
            if (py != nullptr && py->hasField())
                expectEquals (py->field.values[1], 2.0f);
            auto corrupt = SoundMapData::parse ("{\"latent_dim\":2,\"field\":{\"res\":2,\"dim\":2,\"data\":\"AAAA\"},\"points\":[{\"x\":0,\"y\":0,\"latent\":[1,2]}]}", err);
            expect (corrupt != nullptr && ! corrupt->hasField());
        }

        beginTest ("snapshots stay valid after the map is replaced");
        auto snap = holder.snapshot();
        holder.clear();
        expect (! holder.isLoaded());
        expectEquals ((int) snap->points.size(), 3);
    }
};

static SoundMapTests soundMapTests;
