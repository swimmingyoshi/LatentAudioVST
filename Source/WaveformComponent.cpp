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

//==============================================================================
WaveformComponent::WaveformComponent()
{
}

WaveformComponent::~WaveformComponent()
{
}

void WaveformComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.3f));

    g.setColour(juce::Colours::cyan);
    g.drawRect(getLocalBounds(), 1);

    if (waveform.empty())
        return;

    auto w = (float)getWidth();
    auto h = (float)getHeight();
    auto centerY = h * 0.5f;
    auto scaleY = h * 0.45f;

    juce::Path path;
    path.startNewSubPath(0.0f, centerY);

    int step = 1;
    if (waveform.size() > (size_t)w)
        step = (int)(waveform.size() / w);

    for (size_t i = 0; i < waveform.size(); i += step)
    {
        float x = (float)i / (float)waveform.size() * w;
        float y = centerY - (waveform[i] * scaleY);
        path.lineTo(x, y);
    }

    g.strokePath(path, juce::PathStrokeType(1.5f));

    // Draw Sample Start Indicator
    if (startPos > 0.0f)
    {
        float startX = startPos * w;
        g.setColour(juce::Colours::green.withAlpha(0.6f));
        g.drawLine(startX, 0.0f, startX, h, 2.0f);
    }

    // Draw Loop Length Indicator
    if (loopLen > 0.0f && loopLen < 1.0f)
    {
        float loopX = loopLen * w;
        g.setColour(juce::Colours::yellow.withAlpha(0.6f));
        g.drawLine(loopX, 0.0f, loopX, h, 2.0f);

        // Optional: shade the played portion
        if (startPos < loopLen)
        {
            g.setColour(juce::Colours::yellow.withAlpha(0.1f));
            g.fillRect(startPos * w, 0.0f, (loopX - startPos * w), h);
        }
    }



    // Draw Playhead
    if (playbackPos >= 0.0f && playbackPos <= 1.0f)
    {
        float x = playbackPos * w;
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.drawLine(x, 0.0f, x, h, 2.0f);
    }
}

void WaveformComponent::resized()
{
}

void WaveformComponent::setWaveform(const std::vector<float>& audioData)
{
    waveform = audioData;
    repaint();
}

void WaveformComponent::setPlaybackPosition(float normalizedPosition)
{
    playbackPos = normalizedPosition;
    repaint();
}

void WaveformComponent::setLoopLength(float normalizedLength)
{
    loopLen = normalizedLength;
    repaint();
}

void WaveformComponent::setSampleStart(float normalizedStart)
{
    startPos = normalizedStart;
    repaint();
}

void WaveformComponent::setFades(float normalizedIn, float normalizedOut)
{
    // Kept for compatibility but unused
    repaint();
}