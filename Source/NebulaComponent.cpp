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
#include "LatentLookAndFeel.h"

namespace
{
    constexpr int kBlendNeighbours = 5;   // must match SoundMapData::interpolate's default
    constexpr float kHoverRadius = 10.0f; // px
}

//==============================================================================
void NebulaComponent::setMap (std::shared_ptr<const SoundMapData> newMap)
{
    if (newMap == map)
        return;
    map = std::move (newMap);
    hoverIndex = -1;
    resetView();
    updateNeighbours();
}

void NebulaComponent::setCursorPosition (float x, float y)
{
    if (x == cursorX && y == cursorY)
        return;
    cursorX = x;
    cursorY = y;
    updateNeighbours();
    repaint();
}

void NebulaComponent::resetView()
{
    zoom = 1.0f;
    panX = panY = 0.0f;
    layerDirty = true;
    repaint();
}

void NebulaComponent::setOverlayMessage (const juce::String& message, bool isError)
{
    if (message == overlay && isError == overlayIsError)
        return;
    overlay = message;
    overlayIsError = isError;
    repaint();
}

void NebulaComponent::updateNeighbours()
{
    neighbours.clear();
    neighbourWeights.clear();
    if (map == nullptr || map->points.empty())
        return;

    neighbours = map->nearestIndices (cursorX, cursorY, kBlendNeighbours);
    float total = 0.0f;
    for (int idx : neighbours)
    {
        const auto& p = map->points[(size_t) idx];
        const float d2 = juce::square (p.x - cursorX) + juce::square (p.y - cursorY);
        const float w = d2 < 1.0e-10f ? 1.0e10f : 1.0f / d2;
        neighbourWeights.push_back (w);
        total += w;
    }
    for (auto& w : neighbourWeights)
        w /= total;
}

//==============================================================================
juce::Point<float> NebulaComponent::mapToScreen (float x, float y) const
{
    const float w = (float) getWidth(), h = (float) getHeight();
    if (map == nullptr || w <= 0.0f || h <= 0.0f)
        return { w * 0.5f, h * 0.5f };

    const float cx = map->minX + map->rangeX() * 0.5f + panX;
    const float cy = map->minY + map->rangeY() * 0.5f + panY;
    // 6% margin so edge points (and their glow) are never clipped.
    const float scale = zoom * 0.94f;
    return { ((x - cx) / map->rangeX() * scale + 0.5f) * w,
             ((y - cy) / map->rangeY() * scale + 0.5f) * h };
}

juce::Point<float> NebulaComponent::screenToMap (float sx, float sy) const
{
    const float w = (float) getWidth(), h = (float) getHeight();
    if (map == nullptr || w <= 0.0f || h <= 0.0f)
        return {};

    const float cx = map->minX + map->rangeX() * 0.5f + panX;
    const float cy = map->minY + map->rangeY() * 0.5f + panY;
    const float scale = zoom * 0.94f;
    return { cx + (sx / w - 0.5f) * map->rangeX() / scale,
             cy + (sy / h - 0.5f) * map->rangeY() / scale };
}

//==============================================================================
void NebulaComponent::rebuildPointsLayer()
{
    layerDirty = false;
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0)
        return;

    const float scale = juce::Component::getApproximateScaleFactorForComponent (this);
    pointsLayer = juce::Image (juce::Image::ARGB, juce::roundToInt (w * scale), juce::roundToInt (h * scale), true);
    juce::Graphics g (pointsLayer);
    g.addTransform (juce::AffineTransform::scale (scale));

    using namespace LatentPalette;

    // Dot grid
    g.setColour (juce::Colour (0xff1a1a30));
    for (float gx = 14.0f; gx < (float) w; gx += 28.0f)
        for (float gy = 14.0f; gy < (float) h; gy += 28.0f)
            g.fillRect (gx, gy, 1.0f, 1.0f);

    if (map == nullptr)
        return;

    // Points: faint halo + core. Plain circles keep this fast for large maps.
    const float r = juce::jlimit (1.5f, 3.5f, 1.5f + std::log2 (zoom));
    const auto halo = accentCyan().withAlpha (0.10f);
    const auto core = accentCyan().withAlpha (0.70f);
    const auto bounds = getLocalBounds().toFloat().expanded (10.0f);

    for (const auto& p : map->points)
    {
        const auto s = mapToScreen (p.x, p.y);
        if (! bounds.contains (s))
            continue;
        g.setColour (halo);
        g.fillEllipse (s.x - r * 3.0f, s.y - r * 3.0f, r * 6.0f, r * 6.0f);
        g.setColour (core);
        g.fillEllipse (s.x - r, s.y - r, r * 2.0f, r * 2.0f);
    }
}

