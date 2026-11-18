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
// Min/max peak view of the generated sound with the start/end region and playhead.
class WaveformComponent : public juce::Component
{
public:
    WaveformComponent() = default;

    void setWaveform (std::vector<float> audioData);
    void setPlaybackPosition (float normalizedPosition); // < 0 hides the playhead
    void setRegion (float normalizedStart, float normalizedEnd);

    void paint (juce::Graphics&) override;
    void resized() override { peaks.clear(); }

private:
    void computePeaks (int width);

    std::vector<float> waveform;
    std::vector<std::pair<float, float>> peaks; // per pixel column, cached
    float playbackPos = -1.0f;
    float regionStart = 0.0f, regionEnd = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformComponent)
};
