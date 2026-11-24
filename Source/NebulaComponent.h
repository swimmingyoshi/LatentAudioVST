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
// The sky map. Sounds are stars (colour = brightness, size = length); sky maps
// also show the model's sound between the stars as a nebula. Left click/drag picks
// a position, right/middle drag pans, wheel zooms toward the mouse, double-click resets.
class NebulaComponent : public juce::Component
{
public:
    NebulaComponent() = default;

    void setMap (std::shared_ptr<const SoundMapData> newMap);
    void setCursorPosition (float x, float y);
    void setSnapping (bool shouldSnap) { isSnapping = shouldSnap; }
    void setBlend (float newBlend);
    // A/B morph mode: B's position (map coordinates) and the morph amount 0..1.
    void setMorph (bool enabled, juce::Point<float> target, float amount);
    void resetView();

    // Text drawn over the map (loading / errors / empty state). Empty = none.
    void setOverlayMessage (const juce::String& message, bool isError = false);

    std::function<void (float, float, bool isDrag)> onCursorChanged;  // map coordinates
    std::function<void()> onMouseReleased;
    std::function<void (float, float)> onMorphTargetChanged; // Shift/Alt-click in morph mode

    std::function<void()> onContextMenu; // right-click without dragging

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
    juce::Point<float> pickPoint (juce::Point<float> screenPos) const; // clamped (and snapped)
    void rebuildPointsLayer();
    static juce::Colour starColour (float brightness);
    static juce::String displayName (const SoundPoint& p, int index);
    void updateNeighbours();
    bool isPanGesture (const juce::MouseEvent& e) const;

    std::shared_ptr<const SoundMapData> map;
    juce::Image pointsLayer;
    bool layerDirty = true;

    float cursorX = 0.0f, cursorY = 0.0f;
    std::vector<int> neighbours;          // sounds in the blend at the cursor, largest share first
    std::vector<float> neighbourWeights;  // their shares of the blend
    float effectiveCount = 0.0f;          // how many sounds effectively take part
    float blend = 0.0f;
    bool morphMode = false, draggingB = false;
    juce::Point<float> morphTarget;
    float morphAmount = 0.5f;
    int hoverIndex = -1;

    bool isSnapping = false;
    bool isPanning = false, rightPress = false;
    float zoom = 1.0f;
    float panX = 0.0f, panY = 0.0f;       // view centre offset from map centre (map units)
    float panStartX = 0.0f, panStartY = 0.0f;

    juce::String overlay;
    bool overlayIsError = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NebulaComponent)
};
