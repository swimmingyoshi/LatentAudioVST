#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WaveformComponent.h"

using namespace TestHelpers;

namespace
{
    struct TimedMidi
    {
        int sample;
        juce::MidiMessage message;
    };

    // Drives a processor like a host would.
    struct Rig
    {
        LatentAudioVSTAudioProcessor proc;
        double sampleRate;
        int blockSize;
        int channels;

        explicit Rig (double sr = 44100.0, int block = 512, int numChannels = 2)
            : sampleRate (sr), blockSize (block), channels (numChannels)
        {
            proc.prepareToPlay (sr, block);
        }

        std::vector<float> render (int numSamples, std::vector<TimedMidi> events = {})
        {
            std::vector<float> out;
            out.reserve ((size_t) numSamples);
            juce::AudioBuffer<float> buffer (channels, blockSize);
            for (int done = 0; done < numSamples; done += blockSize)
            {
                const int n = std::min (blockSize, numSamples - done);
                buffer.setSize (channels, n, false, false, true);
                juce::MidiBuffer midi;
                for (auto& e : events)
                    if (e.sample >= done && e.sample < done + n)
                        midi.addEvent (e.message, e.sample - done);
                proc.processBlock (buffer, midi);
                for (int i = 0; i < n; ++i)
                    out.push_back (buffer.getSample (0, i));
                if (channels > 1)
                    for (int i = 0; i < n; ++i)
                        jassert (buffer.getSample (1, i) == buffer.getSample (0, i));
            }
            return out;
        }

        void set (const juce::String& id, float plainValue)
        {
            auto* p = proc.getAPVTS().getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        }

        float get (const juce::String& id) { return proc.getAPVTS().getRawParameterValue (id)->load(); }

        // Hands `audio` to the player and starts it like a click on the map would.
        void playAudio (std::vector<float> audio)
        {
            proc.injectDecodedAudio (std::move (audio));
            proc.triggerFromUI (0.0f, 0.0f);
        }
    };

    bool allFinite (const std::vector<float>& x)
    {
        return std::all_of (x.begin(), x.end(), [] (float v) { return std::isfinite (v); });
    }

    float maxStep (const std::vector<float>& x)
    {
        float m = 0.0f;
        for (size_t i = 1; i < x.size(); ++i)
            m = std::max (m, std::abs (x[i] - x[i - 1]));
        return m;
    }

    void pumpMessages (int ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }
}

//==============================================================================
class PlaybackTests : public juce::UnitTest
{
public:
    PlaybackTests() : juce::UnitTest ("Playback", "LatentAudio") {}

