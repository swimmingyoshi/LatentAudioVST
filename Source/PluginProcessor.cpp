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

//==============================================================================
LatentAudioVSTAudioProcessor::LatentAudioVSTAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
    inferenceThread(decoder)
#endif
{
    // Set up file-based logging for DAW debugging
    juce::File logFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                            .getChildFile("LatentAudioVST_debug.log");
    juce::Logger::setCurrentLogger(new juce::FileLogger(logFile, "LatentAudioVST Debug Log"));

    juce::Logger::writeToLog("[PluginProcessor] === CONSTRUCTOR ===");
    juce::Logger::writeToLog("[PluginProcessor] this=" + juce::String::toHexString((juce::int64)this));
    juce::Logger::writeToLog("[PluginProcessor] LatentDecoder instance count: " + juce::String(LatentDecoder::getInstanceCount()));

    inferenceThread.onInferenceFinished = [this](const std::vector<float>& rawAudio)
        {
            // juce::Logger::writeToLog("[InferenceFinished] Received " + juce::String(rawAudio.size()) + " samples");

            if (rawAudio.empty())
            {
                // juce::Logger::writeToLog("[InferenceFinished] EMPTY AUDIO - aborting");
                return;
            }

            // Validate audio data BEFORE processing
            bool hasInvalidData = false;
            float maxAmp = 0.0f;
            for (float sample : rawAudio)
            {
                if (!std::isfinite(sample))
                {
                    hasInvalidData = true;
                    break;
                }
                maxAmp = std::max(maxAmp, std::abs(sample));
            }

            if (hasInvalidData)
            {
                // juce::Logger::writeToLog("[InferenceFinished] INVALID DATA (NaN/Inf) - aborting");
                return;
            }

            if (maxAmp < 0.0001f)
            {
                // juce::Logger::writeToLog("[InferenceFinished] WARNING: Audio is silent (max=" + juce::String(maxAmp) + ")");
            }

            // juce::Logger::writeToLog("[InferenceFinished] Valid audio, max amplitude: " + juce::String(maxAmp));

            // FILTER RESET REMOVED TO PREVENT CLICKS/CRACKLES
            /*
            {
                const juce::ScopedLock sl(filterLock);
                lpFilter1.reset();
                lpFilter2.reset();
                // juce::Logger::writeToLog("[InferenceFinished] Filters reset for new audio");
            }
            */

            // Now apply post-processing
            std::vector<float> processedAudio = rawAudio;
            applyPostProcessing(processedAudio);

            // Check post-processing didn't break it
            maxAmp = 0.0f;
            for (float sample : processedAudio)
            {
                if (!std::isfinite(sample))
                {
                    // juce::Logger::writeToLog("[InferenceFinished] POST-PROCESSING CREATED INVALID DATA - aborting");
                    return;
                }
                maxAmp = std::max(maxAmp, std::abs(sample));
            }

            // juce::Logger::writeToLog("[InferenceFinished] Post-processed, max amplitude: " + juce::String(maxAmp));

            // Copy to back buffer
            {
                juce::ScopedLock sl(bufferLock);

                int backIndex = 1 - activeBufferIndex.load();
                auto& backBuffer = soundBuffers[backIndex];

                if (backBuffer.getNumSamples() != (int)processedAudio.size())
                    backBuffer.setSize(2, (int)processedAudio.size(), false, false, false);

                backBuffer.clear();

                for (int ch = 0; ch < backBuffer.getNumChannels(); ++ch)
                    juce::FloatVectorOperations::copy(backBuffer.getWritePointer(ch),
                        processedAudio.data(),
                        (int)processedAudio.size());

                triggerCrossfade.store(true);

                // juce::Logger::writeToLog("[InferenceFinished] Copied to buffer " + juce::String(backIndex));
            }

            // Start playback if not already playing
            if (playbackPosition.load() < 0)
            {
                playbackPosition.store(0);
                fractionalPosition = 0.0f;

                int backIndex = 1 - activeBufferIndex.load();
                activeBufferIndex.store(backIndex);
                triggerCrossfade.store(false);

                hasValidAudio.store(true);  // Mark that we have audio

                // juce::Logger::writeToLog("[InferenceFinished] Started playback on buffer " + juce::String(backIndex));
            }

            // Update waveform display
            lastGeneratedWaveform.resize(processedAudio.size());
            std::memcpy(lastGeneratedWaveform.data(), processedAudio.data(), processedAudio.size() * sizeof(float));
            newWaveformAvailable.store(true);

            // juce::Logger::writeToLog("[InferenceFinished] === COMPLETE ===");
        };

    inferenceThread.startThread();

    // FIXED: Create parameters vector first, then pass to ParameterLayout
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Playback
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "playbackRate", "Playback Rate",
        juce::NormalisableRange<float>(0.1f, 2.0f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "fineTune", "Fine Tune (cents)",
        juce::NormalisableRange<float>(-100.0f, 100.0f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "crossfade", "Crossfade Amount",
        juce::NormalisableRange<float>(0.0f, 0.5f), 0.05f));

    // Filter
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "filterCutoff", "Filter Cutoff (Hz)",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f), 10000.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "filterResonance", "Filter Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // Envelope
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "attackTime", "Attack Time (sec)",
        juce::NormalisableRange<float>(0.001f, 2.0f), 0.01f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "decayTime", "Decay Time (sec)",
        juce::NormalisableRange<float>(0.001f, 2.0f), 0.1f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "sustainLevel", "Sustain Level",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.7f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "releaseTime", "Release Time (sec)",
        juce::NormalisableRange<float>(0.01f, 5.0f), 0.5f));

    // Morph
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "morphSpeed", "Morph Speed",
        juce::NormalisableRange<float>(0.0f, 5.0f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "morphRate", "Morph Rate (Hz)",
        juce::NormalisableRange<float>(0.1f, 20.0f), 2.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "morphDepthX", "Morph Depth X",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "morphDepthY", "Morph Depth Y",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));

    // Master
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "outputGain", "Output Gain",
        juce::NormalisableRange<float>(0.0f, 2.0f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mixAmount", "Dry/Wet Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "sampleLength", "Sample Length",
        juce::NormalisableRange<float>(0.1f, 1.0f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "sampleStart", "Sample Start Position",
        juce::NormalisableRange<float>(0.0f, 0.9f), 0.0f));

    // Position automation parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "positionX", "Position X",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "positionY", "Position Y",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));


    // RateLimit (inferenceRate)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "inferenceRate", "Inference Rate (Hz)",
        juce::NormalisableRange<float>(0.0f, 50.0f), 10.0f));

    // Toggles - use AudioParameterBool instead
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "latchMode", "Latch Mode", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "triggerOnMidi", "Trigger on MIDI", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "snapMode", "Snap Mode", false));

    // NOW create the APVTS with the vector
    parameters = std::make_unique<juce::AudioProcessorValueTreeState>(
        *this, nullptr, "PARAMETERS",
        juce::AudioProcessorValueTreeState::ParameterLayout(params.begin(), params.end()));

    // Add parameter listeners
    parameters->addParameterListener("playbackRate", this);
    parameters->addParameterListener("fineTune", this);
    parameters->addParameterListener("crossfade", this);
    parameters->addParameterListener("filterCutoff", this);
    parameters->addParameterListener("filterResonance", this);
    parameters->addParameterListener("attackTime", this);
    parameters->addParameterListener("decayTime", this);
    parameters->addParameterListener("sustainLevel", this);
    parameters->addParameterListener("releaseTime", this);
    parameters->addParameterListener("morphSpeed", this);
    parameters->addParameterListener("morphRate", this);
    parameters->addParameterListener("morphDepthX", this);
    parameters->addParameterListener("morphDepthY", this);
    parameters->addParameterListener("outputGain", this);
    parameters->addParameterListener("mixAmount", this);
    parameters->addParameterListener("sampleLength", this);
    parameters->addParameterListener("sampleStart", this);
    parameters->addParameterListener("positionX", this);
    parameters->addParameterListener("positionY", this);
    parameters->addParameterListener("inferenceRate", this);
    parameters->addParameterListener("latchMode", this);
    parameters->addParameterListener("triggerOnMidi", this);
    parameters->addParameterListener("snapMode", this);

}

