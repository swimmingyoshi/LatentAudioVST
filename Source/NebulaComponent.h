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
#include "PluginProcessor.h"

//==============================================================================
class NebulaComponent : public juce::Component
{
public:
    NebulaComponent (SoundMap& map);
    ~NebulaComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    // Callback when the cursor position changes (x, y in map coordinates)
    std::function<void (float, float)> onCursorChanged;
    
    // Callback when the mouse is released
    std::function<void ()> onMouseReleased;

    // Settings
    void setSnapping (bool shouldSnap);
    void setCursorPosition(float x, float y);
    void resetView(); // Reset zoom and pan to default



private:
    SoundMap& soundMap;

    // Current cursor position in map coordinates
    float cursorX = 0.0f;
    float cursorY = 0.0f;
    
    bool isSnapping = false;

    // View Transform
    float zoomFactor = 1.0f;
    float panX = 0.0f; // Map coordinate offset X
    float panY = 0.0f; // Map coordinate offset Y
    
    // Panning state
    float startPanX = 0.0f;
    float startPanY = 0.0f;

    // Helper to map coordinates
    juce::Point<float> mapToScreen (float x, float y) const;
    juce::Point<float> screenToMap (float x, float y) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NebulaComponent)
};