    void runTest() override
    {
        beginTest ("pitch and length are correct at 48 kHz (model is 44.1 kHz)");
        {
            Rig r (48000.0);
            r.playAudio (sine (1000.0, 44100.0, 44100));
            auto out = r.render (52000);
            const double f = measureFrequency (out, 2400, 40000, 48000.0);
            expectWithinAbsoluteError (f, 1000.0, 3.0);
            expect (rms (out, 46000, 47500) > 0.1f, "still playing just before 1 s");
            expectLessThan (peak (out, 48700, 52000), 1.0e-3f);
        }

        beginTest ("pitch is correct at 44.1 kHz and 96 kHz");
        for (double sr : { 44100.0, 96000.0 })
        {
            Rig r (sr);
            r.playAudio (sine (1000.0, 44100.0, 44100));
            auto out = r.render ((int) (sr * 0.5));
            expectWithinAbsoluteError (measureFrequency (out, (int) (sr * 0.05), (int) (sr * 0.45), sr), 1000.0, 3.0);
        }

        beginTest ("mono output bus does not crash and plays");
        {
            Rig r (44100.0, 512, 1);
            r.playAudio (sine (440.0, 44100.0, 44100));
            expect (rms (r.render (8192), 0, 8192) > 0.1f);
        }

        beginTest ("MIDI note-on is sample accurate");
        {
            Rig r;
            r.set ("triggerOnMidi", 1.0f);
            r.proc.injectDecodedAudio (std::vector<float> (44100, 0.5f));
            r.render (512);
            auto out = r.render (512, { { 300, juce::MidiMessage::noteOn (1, 60, 1.0f) } });
            expectEquals (peak (out, 0, 300), 0.0f);
            expect (rms (out, 320, 512) > 0.3f);
        }

        beginTest ("note-off only releases the note that is sounding");
        {
            Rig r;
            r.set ("triggerOnMidi", 1.0f);
            r.proc.injectDecodedAudio (sine (440.0, 44100.0, 44100));
            r.render (512);
            auto out = r.render (4000, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) },
                                         { 100, juce::MidiMessage::noteOn (1, 64, 1.0f) },
                                         { 200, juce::MidiMessage::noteOff (1, 60) } });
            expect (rms (out, 3000, 4000) > 0.1f, "note 64 still holds the voice");
            out = r.render (4000, { { 0, juce::MidiMessage::noteOff (1, 64) } });
            expectLessThan (peak (out, 2000, 4000), 1.0e-3f);
        }

        beginTest ("Play on Note: moving the position does not make a sound");
        {
            Rig r;
            r.set ("triggerOnMidi", 1.0f);
            r.set ("positionX", 0.3f);
            r.proc.injectDecodedAudio (sine (440.0, 44100.0, 44100));
            expectEquals (peak (r.render (4800)), 0.0f);

            r.set ("triggerOnMidi", 0.0f);
            r.set ("positionX", 0.6f); // free mode: moving auditions
            r.proc.injectDecodedAudio (sine (440.0, 44100.0, 44100));
            expect (rms (r.render (4800), 0, 4800) > 0.1f);
        }

        beginTest ("Key Track transposes relative to the root note");
        {
            Rig r;
            r.set ("triggerOnMidi", 1.0f);
            r.set ("keyTrack", 1.0f);
            r.set ("rootNote", 60.0f);
            r.proc.injectDecodedAudio (sine (500.0, 44100.0, 44100));
            r.render (512);
            auto up = r.render (20000, { { 0, juce::MidiMessage::noteOn (1, 72, 1.0f) } });
            expectWithinAbsoluteError (measureFrequency (up, 1000, 19000, 44100.0), 1000.0, 5.0);
            auto down = r.render (30000, { { 0, juce::MidiMessage::noteOn (1, 48, 1.0f) } });
            expectWithinAbsoluteError (measureFrequency (down, 2000, 29000, 44100.0), 250.0, 2.0);
        }

        beginTest ("velocity sensitivity scales level");
        {
            Rig r;
            r.set ("triggerOnMidi", 1.0f);
            r.set ("velocitySens", 1.0f);
            r.proc.injectDecodedAudio (sine (440.0, 44100.0, 44100));
            r.render (512);
            auto loud = r.render (4096, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } });
            auto soft = r.render (4096, { { 0, juce::MidiMessage::noteOn (1, 60, 0.5f) } });
            expectWithinAbsoluteError (rms (soft, 1000, 4000) / rms (loud, 1000, 4000), 0.5f, 0.03f);
        }

        beginTest ("Hold loops until switched off");
        {
            Rig r;
            r.set ("latchMode", 1.0f);
            r.set ("sampleLength", 0.5f);
            r.playAudio (sine (440.0, 44100.0, 44100));
            r.proc.releaseFromUI();
            auto out = r.render (44100 * 3);
            expect (rms (out, 44100 * 2, 44100 * 3) > 0.1f);
            r.set ("latchMode", 0.0f);
            out = r.render (4096);
            expectLessThan (peak (out, 2048, 4096), 1.0e-3f);
        }

        beginTest ("Start past End still plays (was silent)");
        {
            Rig r;
            r.set ("sampleStart", 0.9f);
            r.set ("sampleLength", 0.1f);
            r.playAudio (sine (440.0, 44100.0, 44100));
            expect (rms (r.render (2048), 0, 2048) > 0.05f);
        }

        beginTest ("new sound crossfades in without jumps");
        {
            Rig r;
            r.playAudio (sine (1000.0, 44100.0, 44100));
            auto a = r.render (4410);
            r.proc.injectDecodedAudio (sine (500.0, 44100.0, 44100));
            auto b = r.render (22050);
            a.insert (a.end(), b.begin(), b.end());
            expectLessThan (maxStep (a), 0.15f);
            expectWithinAbsoluteError (measureFrequency (b, 8000, 22000, 44100.0), 500.0, 3.0);
        }

        beginTest ("retriggering mid-sound is declicked");
        {
            Rig r;
            r.playAudio (std::vector<float> (44100, 0.5f));
            auto a = r.render (4000);
            r.proc.triggerFromUI (0.0f, 0.0f);
            auto b = r.render (4000);
            a.insert (a.end(), b.begin(), b.end());
            expectLessThan (maxStep (a), 0.05f);
        }

        beginTest ("dragging keeps the voice playing (no restart per mouse move)");
        {
            Rig r;
            r.playAudio (sine (440.0, 44100.0, 44100));
            r.render (8820);
            const float before = r.proc.getPlayheadNormalized();
            for (int i = 0; i < 10; ++i)
            {
                r.proc.triggerFromUI (0.01f * (float) i, 0.0f, false);
                r.render (256);
            }
            expect (r.proc.getPlayheadNormalized() > before, "playhead kept advancing");
        }

        beginTest ("a quick click plays the whole one-shot");
        {
            Rig r;
            r.proc.injectDecodedAudio (sine (440.0, 44100.0, 22050));
            r.proc.triggerFromUI (0.0f, 0.0f);
            r.proc.releaseFromUI(); // mouse up before the first audio block
            auto out = r.render (26000);
            expect (rms (out, 19000, 21500) > 0.1f, "still sounding near the end of the sample");
            expectLessThan (peak (out, 23500, 26000), 1.0e-3f);
        }

        beginTest ("pitched Key Track: plays the note at once, then crossfades to the model's real note");
        {
            Rig r;
            r.set ("triggerOnMidi", 1.0f);
            r.set ("keyTrack", 1.0f);
            r.proc.injectDecodedAudio (sine (440.0, 44100.0, 44100), 69.0f);  // decoded at A4
            r.render (512);
            // A5: resampled x2 immediately
            auto first = r.render (8820, { { 0, juce::MidiMessage::noteOn (1, 81, 1.0f) } });
            expectWithinAbsoluteError (measureFrequency (first, 500, 8800, 44100.0), 880.0, 4.0);
            // (no model loaded here, so nothing is requested; the model-request path is
            //  covered by the "pitched model" test with the tiny pitched model)
            const float headBefore = r.proc.getPlayheadNormalized();
            // the model's own A5 arrives (labelled 81): played at ratio 1, still 880 Hz, no jump
            r.proc.injectDecodedAudio (sine (880.0, 44100.0, 44100), 81.0f);
            auto second = r.render (8820);
            expectWithinAbsoluteError (measureFrequency (second, 4000, 8800, 44100.0), 880.0, 4.0);
            first.insert (first.end(), second.begin(), second.end());
            expectLessThan (maxStep (first), 0.2f);
            // after the swap the playhead advances at ratio 1 (it moved at ratio 2 before)
            const float headAfter = r.proc.getPlayheadNormalized();
            expect (headAfter > 0.0f && headAfter < headBefore + 0.3f, "playhead continues, slower");
        }

        beginTest ("unpitched sounds still transpose relative to Root");
        {
            Rig r;
            r.set ("triggerOnMidi", 1.0f);
            r.set ("keyTrack", 1.0f);
            r.set ("rootNote", 60.0f);
            r.proc.injectDecodedAudio (sine (500.0, 44100.0, 44100));  // no pitch label
            r.render (512);
            auto out = r.render (20000, { { 0, juce::MidiMessage::noteOn (1, 72, 1.0f) } });
            expectWithinAbsoluteError (measureFrequency (out, 1000, 19000, 44100.0), 1000.0, 5.0);
            expect (! r.proc.isNoteWanted (72), "no note is requested from unpitched models");
        }

        beginTest ("polyphony: chords, voice limit, release tails");
        {
            Rig r;
            r.set ("triggerOnMidi", 1.0f);
            r.set ("keyTrack", 1.0f);
            r.set ("rootNote", 69.0f);
            r.proc.injectDecodedAudio (sine (440.0, 44100.0, 44100));
            r.render (512);
            auto one = r.render (4410, { { 0, juce::MidiMessage::noteOn (1, 69, 1.0f) } });
            r.render (2048, { { 0, juce::MidiMessage::noteOff (1, 69) } });
            expectEquals (r.proc.getActiveVoiceCount(), 0);

            auto chord = r.render (4410, { { 0, juce::MidiMessage::noteOn (1, 69, 1.0f) },
                                           { 0, juce::MidiMessage::noteOn (1, 76, 1.0f) },
                                           { 0, juce::MidiMessage::noteOn (1, 81, 1.0f) } });
            expectEquals (r.proc.getActiveVoiceCount(), 3);
            expect (rms (chord, 1000, 4400) > 1.4f * rms (one, 1000, 4400), "three notes are louder than one");
            expect (allFinite (chord));
            expectLessThan (maxStep (chord), 0.5f);

            r.render (512, { { 0, juce::MidiMessage::noteOff (1, 76) } });
            r.render (2048);
            expectEquals (r.proc.getActiveVoiceCount(), 2);   // only the released note stopped
            r.render (512, { { 0, juce::MidiMessage::allNotesOff (1) } });
            r.render (2048);
            expectEquals (r.proc.getActiveVoiceCount(), 0);

            r.set ("polyphony", 2.0f);
            r.render (512, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) }, { 10, juce::MidiMessage::noteOn (1, 62, 1.0f) },
                             { 20, juce::MidiMessage::noteOn (1, 64, 1.0f) } });
            r.render (1024);
            expectEquals (r.proc.getActiveVoiceCount(), 2);   // oldest note was cut

            r.set ("polyphony", 1.0f);
            auto mono = r.render (4096, { { 0, juce::MidiMessage::noteOn (1, 65, 1.0f) } });
            expectEquals (r.proc.getActiveVoiceCount(), 1);
            expectLessThan (maxStep (mono), 0.5f);             // the cut is declicked
            r.render (512, { { 0, juce::MidiMessage::allNotesOff (1) } });

            // same note twice: retrigger, not a doubled voice
            r.set ("polyphony", 8.0f);
            r.render (2048, { { 0, juce::MidiMessage::noteOn (1, 69, 1.0f) }, { 1000, juce::MidiMessage::noteOn (1, 69, 1.0f) } });
            expectEquals (r.proc.getActiveVoiceCount(), 1);
        }

        beginTest ("export applies the plugin's settings (gain, rate, start/end, filter)");
        {
            Rig r;
            r.proc.injectDecodedAudio (sine (8000.0, 44100.0, 44100));
            const auto plain = r.proc.renderForExport();
            expect (plain.size() >= 44100 && plain.size() < 44100 + 11100);
            const float base = rms (plain, 2000, 40000);
            expectWithinAbsoluteError (base, 0.5f * 0.7071f, 0.03f); // default 10 kHz cutoff barely touches 8 kHz

            r.set ("outputGain", 0.5f);
            expectWithinAbsoluteError (rms (r.proc.renderForExport(), 2000, 40000), base * 0.5f, 0.02f);
            r.set ("outputGain", 1.0f);

            r.set ("playbackRate", 2.0f);
            const auto fast = r.proc.renderForExport();
            expect (fast.size() >= 22050 && fast.size() < 22050 + 11100, "double speed halves the length");
            r.set ("playbackRate", 1.0f);

            r.set ("sampleLength", 0.5f);
            expect (r.proc.renderForExport().size() < 22050 + 11100, "End trims the sound");
            r.set ("sampleLength", 1.0f);

            r.set ("filterCutoff", 500.0f);
            const auto dark = r.proc.renderForExport();
            expect (rms (dark, 2000, 40000) < base * 0.01f, "low-pass removes an 8 kHz tone");
            expect (allFinite (dark));
        }

        beginTest ("decode clean-up: no DC or rumble, music band kept, smooth end");
        {
            auto tone = [] (double f, float dc)
            {
                std::vector<float> x (88200);
                for (size_t i = 0; i < x.size(); ++i)
                    x[i] = dc + 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * f * (double) i / 44100.0);
                return x;
            };
            auto mean = [] (const std::vector<float>& x, int a, int b) { double s = 0; for (int i = a; i < b; ++i) s += x[(size_t) i]; return (float) (s / (b - a)); };

            auto music = tone (100.0, 0.2f);   // 100 Hz with a DC offset
            expect (LatentAudioVSTAudioProcessor::postProcess (music, 44100.0));
            expectLessThan (std::abs (mean (music, 22050, 66150)), 0.002f);                // DC gone
            expectWithinAbsoluteError (rms (music, 22050, 66150), 0.3536f, 0.01f);           // 100 Hz kept

            auto rumble = tone (8.0, 0.0f);
            LatentAudioVSTAudioProcessor::postProcess (rumble, 44100.0);
            expectLessThan (rms (rumble, 22050, 66150), 0.3536f * 0.25f);                     // 8 Hz cut > 12 dB

            // A sound still ringing at the end of the decode fades out instead of stopping dead.
            const int n = (int) music.size();
            expectEquals (music.back(), 0.0f);
            expectLessThan (maxStep (std::vector<float> (music.end() - 1400, music.end())), 0.03f);
            expect (std::abs (music[(size_t) (n - 1400)]) > 0.0f || std::abs (music[(size_t) (n - 1401)]) > 0.0f);
        }

        beginTest ("Low Cut: off by default, removes lows when turned up (also in exports)");
        {
            Rig r;
            r.proc.injectDecodedAudio (sine (50.0, 44100.0, 44100));
            const float off = rms (r.proc.renderForExport(), 4000, 40000);
            r.set ("lowCut", 200.0f);
            const float on = rms (r.proc.renderForExport(), 4000, 40000);
            expect (on < off * 0.05f, "50 Hz is cut by a 200 Hz Low Cut");
            r.set ("lowCut", 20.0f);
        }

        beginTest ("filter: stable at full resonance, correct response at zero");
        {
            Rig r;
            r.set ("latchMode", 1.0f);
            r.set ("filterResonance", 1.0f);
            juce::Random rng (42);
            std::vector<float> noise (44100);
            for (auto& s : noise) s = rng.nextFloat() - 0.5f;
            r.playAudio (noise);
            std::vector<float> all;
            for (int i = 0; i < 80; ++i)
            {
                r.set ("filterCutoff", 20.0f * std::pow (1000.0f, (float) i / 79.0f));
                auto o = r.render (512);
                all.insert (all.end(), o.begin(), o.end());
            }
            expect (allFinite (all));
            expectLessThan (peak (all), 10.0f);

            Rig lp;
            lp.set ("latchMode", 1.0f);
            lp.set ("filterCutoff", 1000.0f);
            lp.playAudio (sine (100.0, 44100.0, 44100));
            auto low = lp.render (16384);
            Rig hp;
            hp.set ("latchMode", 1.0f);
            hp.set ("filterCutoff", 1000.0f);
            hp.playAudio (sine (10000.0, 44100.0, 44100));
            auto high = hp.render (16384);
            expectWithinAbsoluteError (rms (low, 4000, 16000), 0.3535f, 0.03f);
            expectLessThan (rms (high, 4000, 16000), 0.0035f); // > 40 dB down
        }

        beginTest ("unloading silences and clears the sound");
        {
            Rig r;
            r.playAudio (sine (440.0, 44100.0, 44100));
            r.render (2048);
            expect (r.proc.hasAudio());
            r.proc.unloadModel();
            auto out = r.render (2048);
            expectEquals (peak (out), 0.0f);
            expect (! r.proc.hasAudio());
        }
    }
};