LatentAudioVSTAudioProcessor::~LatentAudioVSTAudioProcessor()
{
    juce::Logger::writeToLog("[PluginProcessor] === DESTRUCTOR ===");
    juce::Logger::writeToLog("[PluginProcessor] this=" + juce::String::toHexString((juce::int64)this));
    juce::Logger::writeToLog("[PluginProcessor] LatentDecoder instance count before: " + juce::String(LatentDecoder::getInstanceCount()));
    juce::Logger::writeToLog("[PluginProcessor] Stopping inference thread...");
    inferenceThread.stopThread(4000);
    juce::Logger::writeToLog("[PluginProcessor] Inference thread stopped");
    juce::Logger::writeToLog("[PluginProcessor] === DESTRUCTOR END ===");
    juce::Logger::setCurrentLogger(nullptr);
}

//==============================================================================
// ============================================================================
// FIXED: parameterChanged for bool parameters
// ============================================================================

void LatentAudioVSTAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == "playbackRate")
        setPlaybackSpeed(newValue);
    else if (parameterID == "fineTune")
    {
        // Convert cents to pitch multiplier
        float semitones = newValue / 100.0f;
        float multiplier = std::pow(2.0f, semitones / 12.0f);
        setFineTune(multiplier);
    }
    else if (parameterID == "crossfade")
        setCrossfade(newValue);
    else if (parameterID == "filterCutoff")
        setFilterCutoff(newValue);
    else if (parameterID == "filterResonance")
        setFilterResonance(newValue);
    else if (parameterID == "attackTime")
        attackTime.store(newValue);
    else if (parameterID == "decayTime")
        decayTime.store(newValue);
    else if (parameterID == "sustainLevel")
        sustainLevel.store(newValue);
    else if (parameterID == "releaseTime")
        releaseTime.store(newValue);
    else if (parameterID == "morphSpeed")
        setMorphSpeed(newValue);
    else if (parameterID == "morphRate")
        setMorphRate(newValue);
    else if (parameterID == "morphDepthX")
        setMorphDepthX(newValue);
    else if (parameterID == "morphDepthY")
        setMorphDepthY(newValue);
    else if (parameterID == "outputGain")
        setGain(newValue); // Already linear, no need for dB conversion
    else if (parameterID == "mixAmount")
        setMix(newValue);
    else if (parameterID == "sampleLength")
        setSampleLength(newValue);
    else if (parameterID == "sampleStart")
        setSampleStart(newValue);
    else if (parameterID == "positionX")
        setPositionX(newValue);
    else if (parameterID == "positionY")
        setPositionY(newValue);

    else if (parameterID == "latchMode")
        setLatch(newValue > 0.5f); // Bool parameter
    else if (parameterID == "inferenceRate")
        ; // Handled in position setter methods
    else if (parameterID == "triggerOnMidi")
        setTriggerOnMidi(newValue > 0.5f); // Bool parameter
    else if (parameterID == "snapMode")
        ; // Handled in editor
}

//==============================================================================
const juce::String LatentAudioVSTAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool LatentAudioVSTAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool LatentAudioVSTAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool LatentAudioVSTAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double LatentAudioVSTAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int LatentAudioVSTAudioProcessor::getNumPrograms()
{
    return 1;
}

int LatentAudioVSTAudioProcessor::getCurrentProgram()
{
    return 0;
}

void LatentAudioVSTAudioProcessor::setCurrentProgram(int index)
{
}

const juce::String LatentAudioVSTAudioProcessor::getProgramName(int index)
{
    return {};
}

void LatentAudioVSTAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

//==============================================================================
void LatentAudioVSTAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::Logger::writeToLog("[prepareToPlay] === START ===");
    juce::Logger::writeToLog("[prepareToPlay] this=" + juce::String::toHexString((juce::int64)this));
    juce::Logger::writeToLog("[prepareToPlay] isNonRealtime=" + juce::String(isNonRealtime() ? "TRUE (EXPORT MODE)" : "FALSE (REALTIME)"));
    juce::Logger::writeToLog("[prepareToPlay] Sample rate: " + juce::String(sampleRate));
    juce::Logger::writeToLog("[prepareToPlay] Buffer size: " + juce::String(samplesPerBlock));
    juce::Logger::writeToLog("[prepareToPlay] decoder.isModelLoaded=" + juce::String(decoder.isModelLoaded() ? "TRUE" : "FALSE"));
    juce::Logger::writeToLog("[prepareToPlay] soundMap.getLatentDim=" + juce::String(soundMap.getLatentDim()));
    juce::Logger::writeToLog("[prepareToPlay] hasValidAudio=" + juce::String(hasValidAudio.load() ? "TRUE" : "FALSE"));
    juce::Logger::writeToLog("[prepareToPlay] LatentDecoder instance count: " + juce::String(LatentDecoder::getInstanceCount()));

    currentSampleRate = sampleRate;

    // Detect offline (export) mode for synchronous inference
    isOfflineMode.store(isNonRealtime());
    lastOfflineX = -999.0f;  // Reset offline position tracking
    lastOfflineY = -999.0f;

    // Pre-allocate buffers for synchronous inference in offline mode
    if (isOfflineMode.load())
    {
        int latentDim = soundMap.getLatentDim();
        if (latentDim > 0)
        {
            offlineLatentBuffer.resize(latentDim);
            offlineAudioBuffer.reserve(44100);  // Reserve for typical audio length
        }
        juce::Logger::writeToLog("[prepareToPlay] Offline mode enabled - synchronous inference active");
    }

    // Mark for potential auto-generation on first process
    isFirstProcess.store(true);

    // Initialize buffers if we don't have valid audio
    if (!hasValidAudio.load())
    {
        juce::ScopedLock sl(bufferLock);
        soundBuffers[0].setSize(2, 0, false, false, false);
        soundBuffers[1].setSize(2, 0, false, false, false);
        soundBuffers[0].clear();
        soundBuffers[1].clear();
    }

    // Reset filters for new sample rate
    {
        const juce::ScopedLock sl(filterLock);
        lpFilter1.reset();
        lpFilter2.reset();
        lastCutoff = -1.0f;
        lastReso = -1.0f;
    }

    juce::Logger::writeToLog("[prepareToPlay] === END ===");
}

