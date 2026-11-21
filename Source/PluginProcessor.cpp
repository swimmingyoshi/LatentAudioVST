//==============================================================================
/*
  ==============================================================================

  This file is part of LatentAudioVST.

  Copyright (C) 2026 swimmingyoshi

  LatentAudioVST is an open source project subject to the terms of the GNU Affero General Public License Version 3 (the "License"). You may not use this file except in compliance with the License. You can obtain a copy of the License at

  https://www.gnu.org/licenses/agpl-3.0.html

  Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "LatentLog.h"

namespace
{
    constexpr double kAttackSeconds     = 0.001;  // fixed declick gate
    constexpr double kReleaseSeconds    = 0.02;
    constexpr double kSwapFadeSeconds   = 0.05;   // crossfade when new audio replaces old
    constexpr double kStealFadeSeconds  = 0.004;  // retrigger declick
    constexpr int    kStateVersion      = 2;

    // 4-point, 3rd-order Hermite interpolation with edge clamping.
    inline float readHermite (const float* data, int len, double pos) noexcept
    {
        const int i = (int) pos;
        const float t = (float) (pos - (double) i);
        auto at = [data, len] (int k) noexcept { return data[juce::jlimit (0, len - 1, k)]; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    juce::AudioParameterFloatAttributes floatAttr (std::function<juce::String (float)> toText)
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [toText] (float v, int) { return toText (v); });
    }

    juce::String formatHz (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz" : juce::String ((int) hz) + " Hz";
    }

    juce::String formatDb (float linear)
    {
        return linear <= 0.0001f ? juce::String ("-inf dB")
                                 : juce::String (juce::Decibels::gainToDecibels (linear), 1) + " dB";
    }

    juce::String formatPercent (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
}

//==============================================================================
LatentAudioVSTAudioProcessor::LatentAudioVSTAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      inferenceThread ([this] { serviceInference(); })
{
    using PID = juce::ParameterID;
    using Range = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // IDs and ranges of version-1 parameters are unchanged so existing DAW
    // projects and automation keep working.
    p.push_back (std::make_unique<juce::AudioParameterFloat> (PID { "playbackRate", 1 }, "Playback Rate", Range (0.1f, 2.0f), 1.0f,
        floatAttr ([] (float v) { return juce::String (v, 2) + "x"; })));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (PID { "fineTune", 1 }, "Fine Tune (cents)", Range (-100.0f, 100.0f), 0.0f,
        floatAttr ([] (float v) { return juce::String (juce::roundToInt (v)) + " ct"; })));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (PID { "crossfade", 1 }, "Loop Crossfade", Range (0.0f, 0.5f), 0.05f,
        floatAttr (formatPercent)));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (PID { "filterCutoff", 1 }, "Filter Cutoff (Hz)", Range (20.0f, 20000.0f, 1.0f, 0.3f), 10000.0f,
        floatAttr (formatHz)));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (PID { "filterResonance", 1 }, "Filter Resonance", Range (0.0f, 1.0f), 0.0f,
        floatAttr (formatPercent)));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (PID { "outputGain", 1 }, "Output Gain", Range (0.0f, 2.0f), 1.0f,
        floatAttr (formatDb)));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (PID { "mixAmount", 1 }, "Level", Range (0.0f, 1.0f), 1.0f,
        floatAttr (formatPercent)));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (PID { "sampleLength", 1 }, "Sample End", Range (0.1f, 1.0f), 1.0f,
        floatAttr (formatPercent)));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (PID { "sampleStart", 1 }, "Sample Start", Range (0.0f, 0.9f), 0.0f,
        floatAttr (formatPercent)));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (PID { "positionX", 1 }, "Position X", Range (0.0f, 1.0f), 0.5f,
        floatAttr ([] (float v) { return juce::String (v, 3); })));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (PID { "positionY", 1 }, "Position Y", Range (0.0f, 1.0f), 0.5f,
        floatAttr ([] (float v) { return juce::String (v, 3); })));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (PID { "inferenceRate", 1 }, "Inference Rate (Hz)", Range (0.0f, 50.0f), 10.0f,
        floatAttr ([] (float v) { return v <= 0.0f ? juce::String ("Unlimited") : juce::String (v, 1) + " Hz"; })));
    p.push_back (std::make_unique<juce::AudioParameterBool> (PID { "latchMode", 1 }, "Hold (Loop)", false));
    p.push_back (std::make_unique<juce::AudioParameterBool> (PID { "triggerOnMidi", 1 }, "Play on Note", false));
    p.push_back (std::make_unique<juce::AudioParameterBool> (PID { "snapMode", 1 }, "Snap to Points", false));

    // Version 2
    p.push_back (std::make_unique<juce::AudioParameterBool> (PID { "keyTrack", 2 }, "Key Track", false));
    p.push_back (std::make_unique<juce::AudioParameterInt> (PID { "rootNote", 2 }, "Root Note", 0, 127, 60,
        juce::AudioParameterIntAttributes().withStringFromValueFunction (
            [] (int n, int) { return juce::MidiMessage::getMidiNoteName (n, true, true, 4); })));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (PID { "velocitySens", 2 }, "Velocity Sensitivity", Range (0.0f, 1.0f), 0.0f,
        floatAttr (formatPercent)));

    // Version 3
    p.push_back (std::make_unique<juce::AudioParameterInt> (PID { "polyphony", 3 }, "Voices", 1, 8, 8));

    parameters = std::make_unique<juce::AudioProcessorValueTreeState> (
        *this, nullptr, "PARAMETERS", juce::AudioProcessorValueTreeState::ParameterLayout (p.begin(), p.end()));

    auto raw = [this] (const char* id) { return parameters->getRawParameterValue (id); };
    pRate = raw ("playbackRate");     pTune = raw ("fineTune");       pLoopXfade = raw ("crossfade");
    pCutoff = raw ("filterCutoff");   pReso = raw ("filterResonance"); pGain = raw ("outputGain");
    pMix = raw ("mixAmount");         pEnd = raw ("sampleLength");    pStart = raw ("sampleStart");
    pPosX = raw ("positionX");        pPosY = raw ("positionY");      pInferenceRate = raw ("inferenceRate");
    pLatch = raw ("latchMode");       pTriggerOnMidi = raw ("triggerOnMidi");
    pKeyTrack = raw ("keyTrack");     pRootNote = raw ("rootNote");   pVelocity = raw ("velocitySens");
    pPolyphony = raw ("polyphony");

    parameters->addParameterListener ("positionX", this);
    parameters->addParameterListener ("positionY", this);

    {
        const juce::ScopedLock sl (statusLock);
        status.message = "No model loaded";
    }

    inferenceThread.startThread();
}

LatentAudioVSTAudioProcessor::~LatentAudioVSTAudioProcessor()
{
    parameters->removeParameterListener ("positionX", this);
    parameters->removeParameterListener ("positionY", this);
    inferenceThread.stopThread (4000);
    // Loading a model can't be interrupted; wait so the job never outlives us.
    loaderPool.removeAllJobs (true, 60000);
}

//==============================================================================
juce::AudioProcessorEditor* LatentAudioVSTAudioProcessor::createEditor()
{
    return new LatentAudioVSTAudioProcessorEditor (*this);
}

bool LatentAudioVSTAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && layouts.getMainInputChannelSet().isDisabled();
}

//==============================================================================
juce::Point<float> LatentAudioVSTAudioProcessor::mapToNormalized (const SoundMapData& map, float x, float y)
{
    return { juce::jlimit (0.0f, 1.0f, (x - map.minX) / map.rangeX()),
             juce::jlimit (0.0f, 1.0f, (map.maxY - y) / map.rangeY()) };
}

juce::Point<float> LatentAudioVSTAudioProcessor::normalizedToMap (const SoundMapData& map, float nx, float ny)
{
    return { map.minX + nx * map.rangeX(), map.maxY - ny * map.rangeY() };
}

//==============================================================================
// Position: parameters <-> cursor
void LatentAudioVSTAudioProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == "positionX" || id == "positionY")
        applyPositionParameters();
}

void LatentAudioVSTAudioProcessor::applyPositionParameters()
{
    // May run on the audio thread (host automation): atomics only.
    if (syncingPositionParams.load())
        return;

    const float minX = mapMinX.load(), maxX = mapMaxX.load();
    const float minY = mapMinY.load(), maxY = mapMaxY.load();
    currentX.store (minX + pPosX->load() * (maxX - minX));
    currentY.store (maxY - pPosY->load() * (maxY - minY));

    // In free mode moving the position auditions it; in "Play on Note" mode it
    // only changes what the next note will play.
    requestInference (pTriggerOnMidi->load() < 0.5f);
}

void LatentAudioVSTAudioProcessor::requestInference (bool audition)
{
    if (audition)
        playOnArrival.store (true);
    requestSeq.fetch_add (1);

    // Waking takes a mutex, so only do it off the audio thread; otherwise the
    // inference thread's 2 ms poll picks the request up.
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        if (mm->isThisTheMessageThread())
            inferenceThread.wake();
}

void LatentAudioVSTAudioProcessor::setCursorAndSyncHost (float x, float y, bool notifyHost)
{
    auto map = soundMap.snapshot();
    if (map != nullptr)
    {
        x = juce::jlimit (map->minX, map->maxX, x);
        y = juce::jlimit (map->minY, map->maxY, y);
    }

    currentX.store (x);
    currentY.store (y);

    if (map != nullptr && notifyHost)
    {
        const auto n = mapToNormalized (*map, x, y);
        syncingPositionParams.store (true);
        parameters->getParameter ("positionX")->setValueNotifyingHost (n.x);
        parameters->getParameter ("positionY")->setValueNotifyingHost (n.y);
        syncingPositionParams.store (false);
    }
}

//==============================================================================
// UI interaction
void LatentAudioVSTAudioProcessor::triggerFromUI (float mapX, float mapY, bool restart)
{
    // Wrap a drag in a gesture so hosts record it as one automation move.
    if (! uiGestureActive)
    {
        uiGestureActive = true;
        parameters->getParameter ("positionX")->beginChangeGesture();
        parameters->getParameter ("positionY")->beginChangeGesture();
    }
    setCursorAndSyncHost (mapX, mapY, true);
    requestInference (true);
    if (restart)
        startRequested.store (true);
}

void LatentAudioVSTAudioProcessor::releaseFromUI()
{
    if (uiGestureActive)
    {
        uiGestureActive = false;
        parameters->getParameter ("positionX")->endChangeGesture();
        parameters->getParameter ("positionY")->endChangeGesture();
    }
    // Mouse-up does not cut the sound: a click auditions the whole one-shot
    // (like a drum pad). Hold loops until switched off; MIDI note-off still gates.
}

void LatentAudioVSTAudioProcessor::moveCursorFromUI (float mapX, float mapY)
{
    setCursorAndSyncHost (mapX, mapY, true);
    requestInference (false);
}

void LatentAudioVSTAudioProcessor::triggerRandomPosition()
{
    auto map = soundMap.snapshot();
    if (map == nullptr || map->points.empty())
        return;

    // Land near a real sound (uniform positions often fall in empty space).
    auto& rng = juce::Random::getSystemRandom();
    const auto& pt = map->points[(size_t) rng.nextInt ((int) map->points.size())];
    const float jx = (rng.nextFloat() - 0.5f) * 0.04f * map->rangeX();
    const float jy = (rng.nextFloat() - 0.5f) * 0.04f * map->rangeY();
    triggerFromUI (pt.x + jx, pt.y + jy);

    // Close the gesture but let the sound play out (a release would cut it off).
    if (uiGestureActive)
    {
        uiGestureActive = false;
        parameters->getParameter ("positionX")->endChangeGesture();
        parameters->getParameter ("positionY")->endChangeGesture();
    }
}

void LatentAudioVSTAudioProcessor::resetParametersToDefaults()
{
    for (auto* param : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (ranged == nullptr)
            continue;
        const auto id = ranged->getParameterID();
        if (id == "positionX" || id == "positionY")
            continue;
        ranged->beginChangeGesture();
        ranged->setValueNotifyingHost (ranged->getDefaultValue());
        ranged->endChangeGesture();
    }
}

void LatentAudioVSTAudioProcessor::injectDecodedAudio (std::vector<float> audio, float midi, int note)
{
    if (audio.empty() || ! std::all_of (audio.begin(), audio.end(), [] (float v) { return std::isfinite (v); }))
        return;
    publishAudio (std::move (audio), modelGeneration.load(), midi, note, requestSeq.load());
}

void LatentAudioVSTAudioProcessor::copyDisplayWaveform (std::vector<float>& dest) const
{
    const juce::ScopedLock sl (displayLock);
    dest = displayWaveform;
}

//==============================================================================
// Inference thread
void LatentAudioVSTAudioProcessor::serviceInference()
{
    // During offline rendering processBlock decodes synchronously instead.
    if (isNonRealtime())
        return;

    auto forEachWantedNote = [this] (auto&& fn)
    {
        for (int w = 0; w < 2; ++w)
        {
            const uint64_t bits = wantedNotes[w].load();
            for (int b = 0; b < 64; ++b)
                if ((bits >> b) & 1u)
                    if (! fn (w * 64 + b))
                        return;
        }
    };

    // 1. Notes just played get their own decode right away (not rate limited).
    forEachWantedNote ([this] (int n) { decodeNoteIfNeeded (n, false); return true; });

    // 2. Cursor moved: the sound at the cursor, then the held notes at the new place.
    const auto seq = requestSeq.load();
    const bool cursorStale = seq != servicedSeq;
    bool notesStale = false;
    forEachWantedNote ([&] (int n) { notesStale = noteDecodedSeq[n].load() != seq + 1; return ! notesStale; });
    if (! cursorStale && ! notesStale)
        return;

    const float rate = pInferenceRate->load();
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (rate > 0.0f && now - lastDecodeMs < 1000.0 / rate)
        return; // rate-limited: the poll retries shortly with the newest position

    if (cursorStale)
    {
        servicedSeq = seq;
        const auto generation = modelGeneration.load();
        std::vector<float> audio;
        float midi = 0.0f;
        if (decodeAt (currentX.load(), currentY.load(), std::numeric_limits<float>::quiet_NaN(), audio, midi))
            publishAudio (std::move (audio), generation, midi, -1, seq);
    }
    forEachWantedNote ([this] (int n)
    {
        decodeNoteIfNeeded (n, true);
        return true;
    });
    lastDecodeMs = juce::Time::getMillisecondCounterHiRes();
}

bool LatentAudioVSTAudioProcessor::decodeNoteIfNeeded (int note, bool refresh)
{
    // refresh = false: only notes never decoded for this hold; true: also stale ones.
    const auto seq = requestSeq.load();   // read before the position: errs towards re-decoding
    const auto done = noteDecodedSeq[note].load();
    if (done == seq + 1 || (! refresh && done != 0))
        return false;

    const auto generation = modelGeneration.load();
    std::vector<float> audio;
    float midi = 0.0f;
    if (! decodeAt (currentX.load(), currentY.load(), (float) note, audio, midi))
        return false;
    if (publishAudio (std::move (audio), generation, midi, note, seq))
        noteDecodedSeq[note].store (seq + 1);
    return true;
}

bool LatentAudioVSTAudioProcessor::decodeAt (float x, float y, float wantMidi, std::vector<float>& audio, float& outMidi) const
{
    auto map = soundMap.snapshot();
    if (map == nullptr || map->points.empty())
        return false;

    auto latent = map->interpolate (x, y);
    outMidi = std::numeric_limits<float>::quiet_NaN();
    if (map->hasPitch())
    {
        // Pitched models: ask for a specific note, or keep the blended natural pitch.
        if (std::isfinite (wantMidi))
            latent[(size_t) map->pitchIndex] = map->midiToLatent (wantMidi);
        outMidi = map->latentToMidi (latent[(size_t) map->pitchIndex]);
    }
    if (! decoder.decode (latent, audio))
        return false;

    return postProcess (audio);
}

double LatentAudioVSTAudioProcessor::ratioFor (float bufferMidi, int note) const
{
    // Playback-speed factor so a buffer sounds at the held note.
    if (pKeyTrack->load() < 0.5f || note < 0)
        return 1.0;
    const double from = std::isfinite (bufferMidi) ? (double) bufferMidi : (double) pRootNote->load();
    return std::pow (2.0, ((double) note - from) / 12.0);
}

bool LatentAudioVSTAudioProcessor::postProcess (std::vector<float>& audio)
{
    if (audio.empty())
        return false;

    for (float s : audio)
        if (! std::isfinite (s))
            return false;

    // Trim the inaudible tail with a short fade so loops/one-shots end cleanly.
    const float threshold = juce::Decibels::decibelsToGain (-60.0f);
    const int n = (int) audio.size();
    int lastHot = -1;
    for (int i = n - 1; i >= 0; --i)
        if (std::abs (audio[(size_t) i]) > threshold) { lastHot = i; break; }

    if (lastHot < 0)
    {
        std::fill (audio.begin(), audio.end(), 0.0f);
        return true;
    }

    if (lastHot < n - 882)
    {
        const int fadeStart = lastHot + 10;
        const int fadeLen = 512;
        const int fadeEnd = std::min (fadeStart + fadeLen, n);
        for (int i = fadeStart; i < fadeEnd; ++i)
            audio[(size_t) i] *= 1.0f - (float) (i - fadeStart) / (float) fadeLen;
        std::fill (audio.begin() + fadeEnd, audio.end(), 0.0f);
    }

    // Always end at zero.
    const int rampLen = std::min (32, n);
    for (int i = 0; i < rampLen; ++i)
        audio[(size_t) (n - rampLen + i)] *= 1.0f - (float) (i + 1) / (float) rampLen;

    float peak = 0.0f;
    for (float s : audio)
        peak = std::max (peak, std::abs (s));
    if (peak > 1.0f)
        for (float& s : audio)
            s /= peak;

    return true;
}

bool LatentAudioVSTAudioProcessor::publishAudio (std::vector<float>&& audio, uint32_t generation, float midi,
                                                 int note, uint32_t seq)
{
    if (generation != modelGeneration.load())
        return true; // decoded by a model that has since been replaced

    {
        const juce::ScopedLock sl (displayLock);
        displayWaveform = audio;
    }
    displayVersion.fetch_add (1);
    lastPublishedMidi.store (midi);

    {
        const juce::SpinLock::ScopedLockType sl (pendingLock);
        // Entry 0 is the cursor sound; notes use the entry already holding that note, or a free one.
        int target = note < 0 ? 0 : -1;
        for (int i = 1; i < kSlots && target < 0; ++i)
            if (pending[i].ready && pending[i].note == note)
                target = i;
        for (int i = 1; i < kSlots && target < 0; ++i)
            if (! pending[i].ready)
                target = i;
        if (target < 0)
            return false; // audio thread is behind; retried on the next poll

        auto& e = pending[target];
        std::swap (e.audio, audio);
        e.midi = midi;
        e.note = note;
        e.generation = generation;
        e.seq = seq;
        e.ready = true;
        pendingReady.store (true, std::memory_order_release);
    }
    // `audio` now holds a previous buffer and is freed here, off the audio thread.
    return true;
}

//==============================================================================
// Model loading
bool LatentAudioVSTAudioProcessor::findModelFiles (const juce::File& target, juce::File& onnx, juce::File& json, juce::String& error)
{
    if (! target.exists())
    {
        error = "Not found: " + target.getFullPathName();
        return false;
    }
    const auto folder = target.isDirectory() ? target : target.getParentDirectory();

    auto pick = [&folder] (const juce::String& wildcard, const juce::String& preferred) -> juce::File
    {
        auto preferredFile = folder.getChildFile (preferred);
        if (preferredFile.existsAsFile())
            return preferredFile;
        auto files = folder.findChildFiles (juce::File::findFiles, false, wildcard);
        files.sort();
        return files.isEmpty() ? juce::File() : files.getFirst();
    };

    onnx = target.hasFileExtension ("onnx") ? target : pick ("*.onnx", "latent_decoder.onnx");
    json = target.hasFileExtension ("json") ? target : pick ("*.json", "sound_map.json");

    if (! onnx.existsAsFile())
        error = "No .onnx model found in " + folder.getFileName();
    else if (! json.existsAsFile())
        error = "No sound map (.json) found in " + folder.getFileName();
    return error.isEmpty();
}

void LatentAudioVSTAudioProcessor::loadModelFolderAsync (const juce::File& folderOrFile)
{
    const auto token = loadToken.fetch_add (1) + 1;
    loadInProgress.store (true);
    {
        const juce::ScopedLock sl (statusLock);
        status.state = LoadState::Loading;
        status.folder = folderOrFile.isDirectory() ? folderOrFile : folderOrFile.getParentDirectory();
        status.message = "Loading " + status.folder.getFileName() + "...";
    }
    loaderPool.addJob ([this, folderOrFile, token] { runLoadJob (folderOrFile, false, token); });
}

void LatentAudioVSTAudioProcessor::runLoadJob (juce::File target, bool keepRestoredAudio, uint32_t token)
{
    auto fail = [this, token] (const juce::String& message)
    {
        LatentLog::write ("[Load] " + message);
        if (token == loadToken.load())
        {
            const juce::ScopedLock sl (statusLock);
            status.state = LoadState::Failed;
            status.message = message;
            loadInProgress.store (false);
        }
    };

    juce::File onnx, json;
    juce::String error;
    if (! findModelFiles (target, onnx, json, error))
        return fail (error);

    auto map = SoundMapData::load (json, error);
    if (map == nullptr)
        return fail (error);

    auto result = decoder.loadModel (onnx, map->latentDim);
    if (! result.ok)
        return fail (result.error);

    if (token != loadToken.load())
        return; // superseded by a newer load or an unload

    soundMap.setData (map);
    mapMinX.store (map->minX); mapMaxX.store (map->maxX);
    mapMinY.store (map->minY); mapMaxY.store (map->maxY);
    modelSampleRate.store (map->sampleRate);
    mapHasPitch.store (map->hasPitch());
    for (auto& d : noteDecodedSeq) d.store (0);

    if (! keepRestoredAudio)
    {
        modelGeneration.fetch_add (1);
        resetRequested.store (true);
        {
            const juce::ScopedLock sl (displayLock);
            displayWaveform.clear();
        }
        displayVersion.fetch_add (1);
    }

    // Keep the cursor if it lies on the new map, otherwise start on the first sound.
    float x = currentX.load(), y = currentY.load();
    if (x < map->minX || x > map->maxX || y < map->minY || y > map->maxY)
    {
        x = map->points.front().x;
        y = map->points.front().y;
    }
    currentX.store (x);
    currentY.store (y);

    {
        const juce::ScopedLock sl (statusLock);
        status.state = LoadState::Ready;
        status.folder = onnx.getParentDirectory();
        status.message = juce::String ((int) map->points.size()) + " sounds"
                       + (map->hasPitch() ? juce::String ("  |  plays any note with Key Track") : juce::String());
    }
    loadInProgress.store (false);
    LatentLog::write ("[Load] Loaded " + onnx.getFullPathName());

    // Prime the buffer so the first note plays immediately (unless restored audio is kept).
    if (! keepRestoredAudio || ! audioAvailable.load())
    {
        requestSeq.fetch_add (1);
        inferenceThread.wake();
    }

    // Position parameters must follow the cursor; notifying the host belongs on the message thread.
    juce::MessageManager::callAsync ([safeThis = juce::WeakReference<LatentAudioVSTAudioProcessor> (this)]
    {
        if (auto* self = safeThis.get())
        {
            const auto c = self->getCursor();
            self->setCursorAndSyncHost (c.x, c.y, true);
        }
    });
}

void LatentAudioVSTAudioProcessor::unloadModel()
{
    loadToken.fetch_add (1); // cancels any load in flight
    loadInProgress.store (false);
    decoder.unload();
    soundMap.clear();
    mapHasPitch.store (false);
    modelGeneration.fetch_add (1);
    resetRequested.store (true);
    {
        const juce::ScopedLock sl (displayLock);
        displayWaveform.clear();
    }
    displayVersion.fetch_add (1);
    {
        const juce::ScopedLock sl (statusLock);
        status = {};
        status.message = "No model loaded";
    }
}

LatentAudioVSTAudioProcessor::LoadStatus LatentAudioVSTAudioProcessor::getLoadStatus() const
{
    const juce::ScopedLock sl (statusLock);
    return status;
}

bool LatentAudioVSTAudioProcessor::waitForModelLoad (int timeoutMs) const
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (loadInProgress.load())
    {
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;
        juce::Thread::sleep (5);
    }
    return true;
}

//==============================================================================
// State
void LatentAudioVSTAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters->copyState();
    state.setProperty ("stateVersion", kStateVersion, nullptr);
    state.setProperty ("modelFolder", getLoadStatus().folder.getFullPathName(), nullptr);
    state.setProperty ("currentX", currentX.load(), nullptr);
    state.setProperty ("currentY", currentY.load(), nullptr);

    // Keep the current sound (mono float32) so the project still plays even if
    // the model folder is missing on another machine.
    {
        const juce::ScopedLock sl (displayLock);
        if (! displayWaveform.empty())
        {
            juce::MemoryBlock mb (displayWaveform.data(), displayWaveform.size() * sizeof (float));
            state.setProperty ("audio", mb.toBase64Encoding(), nullptr);
            if (std::isfinite (lastPublishedMidi.load()))
                state.setProperty ("audioMidi", lastPublishedMidi.load(), nullptr);
        }
    }

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void LatentAudioVSTAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters->state.getType()))
        return;

    const auto state = juce::ValueTree::fromXml (*xml);

    syncingPositionParams.store (true);
    parameters->replaceState (state);
    syncingPositionParams.store (false);

    currentX.store ((float) state.getProperty ("currentX", 0.0f));
    currentY.store ((float) state.getProperty ("currentY", 0.0f));

    // Model location: v2 stores the folder, v1 stored the .onnx file path.
    juce::File target;
    if (state.hasProperty ("modelFolder"))
        target = juce::File (state["modelFolder"].toString());
    else if (state.hasProperty ("modelPath"))
        target = juce::File (state["modelPath"].toString());

    // Saved audio: v2 = "audio"; v1 = per-buffer, per-channel copies.
    auto decodeFloats = [] (const juce::String& base64, int maxSamples)
    {
        std::vector<float> out;
        juce::MemoryBlock mb;
        if (base64.isNotEmpty() && mb.fromBase64Encoding (base64))
        {
            auto n = (int) (mb.getSize() / sizeof (float));
            if (maxSamples > 0) n = std::min (n, maxSamples);
            out.assign ((const float*) mb.getData(), (const float*) mb.getData() + n);
        }
        return out;
    };

    std::vector<float> audio;
    if (state.hasProperty ("audio"))
        audio = decodeFloats (state["audio"].toString(), 0);
    else if ((bool) state.getProperty ("hasValidAudio", false))
    {
        const int idx = juce::jlimit (0, 1, (int) state.getProperty ("activeBufferIndex", 0));
        const auto prefix = "buffer" + juce::String (idx);
        audio = decodeFloats (state[prefix + "_ch0"].toString(), (int) state.getProperty (prefix + "_samples", 0));
    }

    // Drop whatever was playing, then hand over the restored sound.
    modelGeneration.fetch_add (1);
    resetRequested.store (true);
    const bool haveAudio = ! audio.empty();
    if (haveAudio)
        injectDecodedAudio (std::move (audio), state.hasProperty ("audioMidi")
                                                   ? (float) state["audioMidi"]
                                                   : std::numeric_limits<float>::quiet_NaN());

    if (target != juce::File() && (target.isDirectory() || target.existsAsFile()))
    {
        const auto token = loadToken.fetch_add (1) + 1;
        loadInProgress.store (true);
        {
            const juce::ScopedLock sl (statusLock);
            status.state = LoadState::Loading;
            status.folder = target.isDirectory() ? target : target.getParentDirectory();
            status.message = "Loading " + status.folder.getFileName() + "...";
        }
        loaderPool.addJob ([this, target, token, haveAudio] { runLoadJob (target, haveAudio, token); });
    }
    else if (target != juce::File())
    {
        const juce::ScopedLock sl (statusLock);
        status.state = LoadState::Failed;
        status.folder = target.isDirectory() ? target : target.getParentDirectory();
        status.message = "Model folder not found - the saved sound still plays. Load the folder to explore again.";
    }
}

//==============================================================================
// Audio thread
void LatentAudioVSTAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate = sampleRate;

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), 1 };
    for (auto* f : { &filterA, &filterB })
    {
        f->prepare (spec);
        f->setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        f->reset();
    }

    cutoffSmoothed.reset (sampleRate, 0.02);
    cutoffSmoothed.setCurrentAndTargetValue (juce::jlimit (20.0f, (float) (0.45 * sampleRate), pCutoff->load()));
    gainSmoothed.reset (sampleRate, 0.02);
    gainSmoothed.setCurrentAndTargetValue (pGain->load() * pMix->load());

    offlineServicedSeq = requestSeq.load() - 1; // force a decode on the first offline block
}

void LatentAudioVSTAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    if (resetRequested.exchange (false))
    {
        for (auto& sl : slots)
        {
            sl.cur.clear();      // clear() keeps capacity: no deallocation here
            sl.prev.clear();
            sl.curMidi = sl.prevMidi = std::numeric_limits<float>::quiet_NaN();
            sl.fade = 1.0f;
            sl.note = -1;
        }
        for (auto& v : voices)
            v = {};
        numWaiting = 0;
        audioAvailable.store (false);
        filterA.reset();
        filterB.reset();
    }

    // A new model invalidates the per-note sounds.
    if (const auto gen = modelGeneration.load(); gen != audioGeneration)
    {
        audioGeneration = gen;
        for (int i = 1; i < kSlots; ++i)
        {
            slots[i].note = -1;
            slots[i].cur.clear();
            slots[i].prev.clear();
        }
        for (auto& v : voices)
        {
            if (v.slot > 0 || v.fromSlot > 0)
            {
                v.released = v.fastRelease = true;
                v.fromSlot = -1;
            }
            v.wantSlot = -1;
        }
    }

    if (isNonRealtime())
        renderOfflineInference();
    else
        takePendingAudio();

    if (startRequested.exchange (false) && ! slots[0].cur.empty())
        startVoice (-1, 1.0f);

    // Turning Hold off lets looping voices finish (unless a note still holds them).
    const bool latch = pLatch->load() > 0.5f;
    if (lastLatch && ! latch)
        for (auto& v : voices)
            if (v.playing && ! v.held)
                v.released = true;
    lastLatch = latch;

    const double ratio = (double) modelSampleRate.load() / hostSampleRate;
    const double tune = std::pow (2.0, (double) pTune->load() / 1200.0);
    const double baseSpeed = (double) pRate->load() * tune * ratio;

    // Render in segments between MIDI events for sample-accurate timing.
    float* out = buffer.getWritePointer (0);
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, numSamples, meta.samplePosition);
        renderVoices (out, pos, at, baseSpeed);
        handleMidi (meta.getMessage());
        pos = at;
    }
    renderVoices (out, pos, numSamples, baseSpeed);
    updateWantedNotes();

    // Filter + output level
    const float nyquistSafe = (float) (0.45 * hostSampleRate);
    cutoffSmoothed.setTargetValue (juce::jlimit (20.0f, nyquistSafe, pCutoff->load()));
    gainSmoothed.setTargetValue (pGain->load() * pMix->load());
    const float reso = juce::jlimit (0.0f, 1.0f, pReso->load());

    // 4-pole: Butterworth pair at zero resonance; resonance only on the second stage.
    filterA.setResonance (0.5412f);
    filterB.setResonance (1.3066f + reso * 7.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        if ((i & 7) == 0)
        {
            const float c = cutoffSmoothed.getNextValue();
            filterA.setCutoffFrequency (c);
            filterB.setCutoffFrequency (c);
            cutoffSmoothed.skip (std::min (7, numSamples - i - 1));
        }
        const float y = filterB.processSample (0, filterA.processSample (0, out[i]));
        out[i] = y * gainSmoothed.getNextValue();
    }

    for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

    // Playhead of the newest sounding voice.
    const Voice* newest = nullptr;
    int count = 0;
    for (const auto& v : voices)
    {
        if (! v.playing) continue;
        ++count;
        if (! v.released && (newest == nullptr || v.age > newest->age))
            newest = &v;
    }
    activeVoiceCount.store (count);
    float head = -1.0f;
    if (newest != nullptr)
    {
        const auto& sl = slots[newest->slot];
        if (! sl.cur.empty())
            head = (float) (newest->t * ratioFor (sl.curMidi, newest->note) / (double) sl.cur.size());
    }
    playheadNorm.store (head);
}

bool LatentAudioVSTAudioProcessor::slotInUse (int slotIndex) const
{
    for (const auto& v : voices)
        if (v.playing && (v.slot == slotIndex || v.fromSlot == slotIndex || v.wantSlot == slotIndex))
            return true;
    for (int i = 0; i < numWaiting; ++i)
        if (findNoteSlot (waiting[i].note) == slotIndex)
            return true;
    return false;
}

int LatentAudioVSTAudioProcessor::findNoteSlot (int note) const
{
    if (note < 0)
        return -1;
    for (int i = 1; i < kSlots; ++i)
        if (slots[i].note == note)
            return i;
    return -1;
}

int LatentAudioVSTAudioProcessor::allocateNoteSlot (int note)
{
    if (const int existing = findNoteSlot (note); existing > 0)
        return existing;
    // A free slot: never assigned, or no voice uses it any more.
    int best = -1;
    for (int i = 1; i < kSlots && best < 0; ++i)
        if (slots[i].note < 0)
            best = i;
    for (int i = 1; i < kSlots && best < 0; ++i)
        if (! slotInUse (i))
            best = i;
    if (best < 0)
        return -1; // all busy: the note plays the cursor sound resampled

    auto& sl = slots[best];
    sl.note = note;
    sl.cur.clear();
    sl.prev.clear();
    sl.fade = 1.0f;
    sl.seq = 0;
    return best;
}

void LatentAudioVSTAudioProcessor::updateWantedNotes()
{
    uint64_t bits[2] = { 0, 0 };
    if (pitchedKeyTrack())
    {
        for (int i = 1; i < kSlots; ++i)
        {
            const int n = slots[i].note;
            bool used = false;
            for (const auto& v : voices)
                used = used || (v.playing && ! v.released && (v.slot == i || v.wantSlot == i));
            for (int w = 0; w < numWaiting && ! used; ++w)
                used = waiting[w].note == n;
            if (n >= 0 && used)
                bits[n / 64] |= uint64_t (1) << (n % 64);
        }
    }
    const bool added = (bits[0] & ~wantedNotes[0].load()) != 0 || (bits[1] & ~wantedNotes[1].load()) != 0;
    wantedNotes[0].store (bits[0]);
    wantedNotes[1].store (bits[1]);
    if (added)
        inferenceThread.wake();
}

void LatentAudioVSTAudioProcessor::storeInSlot (int slotIndex, std::vector<float>& audio, float midi, int note,
                                                uint32_t seq, bool fade)
{
    auto& sl = slots[slotIndex];
    bool used = false;
    for (const auto& v : voices)
        used = used || (v.playing && (v.slot == slotIndex || v.fromSlot == slotIndex));

    if (fade && used && ! sl.cur.empty())
    {
        // Fade from the old content to the new one (voices on this slot follow).
        std::swap (sl.prev, sl.cur);
        sl.prevMidi = sl.curMidi;
        std::swap (sl.cur, audio);
        sl.fade = 0.0f;
        sl.fadeInc = (float) (1.0 / (kSwapFadeSeconds * hostSampleRate));
    }
    else
    {
        std::swap (sl.cur, audio);
        sl.fade = 1.0f;
    }
    sl.curMidi = midi;
    sl.seq = seq;
    if (slotIndex > 0)
        sl.note = note;

    if (slotIndex > 0 && sl.cur.size() >= 4)
    {
        // Voices waiting for this note's own sound crossfade over to it.
        for (auto& v : voices)
        {
            if (v.playing && v.wantSlot == slotIndex && v.slot != slotIndex)
            {
                v.fromSlot = v.slot;
                v.slot = slotIndex;
                v.switchFade = 0.0f;
            }
        }
    }

    // Notes that arrived before any sound existed start now.
    if (numWaiting > 0)
    {
        int keep = 0;
        for (int i = 0; i < numWaiting; ++i)
        {
            const auto w = waiting[i];
            const int ns = findNoteSlot (w.note);
            const bool ready = ! slots[0].cur.empty() || (ns > 0 && ! slots[ns].cur.empty());
            if (ready)
                startVoice (w.note, w.velocity);
            else
                waiting[keep++] = w;
        }
        numWaiting = keep;
    }
}

void LatentAudioVSTAudioProcessor::takePendingAudio()
{
    if (! pendingReady.load (std::memory_order_acquire))
        return;

    const juce::SpinLock::ScopedTryLockType lock (pendingLock);
    if (! lock.isLocked())
        return; // inference thread is mid-publish; next block

    bool remaining = false;
    const auto gen = modelGeneration.load();
    for (int i = 0; i < kSlots; ++i)
    {
        auto& e = pending[i];
        if (! e.ready)
            continue;
        if (e.generation != gen || e.audio.empty())
        {
            e.ready = false;
            continue;
        }

        const int slotIndex = e.note < 0 ? 0 : findNoteSlot (e.note);
        if (slotIndex < 0 || (slotIndex > 0 && ! pitchedKeyTrack()))
        {
            e.ready = false; // the note was let go and its slot reused
            continue;
        }
        if (slots[slotIndex].fade < 1.0f)
        {
            remaining = true; // never change content mid-fade; take it once the fade completes
            continue;
        }

        storeInSlot (slotIndex, e.audio, e.midi, e.note, e.seq, true);
        e.ready = false;
        if (slotIndex == 0)
        {
            audioAvailable.store (true);
            if (playOnArrival.exchange (false))
            {
                bool uiVoice = false;
                for (const auto& v : voices)
                    uiVoice = uiVoice || (v.playing && ! v.released && v.note < 0);
                if (! uiVoice)
                    startVoice (-1, 1.0f);
            }
        }
    }
    pendingReady.store (remaining);
}

void LatentAudioVSTAudioProcessor::renderOfflineInference()
{
    // Offline rendering may block: wait for a model that is still loading and
    // decode synchronously so exports are deterministic.
    if (loadInProgress.load())
        waitForModelLoad (120000);

    if (! decoder.isModelLoaded())
    {
        takePendingAudio(); // e.g. restored audio without a model
        return;
    }

    const auto seq = requestSeq.load();
    if (seq == offlineServicedSeq && ! slots[0].cur.empty())
        return;
    offlineServicedSeq = seq;

    std::vector<float> audio;
    float midi = 0.0f;
    if (! decodeAt (currentX.load(), currentY.load(), std::numeric_limits<float>::quiet_NaN(), audio, midi))
        return;

    {
        const juce::ScopedLock sl (displayLock);
        displayWaveform = audio;
    }
    displayVersion.fetch_add (1);
    lastPublishedMidi.store (midi);
    storeInSlot (0, audio, midi, -1, seq, true);
    audioAvailable.store (true);

    // Held notes follow the cursor.
    if (pitchedKeyTrack())
    {
        for (int i = 1; i < kSlots; ++i)
        {
            if (slots[i].note < 0 || ! slotInUse (i))
                continue;
            if (slots[i].fade < 1.0f)
                slots[i].fade = 1.0f;
            if (decodeAt (currentX.load(), currentY.load(), (float) slots[i].note, audio, midi))
                storeInSlot (i, audio, midi, slots[i].note, seq, true);
        }
    }

    if (playOnArrival.exchange (false))
    {
        bool uiVoice = false;
        for (const auto& v : voices)
            uiVoice = uiVoice || (v.playing && ! v.released && v.note < 0);
        if (! uiVoice)
            startVoice (-1, 1.0f);
    }
}

void LatentAudioVSTAudioProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        midiActivityCount.fetch_add (1);
        if (pTriggerOnMidi->load() >= 0.5f)
            noteOn (m.getNoteNumber(), m.getFloatVelocity());
    }
    else if (m.isNoteOff())
    {
        midiActivityCount.fetch_add (1);
        if (pTriggerOnMidi->load() < 0.5f)
            return;

        const int note = m.getNoteNumber();
        int keep = 0;
        for (int i = 0; i < numWaiting; ++i)
            if (waiting[i].note != note)
                waiting[keep++] = waiting[i];
        numWaiting = keep;
        releaseVoicesForNote (note);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        numWaiting = 0;
        releaseAllVoices();
    }
}

void LatentAudioVSTAudioProcessor::noteOn (int note, float velocity)
{
    // Pitched model + Key Track: every note gets its own decode from the model.
    // The voice starts now on the cursor sound (resampled) and crossfades to the
    // real note when it arrives; offline it is decoded right here.
    if (pitchedKeyTrack())
    {
        const int ns = allocateNoteSlot (note);
        if (ns > 0)
        {
            const auto seq = requestSeq.load();
            auto& sl = slots[ns];
            const bool fresh = ! sl.cur.empty() && sl.seq == seq && sl.fade >= 1.0f;
            if (! fresh)
            {
                if (isNonRealtime() && decoder.isModelLoaded())
                {
                    std::vector<float> audio;
                    float midi = 0.0f;
                    if (decodeAt (currentX.load(), currentY.load(), (float) note, audio, midi))
                        storeInSlot (ns, audio, midi, note, seq, false);
                }
                else
                {
                    noteDecodedSeq[note].store (0);
                }
            }
        }
    }

    const int ns = pitchedKeyTrack() ? findNoteSlot (note) : -1;
    const bool haveSound = ! slots[0].cur.empty() || (ns > 0 && ! slots[ns].cur.empty());
    if (! haveSound)
    {
        // No sound yet: start as soon as one arrives.
        if (numWaiting < kMaxVoices)
            waiting[numWaiting++] = { note, velocity };
        if (decoder.isModelLoaded())
            requestInference (false);
        updateWantedNotes();
        return;
    }

    startVoice (note, velocity);
    updateWantedNotes();
}

void LatentAudioVSTAudioProcessor::startVoice (int note, float velocity)
{
    int slotIndex = 0, wantSlot = -1;
    if (pitchedKeyTrack() && note >= 0)
    {
        wantSlot = findNoteSlot (note);
        if (wantSlot > 0 && slots[wantSlot].cur.size() >= 4)
            slotIndex = wantSlot;
    }
    if (slots[slotIndex].cur.size() < 4)
        return;

    // Same note (or another click on the map) retriggers: the old voice fades out quickly.
    // Over the voice limit: the oldest held voice fades out quickly.
    const int limit = juce::jlimit (1, kMaxVoices, (int) std::lround (pPolyphony->load()));
    int held = 0;
    for (auto& v : voices)
    {
        if (! v.playing || v.released)
            continue;
        if (v.note == note)
            v.released = v.fastRelease = true;
        else
            ++held;
    }
    while (held >= limit)
    {
        Voice* oldest = nullptr;
        for (auto& v : voices)
            if (v.playing && ! v.released && (oldest == nullptr || v.age < oldest->age))
                oldest = &v;
        if (oldest == nullptr)
            break;
        oldest->released = oldest->fastRelease = true;
        --held;
    }

    // A free voice, or the quietest releasing one.
    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.playing) { target = &v; break; }
    if (target == nullptr)
        for (auto& v : voices)
            if (v.released && (target == nullptr || v.env < target->env))
                target = &v;
    if (target == nullptr)
        target = &voices[0];

    const auto& sl = slots[slotIndex];
    const float sens = juce::jlimit (0.0f, 1.0f, pVelocity->load());
    Voice v;
    v.playing = true;
    v.held = note >= 0;
    v.note = note;
    v.slot = slotIndex;
    v.wantSlot = wantSlot != slotIndex ? wantSlot : -1;
    v.velocityGain = 1.0f - sens + sens * juce::jlimit (0.0f, 1.0f, velocity);
    v.t = juce::jlimit (0.0f, 0.9f, pStart->load()) * (double) sl.cur.size() / ratioFor (sl.curMidi, note);
    v.age = ++voiceCounter;
    *target = v;
}

void LatentAudioVSTAudioProcessor::releaseVoicesForNote (int note)
{
    const bool latch = pLatch->load() > 0.5f;
    for (auto& v : voices)
    {
        if (v.playing && v.note == note && v.held)
        {
            v.held = false;
            if (! latch)
                v.released = true;
        }
    }
}

void LatentAudioVSTAudioProcessor::releaseAllVoices()
{
    for (auto& v : voices)
    {
        v.held = false;
        if (v.playing)
            v.released = true;
    }
}

void LatentAudioVSTAudioProcessor::renderVoices (float* out, int startSample, int endSample, double baseSpeed)
{
    const int n = endSample - startSample;
    if (n <= 0)
        return;

    const bool latch = pLatch->load() > 0.5f;
    const double startFrac = juce::jlimit (0.0f, 0.9f, pStart->load());
    const double endFrac = juce::jlimit (0.1f, 1.0f, pEnd->load());
    const double seamFrac = juce::jlimit (0.0f, 0.5f, pLoopXfade->load());
    const float attackInc = (float) (1.0 / (kAttackSeconds * hostSampleRate));
    const float releaseDec = (float) (1.0 / (kReleaseSeconds * hostSampleRate));
    const float fastDec = (float) (1.0 / (kStealFadeSeconds * hostSampleRate));
    const float switchInc = (float) (1.0 / (kSwapFadeSeconds * hostSampleRate));

    struct Region { double start, end; };
    auto regionFor = [&] (int len)
    {
        const double minLen = std::max (64.0, 0.01 * len);
        double ls = startFrac * len;
        const double le = std::min ((double) len, std::max (endFrac * len, ls + minLen));
        ls = std::max (0.0, std::min (ls, le - minLen));
        return Region { ls, le };
    };

    // One buffer read at source time t, played at `ratio`, with the loop seam.
    auto readBuf = [&] (const std::vector<float>& b, double ratio, double t) -> float
    {
        const int len = (int) b.size();
        if (len < 4)
            return 0.0f;
        const auto r = regionFor (len);
        double p = t * ratio;
        if (latch && p >= r.end)
            p = r.start + std::fmod (p - r.start, r.end - r.start);
        if (p >= (double) len)
            return 0.0f;
        float sig = readHermite (b.data(), len, p);
        if (latch && seamFrac > 0.001)
        {
            const double seam = (r.end - r.start) * seamFrac;
            const double toEnd = r.end - p;
            if (toEnd < seam && toEnd >= 0.0)
            {
                const float w = (float) ((seam - toEnd) / seam);
                sig = sig * (1.0f - w) + readHermite (b.data(), len, r.start + (seam - toEnd)) * w;
            }
        }
        return sig;
    };

    for (auto& v : voices)
    {
        if (! v.playing)
            continue;

        const Slot& S = slots[v.slot];
        if (S.cur.size() < 4)
        {
            v.playing = false;
            continue;
        }
        const double rCur = ratioFor (S.curMidi, v.note);
        const double rPrev = ratioFor (S.prevMidi, v.note);
        const Slot* F = v.fromSlot >= 0 ? &slots[v.fromSlot] : nullptr;
        const double rFCur = F != nullptr ? ratioFor (F->curMidi, v.note) : 1.0;
        const double rFPrev = F != nullptr ? ratioFor (F->prevMidi, v.note) : 1.0;
        const auto region = regionFor ((int) S.cur.size());

        float sFade = S.fade;
        float fFade = F != nullptr ? F->fade : 1.0f;

        for (int i = startSample; i < endSample; ++i)
        {
            // Loop / end handling on the voice's own buffer.
            const double p = v.t * rCur;
            if (p >= region.end)
            {
                if (latch)
                    v.t = (region.start + std::fmod (p - region.start, region.end - region.start)) / rCur;
                else
                    v.released = true; // one-shot reached its end
            }

            float sig = readBuf (S.cur, rCur, v.t);
            if (sFade < 1.0f)
            {
                sig = readBuf (S.prev, rPrev, v.t) * (1.0f - sFade) + sig * sFade;
                sFade = std::min (1.0f, sFade + S.fadeInc);
            }
            if (F != nullptr)
            {
                float from = readBuf (F->cur, rFCur, v.t);
                if (fFade < 1.0f)
                {
                    from = readBuf (F->prev, rFPrev, v.t) * (1.0f - fFade) + from * fFade;
                    fFade = std::min (1.0f, fFade + F->fadeInc);
                }
                sig = from * (1.0f - v.switchFade) + sig * v.switchFade;
                v.switchFade += switchInc;
                if (v.switchFade >= 1.0f)
                {
                    v.switchFade = 1.0f;
                    v.fromSlot = -1;
                    v.wantSlot = -1;
                    F = nullptr;
                }
            }

            if (v.released)
            {
                v.env -= v.fastRelease ? fastDec : releaseDec;
                if (v.env <= 0.0f)
                {
                    v.env = 0.0f;
                    v.playing = false;
                    break;
                }
            }
            else if (v.env < 1.0f)
            {
                v.env = std::min (1.0f, v.env + attackInc);
            }

            out[i] += sig * v.env * v.velocityGain;
            v.t += baseSpeed;
        }
        if (! v.playing)
        {
            v.fromSlot = v.wantSlot = -1;
        }
    }

    // Slot fades advance once per segment (every voice above read them locally).
    for (auto& sl : slots)
        if (sl.fade < 1.0f)
            sl.fade = std::min (1.0f, sl.fade + sl.fadeInc * (float) n);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LatentAudioVSTAudioProcessor();
}