//==============================================================================
class StateTests : public juce::UnitTest
{
public:
    StateTests() : juce::UnitTest ("State", "LatentAudio") {}

    void runTest() override
    {
        beginTest ("save / restore round trip");
        {
            Rig a;
            a.set ("playbackRate", 1.5f);
            a.set ("outputGain", 0.5f);
            a.set ("keyTrack", 1.0f);
            a.proc.triggerFromUI (0.25f, -0.5f);
            a.proc.releaseFromUI();
            a.proc.injectDecodedAudio (sine (440.0, 44100.0, 44100));
            a.render (512);

            juce::MemoryBlock state;
            a.proc.getStateInformation (state);
            expectLessThan ((int) state.getSize(), 300 * 1024); // was ~1 MB (2 buffers x 2 channels)

            Rig b;
            b.proc.setStateInformation (state.getData(), (int) state.getSize());
            b.render (512);
            expect (b.proc.hasAudio());
            expectWithinAbsoluteError (b.get ("playbackRate"), 1.5f, 1.0e-4f);
            expectWithinAbsoluteError (b.get ("outputGain"), 0.5f, 1.0e-4f);
            expectEquals (b.get ("keyTrack"), 1.0f);
            expectEquals (b.proc.getCursor().x, 0.25f);
            expectEquals (b.proc.getCursor().y, -0.5f);
            std::vector<float> wave;
            b.proc.copyDisplayWaveform (wave);
            expectEquals ((int) wave.size(), 44100);
        }

        beginTest ("loads version-1 project state (old buffer format, missing model)");
        {
            Rig a;
            auto tree = a.proc.getAPVTS().copyState();
            std::vector<float> samples (1000, 0.25f);
            juce::MemoryBlock mb (samples.data(), samples.size() * sizeof (float));
            tree.setProperty ("hasValidAudio", true, nullptr);
            tree.setProperty ("activeBufferIndex", 1, nullptr);
            tree.setProperty ("buffer1_samples", 1000, nullptr);
            tree.setProperty ("buffer1_ch0", mb.toBase64Encoding(), nullptr);
            tree.setProperty ("modelPath", "C:/definitely/not/here/latent_decoder.onnx", nullptr);
            tree.setProperty ("currentX", 0.1f, nullptr);
            juce::MemoryBlock state;
            a.proc.copyXmlToBinary (*tree.createXml(), state);

            Rig b;
            b.proc.setStateInformation (state.getData(), (int) state.getSize());
            b.render (512);
            expect (b.proc.hasAudio());
            expectEquals (b.proc.getCursor().x, 0.1f);
            const auto status = b.proc.getLoadStatus();
            expect (status.state == LatentAudioVSTAudioProcessor::LoadState::Failed);
            expect (status.message.contains ("not found"), status.message);
        }

        beginTest ("garbage state is ignored");
        {
            Rig r;
            const char junk[] = "this is not a plugin state";
            r.proc.setStateInformation (junk, (int) sizeof (junk));
            r.proc.setStateInformation (nullptr, 0);
            expect (! r.proc.hasAudio());
        }
    }
};