//==============================================================================
void LatentAudioVSTAudioProcessor::releaseResources()
{
    juce::Logger::writeToLog("[releaseResources] === CALLED ===");
    juce::Logger::writeToLog("[releaseResources] this=" + juce::String::toHexString((juce::int64)this));
    juce::Logger::writeToLog("[releaseResources] hasValidAudio=" + juce::String(hasValidAudio.load() ? "TRUE" : "FALSE"));
    juce::Logger::writeToLog("[releaseResources] isNonRealtime=" + juce::String(isNonRealtime() ? "TRUE (EXPORT MODE)" : "FALSE (REALTIME)"));
    juce::Logger::writeToLog("[releaseResources] LatentDecoder instance count: " + juce::String(LatentDecoder::getInstanceCount()));

    // Don't clear buffers on releaseResources - they may be needed for state persistence
    // The buffers will be properly cleared when loading a new model or on destruction
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool LatentAudioVSTAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

//==============================================================================
void LatentAudioVSTAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Add call counter for periodic logging (every 1000 calls)
    static std::atomic<int> processBlockCounter{0};
    int callCount = processBlockCounter.fetch_add(1);

    /* LOGGING DISABLED FOR PERFORMANCE
    if (callCount == 0 || callCount % 1000 == 0)
    {
        juce::Logger::writeToLog("[processBlock] Call #" + juce::String(callCount));
        juce::Logger::writeToLog("[processBlock] this=" + juce::String::toHexString((juce::int64)this));
        juce::Logger::writeToLog("[processBlock] hasValidAudio=" + juce::String(hasValidAudio.load() ? "TRUE" : "FALSE"));
        juce::Logger::writeToLog("[processBlock] playbackPosition=" + juce::String(playbackPosition.load()));
        juce::Logger::writeToLog("[processBlock] activeBufferIndex=" + juce::String(activeBufferIndex.load()));
        juce::Logger::writeToLog("[processBlock] isNonRealtime=" + juce::String(isNonRealtime() ? "TRUE (EXPORT MODE)" : "FALSE (REALTIME)"));
        juce::Logger::writeToLog("[processBlock] decoder.isModelLoaded()=" + juce::String(decoder.isModelLoaded() ? "TRUE" : "FALSE"));
        juce::Logger::writeToLog("[processBlock] soundMap.getLatentDim()=" + juce::String(soundMap.getLatentDim()));
    }
    */

    // Clear output buffer
    for (int ch = 0; ch < totalNumOutputChannels; ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    // OFFLINE MODE: Run synchronous inference when position changes significantly
    if (isOfflineMode.load() && decoder.isModelLoaded() && soundMap.getLatentDim() > 0)
    {
        // Skip if crossfade is already in progress to prevent crackles
        if (isCrossfading)
        {
            // Update tracking but don't trigger new inference
            lastOfflineX = currentX.load();
            lastOfflineY = currentY.load();
        }
        else
        {
            float x = currentX.load();
            float y = currentY.load();

            // Always trigger on position changes (no threshold) - matches real-time behavior
            // Also trigger on first call (lastOfflineX/Y initialized to -999)
            bool isFirstCall = (lastOfflineX < -900.0f);
            bool positionChanged = (x != lastOfflineX || y != lastOfflineY);

            if (isFirstCall || positionChanged)
            {
                processOfflinePositionChange(x, y);
            }
        }
    }

    // COMMON PLAYBACK CODE variables
    int currentPos = playbackPosition.load();
    double sampleRate = getSampleRate();

    // MAIN PROCESSING
    {

        // First-process auto-generation
        if (isFirstProcess.load())
        {
            isFirstProcess.store(false);

            if (!hasValidAudio.load() && decoder.isModelLoaded() && soundMap.getLatentDim() > 0)
            {
                juce::Logger::writeToLog("[processBlock] Auto-generating audio for playback");

                float x = currentX.load();
                float y = currentY.load();

                if (x == 0.0f && y == 0.0f && !soundMap.getPoints().empty())
                {
                    const auto& firstPoint = soundMap.getPoints()[0];
                    x = firstPoint.x;
                    y = firstPoint.y;
                    currentX.store(x);
                    currentY.store(y);
                }

                triggerInference(x, y);

                int maxWaitMs = 5000;
                int waitedMs = 0;
                while (!hasValidAudio.load() && waitedMs < maxWaitMs)
                {
                    juce::Thread::sleep(50);
                    waitedMs += 50;
                }

                if (hasValidAudio.load())
                {
                    playbackPosition.store(0);
                    fractionalPosition = 0.0f;
                    envState = EnvelopeState::Attack;
                }
            }
        }

        // MIDI handling
        if (triggerOnMidi.load())
        {
            midiActivityCounter += buffer.getNumSamples();

            if (midiActivityCounter > 2205)
            {
                midiActive.store(false);
                midiActivityCounter = 0;
            }

            for (const auto metadata : midiMessages)
            {
                if (metadata.getMessage().isNoteOn())
                {
                    midiActive.store(true);
                    midiActivityCounter = 0;

                    if (hasValidAudio.load())
                    {
                        playbackPosition.store(0);
                        fractionalPosition = 0.0f;
                        envState = EnvelopeState::Attack;
                    }
                    else
                    {
                        triggerInference(currentX.load(), currentY.load());
                    }
                }
                else if (metadata.getMessage().isNoteOff())
                {
                    midiActive.store(true);
                    midiActivityCounter = 0;
                    releaseNote();
                }
            }
        }

        // Morph handling
        float morphSpd = morphSpeed.load();
        bool shouldMorph = (morphSpd > 0.0f && currentPos >= 0 && envState != EnvelopeState::Idle);

        if (shouldMorph)
        {
            if (!wasMorphing)
            {
                morphAnchorX = currentX.load();
                morphAnchorY = currentY.load();
                morphPhase = 0.0f;
                wasMorphing = true;
                lastMorphX = morphAnchorX;
                lastMorphY = morphAnchorY;
                samplesUntilUpdate = 0.0f; // FIXED: Initialize properly
            }

            float phaseIncrement = morphSpd * 0.1f * buffer.getNumSamples() / (float)sampleRate;
            morphPhase += phaseIncrement;

            float minX = soundMap.getMinX();
            float maxX = soundMap.getMaxX();
            float minY = soundMap.getMinY();
            float maxY = soundMap.getMaxY();

            float radiusX = (maxX - minX) * 0.3f * morphDepthX.load();
            float radiusY = (maxY - minY) * 0.3f * morphDepthY.load();

            float offsetX = std::sin(morphPhase) * radiusX;
            float offsetY = std::sin(morphPhase * 0.7f) * radiusY;

            float newX = juce::jlimit(minX, maxX, morphAnchorX + offsetX);
            float newY = juce::jlimit(minY, maxY, morphAnchorY + offsetY);

            currentX.store(newX);
            currentY.store(newY);

            float samplesPerUpdate = (float)sampleRate / morphRateHz.load();

            samplesUntilUpdate -= (float)buffer.getNumSamples();

            // FIXED: Prevent negative accumulation
            if (samplesUntilUpdate <= 0.0f)
            {
                // Reset with proper wraparound
                while (samplesUntilUpdate <= 0.0f)
                    samplesUntilUpdate += samplesPerUpdate;

                float dx = newX - lastMorphX;
                float dy = newY - lastMorphY;
                float distSq = dx * dx + dy * dy;

                float mapScale = (maxX - minX) * (maxY - minY);
                float threshold = mapScale * 0.00001f;

                if (distSq > threshold)
                {
                    lastMorphX = newX;
                    lastMorphY = newY;
                    triggerInference(newX, newY);
                }
            }
        }
        else
        {
            // FIXED: Properly reset morph state
            if (wasMorphing)
            {
                wasMorphing = false;
                samplesUntilUpdate = 0.0f;
                morphPhase = 0.0f;
            }
        }

        // BACKGROUND INFERENCE: inferenceRate controls timing when morphSpeed == 0
        if (morphSpeed.load() == 0.0f && decoder.isModelLoaded() && soundMap.getLatentDim() > 0)
        {
            float rate = inferenceRate.load();

            if (rate == 0.0f)
            {
                // Immediate background inference: trigger on position change
                if (backgroundPositionChanged)
                {
                    triggerInference(currentX.load(), currentY.load());
                    backgroundPositionChanged = false;
                }
            }
            else
            {
                // Rate-limited background inference: use counter + position change check
                paramUpdateCounter -= (float)buffer.getNumSamples();
                if (paramUpdateCounter <= 0.0f && backgroundPositionChanged)
                {
                    triggerInference(currentX.load(), currentY.load());
                    paramUpdateCounter = (float)getSampleRate() / rate; // Reset counter
                    backgroundPositionChanged = false;
                }
                else if (paramUpdateCounter < 0.0f)
                {
                    // Reset counter even if no position change
                    paramUpdateCounter = (float)getSampleRate() / rate;
                }
            }
        }
    }

    // PLAYBACK CODE
    if (currentPos < 0)
        return;

    if (!hasValidAudio.load())
    {
        playbackPosition.store(-1);
        envState = EnvelopeState::Idle;
        return;
    }

    float baseSpeed = playbackSpeed.load();
    float tuneMultiplier = fineTuneMultiplier.load();
    float speed = baseSpeed * tuneMultiplier;

    float xfadeFactor = crossfade.load();
    float gainVal = outputGain.load();
    float mixVal = mixAmount.load();

    float atk = attackTime.load();
    float dec = decayTime.load();
    float sus = sustainLevel.load();
    float rel = releaseTime.load();

    if (atk < 0.001f) atk = 0.001f;
    if (dec < 0.001f) dec = 0.001f;
    if (rel < 0.001f) rel = 0.001f;

    float attackInc = 1.0f / (atk * (float)sampleRate);
    float decayDec = 1.0f / (dec * (float)sampleRate);
    float releaseDec = 1.0f / (rel * (float)sampleRate);

    if (currentPos >= 0)
    {
        if (triggerCrossfade.load())
        {
            isCrossfading = true;
            crossfadePosition = 0.0f;
            float crossfadeSamples = (float)getSampleRate() * 0.05f;
            if (crossfadeSamples < 1.0f) crossfadeSamples = 1.0f;
            crossfadeIncrement = 1.0f / crossfadeSamples;
            triggerCrossfade.store(false);
        }

        const juce::ScopedLock sl(bufferLock);

        int activeIndex = activeBufferIndex.load();
        int nextIndex = 1 - activeIndex;

        const auto* activeBuf = &soundBuffers[activeIndex];
        const auto* nextBuf = &soundBuffers[nextIndex];

        int totalGenerated = activeBuf->getNumSamples();

        if (isCrossfading && nextBuf->getNumSamples() == 0)
            isCrossfading = false;

        if (totalGenerated == 0) return;

        float startPos = sampleStart.load();
        int loopStartSample = (int)(startPos * (float)totalGenerated);
        int loopEndSample = juce::jmin(totalGenerated, (int)(sampleLength.load() * (float)totalGenerated));

        auto* leftOut = buffer.getWritePointer(0);
        auto* rightOut = buffer.getWritePointer(1);

        const auto* activeL = activeBuf->getReadPointer(0);
        const auto* activeR = activeBuf->getReadPointer(1);

        const float* nextL = nullptr;
        const float* nextR = nullptr;

        if (isCrossfading)
        {
            nextL = nextBuf->getReadPointer(0);
            nextR = nextBuf->getReadPointer(1);
        }

        int outputSamples = buffer.getNumSamples();

        for (int i = 0; i < outputSamples; ++i)
        {
            if (currentPos < 0) break;

            if (fractionalPosition >= loopEndSample)
            {
                if (isLatch.load())
                {
                    fractionalPosition = (float)loopStartSample;
                }
                else
                {
                    envState = EnvelopeState::Release;
                    fractionalPosition = loopEndSample - 1.0f;
                }
            }
            if (fractionalPosition < loopStartSample)
                fractionalPosition = (float)loopStartSample;

            int idxA = (int)fractionalPosition;
            int idxB = idxA + 1;
            float alpha = fractionalPosition - (float)idxA;

            idxB = juce::jmin(idxB, (int)loopEndSample - 1);

            float lA = activeL[idxA];
            float lB = activeL[idxB];
            float sigL = lA + alpha * (lB - lA);

            float rA = activeR[idxA];
            float rB = activeR[idxB];
            float sigR = rA + alpha * (rB - rA);

            if (xfadeFactor > 0.001f)
            {
                float loopDuration = (float)(loopEndSample - loopStartSample);
                float relPos = fractionalPosition - loopStartSample;
                float timeToEnd = loopDuration - relPos;
                float xfadeLen = loopDuration * xfadeFactor;

                if (timeToEnd < xfadeLen)
                {
                    float distFromEnd = xfadeLen - timeToEnd;
                    float wrapPos = loopStartSample + distFromEnd;

                    int wIdxA = (int)wrapPos;
                    int wIdxB = wIdxA + 1;
                    float wAlpha = wrapPos - (float)wIdxA;

                    wIdxB = juce::jmin(wIdxB, totalGenerated - 1);

                    float wLA = activeL[wIdxA];
                    float wLB = activeL[wIdxB];
                    float wSigL = wLA + wAlpha * (wLB - wLA);

                    float wRA = activeR[wIdxA];
                    float wRB = activeR[wIdxB];
                    float wSigR = wRA + wAlpha * (wRB - wRA);

                    float mix = distFromEnd / xfadeLen;
                    sigL = sigL * (1.0f - mix) + wSigL * mix;
                    sigR = sigR * (1.0f - mix) + wSigR * mix;
                }
            }

            if (isCrossfading)
            {
                int nTotal = nextBuf->getNumSamples();
                int nIdxA = idxA % nTotal;
                int nIdxB = idxB % nTotal;

                float nLA = nextL[nIdxA];
                float nLB = nextL[nIdxB];
                float nextSigL = nLA + alpha * (nLB - nLA);

                float nRA = nextR[nIdxA];
                float nRB = nextR[nIdxB];
                float nextSigR = nRA + alpha * (nRB - nRA);

                sigL = sigL * (1.0f - crossfadePosition) + nextSigL * crossfadePosition;
                sigR = sigR * (1.0f - crossfadePosition) + nextSigR * crossfadePosition;

                crossfadePosition += crossfadeIncrement;
                if (crossfadePosition >= 1.0f)
                {
                    isCrossfading = false;
                    activeBufferIndex.store(nextIndex);
                    activeIndex = nextIndex;
                    nextIndex = 1 - activeIndex;
                    activeBuf = &soundBuffers[activeIndex];
                    activeL = activeBuf->getReadPointer(0);
                    activeR = activeBuf->getReadPointer(1);
                }
            }

            switch (envState)
            {
            case EnvelopeState::Idle:
                currentEnvLevel = 0.0f;
                break;

            case EnvelopeState::Attack:
                currentEnvLevel += attackInc;
                if (currentEnvLevel >= 1.0f)
                {
                    currentEnvLevel = 1.0f;
                    envState = EnvelopeState::Decay;
                }
                break;

            case EnvelopeState::Decay:
                currentEnvLevel -= decayDec;
                if (currentEnvLevel <= sus)
                {
                    currentEnvLevel = sus;
                    envState = EnvelopeState::Sustain;
                }
                break;

            case EnvelopeState::Sustain:
                currentEnvLevel = sus;
                break;

            case EnvelopeState::Release:
                currentEnvLevel -= releaseDec;
                if (currentEnvLevel <= 0.0f)
                {
                    currentEnvLevel = 0.0f;
                    envState = EnvelopeState::Idle;
                    playbackPosition.store(-1);
                }
                break;
            }

            float finalGain = currentEnvLevel * gainVal;
            float wetL = sigL * finalGain;
            float wetR = sigR * finalGain;

            leftOut[i] += wetL * mixVal;
            rightOut[i] += wetR * mixVal;

            fractionalPosition += speed;

            if (envState == EnvelopeState::Idle) break;
        }

        if (envState != EnvelopeState::Idle)
            playbackPosition.store((int)fractionalPosition);
    }
}

//==============================================================================
void LatentAudioVSTAudioProcessor::setPlaybackSpeed(float newSpeed)
{
    playbackSpeed.store(newSpeed);
}

void LatentAudioVSTAudioProcessor::setFineTune(float multiplier)
{
    fineTuneMultiplier.store(multiplier);
}

void LatentAudioVSTAudioProcessor::setCrossfade(float amount)
{
    crossfade.store(amount);
}

void LatentAudioVSTAudioProcessor::setLatch(bool shouldLatch)
{
    isLatch.store(shouldLatch);
}

void LatentAudioVSTAudioProcessor::setEnvelope(float attack, float decay, float sustain, float release)
{
    attackTime.store(attack);
    decayTime.store(decay);
    sustainLevel.store(sustain);
    releaseTime.store(release);
}

void LatentAudioVSTAudioProcessor::setGain(float newGain)
{
    outputGain.store(newGain);
}

void LatentAudioVSTAudioProcessor::setMix(float wetAmount)
{
    mixAmount.store(juce::jlimit(0.0f, 1.0f, wetAmount));
}

void LatentAudioVSTAudioProcessor::setFilterCutoff(float cutoffHz)
{
    float oldCutoff = lpCutoff.load();
    lpCutoff.store(cutoffHz);

    // Re-trigger inference if value changed significantly and we have valid audio
    if (hasValidAudio.load() && std::abs(cutoffHz - oldCutoff) > 1.0f)
    {
        triggerInference(currentX.load(), currentY.load());
    }
}

void LatentAudioVSTAudioProcessor::setFilterResonance(float reso)
{
    float newReso = juce::jlimit(0.0f, 1.0f, reso);
    float oldReso = lpReso.load();
    lpReso.store(newReso);

    // Re-trigger inference if value changed significantly and we have valid audio
    if (hasValidAudio.load() && std::abs(newReso - oldReso) > 0.01f)
    {
        triggerInference(currentX.load(), currentY.load());
    }
}

void LatentAudioVSTAudioProcessor::setMorphSpeed(float speed)
{
    morphSpeed.store(speed);
}

void LatentAudioVSTAudioProcessor::setMorphDepthX(float depth)
{
    morphDepthX.store(juce::jlimit(0.0f, 1.0f, depth));
}

void LatentAudioVSTAudioProcessor::setMorphDepthY(float depth)
{
    morphDepthY.store(juce::jlimit(0.0f, 1.0f, depth));
}

void LatentAudioVSTAudioProcessor::setMorphRate(float hz)
{
    morphRateHz.store(juce::jlimit(0.1f, 20.0f, hz));
}

void LatentAudioVSTAudioProcessor::setTriggerOnMidi(bool shouldTrigger)
{
    triggerOnMidi.store(shouldTrigger);
}

void LatentAudioVSTAudioProcessor::setSampleLength(float len)
{
    sampleLength.store(juce::jlimit(0.1f, 1.0f, len));
}

void LatentAudioVSTAudioProcessor::setSampleStart(float start)
{
    sampleStart.store(juce::jlimit(0.0f, 0.9f, start));
}

void LatentAudioVSTAudioProcessor::setPositionX(float normalizedX)
{
    float minX = soundMap.getMinX();
    float maxX = soundMap.getMaxX();
    float actualX = minX + normalizedX * (maxX - minX);

    // Offline mode: just update position, let processBlock handle sync inference
    if (isOfflineMode.load())
    {
        currentX.store(actualX);
        return;
    }

    // Priority: morph takes precedence when active
    if (morphSpeed.load() > 0.0f)
    {
        // Morph active: update position but don't trigger inference
        currentX.store(actualX);
        // Update tracking for when morph becomes inactive
        lastParamX = actualX;
        lastParamY = currentY.load();
        return;
    }

    // Check if position changed for background inference tracking
    if (std::abs(actualX - lastBackgroundX) > 0.001f ||
        std::abs(currentY.load() - lastBackgroundY) > 0.001f)
    {
        backgroundPositionChanged = true;
        lastBackgroundX = actualX;
        lastBackgroundY = currentY.load();
    }

    // Morph inactive: trigger inference immediately
    triggerInference(actualX, currentY.load());
}

void LatentAudioVSTAudioProcessor::setPositionY(float normalizedY)
{
    float minY = soundMap.getMinY();
    float maxY = soundMap.getMaxY();
    float actualY = minY + normalizedY * (maxY - minY);

    // Offline mode: just update position, let processBlock handle sync inference
    if (isOfflineMode.load())
    {
        currentY.store(actualY);
        return;
    }

    // Priority: morph takes precedence when active
    if (morphSpeed.load() > 0.0f)
    {
        // Morph active: update position but don't trigger inference
        currentY.store(actualY);
        // Update tracking for when morph becomes inactive
        lastParamX = currentX.load();
        lastParamY = actualY;
        return;
    }

    // Check if position changed for background inference tracking
    if (std::abs(currentX.load() - lastBackgroundX) > 0.001f ||
        std::abs(actualY - lastBackgroundY) > 0.001f)
    {
        backgroundPositionChanged = true;
        lastBackgroundX = currentX.load();
        lastBackgroundY = actualY;
    }

    // Morph inactive: trigger inference immediately
    triggerInference(currentX.load(), actualY);
}

void LatentAudioVSTAudioProcessor::triggerRandomPosition()
{
    if (soundMap.getPoints().empty()) return;

    juce::Random rand;
    float minX = soundMap.getMinX();
    float maxX = soundMap.getMaxX();
    float minY = soundMap.getMinY();
    float maxY = soundMap.getMaxY();

    float randomX = minX + rand.nextFloat() * (maxX - minX);
    float randomY = minY + rand.nextFloat() * (maxY - minY);

    triggerInference(randomX, randomY);
}

//==============================================================================
void LatentAudioVSTAudioProcessor::triggerInference(float x, float y)
{
    // juce::Logger::writeToLog("[triggerInference] Called with x=" + juce::String(x) + ", y=" + juce::String(y));
    // juce::Logger::writeToLog("[triggerInference] soundMap.getLatentDim()=" + juce::String(soundMap.getLatentDim()));
    // juce::Logger::writeToLog("[triggerInference] decoder.isModelLoaded()=" + juce::String(decoder.isModelLoaded() ? "TRUE" : "FALSE"));

    if (soundMap.getLatentDim() == 0)
    {
        // juce::Logger::writeToLog("[triggerInference] FAIL: soundMap latentDim is 0");
        return;
    }

    // Update active position tracking
    currentX.store(x);
    currentY.store(y);

    if (playbackPosition.load() < 0)
    {
        playbackPosition.store(0);
        fractionalPosition = 0.0f;
    }

    if (envState == EnvelopeState::Idle || envState == EnvelopeState::Release)
    {
        envState = EnvelopeState::Attack;
    }

    auto vector = soundMap.getInterpolatedVector(x, y);
    // juce::Logger::writeToLog("[triggerInference] interpolated vector size=" + juce::String(vector.size()));
    inferenceThread.trigger(vector);
}

void LatentAudioVSTAudioProcessor::releaseNote()
{
    if (isLatch.load()) return;

    if (envState != EnvelopeState::Idle)
    {
        envState = EnvelopeState::Release;
    }
}

void LatentAudioVSTAudioProcessor::processOfflinePositionChange(float x, float y)
{
    // Synchronous inference for offline/export mode
    // This blocks until inference is complete - necessary for correct export

    if (soundMap.getLatentDim() == 0 || !decoder.isModelLoaded())
        return;

    // juce::Logger::writeToLog("[processOfflinePositionChange] Sync inference at (" + juce::String(x) + ", " + juce::String(y) + ")");

    // Get interpolated latent vector
    auto latentVector = soundMap.getInterpolatedVector(x, y);
    if (latentVector.empty())
        return;

    // Run synchronous decode (blocking call)
    std::vector<float> rawAudio;
    bool decodeSuccess = decoder.decode(latentVector, rawAudio);
    if (!decodeSuccess || rawAudio.empty())
    {
        // juce::Logger::writeToLog("[processOfflinePositionChange] Decode returned empty audio");
        return;
    }

    // Validate audio data
    bool hasInvalidData = false;
    float maxAmp = 0.0f;
    for (float sample : rawAudio)
    {
        if (!std::isfinite(sample))
        {
            hasInvalidData = true;
            break;
        }
        maxAmp = std::max(maxAmp, std::abs(sample));
    }

    if (hasInvalidData)
    {
        // juce::Logger::writeToLog("[processOfflinePositionChange] Invalid data (NaN/Inf) - aborting");
        return;
    }

    // FILTER RESET REMOVED TO PREVENT CRACKLES
    /*
    {
        const juce::ScopedLock sl(filterLock);
        lpFilter1.reset();
        lpFilter2.reset();
    }
    */

    // Apply post-processing
    std::vector<float> processedAudio = rawAudio;
    applyPostProcessing(processedAudio);

    // Copy to back buffer and trigger crossfade (same as realtime mode)
    {
        juce::ScopedLock sl(bufferLock);

        int backIndex = 1 - activeBufferIndex.load();
        auto& backBuffer = soundBuffers[backIndex];

        if (backBuffer.getNumSamples() != (int)processedAudio.size())
            backBuffer.setSize(2, (int)processedAudio.size(), false, false, false);

        backBuffer.clear();

        for (int ch = 0; ch < backBuffer.getNumChannels(); ++ch)
            juce::FloatVectorOperations::copy(backBuffer.getWritePointer(ch),
                processedAudio.data(),
                (int)processedAudio.size());

        triggerCrossfade.store(true);
    }

    // Only start playback if not already playing (match realtime behavior)
    if (playbackPosition.load() < 0)
    {
        playbackPosition.store(0);
        fractionalPosition = 0.0f;

        // Swap to back buffer immediately for first playback
        int backIndex = 1 - activeBufferIndex.load();
        activeBufferIndex.store(backIndex);
        triggerCrossfade.store(false);

        hasValidAudio.store(true);

        if (envState == EnvelopeState::Idle || envState == EnvelopeState::Release)
            envState = EnvelopeState::Attack;
    }
    else
    {
        hasValidAudio.store(true);
    }

    // Update tracking
    lastOfflineX = x;
    lastOfflineY = y;

    juce::Logger::writeToLog("[processOfflinePositionChange] Sync inference complete, samples=" +
        juce::String((int)processedAudio.size()));
}

float LatentAudioVSTAudioProcessor::getPlaybackPositionNormalized() const
{
    int pos = playbackPosition.load();
    int idx = activeBufferIndex.load();
    int len = soundBuffers[idx].getNumSamples();

    if (pos < 0 || len == 0)
        return -1.0f;

    return (float)pos / (float)len;
}

//==============================================================================


//==============================================================================
bool LatentAudioVSTAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* LatentAudioVSTAudioProcessor::createEditor()
{
    return new LatentAudioVSTAudioProcessorEditor(*this);
}

//==============================================================================
// ============================================================================
// SIMPLIFIED: State Management (APVTS handles this automatically!)
// ============================================================================

void LatentAudioVSTAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::Logger::writeToLog("[getStateInformation] Saving state...");

    // APVTS handles parameter state automatically
    auto state = parameters->copyState();

    // Add custom data (model paths, cursor position)
    juce::String modelPathStr = modelPath.getFullPathName();
    juce::String mapPathStr = soundMapPath.getFullPathName();

    juce::Logger::writeToLog("[getStateInformation] Saving modelPath: " + modelPathStr);
    juce::Logger::writeToLog("[getStateInformation] Saving soundMapPath: " + mapPathStr);
    juce::Logger::writeToLog("[getStateInformation] Saving cursor: (" + juce::String(currentX.load()) + ", " + juce::String(currentY.load()) + ")");

    state.setProperty("modelPath", modelPathStr, nullptr);
    state.setProperty("soundMapPath", mapPathStr, nullptr);
    state.setProperty("currentX", currentX.load(), nullptr);
    state.setProperty("currentY", currentY.load(), nullptr);

    // Store audio data as Base64 in the XML (no custom binary framing)
    if (hasValidAudio.load())
    {
        juce::Logger::writeToLog("[getStateInformation] Encoding audio buffers as Base64...");

        state.setProperty("hasValidAudio", true, nullptr);
        state.setProperty("activeBufferIndex", activeBufferIndex.load(), nullptr);
        state.setProperty("playbackPosition", playbackPosition.load(), nullptr);
        state.setProperty("envState", static_cast<int>(envState), nullptr);

        juce::ScopedLock sl(bufferLock);
        for (int bufIdx = 0; bufIdx < 2; ++bufIdx)
        {
            const auto& buffer = soundBuffers[bufIdx];
            int numChannels = buffer.getNumChannels();
            int numSamples = buffer.getNumSamples();

            state.setProperty("buffer" + juce::String(bufIdx) + "_channels", numChannels, nullptr);
            state.setProperty("buffer" + juce::String(bufIdx) + "_samples", numSamples, nullptr);

            juce::Logger::writeToLog("[getStateInformation] Buffer " + juce::String(bufIdx) +
                ": channels=" + juce::String(numChannels) + ", samples=" + juce::String(numSamples));

            if (numChannels > 0 && numSamples > 0)
            {
                // Encode each channel as Base64
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float* data = buffer.getReadPointer(ch);
                    juce::MemoryBlock audioBlock(data, sizeof(float) * numSamples);
                    juce::String base64 = audioBlock.toBase64Encoding();
                    state.setProperty("buffer" + juce::String(bufIdx) + "_ch" + juce::String(ch), base64, nullptr);
                }
            }
        }

        juce::Logger::writeToLog("[getStateInformation] Audio data encoded as Base64");
    }
    else
    {
        state.setProperty("hasValidAudio", false, nullptr);
        juce::Logger::writeToLog("[getStateInformation] No audio to save");
    }

    // Use JUCE's standard copyXmlToBinary directly - no custom framing!
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);

    juce::Logger::writeToLog("[getStateInformation] State saved successfully using standard JUCE format, total size: " + juce::String(destData.getSize()));
}

void LatentAudioVSTAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::Logger::writeToLog("[setStateInformation] === LOADING STATE ===");
    juce::Logger::writeToLog("[setStateInformation] sizeInBytes: " + juce::String(sizeInBytes));

    if (sizeInBytes < 4)
    {
        juce::Logger::writeToLog("[setStateInformation] Data too small, aborting");
        return;
    }

    // Use JUCE's standard getXmlFromBinary directly on the raw DAW data
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState == nullptr)
    {
        juce::Logger::writeToLog("[setStateInformation] XML parse failed");
        return;
    }

    juce::Logger::writeToLog("[setStateInformation] XML parsed successfully using standard JUCE format");

    if (xmlState->hasTagName(parameters->state.getType()))
    {
        auto state = juce::ValueTree::fromXml(*xmlState);

        // Restore APVTS state (this updates all parameters)
        parameters->replaceState(state);

        // Restore custom data
        bool shouldLoadModel = false;

        if (state.hasProperty("modelPath"))
        {
            juce::String pathStr = state.getProperty("modelPath").toString();
            juce::Logger::writeToLog("[setStateInformation] Restored modelPath: " + pathStr);

            if (pathStr.isNotEmpty())
            {
                juce::File loadedModelPath(pathStr);
                if (loadedModelPath.existsAsFile())
                {
                    modelPath = loadedModelPath;
                    shouldLoadModel = true;
                    juce::Logger::writeToLog("[setStateInformation] Model file exists, will load");
                }
                else
                {
                    juce::Logger::writeToLog("[setStateInformation] WARNING: Model file does not exist!");
                }
            }
        }

        if (state.hasProperty("soundMapPath"))
        {
            juce::String pathStr = state.getProperty("soundMapPath").toString();
            juce::Logger::writeToLog("[setStateInformation] Restored soundMapPath: " + pathStr);

            if (pathStr.isNotEmpty())
            {
                juce::File loadedSoundMapPath(pathStr);
                if (loadedSoundMapPath.existsAsFile())
                {
                    soundMapPath = loadedSoundMapPath;
                    shouldLoadModel = true;
                    juce::Logger::writeToLog("[setStateInformation] Sound map file exists, will load");
                }
                else
                {
                    juce::Logger::writeToLog("[setStateInformation] WARNING: Sound map file does not exist!");
                }
            }
        }

        // Restore cursor position
        float restoredX = 0.0f;
        float restoredY = 0.0f;

        if (state.hasProperty("currentX"))
        {
            restoredX = state.getProperty("currentX");
            currentX.store(restoredX);
            juce::Logger::writeToLog("[setStateInformation] Restored currentX: " + juce::String(restoredX));
        }

        if (state.hasProperty("currentY"))
        {
            restoredY = state.getProperty("currentY");
            currentY.store(restoredY);
            juce::Logger::writeToLog("[setStateInformation] Restored currentY: " + juce::String(restoredY));
        }

        // Load model/map files if paths were found
        juce::Logger::writeToLog("[setStateInformation] shouldLoadModel: " + juce::String(shouldLoadModel ? "true" : "false"));
        if (shouldLoadModel)
        {
            juce::Logger::writeToLog("[setStateInformation] Loading model and map...");
            loadModelAndMap();
        }

        // Restore audio data from Base64 properties
        bool hasAudio = state.getProperty("hasValidAudio", false);
        juce::Logger::writeToLog("[setStateInformation] hasValidAudio property: " + juce::String(hasAudio ? "true" : "false"));

        if (hasAudio)
        {
            int activeIdx = state.getProperty("activeBufferIndex", 0);
            activeBufferIndex.store(activeIdx);

            int playPos = state.getProperty("playbackPosition", -1);
            playbackPosition.store(playPos);
            fractionalPosition = (float)playPos;

            int envInt = state.getProperty("envState", 0);
            envState = static_cast<EnvelopeState>(envInt);

            juce::ScopedLock sl(bufferLock);
            for (int bufIdx = 0; bufIdx < 2; ++bufIdx)
            {
                int numChannels = state.getProperty("buffer" + juce::String(bufIdx) + "_channels", 0);
                int numSamples = state.getProperty("buffer" + juce::String(bufIdx) + "_samples", 0);

                juce::Logger::writeToLog("[setStateInformation] Buffer " + juce::String(bufIdx) +
                    ": channels=" + juce::String(numChannels) + ", samples=" + juce::String(numSamples));

                if (numChannels > 0 && numSamples > 0)
                {
                    soundBuffers[bufIdx].setSize(numChannels, numSamples, false, false, false);

                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        juce::String base64 = state.getProperty("buffer" + juce::String(bufIdx) + "_ch" + juce::String(ch), "").toString();
                        if (base64.isNotEmpty())
                        {
                            juce::MemoryBlock audioBlock;
                            if (audioBlock.fromBase64Encoding(base64))
                            {
                                float* writePtr = soundBuffers[bufIdx].getWritePointer(ch);
                                std::memcpy(writePtr, audioBlock.getData(), sizeof(float) * numSamples);
                            }
                        }
                    }
                }
                else
                {
                    soundBuffers[bufIdx].setSize(2, 0, false, false, false);
                }
            }

            hasValidAudio.store(true);
            juce::Logger::writeToLog("[setStateInformation] Audio data decoded from Base64, active buffer: " + juce::String(activeIdx));
        }
        else
        {
            hasValidAudio.store(false);
            juce::Logger::writeToLog("[setStateInformation] No audio data to load");
        }

        // Update UI on message thread
        juce::MessageManager::callAsync([this, restoredX, restoredY]() {
            if (auto* editor = dynamic_cast<LatentAudioVSTAudioProcessorEditor*>(getActiveEditor()))
            {
                juce::Logger::writeToLog("[setStateInformation] Updating UI from processor");
                editor->updateUIFromProcessor();

                // CRITICAL: Update nebula cursor position
                juce::Logger::writeToLog("[setStateInformation] Setting nebula cursor to (" + juce::String(restoredX) + ", " + juce::String(restoredY) + ")");
                editor->nebulaComp.setCursorPosition(restoredX, restoredY);
            }
        });

        juce::Logger::writeToLog("[setStateInformation] === STATE LOADED ===");
    }
}