void NebulaComponent::paint (juce::Graphics& g)
{
    using namespace LatentPalette;
    const auto bounds = getLocalBounds().toFloat();

    g.fillAll (darkBg());
    if (layerDirty)
        rebuildPointsLayer();
    g.drawImage (pointsLayer, bounds);

    if (map != nullptr && ! map->points.empty())
    {
        const auto c = mapToScreen (cursorX, cursorY);

        // Blend lines: which sounds the current position mixes, and how much.
        for (size_t i = 0; i < neighbours.size(); ++i)
        {
            const auto& p = map->points[(size_t) neighbours[i]];
            const auto s = mapToScreen (p.x, p.y);
            const float w = neighbourWeights[i];
            g.setColour (accentAmber().withAlpha (0.15f + 0.6f * w));
            g.drawLine (c.x, c.y, s.x, s.y, 0.6f + 1.6f * w);
            g.setColour (accentAmber().withAlpha (0.35f + 0.65f * w));
            const float rr = 2.5f + 3.0f * w;
            g.fillEllipse (s.x - rr, s.y - rr, rr * 2.0f, rr * 2.0f);
        }

        // Hovered point
        if (hoverIndex >= 0 && hoverIndex < (int) map->points.size())
        {
            const auto& p = map->points[(size_t) hoverIndex];
            const auto s = mapToScreen (p.x, p.y);
            g.setColour (textPrimary());
            g.drawEllipse (s.x - 6.0f, s.y - 6.0f, 12.0f, 12.0f, 1.2f);

            auto label = p.label.isNotEmpty() ? p.label : ("Sound " + juce::String (hoverIndex + 1));
            if (std::isfinite (p.midi))
                label << "  (" << juce::MidiMessage::getMidiNoteName (juce::roundToInt (p.midi), true, true, 4) << ")";
            g.setFont (juce::FontOptions (12.0f));
            auto box = juce::Rectangle<float> (s.x + 10.0f, s.y - 22.0f,
                                               juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 12.0f, 18.0f);
            box = box.constrainedWithin (bounds.reduced (4.0f));
            g.setColour (panelBg().withAlpha (0.92f));
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (textPrimary());
            g.drawText (label, box, juce::Justification::centred, false);
        }

        // Cursor
        constexpr float ringR = 9.0f, lineIn = 12.0f, lineOut = 20.0f;
        g.setColour (accentAmber().withAlpha (0.18f));
        g.fillEllipse (c.x - 18.0f, c.y - 18.0f, 36.0f, 36.0f);
        g.setColour (accentAmber());
        g.drawEllipse (c.x - ringR, c.y - ringR, ringR * 2.0f, ringR * 2.0f, 1.5f);
        g.drawLine (c.x, c.y - lineOut, c.x, c.y - lineIn, 1.5f);
        g.drawLine (c.x, c.y + lineIn, c.x, c.y + lineOut, 1.5f);
        g.drawLine (c.x - lineOut, c.y, c.x - lineIn, c.y, 1.5f);
        g.drawLine (c.x + lineIn, c.y, c.x + lineOut, c.y, 1.5f);
        g.fillEllipse (c.x - 2.0f, c.y - 2.0f, 4.0f, 4.0f);

        // Hint
        g.setFont (juce::FontOptions (10.5f));
        g.setColour (textSecondary().withAlpha (0.6f));
        g.drawText ("click: play   right-drag: pan   wheel: zoom   double-click: reset",
                    bounds.reduced (8.0f, 4.0f), juce::Justification::bottomRight, false);
    }

    if (overlay.isNotEmpty())
    {
        g.setFont (juce::FontOptions (15.0f));
        g.setColour (overlayIsError ? juce::Colour (0xfff06080) : textSecondary());
        g.drawFittedText (overlay, getLocalBounds().reduced (40), juce::Justification::centred, 3);
    }

    g.setColour (borderCol());
    g.drawRect (bounds, 1.0f);
}