//==============================================================================
class ModelLoadTests : public juce::UnitTest
{
public:
    ModelLoadTests() : juce::UnitTest ("Model loading", "LatentAudio") {}

    void runTest() override
    {
        using LoadState = LatentAudioVSTAudioProcessor::LoadState;

        beginTest ("loads the tiny model folder asynchronously and plays it");
        Rig r;
        r.proc.loadModelFolderAsync (tinyModelDir());
        expect (r.proc.waitForModelLoad (15000));
        auto status = r.proc.getLoadStatus();
        expect (status.state == LoadState::Ready, status.message);
        auto map = r.proc.getMapSnapshot();
        expect (map != nullptr && map->points.size() == 3);
        pumpMessages (100);

        // Let the post-load priming decode land first, so the next one is ours.
        for (int i = 0; i < 2000 && r.proc.getDisplayVersion() < 2; ++i)
            juce::Thread::sleep (1);
        const auto versionBefore = r.proc.getDisplayVersion();
        r.proc.triggerFromUI (-1.0f, -1.0f); // exactly on point "a" -> latent e0
        r.proc.releaseFromUI();
        std::vector<float> out;
        const auto deadline = juce::Time::getMillisecondCounter() + 5000;
        while (juce::Time::getMillisecondCounter() < deadline)
        {
            auto o = r.render (512);
            out.insert (out.end(), o.begin(), o.end());
            if (rms (o, 0, 512) > 0.01f) break;
            juce::Thread::sleep (1);
        }
        expect (r.proc.hasAudio(), "decoded audio arrived");
        expect (rms (out, 0, (int) out.size()) > 0.01f, "and it played");

        for (int i = 0; i < 5000 && r.proc.getDisplayVersion() == versionBefore; ++i)
            juce::Thread::sleep (1);
        expect (r.proc.getDisplayVersion() > versionBefore, "decode for the clicked point arrived");

        std::vector<float> wave;
        r.proc.copyDisplayWaveform (wave);
        std::vector<float> e0 (kTinyLatentDim, 0.0f);
        e0[0] = 1.0f;
        auto expected = tinyModelExpected (e0);
        LatentAudioVSTAudioProcessor::postProcess (expected, 44100.0); // what every decode goes through
        expectEquals ((int) wave.size(), kTinySamples);
        if (wave.size() > 500)
            expectWithinAbsoluteError (wave[500], expected[500], 1.0e-5f);

        beginTest ("position parameters follow the cursor (Y: 1 = top)");
        r.proc.triggerFromUI (0.0f, 0.5f);
        r.proc.releaseFromUI();
        expectWithinAbsoluteError (r.get ("positionX"), 0.5f, 1.0e-4f);
        expectWithinAbsoluteError (r.get ("positionY"), 0.25f, 1.0e-4f);
        r.set ("positionY", 1.0f);
        expectWithinAbsoluteError (r.proc.getCursor().y, -1.0f, 1.0e-4f);
        r.set ("positionX", 0.0f);
        expectWithinAbsoluteError (r.proc.getCursor().x, -1.0f, 1.0e-4f);

        beginTest ("a failed load reports why and keeps the current model");
        r.proc.loadModelFolderAsync (tinyModelDir().getChildFile ("does-not-exist"));
        expect (r.proc.waitForModelLoad (5000));
        status = r.proc.getLoadStatus();
        expect (status.state == LoadState::Failed);
        expect (status.message.isNotEmpty());
        expect (r.proc.getActiveProvider() != "None");
        expect (r.proc.getMapSnapshot() != nullptr);

        beginTest ("model / map latent size mismatch is caught");
        juce::TemporaryFile tmpDir;
        const auto dir = tmpDir.getFile();
        dir.createDirectory();
        tinyModelDir().getChildFile ("latent_decoder.onnx").copyFileTo (dir.getChildFile ("latent_decoder.onnx"));
        dir.getChildFile ("sound_map.json").replaceWithText (
            "{\"latent_dim\": 4, \"sample_rate\": 44100, \"points\": [{\"x\":0,\"y\":0,\"latent\":[1,0,0,0]}]}");
        r.proc.loadModelFolderAsync (dir);
        expect (r.proc.waitForModelLoad (5000));
        status = r.proc.getLoadStatus();
        expect (status.state == LoadState::Failed);
        expect (status.message.contains ("latent size"), status.message);

        beginTest ("prefers sound_map.json when a folder has several .json files");
        dir.getChildFile ("a_config.json").replaceWithText ("{}");
        juce::File onnx, json;
        juce::String err;
        expect (LatentAudioVSTAudioProcessor::findModelFiles (dir, onnx, json, err));
        expectEquals (json.getFileName(), juce::String ("sound_map.json"));
        expect (LatentAudioVSTAudioProcessor::findModelFiles (dir.getChildFile ("latent_decoder.onnx"), onnx, json, err));
        dir.deleteRecursively();

        beginTest ("pitched model: a played note is requested from the model");
        {
            Rig p;
            p.proc.loadModelFolderAsync (tinyPitchedDir());
            expect (p.proc.waitForModelLoad (15000));
            expect (p.proc.isPitchedModel());
            expect (p.proc.getLoadStatus().message.contains ("any note"), p.proc.getLoadStatus().message);
            pumpMessages (100);
            p.set ("triggerOnMidi", 1.0f);
            p.set ("keyTrack", 1.0f);
            p.proc.moveCursorFromUI (-1.0f, -1.0f);  // point "a": natural pitch 60
            for (int i = 0; i < 2000 && ! p.proc.hasAudio(); ++i) { p.render (256); juce::Thread::sleep (1); }
            p.set ("latchMode", 1.0f);   // the tiny sound is 50 ms: keep notes sounding while held
            for (int i = 0; i < 100; ++i) { p.render (256); juce::Thread::sleep (2); }  // cursor decode settles
            std::vector<float> z (kTinyLatentDim + 1, 0.0f);
            z[0] = 1.0f;
            z[(size_t) kTinyLatentDim] = (72.0f - 60.0f) / 24.0f;  // the note we played
            std::vector<float> expected (kTinySamples);
            for (int j = 0; j < kTinySamples; ++j)
            {
                double acc = 0.0;
                for (int i = 0; i <= kTinyLatentDim; ++i)
                    acc += z[(size_t) i] * 0.5 * std::sin (0.37 * i + 0.011 * j);
                expected[(size_t) j] = (float) std::tanh (acc);
            }
            LatentAudioVSTAudioProcessor::postProcess (expected, 44100.0);
            // The cursor's own decode may still land first; wait for the note's.
            p.render (256, { { 0, juce::MidiMessage::noteOn (1, 72, 1.0f) } });
            std::vector<float> wave;
            bool gotNote = false;
            for (int i = 0; i < 3000 && ! gotNote; ++i)
            {
                p.proc.copyDisplayWaveform (wave);
                gotNote = wave.size() > 500 && std::abs (wave[500] - expected[500]) < 1.0e-4f;
                if (! gotNote) { p.render (256); juce::Thread::sleep (1); }
            }
            expect (gotNote, "the model was asked for the played note");

            expect (p.proc.isNoteWanted (72));
            p.set ("latchMode", 0.0f);
            p.render (256, { { 0, juce::MidiMessage::noteOff (1, 72) } });
            p.render (4096);
            expect (! p.proc.isNoteWanted (72), "released notes are no longer decoded");
            p.set ("latchMode", 1.0f);

            beginTest ("pitched model: a chord gets one decode per note");
            const auto v0 = p.proc.getDisplayVersion();
            p.render (256, { { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) },
                             { 0, juce::MidiMessage::noteOn (1, 64, 1.0f) },
                             { 0, juce::MidiMessage::noteOn (1, 67, 1.0f) } });
            expect (p.proc.isNoteWanted (60) && p.proc.isNoteWanted (64) && p.proc.isNoteWanted (67));
            for (int i = 0; i < 3000 && p.proc.getDisplayVersion() < v0 + 3; ++i) { p.render (256); juce::Thread::sleep (1); }
            expect (p.proc.getDisplayVersion() >= v0 + 3, "three notes decoded");
            expectEquals (p.proc.getActiveVoiceCount(), 3);
        }

