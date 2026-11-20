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

    parameters = std::make_unique<juce::AudioProcessorValueTreeState> (
        *this, nullptr, "PARAMETERS", juce::AudioProcessorValueTreeState::ParameterLayout (p.begin(), p.end()));

    auto raw = [this] (const char* id) { return parameters->getRawParameterValue (id); };
    pRate = raw ("playbackRate");     pTune = raw ("fineTune");       pLoopXfade = raw ("crossfade");
    pCutoff = raw ("filterCutoff");   pReso = raw ("filterResonance"); pGain = raw ("outputGain");
    pMix = raw ("mixAmount");         pEnd = raw ("sampleLength");    pStart = raw ("sampleStart");
    pPosX = raw ("positionX");        pPosY = raw ("positionY");      pInferenceRate = raw ("inferenceRate");
    pLatch = raw ("latchMode");       pTriggerOnMidi = raw ("triggerOnMidi");
    pKeyTrack = raw ("keyTrack");     pRootNote = raw ("rootNote");   pVelocity = raw ("velocitySens");

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

void LatentAudioVSTAudioProcessor::injectDecodedAudio (std::vector<float> audio, float midi)
{
    if (audio.empty() || ! std::all_of (audio.begin(), audio.end(), [] (float v) { return std::isfinite (v); }))
        return;
    publishAudio (std::move (audio), modelGeneration.load(), midi);
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

    const auto seq = requestSeq.load();
    if (seq == servicedSeq)
        return;

    const float rate = pInferenceRate->load();
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (rate > 0.0f && now - lastDecodeMs < 1000.0 / rate)
        return; // rate-limited: the poll retries shortly with the newest position

    servicedSeq = seq;
    const auto generation = modelGeneration.load();

    std::vector<float> audio;
    float midi = 0.0f;
    if (! decodeAt (currentX.load(), currentY.load(), requestMidi.load(), audio, midi))
        return;

    lastDecodeMs = juce::Time::getMillisecondCounterHiRes();
    publishAudio (std::move (audio), generation, midi);
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

void LatentAudioVSTAudioProcessor::publishAudio (std::vector<float>&& audio, uint32_t generation, float midi)
{
    if (generation != modelGeneration.load())
        return; // decoded by a model that has since been replaced

    {
        const juce::ScopedLock sl (displayLock);
        displayWaveform = audio;
    }
    displayVersion.fetch_add (1);
    lastPublishedMidi.store (midi);

    {
        const juce::SpinLock::ScopedLockType sl (pendingLock);
        std::swap (pendingAudio, audio);
        pendingGeneration = generation;
        pendingMidi = midi;
        pendingReady.store (true, std::memory_order_release);
    }
    // `audio` now holds the previous pending buffer and is freed here, off the audio thread.
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
    requestMidi.store (std::numeric_limits<float>::quiet_NaN());

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
    requestMidi.store (std::numeric_limits<float>::quiet_NaN());
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
        activeAudio.clear();     // clear() keeps capacity: no deallocation here
        incomingAudio.clear();
        activeMidi = incomingMidi = std::numeric_limits<float>::quiet_NaN();
        voice = {};
        crossfading = false;
        stealGain = 0.0f;
        waitingNoteStart = false;
        audioAvailable.store (false);
        filterA.reset();
        filterB.reset();
    }

    if (isNonRealtime())
        renderOfflineInference();
    else
        takePendingAudio();

    if (startRequested.exchange (false) && ! activeAudio.empty())
        startVoice (-1, 1.0f);

    // Key Track turned off on a pitched model: go back to each sound's natural pitch.
    const bool keyTrack = pKeyTrack->load() > 0.5f;
    if (lastKeyTrack && ! keyTrack && mapHasPitch.load() && std::isfinite (requestMidi.load()))
    {
        requestMidi.store (std::numeric_limits<float>::quiet_NaN());
        requestInference (false);
    }
    lastKeyTrack = keyTrack;

    // Turning Hold off lets a looping voice finish (unless a note still holds it).
    const bool latch = pLatch->load() > 0.5f;
    if (lastLatch && ! latch && voice.playing && voice.note < 0)
        releaseVoice();
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
        renderVoice (out, pos, at, baseSpeed);
        handleMidi (meta.getMessage());
        pos = at;
    }
    renderVoice (out, pos, numSamples, baseSpeed);

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

    const int len = (int) activeAudio.size();
    playheadNorm.store (voice.playing && len > 0 ? (float) (voice.pos / (double) len) : -1.0f);
}

