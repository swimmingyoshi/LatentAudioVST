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
juce::String NebulaComponent::displayName (const SoundPoint& p, int index)
{
    // Labels are dataset paths ("Pack/Folder/Name"): show the sound's own name.
    auto name = p.label.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false);
    if (name.isEmpty())
        name = "Sound " + juce::String (index + 1);
    if (std::isfinite (p.midi))
        name << "  (" << juce::MidiMessage::getMidiNoteName (juce::roundToInt (p.midi), true, true, 4) << ")";
    return name;
}

juce::Colour NebulaComponent::starColour (float brightness)
{
    // Like real stars: dark sounds glow ember red, bright ones burn blue-white.
    static const juce::Colour stops[] = { juce::Colour (0xffe8503a), juce::Colour (0xfff59a3c),
                                          juce::Colour (0xfff7e6b0), juce::Colour (0xffbfe4ff),
                                          juce::Colour (0xff6fb4ff) };
    const float t = juce::jlimit (0.0f, 1.0f, brightness) * 4.0f;
    const int i = std::min (3, (int) t);
    return stops[i].interpolatedWith (stops[i + 1], t - (float) i);
}

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

    // Nebula: the model's sound everywhere on the map (colour = brightness,
    // density = level), decoded at export. Rendered small, drawn smoothed.
    if (map != nullptr && map->sky.isValid() && map->sky.dim >= 2)
    {
        constexpr int kDown = 3;
        const int nw = juce::jmax (1, w / kDown), nh = juce::jmax (1, h / kDown);
        juce::Image gas (juce::Image::ARGB, nw, nh, true);
        juce::Image::BitmapData px (gas, juce::Image::BitmapData::writeOnly);
        std::vector<float> v ((size_t) map->sky.dim);
        const auto& sk = map->sky;
        for (int y = 0; y < nh; ++y)
        {
            for (int x = 0; x < nw; ++x)
            {
                const auto m = screenToMap (((float) x + 0.5f) * (float) kDown, ((float) y + 0.5f) * (float) kDown);
                // Fade out beyond the mapped area.
                const float ox = std::max ({ 0.0f, sk.minX - m.x, m.x - sk.maxX }) / (0.15f * (sk.maxX - sk.minX));
                const float oy = std::max ({ 0.0f, sk.minY - m.y, m.y - sk.maxY }) / (0.15f * (sk.maxY - sk.minY));
                const float edge = juce::jlimit (0.0f, 1.0f, 1.0f - std::sqrt (ox * ox + oy * oy));
                if (edge <= 0.0f)
                    continue;
                sk.sample (m.x, m.y, v.data());
                const float density = juce::jlimit (0.0f, 1.0f, v[1]);
                const float alpha = edge * (0.16f + 0.42f * density);
                px.setPixelColour (x, y, starColour (v[0]).withMultipliedSaturation (0.85f)
                                             .withMultipliedBrightness (0.8f).withAlpha (alpha));
            }
        }
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (gas, juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h));
    }

    // Background stars (fixed pattern, drift slightly with the view).
    {
        juce::Random rng (12345);
        const auto c0 = mapToScreen (0.0f, 0.0f);
        for (int i = 0; i < 260; ++i)
        {
            const float sx = std::fmod (rng.nextFloat() * (float) w * 3.0f + c0.x * 0.15f, (float) w);
            const float sy = std::fmod (rng.nextFloat() * (float) h * 3.0f + c0.y * 0.15f, (float) h);
            const float a = 0.08f + 0.22f * rng.nextFloat() * rng.nextFloat();
            g.setColour (juce::Colours::white.withAlpha (a));
            g.fillRect (sx < 0 ? sx + (float) w : sx, sy < 0 ? sy + (float) h : sy, 1.0f, 1.0f);
        }
    }

    if (map == nullptr)
        return;

    // Sounds as stars: colour = brightness, size = how long it rings.
    const float zr = juce::jlimit (0.0f, 2.0f, std::log2 (zoom));
    const auto bounds = getLocalBounds().toFloat().expanded (10.0f);
    for (const auto& p : map->points)
    {
        const auto s = mapToScreen (p.x, p.y);
        if (! bounds.contains (s))
            continue;
        const bool described = std::isfinite (p.brightness);
        const auto col = described ? starColour (p.brightness) : accentCyan();
        const float len = std::isfinite (p.length) ? p.length : 0.5f;
        const float r = (1.0f + 1.8f * len) * (1.0f + 0.45f * zr);
        g.setColour (col.withAlpha (0.10f));
        g.fillEllipse (s.x - r * 3.2f, s.y - r * 3.2f, r * 6.4f, r * 6.4f);
        g.setColour (col.withAlpha (0.85f));
        g.fillEllipse (s.x - r, s.y - r, r * 2.0f, r * 2.0f);
        if (r > 2.2f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.fillEllipse (s.x - r * 0.4f, s.y - r * 0.4f, r * 0.8f, r * 0.8f);
        }
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

        // Older maps blend the nearest sounds: show which, and how much. Sky maps
        // are a smooth field, so only the nearest sound is marked.
        if (! map->hasField())
        {
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
        }
        else if (! neighbours.empty())
        {
            const auto& p = map->points[(size_t) neighbours.front()];
            const auto s = mapToScreen (p.x, p.y);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawEllipse (s.x - 5.0f, s.y - 5.0f, 10.0f, 10.0f, 1.0f);

            // Nearest sound's name, bottom-left.
            const auto name = displayName (p, neighbours.front());
            g.setFont (juce::FontOptions (11.5f));
            g.setColour (textPrimary().withAlpha (0.85f));
            g.drawText (name, bounds.reduced (10.0f, 6.0f).removeFromTop (16.0f), juce::Justification::topLeft, true);
        }

        // Colour legend
        if (map->sky.isValid())
        {
            auto legend = juce::Rectangle<float> (10.0f, bounds.getBottom() - 20.0f, 90.0f, 5.0f);
            juce::ColourGradient grad (starColour (0.0f), legend.getX(), 0.0f, starColour (1.0f), legend.getRight(), 0.0f, false);
            for (float t : { 0.25f, 0.5f, 0.75f })
                grad.addColour (t, starColour (t));
            g.setGradientFill (grad);
            g.fillRoundedRectangle (legend, 2.5f);
            g.setFont (juce::FontOptions (10.0f));
            g.setColour (textSecondary().withAlpha (0.8f));
            g.drawText ("dark", juce::Rectangle<float> (10.0f, legend.getY() - 14.0f, 40.0f, 12.0f), juce::Justification::left);
            g.drawText ("bright", juce::Rectangle<float> (60.0f, legend.getY() - 14.0f, 40.0f, 12.0f), juce::Justification::right);
        }

        // Hovered point
        if (hoverIndex >= 0 && hoverIndex < (int) map->points.size())
        {
            const auto& p = map->points[(size_t) hoverIndex];
            const auto s = mapToScreen (p.x, p.y);
            g.setColour (textPrimary());
            g.drawEllipse (s.x - 6.0f, s.y - 6.0f, 12.0f, 12.0f, 1.2f);

            const auto label = displayName (p, hoverIndex);
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
        {
            juce::ColourGradient glow (accentAmber().withAlpha (0.35f), c.x, c.y,
                                       accentAmber().withAlpha (0.0f), c.x + 30.0f, c.y, true);
            g.setGradientFill (glow);
            g.fillEllipse (c.x - 30.0f, c.y - 30.0f, 60.0f, 60.0f);
        }
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