//==============================================================================
//==============================================================================
// FIX #3: UPDATE applyPostProcessing with filter protection
void LatentAudioVSTAudioProcessor::applyPostProcessing(std::vector<float>& audio)
{
    if (audio.empty()) return;

    // Use actual sample rate
    double sampleRate = currentSampleRate;

    float cutoff = juce::jlimit(20.0f, 20000.0f, lpCutoff.load());
    float reso = lpReso.load();

    // Thread-safe filter update
    {
        const juce::ScopedLock sl(filterLock);

        // Always recalculate coefficients for new audio (filters were just reset)
        double Q = 0.707 + reso * 10.0;
        currentCoeffs = juce::IIRCoefficients::makeLowPass(sampleRate, (double)cutoff, Q);

        lpFilter1.setCoefficients(currentCoeffs);
        lpFilter2.setCoefficients(currentCoeffs);

        lastCutoff = cutoff;
        lastReso = reso;

        // Apply filters
        lpFilter1.processSamples(audio.data(), (int)audio.size());
        lpFilter2.processSamples(audio.data(), (int)audio.size());
    }

    // Rest of post-processing (tail removal, normalization)
    const float threshold = std::pow(10.0f, -60.0f / 20.0f);
    int lastHotIndex = -1;
    int n = (int)audio.size();

    for (int i = 0; i < n; ++i)
    {
        if (std::abs(audio[i]) > threshold)
            lastHotIndex = i;
    }

    if (lastHotIndex >= 0)
    {
        if (lastHotIndex < n - 882)
        {
            int fadeStart = lastHotIndex + 10;
            int fadeLen = 512;
            int fadeEnd = std::min(fadeStart + fadeLen, n);

            if (fadeStart < n)
            {
                for (int i = fadeStart; i < fadeEnd; ++i)
                {
                    float alpha = 1.0f - (float)(i - fadeStart) / (float)fadeLen;
                    audio[i] *= alpha;
                }
                if (fadeEnd < n)
                {
                    std::fill(audio.begin() + fadeEnd, audio.end(), 0.0f);
                }
            }
        }
    }
    else
    {
        std::fill(audio.begin(), audio.end(), 0.0f);
    }

    if (n > 4 && audio[n - 1] != 0.0f)
    {
        audio[n - 4] *= 0.75f;
        audio[n - 3] *= 0.50f;
        audio[n - 2] *= 0.25f;
        audio[n - 1] *= 0.00f;
    }

    float maxAmp = 0.0f;
    for (float s : audio) maxAmp = std::max(maxAmp, std::abs(s));

    if (maxAmp > 1.0f)
    {
        float scale = 1.0f / maxAmp;
        for (float& s : audio) s *= scale;
    }
}