//==============================================================================
bool NebulaComponent::isPanGesture (const juce::MouseEvent& e) const
{
    return e.mods.isRightButtonDown() || e.mods.isMiddleButtonDown();
}

void NebulaComponent::pickAt (juce::Point<float> screenPos, bool isDrag)
{
    if (map == nullptr || map->points.empty())
        return;

    auto pt = screenToMap (screenPos.x, screenPos.y);
    pt.x = juce::jlimit (map->minX, map->maxX, pt.x);
    pt.y = juce::jlimit (map->minY, map->maxY, pt.y);

    if (isSnapping)
    {
        const int idx = map->nearestIndex (pt.x, pt.y);
        if (idx >= 0)
            pt = { map->points[(size_t) idx].x, map->points[(size_t) idx].y };
    }

    cursorX = pt.x;
    cursorY = pt.y;
    updateNeighbours();
    repaint();

    if (onCursorChanged)
        onCursorChanged (cursorX, cursorY, isDrag);
}

void NebulaComponent::mouseDown (const juce::MouseEvent& e)
{
    if (isPanGesture (e))
    {
        isPanning = true;
        panStartX = panX;
        panStartY = panY;
        return;
    }
    pickAt (e.position, false);
}

void NebulaComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (isPanning)
    {
        if (map == nullptr)
            return;
        const float scale = zoom * 0.94f;
        panX = panStartX - (float) e.getDistanceFromDragStartX() / (float) getWidth() * map->rangeX() / scale;
        panY = panStartY - (float) e.getDistanceFromDragStartY() / (float) getHeight() * map->rangeY() / scale;
        layerDirty = true;
        repaint();
        return;
    }
    pickAt (e.position, true);
}

void NebulaComponent::mouseUp (const juce::MouseEvent&)
{
    if (isPanning)
    {
        isPanning = false;
        return;
    }
    if (onMouseReleased)
        onMouseReleased();
}

void NebulaComponent::mouseMove (const juce::MouseEvent& e)
{
    int newHover = -1;
    if (map != nullptr && ! map->points.empty())
    {
        const auto pt = screenToMap (e.position.x, e.position.y);
        const int idx = map->nearestIndex (pt.x, pt.y);
        if (idx >= 0)
        {
            const auto s = mapToScreen (map->points[(size_t) idx].x, map->points[(size_t) idx].y);
            if (s.getDistanceFrom (e.position) <= kHoverRadius)
                newHover = idx;
        }
    }
    if (newHover != hoverIndex)
    {
        hoverIndex = newHover;
        repaint();
    }
}

void NebulaComponent::mouseExit (const juce::MouseEvent&)
{
    if (hoverIndex >= 0)
    {
        hoverIndex = -1;
        repaint();
    }
}

void NebulaComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    resetView();
}

void NebulaComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (map == nullptr || wheel.deltaY == 0.0f)
        return;

    const float newZoom = juce::jlimit (1.0f, 50.0f, zoom * (wheel.deltaY > 0.0f ? 1.15f : 1.0f / 1.15f));
    if (newZoom == zoom)
        return;

    // Keep the map point under the mouse fixed while zooming.
    const auto anchor = screenToMap (e.position.x, e.position.y);
    zoom = newZoom;
    const float scale = zoom * 0.94f;
    const float cx = anchor.x - (e.position.x / (float) getWidth() - 0.5f) * map->rangeX() / scale;
    const float cy = anchor.y - (e.position.y / (float) getHeight() - 0.5f) * map->rangeY() / scale;
    panX = cx - (map->minX + map->rangeX() * 0.5f);
    panY = cy - (map->minY + map->rangeY() * 0.5f);
    if (zoom <= 1.0f)
        panX = panY = 0.0f;

    layerDirty = true;
    repaint();
}
