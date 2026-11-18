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

#include "WaveformComponent.h"
#include "LatentLookAndFeel.h"

void WaveformComponent::setWaveform (std::vector<float> audioData)
{
    waveform = std::move (audioData);
    peaks.clear();
    repaint();
}

void WaveformComponent::setPlaybackPosition (float normalizedPosition)
{
    if (std::abs (normalizedPosition - playbackPos) < 0.0005f)
        return;
    playbackPos = normalizedPosition;
    repaint();
}

void WaveformComponent::setRegion (float normalizedStart, float normalizedEnd)
{
    if (normalizedStart == regionStart && normalizedEnd == regionEnd)
        return;
    regionStart = normalizedStart;
    regionEnd = normalizedEnd;
    repaint();
}

void WaveformComponent::computePeaks (int width)
{
    peaks.assign ((size_t) width, { 0.0f, 0.0f });
    const size_t n = waveform.size();
    if (n == 0 || width <= 0)
        return;

    for (int x = 0; x < width; ++x)
    {
        const size_t from = (size_t) x * n / (size_t) width;
        const size_t to = std::max (from + 1, (size_t) (x + 1) * n / (size_t) width);
        float lo = waveform[from], hi = waveform[from];
        for (size_t i = from; i < to && i < n; ++i)
        {
            lo = std::min (lo, waveform[i]);
            hi = std::max (hi, waveform[i]);
        }
        peaks[(size_t) x] = { lo, hi };
    }
}

void WaveformComponent::paint (juce::Graphics& g)
{
    using namespace LatentPalette;
    const auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth(), h = bounds.getHeight();
    const float mid = h * 0.5f, scale = h * 0.45f;

    g.setColour (darkBg());
    g.fillRect (bounds);

    // Region that actually plays
    const float rx0 = regionStart * w, rx1 = std::max (rx0 + 1.0f, regionEnd * w);
    g.setColour (accentAmber().withAlpha (0.06f));
    g.fillRect (rx0, 0.0f, rx1 - rx0, h);

    g.setColour (juce::Colour (0xff1a1a30));
    g.drawHorizontalLine ((int) mid, 0.0f, w);

    if (waveform.empty())
    {
        g.setColour (textSecondary().withAlpha (0.5f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Generated sound appears here", bounds, juce::Justification::centred);
    }
    else
    {
        if ((int) peaks.size() != (int) w)
            computePeaks ((int) w);

        juce::Path shape;
        shape.startNewSubPath (0.0f, mid - peaks.front().second * scale);
        for (size_t x = 1; x < peaks.size(); ++x)
            shape.lineTo ((float) x, mid - peaks[x].second * scale);
        for (size_t x = peaks.size(); x-- > 0;)
            shape.lineTo ((float) x, mid - peaks[x].first * scale);
        shape.closeSubPath();

        g.setColour (accentCyan().withAlpha (0.55f));
        g.fillPath (shape);
        g.setColour (accentCyan().withAlpha (0.9f));
        g.strokePath (shape, juce::PathStrokeType (0.8f));

        // Dim what lies outside the region
        g.setColour (darkBg().withAlpha (0.55f));
        g.fillRect (0.0f, 0.0f, rx0, h);
        g.fillRect (rx1, 0.0f, w - rx1, h);
    }

    // Region markers
    g.setColour (accentGreen().withAlpha (0.8f));
    g.drawVerticalLine ((int) rx0, 0.0f, h);
    g.setColour (accentAmber().withAlpha (0.8f));
    g.drawVerticalLine ((int) std::min (rx1, w - 1.0f), 0.0f, h);

    if (playbackPos >= 0.0f && playbackPos <= 1.0f)
    {
        const float x = playbackPos * w;
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawLine (x, 0.0f, x, h, 1.5f);
        juce::Path arrow;
        arrow.addTriangle (x - 4.0f, 0.0f, x + 4.0f, 0.0f, x, 5.0f);
        g.fillPath (arrow);
    }

    g.setColour (borderCol());
    g.drawRect (bounds, 1.0f);
}