//==============================================================================
void LatentAudioVSTAudioProcessor::setModelPath(const juce::File& path)
{
    modelPath = path;
}

void LatentAudioVSTAudioProcessor::setSoundMapPath(const juce::File& path)
{
    soundMapPath = path;
}

void LatentAudioVSTAudioProcessor::loadModelAndMap()
{
    juce::Logger::writeToLog("[loadModelAndMap] === START ===");
    juce::Logger::writeToLog("[loadModelAndMap] soundMapPath: " + soundMapPath.getFullPathName());
    juce::Logger::writeToLog("[loadModelAndMap] modelPath: " + modelPath.getFullPathName());

    if (soundMapPath.existsAsFile())
    {
        juce::Logger::writeToLog("[loadModelAndMap] Loading sound map...");
        if (soundMap.loadFromFile(soundMapPath))
        {
            // Sync latent dimension to decoder
            decoder.setLatentDim(soundMap.getLatentDim());
            juce::Logger::writeToLog("[loadModelAndMap] SoundMap loaded, latentDim=" + juce::String(soundMap.getLatentDim()) + ", points=" + juce::String(soundMap.getPoints().size()));

            // Initialize parameter throttling positions to current position
            lastParamX = currentX.load();
            lastParamY = currentY.load();

            // Sync position parameters with current crosshair position (now that bounds are available)
            float minX = soundMap.getMinX();
            float maxX = soundMap.getMaxX();
            float minY = soundMap.getMinY();
            float maxY = soundMap.getMaxY();

            // Convert current position to normalized coordinates for parameters
            float normalizedX = juce::jlimit(0.0f, 1.0f, (currentX.load() - minX) / (maxX - minX));
            float normalizedY = juce::jlimit(0.0f, 1.0f, (currentY.load() - minY) / (maxY - minY));

            // Update APVTS parameters (silently to avoid callbacks during load)
            parameters->getParameter("positionX")->setValue(normalizedX);
            parameters->getParameter("positionY")->setValue(normalizedY);

            juce::Logger::writeToLog("[loadModelAndMap] Synced position parameters with crosshair: X=" + juce::String(normalizedX) + ", Y=" + juce::String(normalizedY));
        }
        else
        {
            juce::Logger::writeToLog("[loadModelAndMap] SoundMap failed to load!");
        }
    }
    else
    {
        juce::Logger::writeToLog("[loadModelAndMap] soundMapPath does not exist!");
    }

    if (modelPath.existsAsFile())
    {
        juce::Logger::writeToLog("[loadModelAndMap] Loading model...");
        bool modelResult = decoder.loadModel(modelPath);
        juce::Logger::writeToLog("[loadModelAndMap] decoder.loadModel() returned: " + juce::String(modelResult ? "TRUE" : "FALSE"));
        juce::Logger::writeToLog("[loadModelAndMap] decoder.isModelLoaded() = " + juce::String(decoder.isModelLoaded() ? "TRUE" : "FALSE"));
    }
    else
    {
        juce::Logger::writeToLog("[loadModelAndMap] modelPath does not exist!");
    }

    juce::Logger::writeToLog("[loadModelAndMap] === END ===");
}

