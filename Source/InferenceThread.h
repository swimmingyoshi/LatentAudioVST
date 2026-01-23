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
#include "LatentDecoder.h"

//==============================================================================
class InferenceThread : public juce::Thread
{
public:
    InferenceThread (LatentDecoder& d)
        : juce::Thread ("InferenceThread"), decoder (d)
    {
        juce::Logger::writeToLog("[InferenceThread] === CONSTRUCTOR ===");
        juce::Logger::writeToLog("[InferenceThread] this=" + juce::String::toHexString((juce::int64)this));
        // Reserve memory to avoid reallocations
        inputLatent.resize (128, 0.0f);
        outputAudio.reserve (44100);
    }

    ~InferenceThread() override
    {
        juce::Logger::writeToLog("[InferenceThread] === DESTRUCTOR ===");
        juce::Logger::writeToLog("[InferenceThread] this=" + juce::String::toHexString((juce::int64)this));
        stopThread (4000);
        juce::Logger::writeToLog("[InferenceThread] Thread stopped");
    }

    void trigger (const std::vector<float>& latent)
    {
        // juce::Logger::writeToLog("[InferenceThread::trigger] Called with latent size=" + juce::String(latent.size()));
        // Thread-safe copy of input
        {
            const juce::ScopedLock sl (lock);
            // Resize if needed to accept any latent dimension
            if (latent.size() != inputLatent.size())
                inputLatent.resize(latent.size());
            inputLatent = latent;
        }
        // juce::Logger::writeToLog("[InferenceThread::trigger] Signaling thread...");
        startSignal.signal();
    }

    void run() override
    {
        juce::Logger::writeToLog("[InferenceThread] === THREAD STARTED ===");
        juce::Logger::writeToLog("[InferenceThread] Thread ID: " + juce::String::toHexString((juce::int64)juce::Thread::getCurrentThreadId()));

        while (! threadShouldExit())
        {
            // Wait for trigger - only proceed if actually signaled (not on timeout)
            bool signaled = startSignal.wait (100); // 100ms timeout to check exit condition

            if (! signaled)
                continue; // Timeout, not signaled - loop back and wait again

            // juce::Logger::writeToLog("[InferenceThread] Signal received, processing...");

            if (threadShouldExit())
            {
                juce::Logger::writeToLog("[InferenceThread] Exit requested during signal");
                break;
            }

            // Copy input locally to avoid holding lock during inference
            std::vector<float> localInput;
            {
                const juce::ScopedLock sl (lock);
                localInput = inputLatent;
            }

            // Run Inference (Slow)
            // juce::Logger::writeToLog("[InferenceThread] Running inference with input size=" + juce::String(localInput.size()));
            // juce::Logger::writeToLog("[InferenceThread] decoder.isModelLoaded()=" + juce::String(decoder.isModelLoaded() ? "TRUE" : "FALSE"));

            if (decoder.decode (localInput, outputAudio))
            {
                // juce::Logger::writeToLog("[InferenceThread] Inference successful, output size=" + juce::String(outputAudio.size()));
                if (onInferenceFinished)
                    onInferenceFinished (outputAudio);
            }
            else
            {
                juce::Logger::writeToLog("[InferenceThread] Inference FAILED");
            }
        }

        juce::Logger::writeToLog("[InferenceThread] === THREAD EXITING ===");
    }

    std::function<void (const std::vector<float>&)> onInferenceFinished;

private:
    LatentDecoder& decoder;
    juce::WaitableEvent startSignal;
    juce::CriticalSection lock;
    
    std::vector<float> inputLatent;
    std::vector<float> outputAudio;
};