        beginTest ("unload frees the model");
        r.proc.unloadModel();
        expect (r.proc.getActiveProvider() == "None");
        expect (r.proc.getMapSnapshot() == nullptr);
        expect (r.proc.getLoadStatus().state == LoadState::Empty);
    }
};

//==============================================================================
class StressTests : public juce::UnitTest
{
public:
    StressTests() : juce::UnitTest ("Thread safety stress", "LatentAudio") {}

    void runTest() override
    {
        beginTest ("audio + inference + UI + reloads + state, concurrently");
        Rig r (48000.0, 256);
        r.proc.loadModelFolderAsync (tinyModelDir());
        expect (r.proc.waitForModelLoad (15000));

        std::atomic<bool> stop { false };
        std::atomic<bool> finite { true };
        std::atomic<int> blocks { 0 };
        std::thread audio ([&]
        {
            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer midi;
            int n = 0;
            while (! stop.load())
            {
                midi.clear();
                if (n % 7 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60 + n % 12, 0.8f), n % 256);
                if (n % 11 == 0) midi.addEvent (juce::MidiMessage::noteOff (1, 60 + n % 12), 0);
                r.proc.processBlock (buffer, midi);
                for (int i = 0; i < 256; ++i)
                    if (! std::isfinite (buffer.getSample (0, i))) finite = false;
                ++n;
                ++blocks;
            }
        });

