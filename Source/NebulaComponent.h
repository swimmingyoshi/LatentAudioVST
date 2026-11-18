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

//==============================================================================
// 2D sound map ("nebula"). Left click/drag picks a position, right/middle drag
// pans, wheel zooms toward the mouse, double-click resets the view.
class NebulaComponent : public juce::Component
{
public:
    NebulaComponent() = default;

    void setMap (std::shared_ptr<const SoundMapData> newMap);
    void setCursorPosition (float x, float y);
    void setSnapping (bool shouldSnap) { isSnapping = shouldSnap; }
    void resetView();

    // Text drawn over the map (loading / errors / empty state). Empty = none.
    void setOverlayMessage (const juce::String& message, bool isError = false);

    std::function<void (float, float, bool isDrag)> onCursorChanged;  // map coordinates
    std::function<void()> onMouseReleased;

    void paint (juce::Graphics&) override;
    void resized() override { layerDirty = true; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    juce::Point<float> mapToScreen (float x, float y) const;
    juce::Point<float> screenToMap (float x, float y) const;

private:
    void pickAt (juce::Point<float> screenPos, bool isDrag);
    void rebuildPointsLayer();
    void updateNeighbours();
    bool isPanGesture (const juce::MouseEvent& e) const;

    std::shared_ptr<const SoundMapData> map;
    juce::Image pointsLayer;
    bool layerDirty = true;

    float cursorX = 0.0f, cursorY = 0.0f;
    std::vector<int> neighbours;          // nearest points to the cursor (the blend)
    std::vector<float> neighbourWeights;  // their IDW weights, summing to 1
    int hoverIndex = -1;

    bool isSnapping = false;
    bool isPanning = false;
    float zoom = 1.0f;
    float panX = 0.0f, panY = 0.0f;       // view centre offset from map centre (map units)
    float panStartX = 0.0f, panStartY = 0.0f;

    juce::String overlay;
    bool overlayIsError = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NebulaComponent)
};