//==============================================================================
// FIX #4: UPDATE loadModelFolder to completely reset audio state
bool LatentAudioVSTAudioProcessor::loadModelFolder(const juce::File& folder)
{
    juce::Logger::writeToLog("[loadModelFolder] === START ===");

    if (!folder.isDirectory())
        return false;

    juce::Array<juce::File> modelFiles, jsonFiles;
    folder.findChildFiles(modelFiles, juce::File::findFiles, false, "*.onnx");
    folder.findChildFiles(jsonFiles, juce::File::findFiles, false, "*.json");

    // CRITICAL: Complete audio shutdown
    playbackPosition.store(-1);
    envState = EnvelopeState::Idle;
    currentEnvLevel = 0.0f;
    fractionalPosition = 0.0f;
    isCrossfading = false;
    triggerCrossfade.store(false);
    hasValidAudio.store(false);  // Mark audio as invalid

    juce::Thread::sleep(100);  // Let audio thread finish

    bool success = false;

    // Load sound map
    if (jsonFiles.size() > 0)
    {
        soundMapPath = jsonFiles[0];
        if (soundMap.loadFromFile(soundMapPath))
        {
            decoder.setLatentDim(soundMap.getLatentDim());

            lastParamX = currentX.load();
            lastParamY = currentY.load();

            float minX = soundMap.getMinX();
            float maxX = soundMap.getMaxX();
            float minY = soundMap.getMinY();
            float maxY = soundMap.getMaxY();

            float normalizedX = juce::jlimit(0.0f, 1.0f, (currentX.load() - minX) / (maxX - minX));
            float normalizedY = juce::jlimit(0.0f, 1.0f, (currentY.load() - minY) / (maxY - minY));

            parameters->getParameter("positionX")->setValue(normalizedX);
            parameters->getParameter("positionY")->setValue(normalizedY);

            success = true;
        }
    }

    // Load model
    if (modelFiles.size() > 0)
    {
        modelPath = modelFiles[0];
        if (decoder.loadModel(modelPath))
            success = true;
    }

    // CRITICAL: Reset EVERYTHING
    if (success)
    {
        // Clear buffers
        {
            juce::ScopedLock sl(bufferLock);
            soundBuffers[0].setSize(2, 0, false, false, false);
            soundBuffers[1].setSize(2, 0, false, false, false);
            soundBuffers[0].clear();
            soundBuffers[1].clear();
            activeBufferIndex.store(0);
        }

        // Reset filters
        {
            const juce::ScopedLock sl(filterLock);
            lpFilter1.reset();
            lpFilter2.reset();
            lastCutoff = -1.0f;
            lastReso = -1.0f;
        }

        lastGeneratedWaveform.clear();
        newWaveformAvailable.store(false);

        juce::Logger::writeToLog("[loadModelFolder] Complete reset performed");
    }

    juce::Logger::writeToLog("[loadModelFolder] === END: " + juce::String(success ? "SUCCESS" : "FAILED") + " ===");
    return success;
}

