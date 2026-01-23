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
#include <vector>

//==============================================================================
class WaveformComponent : public juce::Component
{
public:
    WaveformComponent();
    ~WaveformComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Updates the waveform data to be displayed
    void setWaveform (const std::vector<float>& audioData);

    // Sets the playback progress (0.0 to 1.0)
    void setPlaybackPosition (float normalizedPosition);
    
    // Sets the loop endpoint (0.0 to 1.0)
    void setLoopLength (float normalizedLength); // 0.1 to 1.0 based on sample length
    void setSampleStart (float normalizedStart); // 0.0 to 0.9 based on sample start
    void setFades (float normalizedIn, float normalizedOut);

private:
    std::vector<float> waveform;
    float playbackPos = -1.0f;
    float loopLen = 1.0f;
    float startPos = 0.0f;
    float fadeIn = 0.0f;
    float fadeOut = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformComponent)
};