void LatentAudioVSTAudioProcessor::takePendingAudio()
{
    if (! pendingReady.load (std::memory_order_acquire) || crossfading)
        return; // never change content mid-fade; take it once the fade completes

    const juce::SpinLock::ScopedTryLockType lock (pendingLock);
    if (! lock.isLocked())
        return; // inference thread is mid-publish; next block

    if (pendingGeneration != modelGeneration.load() || pendingAudio.empty())
    {
        pendingReady.store (false);
        return;
    }

    const bool audition = playOnArrival.exchange (false);

    if (voice.playing && ! voice.released && ! activeAudio.empty())
    {
        std::swap (incomingAudio, pendingAudio);
        incomingMidi = pendingMidi;
        incomingRatio = ratioFor (incomingMidi, voice.note);
        crossfading = true;
        crossfadePos = 0.0f;
        crossfadeInc = (float) (1.0 / (kSwapFadeSeconds * hostSampleRate));
    }
    else
    {
        std::swap (activeAudio, pendingAudio);
        activeMidi = pendingMidi;
        if (waitingNoteStart)
        {
            waitingNoteStart = false;
            startVoice (waitingNote, waitingVelocity);
        }
        else if (audition)
        {
            startVoice (-1, 1.0f);
        }
    }

    pendingReady.store (false);
    audioAvailable.store (true);
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
    if (seq == offlineServicedSeq && ! activeAudio.empty())
        return;
    offlineServicedSeq = seq;

    std::vector<float> audio;
    float midi = 0.0f;
    if (! decodeAt (currentX.load(), currentY.load(), requestMidi.load(), audio, midi))
        return;

    {
        const juce::ScopedLock sl (displayLock);
        displayWaveform = audio;
    }
    displayVersion.fetch_add (1);
    lastPublishedMidi.store (midi);

    if (crossfading)
        finishCrossfade();
    const double oldRatio = voice.noteRatio;
    std::swap (activeAudio, audio);   // hard swap: matches realtime timing best offline
    activeMidi = midi;
    if (voice.playing)
    {
        voice.noteRatio = ratioFor (activeMidi, voice.note);
        voice.pos = voice.pos / oldRatio * voice.noteRatio;
    }
    audioAvailable.store (true);

    const bool audition = playOnArrival.exchange (false);
    if (waitingNoteStart)
    {
        waitingNoteStart = false;
        startVoice (waitingNote, waitingVelocity);
    }
    else if (audition && ! (voice.playing && ! voice.released))
    {
        startVoice (-1, 1.0f);
    }
}

void LatentAudioVSTAudioProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        midiActivityCount.fetch_add (1);
        if (pTriggerOnMidi->load() < 0.5f)
            return;

        const int note = m.getNoteNumber();
        // Pitched model + Key Track: ask the model for this exact note. The voice
        // starts now on the current sound (resampled) and crossfades to the real
        // note when it arrives; offline it is decoded right here.
        const bool askModel = mapHasPitch.load() && pKeyTrack->load() > 0.5f;
        if (askModel)
            requestMidi.store ((float) note);

        if (activeAudio.empty())
        {
            // No sound yet: start as soon as one arrives.
            waitingNoteStart = true;
            waitingNote = note;
            waitingVelocity = m.getFloatVelocity();
            if (decoder.isModelLoaded())
                requestInference (false);
            return;
        }

        if (askModel && ! (std::abs (activeMidi - (float) note) < 0.01f))
        {
            if (isNonRealtime())
            {
                std::vector<float> audio;
                float midi = 0.0f;
                if (decodeAt (currentX.load(), currentY.load(), (float) note, audio, midi))
                {
                    if (crossfading)
                        finishCrossfade();
                    std::swap (activeAudio, audio);
                    activeMidi = midi;
                    offlineServicedSeq = requestSeq.load();
                }
            }
            else
            {
                requestInference (false);
            }
        }
        startVoice (note, m.getFloatVelocity());
    }
    else if (m.isNoteOff())
    {
        midiActivityCount.fetch_add (1);
        if (pTriggerOnMidi->load() < 0.5f)
            return;

        if (waitingNoteStart && m.getNoteNumber() == waitingNote)
            waitingNoteStart = false;

        // Only the note that is sounding may release it (overlapping legato notes).
        if (voice.playing && m.getNoteNumber() == voice.note)
        {
            voice.note = -1;
            if (pLatch->load() < 0.5f)
                releaseVoice();
        }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        waitingNoteStart = false;
        voice.note = -1;
        releaseVoice();
    }
}

void LatentAudioVSTAudioProcessor::startVoice (int note, float velocity)
{
    const int len = (int) activeAudio.size();
    if (len < 4)
        return;

    // Declick: let the old read head fade out briefly instead of jumping.
    if (voice.playing && voice.envLevel * voice.velocityGain > 1.0e-4f)
    {
        stealPos = voice.pos;
        stealSpeed = (double) pRate->load() * voice.noteRatio * (double) modelSampleRate.load() / hostSampleRate;
        stealGain = voice.envLevel * voice.velocityGain;
        stealInc = stealGain / (float) (kStealFadeSeconds * hostSampleRate);
    }

    const float sens = juce::jlimit (0.0f, 1.0f, pVelocity->load());

    voice.playing = true;
    voice.released = false;
    voice.envLevel = 0.0f;
    voice.pos = juce::jlimit (0.0f, 0.9f, pStart->load()) * (double) len;
    voice.note = note;
    voice.velocityGain = 1.0f - sens + sens * juce::jlimit (0.0f, 1.0f, velocity);
    voice.noteRatio = ratioFor (activeMidi, note);
}

