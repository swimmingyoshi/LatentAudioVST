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

#include "NebulaComponent.h"

//==============================================================================
NebulaComponent::NebulaComponent (SoundMap& map) : soundMap (map)
{
    // Initialize cursor to center or first point
    cursorX = 0.0f;
    cursorY = 0.0f;
}

NebulaComponent::~NebulaComponent()
{
}

void NebulaComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    // Draw Points
    g.setColour (juce::Colours::cyan.withAlpha (0.6f));
    const auto& points = soundMap.getPoints();

    for (const auto& p : points)
    {
        auto pt = mapToScreen (p.x, p.y);
        if (isfinite(pt.x) && isfinite(pt.y))
            g.fillEllipse (pt.x - 2.0f, pt.y - 2.0f, 4.0f, 4.0f);
    }

    // Draw Cursor
    auto cursorPt = mapToScreen (cursorX, cursorY);
    if (isfinite(cursorPt.x) && isfinite(cursorPt.y))
    {
        g.setColour (juce::Colours::yellow);
        g.drawEllipse (cursorPt.x - 8.0f, cursorPt.y - 8.0f, 16.0f, 16.0f, 2.0f);
        g.drawLine (cursorPt.x, cursorPt.y - 10.0f, cursorPt.x, cursorPt.y + 10.0f, 2.0f);
        g.drawLine (cursorPt.x - 10.0f, cursorPt.y, cursorPt.x + 10.0f, cursorPt.y, 2.0f);
    }
}

void NebulaComponent::resized()
{
}

void NebulaComponent::setSnapping (bool shouldSnap)
{
    isSnapping = shouldSnap;
}

void NebulaComponent::setCursorPosition(float x, float y)
{
    cursorX = x;
    cursorY = y;
    repaint();
}

void NebulaComponent::resetView()
{
    zoomFactor = 1.0f;
    panX = 0.0f;
    panY = 0.0f;
    repaint();
}

void NebulaComponent::mouseDown (const juce::MouseEvent& event)
{
    // Store pan start state
    if (event.mods.isRightButtonDown() || event.mods.isMiddleButtonDown())
    {
        startPanX = panX;
        startPanY = panY;
        return;
    }

    auto pt = screenToMap ((float) event.x, (float) event.y);
    
    if (isSnapping)
    {
        const auto* nearest = soundMap.getNearestPoint (pt.x, pt.y);
        if (nearest != nullptr)
        {
            cursorX = nearest->x;
            cursorY = nearest->y;
        }
        else
        {
            // Fallback if map is empty
            cursorX = pt.x;
            cursorY = pt.y;
        }
    }
    else
    {
        cursorX = pt.x;
        cursorY = pt.y;
    }

    if (onCursorChanged)
        onCursorChanged (cursorX, cursorY);

    repaint();
}

void NebulaComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (event.mods.isRightButtonDown() || event.mods.isMiddleButtonDown())
    {
        float w = (float) getWidth();
        float h = (float) getHeight();
        
        float mapW = soundMap.getMaxX() - soundMap.getMinX();
        float mapH = soundMap.getMaxY() - soundMap.getMinY();

        // Calculate delta in Map Coordinates
        // Invert X because moving mouse Right should move View Left (Pan)
        // Or wait, dragging map: Move mouse right -> map moves right.
        // Center moves left.
        
        // Delta Screen
        float dxScreen = (float) event.getDistanceFromDragStartX();
        float dyScreen = (float) event.getDistanceFromDragStartY();

        // Screen to Map Ratio
        float scaleX = mapW / w / zoomFactor;
        float scaleY = mapH / h / zoomFactor;

        // Apply to start pos
        // Note: dyScreen positive is Down.
        // Moving mouse Down -> Map moves Down -> Center moves Up (Negative Y direction in Map)
        // Since Y is NOT inverted anymore (Positive Y = Down), dragging Down should DECREASE panY 
        // to move the viewport Center Up (so the Map moves Down).
        
        panX = startPanX - dxScreen * scaleX;
        panY = startPanY - dyScreen * scaleY; // Sign flipped for non-inverted Y

        repaint();
        return;
    }

    mouseDown (event);
}

void NebulaComponent::mouseUp (const juce::MouseEvent& event)
{
    if (onMouseReleased)
        onMouseReleased();
}