        juce::Random rng (7);
        juce::MemoryBlock state;
        for (int i = 0; i < 600; ++i)
        {
            const int action = rng.nextInt (100);
            if (action < 55)       r.proc.triggerFromUI (rng.nextFloat() * 2.0f - 1.0f, rng.nextFloat() * 2.0f - 1.0f);
            else if (action < 70)  r.proc.releaseFromUI();
            else if (action < 80)  r.set ("positionX", rng.nextFloat());
            else if (action < 85)  r.set ("triggerOnMidi", (float) rng.nextInt (2));
            else if (action < 89)  r.proc.loadModelFolderAsync (tinyModelDir());
            else if (action < 91)  r.proc.unloadModel();
            else if (action < 95)  r.proc.getStateInformation (state);
            else if (state.getSize() > 0) r.proc.setStateInformation (state.getData(), (int) state.getSize());
            if (i % 50 == 0) pumpMessages (5);
            juce::Thread::sleep (1);
        }
        r.proc.waitForModelLoad (15000);
        stop = true;
        audio.join();
        expect (finite.load(), "all output finite");
        expect (blocks.load() > 100);
        logMessage ("    rendered " + juce::String (blocks.load()) + " blocks during the run");
    }
};

//==============================================================================
class EditorTests : public juce::UnitTest
{
public:
    EditorTests() : juce::UnitTest ("Editor", "LatentAudio") {}

    void runTest() override
    {
        beginTest ("export: the Start-End region round-trips as a WAV");
        {
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("latentaudio_export_test");
            dir.deleteRecursively();
            const auto file = dir.getChildFile ("out.wav");
            auto audio = sine (441.0, 44100.0, 44100, 0.5f);
            juce::String err;
            expect (WaveformComponent::writeWav (file, audio, 44100, 0.25f, 0.75f, err), err);
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
            expect (reader != nullptr);
            if (reader != nullptr)
            {
                expectEquals ((int) reader->lengthInSamples, 22050);
                expectEquals ((int) reader->sampleRate, 44100);
                juce::AudioBuffer<float> b (1, 22050);
                reader->read (&b, 0, 22050, 0, true, false);
                std::vector<float> back (b.getReadPointer (0), b.getReadPointer (0) + 22050);
                expectWithinAbsoluteError (back[5000], audio[11025 + 5000], 1.0e-4f);
                expectLessThan (std::abs (back.back()), 0.01f);   // faded at the cut
                expectWithinAbsoluteError (measureFrequency (back, 500, 21000, 44100.0), 441.0, 1.0);
            }
            expect (! WaveformComponent::writeWav (file, {}, 44100, 0.0f, 1.0f, err) && err.isNotEmpty());
            dir.deleteRecursively();
        }

        beginTest ("layout works from minimum to large sizes");
        Rig r;
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc.createEditor());
        auto* editor = dynamic_cast<LatentAudioVSTAudioProcessorEditor*> (ed.get());
        expect (editor != nullptr);
        for (auto size : { juce::Point<int> (760, 540), juce::Point<int> (980, 700), juce::Point<int> (1800, 1200) })
        {
            editor->setSize (size.x, size.y);
            expect (editor->getNebula().getHeight() >= 150,
                    "map height at " + juce::String (size.x) + "x" + juce::String (size.y) + ": "
                        + juce::String (editor->getNebula().getHeight()));
        }

        beginTest ("two instances keep independent UI state");
        Rig r2;
        std::unique_ptr<juce::AudioProcessorEditor> ed2 (r2.proc.createEditor());
        r.proc.loadModelFolderAsync (tinyModelDir());
        expect (r.proc.waitForModelLoad (15000));
        pumpMessages (200);
        expect (r2.proc.getMapSnapshot() == nullptr);
        ed2.reset();
        ed.reset();
    }
};

//==============================================================================
// Drives the real editor controls: synthesized mouse events, button clicks, drops.
class UiInteractionTests : public juce::UnitTest
{
public:
    UiInteractionTests() : juce::UnitTest ("UI interaction", "LatentAudio") {}

    template <typename T>
    T* find (juce::Component& root, const juce::String& id)
    {
        auto* c = dynamic_cast<T*> (root.findChildWithID (id));
        expect (c != nullptr, "control '" + id + "' exists");
        return c;
    }

