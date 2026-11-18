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
#include <functional>

//==============================================================================
// Background worker that runs model inference off the audio thread.
//
// It does not queue work. The owner publishes the latest request through
// atomics and the worker calls `service` whenever it wakes, so any number of
// position changes collapse into a single decode of the newest position.
//
// Waking: wake() is for non-realtime threads. The audio thread must not call
// it (WaitableEvent takes a mutex); the 2 ms poll picks up its requests.
class InferenceThread : public juce::Thread
{
public:
    explicit InferenceThread (std::function<void()> serviceCallback)
        : juce::Thread ("LatentAudio Inference"), service (std::move (serviceCallback))
    {
    }

    ~InferenceThread() override
    {
        stopThread (4000);
    }

    void wake() { signal.signal(); }

    void run() override
    {
        while (! threadShouldExit())
        {
            signal.wait (2);
            if (threadShouldExit())
                break;
            service();
        }
    }

private:
    std::function<void()> service;
    juce::WaitableEvent signal;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InferenceThread)
};
