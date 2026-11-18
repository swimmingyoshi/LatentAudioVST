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

    // Lets state restore and tests hand decoded audio to the player.
    void injectDecodedAudio (std::vector<float> audio);

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
    int getModelSampleRate() const { return modelSampleRate.load(); }

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
    bool decodeAt (float x, float y, std::vector<float>& audio) const;
    static bool postProcess (std::vector<float>& audio);
    void publishAudio (std::vector<float>&& audio, uint32_t generation);

    // Loader thread
    void runLoadJob (juce::File folderOrFile, bool keepRestoredAudio, uint32_t token);

    // Audio thread
    void takePendingAudio();
    void renderOfflineInference();
    void handleMidi (const juce::MidiMessage& m);
    void startVoice (int note, float velocity);
    void releaseVoice();
    void renderVoice (float* out, int startSample, int endSample, double baseSpeed);
    void finishCrossfade();

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

    //==========================================================================
    SoundMap soundMap;
    LatentDecoder decoder;

    // Cursor in map coordinates, and decode requests (latest wins).
    std::atomic<float> currentX { 0.0f }, currentY { 0.0f };
    std::atomic<uint32_t> requestSeq { 0 };
    std::atomic<bool> playOnArrival { false };
    std::atomic<bool> syncingPositionParams { false };
    bool uiGestureActive = false; // message thread only

    // Map bounds mirrored as atomics for audio-thread parameter mapping.
    std::atomic<float> mapMinX { -1.0f }, mapMaxX { 1.0f }, mapMinY { -1.0f }, mapMaxY { 1.0f };
    std::atomic<int> modelSampleRate { 44100 };
    std::atomic<uint32_t> modelGeneration { 1 };

    // Inference -> audio hand-off
    juce::SpinLock pendingLock;
    std::vector<float> pendingAudio;     // guarded by pendingLock
    uint32_t pendingGeneration = 0;      // guarded by pendingLock
    std::atomic<bool> pendingReady { false };

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
    struct Voice
    {
        bool playing = false;
        bool released = false;
        double pos = 0.0;
        float envLevel = 0.0f;
        float velocityGain = 1.0f;
        double noteRatio = 1.0;
        int note = -1;
    };

    Voice voice;
    std::vector<float> activeAudio, incomingAudio;
    bool crossfading = false;
    float crossfadePos = 0.0f, crossfadeInc = 0.0f;
    bool waitingNoteStart = false;
    int waitingNote = -1;
    float waitingVelocity = 1.0f;
    bool lastLatch = false;
    uint32_t offlineServicedSeq = 0;

    // Retrigger declick: fades out the old read head when a voice restarts.
    double stealPos = 0.0, stealSpeed = 1.0;
    float stealGain = 0.0f, stealInc = 0.0f;

    double hostSampleRate = 44100.0;
    juce::dsp::StateVariableTPTFilter<float> filterA, filterB;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffSmoothed;
    juce::SmoothedValue<float> gainSmoothed;
    JUCE_DECLARE_WEAK_REFERENCEABLE (LatentAudioVSTAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LatentAudioVSTAudioProcessor)
};
