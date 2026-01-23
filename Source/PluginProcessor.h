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
#include "SoundMap.h"
#include "LatentDecoder.h"
#include "InferenceThread.h"

//==============================================================================

//==============================================================================
class LatentAudioVSTAudioProcessor : public juce::AudioProcessor,
                                      public juce::AudioProcessorValueTreeState::Listener
{
public:
    LatentAudioVSTAudioProcessor();
    ~LatentAudioVSTAudioProcessor() override;

    // Model loading
    void setModelPath(const juce::File& path);
    void setSoundMapPath(const juce::File& path);
    void loadModelAndMap();
    bool loadModelFolder(const juce::File& folder);

    // Manual unload
    bool unloadModel(const juce::String& modelPath);

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // APVTS
    std::unique_ptr<juce::AudioProcessorValueTreeState> parameters;

    // APVTS listener
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    // Trigger inference from UI
    void triggerInference(float x, float y);
    void triggerRandomPosition(); // NEW
    void releaseNote();

    // Parameter setters
    void setPlaybackSpeed(float newSpeed);
    void setFineTune(float multiplier); // NEW
    void setCrossfade(float amount);
    void setLatch(bool shouldLatch);
    void setEnvelope(float attack, float decay, float sustain, float release); // Updated
    void setGain(float newGain);
    void setMix(float wetAmount); // NEW
    void setFilterCutoff(float cutoffHz);
    void setFilterResonance(float reso); // NEW
    void setMorphSpeed(float speed); // NEW
    void setMorphDepthX(float depth); // NEW
    void setMorphDepthY(float depth); // NEW
    void setMorphRate(float hz); // NEW
    void setTriggerOnMidi(bool shouldTrigger);
    void setPositionX(float normalizedX);
    void setPositionY(float normalizedY);
    void setSampleLength(float len);
    void setSampleStart(float start);

    // Public data
    SoundMap soundMap;
    LatentDecoder decoder;

    // Accessors
    float getPlaybackPositionNormalized() const;
    const std::vector<float>& getGeneratedWaveform() const { return lastGeneratedWaveform; }
    bool isMidiActive() const { return midiActive.load(); }

    // Parameters

    std::atomic<bool> newWaveformAvailable{ false };
    std::atomic<float> lpCutoff{ 10000.0f };
    std::atomic<float> lpReso{ 0.0f }; // NEW
    std::atomic<float> currentX{ 0.0f }; // NEW: Track active cursor position
    std::atomic<float> currentY{ 0.0f }; // NEW: Track active cursor position
    std::atomic<bool> midiActive{ false }; // NEW: Track MIDI activity
    int midiActivityCounter{ 0 }; // NEW: Counter to clear MIDI activity after timeout
    std::atomic<float> morphSpeed{ 0.0f }; // Moved to public for Editor access
    std::atomic<float> morphDepthX{ 1.0f }; // NEW: 0.0 to 1.0
    std::atomic<float> morphDepthY{ 1.0f }; // NEW: 0.0 to 1.0
    std::atomic<float> morphRateHz{ 2.0f }; // NEW: Updates per second

    // Envelope params (Full ADSR) - Moved to public for Editor visualization
    std::atomic<float> attackTime{ 0.01f };
    std::atomic<float> decayTime{ 0.1f };
    std::atomic<float> sustainLevel{ 0.7f };
    std::atomic<float> releaseTime{ 0.5f };
    std::atomic<bool> isLatch{ false }; // Moved to public for Editor access
    std::atomic<float> sampleLength{ 1.0f }; // Moved to public for Editor access
    std::atomic<float> sampleStart{ 0.0f }; // Moved to public for Editor access

    // Model paths
    juce::File modelPath;
    juce::File soundMapPath;

private:
    InferenceThread inferenceThread;

    // Double buffering
    juce::AudioBuffer<float> soundBuffers[2];
    std::atomic<int> activeBufferIndex{ 0 };

    // Crossfade state
    std::atomic<bool> triggerCrossfade{ false };
    bool isCrossfading = false;
    float crossfadePosition = 0.0f;
    float crossfadeIncrement = 0.0f;

    std::atomic<int> playbackPosition{ -1 };
    std::atomic<float> playbackSpeed{ 1.0f };
    std::atomic<float> fineTuneMultiplier{ 1.0f }; // NEW
    std::atomic<float> crossfade{ 0.05f };

    // NEW parameters
    std::atomic<float> mixAmount{ 1.0f }; // 0=dry, 1=wet

    // Morph automation
    float morphPhase = 0.0f;
    float lastMorphX = 0.0f;
    float lastMorphY = 0.0f;
    float morphAnchorX = 0.0f;
    float morphAnchorY = 0.0f;
    bool wasMorphing = false;
    float samplesUntilUpdate = 0.0f; // NEW: Counter for update rate

    // Parameter automation throttling
    float lastParamX = 0.0f;
    float lastParamY = 0.0f;
    std::atomic<float> inferenceRate{ 10.0f };
    float paramUpdateCounter = 0.0f; // For background inference rate limiting

    // Background inference position tracking
    float lastBackgroundX = 0.0f;
    float lastBackgroundY = 0.0f;
    bool backgroundPositionChanged = false;

    // Envelope params
    std::atomic<bool> triggerOnMidi{ false };
    std::atomic<float> outputGain{ 1.0f };

    // Envelope state machine
    enum class EnvelopeState { Idle, Attack, Decay, Sustain, Release };
    EnvelopeState envState{ EnvelopeState::Idle };
    float currentEnvLevel{ 0.0f };

    float fractionalPosition = 0.0f;

    std::vector<float> lastGeneratedWaveform;
    std::vector<float> dryBuffer; // NEW: Store unprocessed audio
    juce::CriticalSection bufferLock;

    // DSP
    juce::IIRFilter lpFilter1;
    juce::IIRFilter lpFilter2;

    // Filter coefficient caching
    juce::IIRCoefficients currentCoeffs;
    float lastCutoff = -1.0f;
    float lastReso = -1.0f;

//==============================================================================
// ADD TO PluginProcessor.h private members:
double currentSampleRate = 44100.0;
std::atomic<bool> hasValidAudio{false};  // NEW: Track if we have audio to play
std::atomic<bool> isFirstProcess{true};  // NEW: Track first process call for auto-generation
juce::CriticalSection filterLock;        // NEW: Protect filter state

// Offline mode (export) support - run synchronous inference
std::atomic<bool> isOfflineMode{false};
std::vector<float> offlineLatentBuffer;
std::vector<float> offlineAudioBuffer;
float lastOfflineX = -999.0f;
float lastOfflineY = -999.0f;

// Helper for synchronous inference in offline mode
void processOfflinePositionChange(float x, float y);

    void applyPostProcessing(std::vector<float>& audio);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LatentAudioVSTAudioProcessor)
};