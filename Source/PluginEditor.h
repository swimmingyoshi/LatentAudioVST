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
#include "PluginProcessor.h"
#include "NebulaComponent.h"
#include "WaveformComponent.h"
#include "LatentLookAndFeel.h"

//==============================================================================
class LatentAudioVSTAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer,
    private juce::Button::Listener
{
public:
    LatentAudioVSTAudioProcessorEditor(LatentAudioVSTAudioProcessor&);
    ~LatentAudioVSTAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

public:
    void updateUIFromProcessor();

public:
    NebulaComponent nebulaComp;

private:
    LatentAudioVSTAudioProcessor& audioProcessor;

    // Main Components
    WaveformComponent waveformComp;

    // PLAYBACK Section
    juce::Slider rateSlider;
    juce::Label rateLabel;

    juce::Slider tuneSlider;
    juce::Label tuneLabel;

    juce::Slider crossfadeSlider;
    juce::Label crossfadeLabel;

    // FILTER Section
    juce::Slider cutoffSlider;
    juce::Label cutoffLabel;

    juce::Slider resoSlider;
    juce::Label resoLabel;


    // MASTER Section
    juce::Slider gainSlider;
    juce::Label gainLabel;

    juce::Slider mixSlider;
    juce::Label mixLabel;

    juce::Slider lengthSlider;
    juce::Label lengthLabel;

    juce::Slider startSlider;
    juce::Label startLabel;

    // Toggles
    juce::ToggleButton holdToggle;
    juce::ToggleButton snapToggle;
    juce::ToggleButton triggerToggle;
    juce::TextButton randomButton;
    juce::TextButton resetButton;
    juce::TextButton homeButton;



    // Toggle Labels
    juce::Label holdLabel;
    juce::Label snapLabel;
    juce::Label triggerLabel;

    // Parameter Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tuneAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> crossfadeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> cutoffAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> resoAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lengthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> startAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> latchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> triggerAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> snapAttachment;

    // Model loading
    juce::TextButton loadModelFolderButton;
    juce::TextButton unloadModelButton;
    juce::Label modelFolderPathLabel;
    juce::Label modelStatusLabel;
    juce::Label soundMapStatusLabel;

    // Coordinate display
    juce::Label xCoordLabel;
    juce::Label yCoordLabel;
    juce::Label providerLabel; // shows "CPU" / "GPU: CUDA" / "GPU: DML"
    std::unique_ptr<juce::FileChooser> fileChooser;

    void timerCallback() override;
    void resetAllKnobs();
    void buttonClicked(juce::Button* button) override;

    // Filter Visualization
    class FilterGraph : public juce::Component
    {
    public:
        FilterGraph(LatentAudioVSTAudioProcessor& p) : processor(p) {}
        void paint(juce::Graphics& g) override
        {
            auto w = (float)getWidth();
            auto h = (float)getHeight();
            if (w <= 0.0f || h <= 0.0f) return;

            auto bounds = getLocalBounds().toFloat();

            // Background
            g.setColour(juce::Colour(0xff0d0d1a));
            g.fillRoundedRectangle(bounds, 4.0f);

            // Grid lines
            g.setColour(juce::Colour(0xff1a1a30));
            for (float fx = w * 0.25f; fx < w; fx += w * 0.25f)
                g.drawLine(fx, 0.0f, fx, h, 0.5f);
            g.drawLine(0.0f, h * 0.5f, w, h * 0.5f, 0.5f);

            float cutoff = processor.lpCutoff.load();
            float reso   = processor.lpReso.load();

            juce::Path freqPath;
            for (int i = 0; i < (int)w; i += 2)
            {
                float x     = (float)i;
                float freq  = 20.0f * std::pow(1000.0f, x / w);
                float ratio = freq / cutoff;
                float mag   = 1.0f / std::sqrt(1.0f + std::pow(ratio, 4.0f) - reso * 0.5f);
                float y     = h - (mag * h * 0.9f);
                if (i == 0) freqPath.startNewSubPath(x, y);
                else        freqPath.lineTo(x, y);
            }

            // Gradient fill under curve
            juce::Path fillPath = freqPath;
            fillPath.lineTo(w, h); fillPath.lineTo(0.0f, h); fillPath.closeSubPath();
            juce::ColourGradient fillGrad(juce::Colour(0x3038bdf8), 0.0f, 0.0f,
                                           juce::Colours::transparentBlack, 0.0f, h, false);
            g.setGradientFill(fillGrad);
            g.fillPath(fillPath);

            // Stroke
            g.setColour(juce::Colour(0xff38bdf8).withAlpha(0.9f));
            g.strokePath(freqPath, juce::PathStrokeType(1.5f));

            // Border
            g.setColour(juce::Colour(0xff2a2a4a));
            g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
        }
    private:
        LatentAudioVSTAudioProcessor& processor;
    };

    // MIDI Activity Indicator
    class MidiActivityIndicator : public juce::Component
    {
    public:
        MidiActivityIndicator(LatentAudioVSTAudioProcessor& p) : processor(p) {}

        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat().reduced(2.0f);

            // Dark base circle
            g.setColour(juce::Colour(0xff141428));
            g.fillEllipse(bounds);
            g.setColour(juce::Colour(0xff2a2a4a));
            g.drawEllipse(bounds, 1.0f);

            if (processor.isMidiActive())
            {
                // Outer glow
                juce::ColourGradient glow(juce::Colour(0x604ade80),
                                          bounds.getCentreX(), bounds.getCentreY(),
                                          juce::Colours::transparentBlack,
                                          bounds.getRight(), bounds.getCentreY(), true);
                g.setGradientFill(glow);
                g.fillEllipse(bounds.expanded(3.0f));

                // Core
                g.setColour(juce::Colour(0xff4ade80));
                g.fillEllipse(bounds.reduced(2.0f));
            }
        }

    private:
        LatentAudioVSTAudioProcessor& processor;
    };

    FilterGraph filterGraph;
    MidiActivityIndicator midiIndicator;

    LatentLookAndFeel laf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LatentAudioVSTAudioProcessorEditor)
};