//==============================================================================
// Manual unload functionality
//==============================================================================

bool LatentAudioVSTAudioProcessor::unloadModel(const juce::String& modelPath)
{
    juce::Logger::writeToLog("[unloadModel] Attempting to unload: " + modelPath);

    // Check if this is the currently loaded model
    juce::String currentModelPath = this->modelPath.getFullPathName();
    if (currentModelPath == modelPath)
    {
        juce::Logger::writeToLog("[unloadModel] WARNING: Attempting to unload currently active model - clearing local state");

        // Clear local state but don't unload from cache yet
        // The cache entry will remain for other instances
        playbackPosition.store(-1);
        envState = EnvelopeState::Idle;
        currentEnvLevel = 0.0f;
        fractionalPosition = 0.0f;
        hasValidAudio.store(false);

        // Clear buffers
        {
            juce::ScopedLock sl(bufferLock);
            soundBuffers[0].setSize(2, 0, false, false, false);
            soundBuffers[1].setSize(2, 0, false, false, false);
            soundBuffers[0].clear();
            soundBuffers[1].clear();
        }

        // Reset filters
        {
            const juce::ScopedLock sl(filterLock);
            lpFilter1.reset();
            lpFilter2.reset();
            lastCutoff = -1.0f;
            lastReso = -1.0f;
        }

        lastGeneratedWaveform.clear();
        newWaveformAvailable.store(false);

        juce::Logger::writeToLog("[unloadModel] Cleared local state for active model");
    }

    // Always attempt to unload from shared cache
    bool cacheResult = LatentDecoder::unloadModelFromCache(modelPath);

    if (cacheResult)
    {
        juce::Logger::writeToLog("[unloadModel] Successfully unloaded from cache: " + modelPath);
    }
    else
    {
        juce::Logger::writeToLog("[unloadModel] Model not found in cache or unload failed: " + modelPath);
    }

    return cacheResult;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LatentAudioVSTAudioProcessor();
}