    void click (juce::Button* b) { if (b != nullptr) { b->triggerClick(); pumpMessages (60); } }

    void runTest() override
    {
        using LoadState = LatentAudioVSTAudioProcessor::LoadState;

        beginTest ("drop a model folder onto the editor");
        Rig r;
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc.createEditor());
        auto* editor = dynamic_cast<LatentAudioVSTAudioProcessorEditor*> (ed.get());
        editor->setSize (980, 700);
        expect (editor->isInterestedInFileDrag ({ tinyModelDir().getFullPathName() }));
        expect (! editor->isInterestedInFileDrag ({ dataDir().getChildFile ("readme.txt").getFullPathName() }));
        editor->filesDropped ({ tinyModelDir().getFullPathName() }, 10, 10);
        expect (r.proc.waitForModelLoad (15000));
        expect (r.proc.getLoadStatus().state == LoadState::Ready);
        pumpMessages (150); // editor timer picks up the map

        auto* nebula = find<NebulaComponent> (*editor, "nebula");
        if (nebula == nullptr) return;
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        auto event = [&] (juce::Point<float> pos, juce::ModifierKeys mods, juce::Point<float> downPos, int clicks = 1, bool dragged = false)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (src, pos, mods, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f,
                                     nebula, nebula, now, downPos, now, clicks, dragged);
        };
        const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier);
        const juce::ModifierKeys right (juce::ModifierKeys::rightButtonModifier);

        beginTest ("clicking the map moves the cursor, updates automation and plays");
        auto pb = nebula->mapToScreen (1.0f, -1.0f); // point "b" (top right)
        nebula->mouseDown (event (pb, left, pb));
        nebula->mouseUp (event (pb, {}, pb));
        expectWithinAbsoluteError (r.proc.getCursor().x, 1.0f, 0.02f);
        expectWithinAbsoluteError (r.proc.getCursor().y, -1.0f, 0.02f);
        expectWithinAbsoluteError (r.get ("positionX"), 1.0f, 0.01f);
        expectWithinAbsoluteError (r.get ("positionY"), 1.0f, 0.01f); // top of the map
        std::vector<float> out;
        for (int i = 0; i < 200 && rms (out, 0, (int) out.size()) < 0.01f; ++i)
        {
            auto o = r.render (512);
            out.insert (out.end(), o.begin(), o.end());
            juce::Thread::sleep (2);
        }
        expect (rms (out, 0, (int) out.size()) > 0.01f, "click produced sound");

        beginTest ("dragging moves the cursor continuously");
        auto pa = nebula->mapToScreen (-1.0f, -1.0f);
        nebula->mouseDown (event (pa, left, pa));
        const auto mid = (pa + pb) * 0.5f;
        nebula->mouseDrag (event (mid, left, pa, 1, true));
        expectWithinAbsoluteError (r.proc.getCursor().x, 0.0f, 0.03f);
        nebula->mouseUp (event (mid, {}, pa, 1, true));

        beginTest ("right-drag pans without moving the cursor; wheel zooms; double-click resets");
        const auto cursorBefore = r.proc.getCursor();
        const auto before = nebula->mapToScreen (0.0f, 1.0f);
        nebula->mouseDown (event (mid, right, mid));
        nebula->mouseDrag (event (mid + juce::Point<float> (100.0f, 0.0f), right, mid, 1, true));
        nebula->mouseUp (event (mid + juce::Point<float> (100.0f, 0.0f), {}, mid, 1, true));
        expectWithinAbsoluteError (nebula->mapToScreen (0.0f, 1.0f).x - before.x, 100.0f, 1.0f);
        expect (r.proc.getCursor() == cursorBefore);

        const float spanBefore = nebula->mapToScreen (1.0f, -1.0f).x - nebula->mapToScreen (-1.0f, -1.0f).x;
        juce::MouseWheelDetails wheel { 0.0f, 0.5f, false, false, false };
        nebula->mouseWheelMove (event (mid, {}, mid), wheel);
        const float spanAfter = nebula->mapToScreen (1.0f, -1.0f).x - nebula->mapToScreen (-1.0f, -1.0f).x;
        expect (spanAfter > spanBefore * 1.1f, "wheel zoomed in");

        nebula->mouseDoubleClick (event (mid, left, mid, 2));
        expectWithinAbsoluteError (nebula->mapToScreen (0.0f, 1.0f).x, before.x, 0.5f);

        beginTest ("Snap toggle snaps clicks to the nearest sound");
        click (find<juce::ToggleButton> (*editor, "snapMode"));
        expectEquals (r.get ("snapMode"), 1.0f);
        pumpMessages (100);
        auto near = nebula->mapToScreen (0.1f, -0.9f); // closest to "b"
        nebula->mouseDown (event (near, left, near));
        nebula->mouseUp (event (near, {}, near));
        expectEquals (r.proc.getCursor().x, 1.0f);
        expectEquals (r.proc.getCursor().y, -1.0f);
        click (find<juce::ToggleButton> (*editor, "snapMode"));

        beginTest ("toolbar buttons and toggles");
        click (find<juce::Button> (*editor, "center"));
        expectWithinAbsoluteError (r.proc.getCursor().x, 0.0f, 1.0e-4f);
        expectWithinAbsoluteError (r.proc.getCursor().y, 0.0f, 1.0e-4f);

        click (find<juce::Button> (*editor, "random"));
        const auto c = r.proc.getCursor();
        auto map = r.proc.getMapSnapshot();
        const int nearest = map->nearestIndex (c.x, c.y);
        expect (std::hypot (c.x - map->points[(size_t) nearest].x, c.y - map->points[(size_t) nearest].y) < 0.1f,
                "random lands on a sound");

        for (auto id : { "triggerOnMidi", "latchMode", "keyTrack" })
        {
            click (find<juce::ToggleButton> (*editor, id));
            expectEquals (r.get (id), 1.0f, id);
            click (find<juce::ToggleButton> (*editor, id));
            expectEquals (r.get (id), 0.0f, id);
        }

        beginTest ("knobs drive parameters; Reset restores defaults");
        if (auto* rate = find<juce::Slider> (*editor, "playbackRate"))
            rate->setValue (1.5, juce::sendNotificationSync);
        if (auto* gain = find<juce::Slider> (*editor, "outputGain"))
            gain->setValue (0.5, juce::sendNotificationSync);
        expectWithinAbsoluteError (r.get ("playbackRate"), 1.5f, 1.0e-3f);
        expectWithinAbsoluteError (r.get ("outputGain"), 0.5f, 1.0e-3f);
        if (auto* gain = find<juce::Slider> (*editor, "outputGain"))
            expectEquals (gain->getTextFromValue (0.5), juce::String ("-6.0 dB"));
        click (find<juce::Button> (*editor, "reset"));
        expectWithinAbsoluteError (r.get ("playbackRate"), 1.0f, 1.0e-3f);
        expectWithinAbsoluteError (r.get ("outputGain"), 1.0f, 1.0e-3f);

        beginTest ("Unload clears the model and the map view");
        click (find<juce::Button> (*editor, "unload"));
        expect (r.proc.getLoadStatus().state == LoadState::Empty);
        expect (r.proc.getMapSnapshot() == nullptr);
        pumpMessages (100);
        if (auto* unload = find<juce::Button> (*editor, "unload"))
            expect (! unload->isEnabled(), "Unload disabled with no model");
        ed.reset();
    }
};

