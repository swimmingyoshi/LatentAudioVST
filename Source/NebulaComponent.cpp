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
    auto w      = (float) getWidth();
    auto h      = (float) getHeight();
    auto bounds = getLocalBounds().toFloat();

    // ── Background ──────────────────────────────────────────────────────────
    g.fillAll (juce::Colour (0xff0d0d1a));

    // Subtle radial vignette (slightly darker at corners)
    {
        juce::ColourGradient vig (juce::Colours::transparentBlack, w * 0.5f, h * 0.5f,
                                   juce::Colour (0x55000000), 0.0f, 0.0f, true);
        g.setGradientFill (vig);
        g.fillRect (bounds);
    }

    // Fine dot-grid
    g.setColour (juce::Colour (0xff1a1a30));
    for (float gx = 0.0f; gx < w; gx += 28.0f)
        for (float gy = 0.0f; gy < h; gy += 28.0f)
            g.fillRect (gx, gy, 1.0f, 1.0f);

    // Border
    g.setColour (juce::Colour (0xff2a2a4a));
    g.drawRect (bounds, 1.0f);

    // ── Sound Points ─────────────────────────────────────────────────────────
    const auto& points = soundMap.getPoints();

    if (points.empty())
    {
        g.setFont (juce::Font (14.0f));
        g.setColour (juce::Colour (0xff333355));
        g.drawFittedText ("Load a model folder to explore the sound space",
                          getLocalBounds().reduced (20), juce::Justification::centred, 2);
    }
    else
    {
        for (const auto& p : points)
        {
            auto pt = mapToScreen (p.x, p.y);
            if (!std::isfinite (pt.x) || !std::isfinite (pt.y)) continue;

            // Soft radial glow
            float glowR = 9.0f;
            juce::ColourGradient glow (juce::Colour (0x4038bdf8), pt.x, pt.y,
                                        juce::Colours::transparentBlack,
                                        pt.x + glowR, pt.y, true);
            g.setGradientFill (glow);
            g.fillEllipse (pt.x - glowR, pt.y - glowR, glowR * 2.0f, glowR * 2.0f);

            // Core dot
            g.setColour (juce::Colour (0xff38bdf8).withAlpha (0.75f));
            g.fillEllipse (pt.x - 2.0f, pt.y - 2.0f, 4.0f, 4.0f);
        }
    }

    // ── Cursor ────────────────────────────────────────────────────────────────
    auto cursorPt = mapToScreen (cursorX, cursorY);
    if (std::isfinite (cursorPt.x) && std::isfinite (cursorPt.y))
    {
        constexpr float ringR  = 9.0f;
        constexpr float glowR  = 20.0f;
        constexpr float lineIn = 12.0f;  // gap from centre
        constexpr float lineOut = 20.0f; // end of crosshair arm
        float cx = cursorPt.x, cy = cursorPt.y;

        // Wide ambient glow
        {
            juce::ColourGradient glow (juce::Colour (0x28f5a623), cx, cy,
                                        juce::Colours::transparentBlack,
                                        cx + glowR, cy, true);
            g.setGradientFill (glow);
            g.fillEllipse (cx - glowR, cy - glowR, glowR * 2.0f, glowR * 2.0f);
        }

        // Outer dim ring
        g.setColour (juce::Colour (0x40f5a623));
        g.drawEllipse (cx - 14.0f, cy - 14.0f, 28.0f, 28.0f, 1.0f);

        // Main ring
        g.setColour (juce::Colour (0xfff5a623));
        g.drawEllipse (cx - ringR, cy - ringR, ringR * 2.0f, ringR * 2.0f, 1.5f);

        // Gap crosshair (lines start outside the ring)
        g.drawLine (cx,         cy - lineOut, cx,         cy - lineIn,  1.5f);
        g.drawLine (cx,         cy + lineIn,  cx,         cy + lineOut, 1.5f);
        g.drawLine (cx - lineOut, cy,         cx - lineIn, cy,           1.5f);
        g.drawLine (cx + lineIn,  cy,         cx + lineOut, cy,          1.5f);

        // Centre dot
        g.fillEllipse (cx - 2.0f, cy - 2.0f, 4.0f, 4.0f);
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
