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
    auto w       = (float)getWidth();
    auto h       = (float)getHeight();
    auto bounds  = getLocalBounds().toFloat();
    auto centerY = h * 0.5f;
    auto scaleY  = h * 0.45f;

    // Background
    g.setColour(juce::Colour(0xff0d0d1a));
    g.fillRect(bounds);

    // Centre line
    g.setColour(juce::Colour(0xff1a1a30));
    g.drawLine(0.0f, centerY, w, centerY, 0.5f);

    // Border
    g.setColour(juce::Colour(0xff2a2a4a));
    g.drawRect(bounds, 1.0f);

    if (waveform.empty())
    {
        g.setColour(juce::Colour(0xff333355));
        g.setFont(juce::Font(11.0f));
        g.drawFittedText("No waveform", getLocalBounds(), juce::Justification::centred, 1);
        return;
    }

    int step = 1;
    if (waveform.size() > (size_t)w)
        step = (int)(waveform.size() / w);

    // Build paths
    juce::Path strokePath, fillPath;
    fillPath.startNewSubPath(0.0f, centerY);
    strokePath.startNewSubPath(0.0f, centerY);

    for (size_t i = 0; i < waveform.size(); i += step)
    {
        float x = (float)i / (float)waveform.size() * w;
        float y = centerY - (waveform[i] * scaleY);
        fillPath.lineTo(x, y);
        strokePath.lineTo(x, y);
    }
    fillPath.lineTo(w, centerY);
    fillPath.closeSubPath();

    // Gradient fill
    juce::ColourGradient waveGrad(juce::Colour(0x5538bdf8), 0.0f, 0.0f,
                                   juce::Colours::transparentBlack, 0.0f, h, false);
    g.setGradientFill(waveGrad);
    g.fillPath(fillPath);

    // Stroke
    g.setColour(juce::Colour(0xff38bdf8).withAlpha(0.9f));
    g.strokePath(strokePath, juce::PathStrokeType(1.5f));

    // Sample Start marker
    if (startPos > 0.0f)
    {
        float startX = startPos * w;
        g.setColour(juce::Colour(0xff4ade80).withAlpha(0.8f));
        g.drawLine(startX, 0.0f, startX, h, 1.5f);
    }

    // Loop Length marker
    if (loopLen > 0.0f && loopLen < 1.0f)
    {
        float loopX = loopLen * w;
        g.setColour(juce::Colour(0xfff5a623).withAlpha(0.75f));
        g.drawLine(loopX, 0.0f, loopX, h, 1.5f);

        if (startPos < loopLen)
        {
            g.setColour(juce::Colour(0xfff5a623).withAlpha(0.07f));
            g.fillRect(startPos * w, 0.0f, loopX - startPos * w, h);
        }
    }

    // Playhead
    if (playbackPos >= 0.0f && playbackPos <= 1.0f)
    {
        float x = playbackPos * w;
        g.setColour(juce::Colour(0xffffffff).withAlpha(0.9f));
        g.drawLine(x, 0.0f, x, h, 1.5f);

        // Small downward triangle at top
        juce::Path arrow;
        arrow.addTriangle(x - 4.0f, 0.0f, x + 4.0f, 0.0f, x, 5.0f);
        g.fillPath(arrow);
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