static UiInteractionTests uiInteractionTests;

//==============================================================================
// Optional: set LATENTAUDIO_REAL_MODEL to an exported model folder.
class RealModelTests : public juce::UnitTest
{
public:
    RealModelTests() : juce::UnitTest ("Real model", "LatentAudio") {}

    void runTest() override
    {
        const auto path = juce::SystemStats::getEnvironmentVariable ("LATENTAUDIO_REAL_MODEL", {});
        beginTest ("real model end to end" + juce::String (path.isEmpty() ? " (skipped: LATENTAUDIO_REAL_MODEL not set)" : ""));
        if (path.isEmpty())
            return;

        Rig r (48000.0);
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        r.proc.loadModelFolderAsync (juce::File (path));
        expect (r.proc.waitForModelLoad (300000));
        const auto status = r.proc.getLoadStatus();
        expect (status.state == LatentAudioVSTAudioProcessor::LoadState::Ready, status.message);
        logMessage ("    load: " + juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 2) + " s, " + status.message);

        auto map = r.proc.getMapSnapshot();
        if (map == nullptr) return;

        // Time the decoder directly (what one position change costs), the way the
        // plugin runs it: one thread, on a model file shared through the cache.
        {
            juce::File onnx, json;
            juce::String err;
            LatentAudioVSTAudioProcessor::findModelFiles (juce::File (path), onnx, json, err);
            LatentDecoder dec;
            expect (dec.loadModel (onnx).ok);
            std::vector<double> ms;
            std::vector<float> audio;
            for (int k = 0; k < 12; ++k)
            {
                const auto& p = map->points[(size_t) (k * 37 % (int) map->points.size())];
                const auto t = juce::Time::getMillisecondCounterHiRes();
                expect (dec.decode (p.latent, audio));
                if (k >= 2) ms.push_back (juce::Time::getMillisecondCounterHiRes() - t); // skip warm-up
            }
            std::sort (ms.begin(), ms.end());
            logMessage ("    decode latency median: " + juce::String (ms[ms.size() / 2], 1) + " ms, max: "
                        + juce::String (ms.back(), 1) + " ms (" + dec.getActiveProvider() + ", "
                        + juce::String ((int) audio.size()) + " samples)");
        }

        r.proc.triggerFromUI (map->points.front().x, map->points.front().y);
        r.proc.releaseFromUI();
        for (int i = 0; i < 300 && ! r.proc.hasAudio(); ++i)
        {
            r.render (512);
            juce::Thread::sleep (5);
        }

        auto out = r.render (48000);
        expect (allFinite (out));
        expect (rms (out, 0, 48000) > 0.001f, "real model produced sound");
    }
};

static PlaybackTests playbackTests;
static StateTests stateTests;
static ModelLoadTests modelLoadTests;
static StressTests stressTests;
static EditorTests editorTests;
static RealModelTests realModelTests;

//==============================================================================
// Renders the editor with a loaded model to PNGs (for visual review).
int renderSnapshots (const juce::File& modelDir, const juce::File& outDir)
{
    Rig r (48000.0);
    r.proc.loadModelFolderAsync (modelDir);
    if (! r.proc.waitForModelLoad (300000))
        return 1;
    pumpMessages (200);

    if (auto map = r.proc.getMapSnapshot())
    {
        const auto& p = map->points[map->points.size() / 3];
        r.proc.triggerFromUI (p.x + 0.01f * map->rangeX(), p.y);
        r.proc.releaseFromUI();
    }
    for (int i = 0; i < 400 && r.proc.getDisplayVersion() < 3; ++i)
    {
        r.render (512);
        juce::Thread::sleep (5);
    }
    r.render (9000); // advance the playhead a little

    std::unique_ptr<juce::AudioProcessorEditor> editor (r.proc.createEditor());
    outDir.createDirectory();
    for (auto size : { juce::Point<int> (1080, 720), juce::Point<int> (860, 580), juce::Point<int> (1400, 900) })
    {
        editor->setSize (size.x, size.y);
        pumpMessages (300); // let the editor's timer poll the processor
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        auto file = outDir.getChildFile ("editor_" + juce::String (size.x) + "x" + juce::String (size.y) + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::cout << "wrote " << file.getFullPathName() << std::endl;
    }

    // A/B morph mode, B on a far-away sound.
    if (auto map = r.proc.getMapSnapshot())
    {
        r.set ("blendMode", 1.0f);
        r.set ("morph", 0.35f);
        const auto& b = map->points[map->points.size() * 5 / 6];
        r.proc.setMorphTargetFromUI (b.x, b.y);
        editor->setSize (1080, 720);
        pumpMessages (300);
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        auto file = outDir.getChildFile ("editor_morph_1080x720.png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::cout << "wrote " << file.getFullPathName() << std::endl;
    }
    return 0;
}
