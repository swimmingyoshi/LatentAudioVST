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

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <limits>
#include "SoundMap.h"
#include "LatentDecoder.h"
#include "InferenceThread.h"

//==============================================================================
// Threading model
// ---------------
//  * Audio thread    : processBlock. Owns the voice, playback buffers and filter.
//                      Never blocks: it only try-locks and reads atomics.
//  * Inference thread: decodes the latest requested position and publishes the
//                      result into `pendingAudio` (spin-lock held for a swap only).
//  * Loader thread   : loads model folders so the UI never freezes.
//  * Message thread  : UI and host state calls. Sends commands via atomics.
//
// A model generation counter tags every decode, so audio decoded by a previous
// model is dropped instead of played after a switch.
class LatentAudioVSTAudioProcessor : public juce::AudioProcessor,
                                      private juce::AudioProcessorValueTreeState::Listener
{
public:
    LatentAudioVSTAudioProcessor();
    ~LatentAudioVSTAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Model management (message thread)
    enum class LoadState { Empty, Loading, Ready, Failed };

    struct LoadStatus
    {
        LoadState state = LoadState::Empty;
        juce::String message;   // human readable status / error
        juce::File folder;      // folder of the current (or last attempted) model
    };

    // Loads asynchronously. Accepts a folder (containing an .onnx + sound map
    // .json), or the .onnx / .json file itself.
    void loadModelFolderAsync (const juce::File& folderOrFile);
    void unloadModel();
    LoadStatus getLoadStatus() const;
    bool waitForModelLoad (int timeoutMs) const; // for tests / offline rendering

    // Finds the model + sound map inside a folder. Prefers "sound_map.json".
    static bool findModelFiles (const juce::File& folderOrFile, juce::File& onnx, juce::File& json, juce::String& error);

    //==========================================================================
    // UI interaction (message thread)
    // Click (restart = true) or drag (restart = false: keep playing, crossfade to the new sound).
    void triggerFromUI (float mapX, float mapY, bool restart = true);
    void releaseFromUI();                         // mouse up (ends the automation gesture)
    void moveCursorFromUI (float mapX, float mapY); // move without auditioning
    void triggerRandomPosition();
    void resetParametersToDefaults();

    // Lets state restore and tests hand decoded audio to the player. `midi` is the
    // note the audio is at (pitched models), NaN if unknown. `note` >= 0 delivers it
    // as that held note's own sound (pitched Key Track); -1 = the sound at the cursor.
    void injectDecodedAudio (std::vector<float> audio, float midi = std::numeric_limits<float>::quiet_NaN(),
                             int note = -1);

    //==========================================================================
    // Read-only views for the editor (any thread)
    std::shared_ptr<const SoundMapData> getMapSnapshot() const { return soundMap.snapshot(); }
    juce::String getActiveProvider() const { return decoder.getActiveProvider(); }
    juce::Point<float> getCursor() const { return { currentX.load(), currentY.load() }; }
    float getPlayheadNormalized() const { return playheadNorm.load(); }
    bool hasAudio() const { return audioAvailable.load(); }
    uint32_t getMidiActivityCount() const { return midiActivityCount.load(); }
    uint32_t getDisplayVersion() const { return displayVersion.load(); }
    void copyDisplayWaveform (std::vector<float>& dest) const;

    // The current sound as you hear it, for export: Start/End, Rate + Tune, the
    // low-pass filter and Gain x Level applied (one-shot, full velocity). Mono, at
    // the model's sample rate. Empty if there is no sound.
    std::vector<float> renderForExport() const;
    int getModelSampleRate() const { return modelSampleRate.load(); }
    bool isPitchedModel() const { return mapHasPitch.load(); }
    // Pitched models with Key Track: notes the model is asked to generate.
    bool isNoteWanted (int note) const
    {
        return note >= 0 && note < 128 && ((wantedNotes[note / 64].load() >> (note % 64)) & 1u) != 0;
    }
    int getActiveVoiceCount() const { return activeVoiceCount.load(); }

    juce::AudioProcessorValueTreeState& getAPVTS() { return *parameters; }

    // Map <-> normalised position parameter conversion (Y is inverted: 1 = top).
    static juce::Point<float> mapToNormalized (const SoundMapData& map, float x, float y);
    static juce::Point<float> normalizedToMap (const SoundMapData& map, float nx, float ny);

private:
    //==========================================================================
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void applyPositionParameters();
    void requestInference (bool audition);
    void setCursorAndSyncHost (float x, float y, bool notifyHost);

    // Inference thread
    void serviceInference();
    bool decodeAt (float x, float y, float wantMidi, std::vector<float>& audio, float& outMidi) const;
    static bool postProcess (std::vector<float>& audio);
    bool publishAudio (std::vector<float>&& audio, uint32_t generation, float midi, int note, uint32_t seq);
    bool decodeNoteIfNeeded (int note, bool refresh);
    double ratioFor (float bufferMidi, int note) const;

    // Loader thread
    void runLoadJob (juce::File folderOrFile, bool keepRestoredAudio, uint32_t token);

    // Audio thread
    struct Slot;
    void takePendingAudio();
    void renderOfflineInference();
    void handleMidi (const juce::MidiMessage& m);
    void noteOn (int note, float velocity);
    void startVoice (int note, float velocity);
    void releaseVoicesForNote (int note);
    void releaseAllVoices();
    int findNoteSlot (int note) const;
    int allocateNoteSlot (int note);
    bool slotInUse (int slotIndex) const;
    void storeInSlot (int slotIndex, std::vector<float>& audio, float midi, int note, uint32_t seq, bool fade);
    void updateWantedNotes();
    bool pitchedKeyTrack() const { return mapHasPitch.load() && pKeyTrack->load() > 0.5f; }
    void renderVoices (float* out, int startSample, int endSample, double baseSpeed);

    //==========================================================================
    std::unique_ptr<juce::AudioProcessorValueTreeState> parameters;

    // Raw parameter values (read lock-free on the audio thread)
    std::atomic<float>* pRate = nullptr;
    std::atomic<float>* pTune = nullptr;
    std::atomic<float>* pLoopXfade = nullptr;
    std::atomic<float>* pCutoff = nullptr;
    std::atomic<float>* pReso = nullptr;
    std::atomic<float>* pGain = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pEnd = nullptr;
    std::atomic<float>* pStart = nullptr;
    std::atomic<float>* pPosX = nullptr;
    std::atomic<float>* pPosY = nullptr;
    std::atomic<float>* pInferenceRate = nullptr;
    std::atomic<float>* pLatch = nullptr;
    std::atomic<float>* pTriggerOnMidi = nullptr;
    std::atomic<float>* pKeyTrack = nullptr;
    std::atomic<float>* pRootNote = nullptr;
    std::atomic<float>* pVelocity = nullptr;
    std::atomic<float>* pPolyphony = nullptr;
    std::atomic<float>* pBlend = nullptr;

    //==========================================================================
    SoundMap soundMap;
    LatentDecoder decoder;

    // Cursor in map coordinates, and decode requests (latest wins).
    std::atomic<float> currentX { 0.0f }, currentY { 0.0f };
    std::atomic<uint32_t> requestSeq { 0 };
    std::atomic<bool> playOnArrival { false };
    std::atomic<bool> mapHasPitch { false };
    std::atomic<bool> syncingPositionParams { false };
    bool uiGestureActive = false; // message thread only

    // Map bounds mirrored as atomics for audio-thread parameter mapping.
    std::atomic<float> mapMinX { -1.0f }, mapMaxX { 1.0f }, mapMinY { -1.0f }, mapMaxY { 1.0f };
    std::atomic<int> modelSampleRate { 44100 };
    std::atomic<uint32_t> modelGeneration { 1 };

    static constexpr int kMaxVoices = 16;           // physical voices (release tails included)
    static constexpr int kNoteSlots = 8;            // per-note sounds (pitched models, Key Track)
    static constexpr int kSlots = 1 + kNoteSlots;   // slot 0 = the sound at the cursor

    // Inference -> audio hand-off: one entry per target (cursor sound or a note).
    struct Pending
    {
        std::vector<float> audio;
        float midi = std::numeric_limits<float>::quiet_NaN();
        int note = -1;              // -1 = cursor sound
        uint32_t generation = 0;
        uint32_t seq = 0;           // position request it was decoded for
        bool ready = false;
    };
    juce::SpinLock pendingLock;
    Pending pending[kSlots];        // guarded by pendingLock
    std::atomic<bool> pendingReady { false };
    std::atomic<float> lastPublishedMidi { std::numeric_limits<float>::quiet_NaN() };

    // Audio -> inference: notes held with Key Track on a pitched model (bitmask),
    // and the position request each note was last decoded for (0 = needs decode).
    std::atomic<uint64_t> wantedNotes[2] {};
    std::atomic<uint32_t> noteDecodedSeq[128] {};  // seq + 1 the note was decoded for

    // Inference -> UI
    mutable juce::CriticalSection displayLock;
    std::vector<float> displayWaveform;  // guarded by displayLock
    std::atomic<uint32_t> displayVersion { 0 };

    // UI -> audio commands
    std::atomic<bool> startRequested { false };
    std::atomic<bool> resetRequested { false };

    // Audio -> UI
    std::atomic<float> playheadNorm { -1.0f };
    std::atomic<bool> audioAvailable { false };
    std::atomic<uint32_t> midiActivityCount { 0 };
    std::atomic<int> activeVoiceCount { 0 };

    // Inference thread state
    uint32_t servicedSeq = 0;
    double lastDecodeMs = 0.0;
    InferenceThread inferenceThread;

    // Loader
    juce::ThreadPool loaderPool { 1 };
    mutable juce::CriticalSection statusLock;
    LoadStatus status;                    // guarded by statusLock
    std::atomic<uint32_t> loadToken { 0 };
    std::atomic<bool> loadInProgress { false };

    //==========================================================================
    // Audio-thread-only state
    struct Slot
    {
        std::vector<float> cur, prev;   // prev = content fading out after a swap
        float curMidi = std::numeric_limits<float>::quiet_NaN();
        float prevMidi = std::numeric_limits<float>::quiet_NaN();
        float fade = 1.0f, fadeInc = 0.0f;
        int note = -1;                  // note slots: which note this sound is
        uint32_t seq = 0;
    };

    struct Voice
    {
        bool playing = false;
        bool released = false;
        bool fastRelease = false;
        bool held = false;          // MIDI note still down
        double t = 0.0;             // elapsed source time (model samples at ratio 1)
        float env = 0.0f;
        float velocityGain = 1.0f;
        int note = -1;              // -1 = started from the UI
        int slot = 0;
        int wantSlot = -1;          // note slot this voice moves to once it has audio
        int fromSlot = -1;          // switching from this slot (crossfade)
        float switchFade = 1.0f;
        uint32_t age = 0;
    };

    struct WaitingNote { int note; float velocity; };

    Slot slots[kSlots];
    Voice voices[kMaxVoices];
    WaitingNote waiting[kMaxVoices];
    int numWaiting = 0;
    uint32_t voiceCounter = 0;
    uint32_t audioGeneration = 0;
    bool lastLatch = false;
    uint32_t offlineServicedSeq = 0;

    double hostSampleRate = 44100.0;
    juce::dsp::StateVariableTPTFilter<float> filterA, filterB;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffSmoothed;
    juce::SmoothedValue<float> gainSmoothed;
    JUCE_DECLARE_WEAK_REFERENCEABLE (LatentAudioVSTAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LatentAudioVSTAudioProcessor)
};