void LatentAudioVSTAudioProcessor::releaseVoice()
{
    if (voice.playing)
        voice.released = true;
}

void LatentAudioVSTAudioProcessor::finishCrossfade()
{
    // The incoming buffer may play at a different speed (it was decoded at the held
    // note instead of being resampled to it): keep the same point in time.
    std::swap (activeAudio, incomingAudio);
    voice.pos = voice.pos / voice.noteRatio * incomingRatio;
    voice.noteRatio = incomingRatio;
    std::swap (activeMidi, incomingMidi);
    crossfading = false;
    crossfadePos = 0.0f;
}

void LatentAudioVSTAudioProcessor::renderVoice (float* out, int startSample, int endSample, double baseSpeed)
{
    if (startSample >= endSample || (! voice.playing && stealGain <= 0.0f))
        return;

    const bool latch = pLatch->load() > 0.5f;
    const double startFrac = juce::jlimit (0.0f, 0.9f, pStart->load());
    const double endFrac = juce::jlimit (0.1f, 1.0f, pEnd->load());
    const double seamFrac = juce::jlimit (0.0f, 0.5f, pLoopXfade->load());
    const float attackInc = (float) (1.0 / (kAttackSeconds * hostSampleRate));
    const float releaseDec = (float) (1.0 / (kReleaseSeconds * hostSampleRate));

    const float* data = nullptr;
    int len = 0;
    double loopStart = 0.0, loopEnd = 0.0;
    auto refresh = [&]
    {
        data = activeAudio.data();
        len = (int) activeAudio.size();
        const double minLen = std::max (64.0, 0.01 * len);
        loopStart = startFrac * len;
        loopEnd = std::min ((double) len, std::max (endFrac * len, loopStart + minLen));
        loopStart = std::max (0.0, std::min (loopStart, loopEnd - minLen));
    };
    refresh();

    if (len < 4)
    {
        voice.playing = false;
        stealGain = 0.0f;
        return;
    }

    for (int i = startSample; i < endSample; ++i)
    {
        float sample = 0.0f;

        if (voice.playing)
        {
            const double speed = baseSpeed * voice.noteRatio;
            float sig = 0.0f;

            if (voice.pos >= loopEnd)
            {
                if (latch)
                    voice.pos = loopStart + std::fmod (voice.pos - loopStart, loopEnd - loopStart);
                else
                    voice.released = true; // one-shot reached its end
            }

            if (voice.pos < loopEnd)
            {
                sig = readHermite (data, len, voice.pos);

                // Loop seam: blend the loop start into the end so the wrap is seamless.
                if (latch && seamFrac > 0.001)
                {
                    const double seam = (loopEnd - loopStart) * seamFrac;
                    const double toEnd = loopEnd - voice.pos;
                    if (toEnd < seam)
                    {
                        const float w = (float) ((seam - toEnd) / seam);
                        sig = sig * (1.0f - w) + readHermite (data, len, loopStart + (seam - toEnd)) * w;
                    }
                }

                if (crossfading)
                {
                    const int inLen = (int) incomingAudio.size();
                    const double inPos = voice.pos / voice.noteRatio * incomingRatio;
                    const float next = inLen > 3 ? readHermite (incomingAudio.data(), inLen, inPos) : 0.0f;
                    sig = sig * (1.0f - crossfadePos) + next * crossfadePos;
                    crossfadePos += crossfadeInc;
                    if (crossfadePos >= 1.0f)
                    {
                        finishCrossfade();
                        refresh();
                        if (len < 4) { voice.playing = false; break; }
                    }
                }
            }

            if (voice.released)
            {
                voice.envLevel -= releaseDec;
                if (voice.envLevel <= 0.0f)
                {
                    voice.envLevel = 0.0f;
                    voice.playing = false;
                    if (crossfading)
                    {
                        finishCrossfade();
                        refresh();
                    }
                }
            }
            else if (voice.envLevel < 1.0f)
            {
                voice.envLevel = std::min (1.0f, voice.envLevel + attackInc);
            }

            sample = sig * voice.envLevel * voice.velocityGain;
            voice.pos += speed;
        }

        if (stealGain > 0.0f)
        {
            if (stealPos < (double) len)
                sample += readHermite (data, len, stealPos) * stealGain;
            stealPos += stealSpeed;
            stealGain = std::max (0.0f, stealGain - stealInc);
        }

        out[i] += sample;

        if (! voice.playing && stealGain <= 0.0f)
            break;
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LatentAudioVSTAudioProcessor();
}