void NebulaComponent::mouseExit (const juce::MouseEvent& event)
{
    // If mouse leaves the component, we should probably trigger release too?
    // User might want to drag outside though.
    // Standard VST behavior: Dragging outside usually keeps control.
    // But if button is released outside, mouseUp is called globally by JUCE.
    // So we don't need to do anything here strictly.
    // However, if we want to stop playing when cursor leaves the area (XY Pad style),
    // we would call release here.
    // Let's stick to Button Up logic for now.
}

void NebulaComponent::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    (void)event; // Suppress unreferenced parameter warning
    float zoomDelta = (wheel.deltaY > 0) ? 1.1f : 0.9f;
    float newZoom = juce::jlimit (1.0f, 50.0f, zoomFactor * zoomDelta);

    if (newZoom != zoomFactor)
    {
        // To zoom towards crosshair:
        // We want cursorX, cursorY to remain at the same relative screen position?
        // Actually, prompt says "when zooming in we focus on the crosshair".
        // This implies we should center the view on the cursorX, cursorY.
        
        // Let's implement panning such that the crosshair remains centered or we move towards it.
        // Simple approach: Center the view on the crosshair as we zoom in.
        
        if (newZoom > zoomFactor) // Zooming In
        {
            // Drift pan towards cursor
            float drift = 0.1f; // Amount to drift per step
            panX += (cursorX - panX) * drift;
            panY += (cursorY - panY) * drift;
        }

        zoomFactor = newZoom;
        repaint();
    }
}

juce::Point<float> NebulaComponent::mapToScreen (float x, float y) const
{
    auto w = (float) getWidth();
    auto h = (float) getHeight();

    if (w <= 0.0f || h <= 0.0f)
        return juce::Point<float> (0.0f, 0.0f);

    float minX = soundMap.getMinX();
    float maxX = soundMap.getMaxX();
    float minY = soundMap.getMinY();
    float maxY = soundMap.getMaxY();

    float mapW = maxX - minX;
    float mapH = maxY - minY;

    // Guard against degenerate map bounds
    const float eps = 1.0e-6f;
    if (std::abs(mapW) < eps) mapW = 1.0f;
    if (std::abs(mapH) < eps) mapH = 1.0f;

    // Apply Pan
    // We treat panX/panY as the center of the view in map coordinates
    // If panX=0, panY=0 (default), we might be at origin.
    // Ideally default view should be centered on map center.
    // Let's adjust pan behavior:
    // panX/Y is the offset from the map center.

    float centerX = minX + mapW * 0.5f + panX;
    float centerY = minY + mapH * 0.5f + panY;

    // Normalize relative to view center and zoom
    // (val - center) * zoom / size + 0.5

    float normX = (x - centerX) / mapW * zoomFactor + 0.5f;
    float normY = (y - centerY) / mapH * zoomFactor + 0.5f;

    // Invert Y so positive is up? NO.
    // Python GUI uses Y-Down (Screen Coordinates).
    // To match Python, we DO NOT invert Y.
    // High Y value = Bottom of screen.
    return juce::Point<float> (normX * w, normY * h);
}

juce::Point<float> NebulaComponent::screenToMap (float x, float y) const
{
    auto w = (float) getWidth();
    auto h = (float) getHeight();

    if (w <= 0.0f || h <= 0.0f)
        return juce::Point<float> (0.0f, 0.0f);

    float minX = soundMap.getMinX();
    float maxX = soundMap.getMaxX();
    float minY = soundMap.getMinY();
    float maxY = soundMap.getMaxY();

    float mapW = maxX - minX;
    float mapH = maxY - minY;

    // Guard against degenerate map bounds
    const float eps = 1.0e-6f;
    if (std::abs(mapW) < eps) mapW = 1.0f;
    if (std::abs(mapH) < eps) mapH = 1.0f;

    float centerX = minX + mapW * 0.5f + panX;
    float centerY = minY + mapH * 0.5f + panY;

    float normX = x / w;
    float normY = y / h; // No inversion to match Python

    // Inverse of mapToScreen:
    // norm = (val - center) / mapSize * zoom + 0.5
    // norm - 0.5 = (val - center) / mapSize * zoom
    // (norm - 0.5) / zoom * mapSize = val - center
    // val = center + (norm - 0.5) * mapSize / zoom

    return juce::Point<float> (
        centerX + (normX - 0.5f) * mapW / zoomFactor,
        centerY + (normY - 0.5f) * mapH / zoomFactor
    );
}
