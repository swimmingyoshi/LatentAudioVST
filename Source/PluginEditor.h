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

    // ENVELOPE Section (Full ADSR)
    juce::Slider attackSlider;
    juce::Label attackLabel;

    juce::Slider decaySlider;
    juce::Label decayLabel;

    juce::Slider sustainSlider;
    juce::Label sustainLabel;

    juce::Slider releaseSlider;
    juce::Label releaseLabel;

    // MORPH Section (Now its own section)
    juce::Slider morphSpeedSlider;
    juce::Label morphSpeedLabel;

    juce::Slider morphRateSlider;
    juce::Label morphRateLabel;

    juce::Slider morphDepthXSlider;
    juce::Label morphDepthXLabel;

    juce::Slider morphDepthYSlider;
    juce::Label morphDepthYLabel;

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
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sustainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> morphSpeedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> morphRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> morphDepthXAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> morphDepthYAttachment;
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
    std::unique_ptr<juce::FileChooser> fileChooser;

    void updateEnvelope();
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

            g.fillAll(juce::Colours::black.withAlpha(0.3f));

            float cutoff = processor.lpCutoff.load();
            float reso = processor.lpReso.load();

            g.setColour(juce::Colours::cyan);
            juce::Path p;

            for (int i = 0; i < (int)w; i += 2)
            {
                float x = (float)i;
                float normX = x / w;
                float freq = 20.0f * std::pow(1000.0f, normX);

                float ratio = freq / cutoff;
                float mag1 = 1.0f / std::sqrt(1.0f + std::pow(ratio, 4.0f) - reso * 0.5f);
                float magTotal = mag1 * mag1;

                float y = h - (magTotal * h);

                if (i == 0) p.startNewSubPath(x, y);
                else p.lineTo(x, y);
            }
            g.strokePath(p, juce::PathStrokeType(1.5f));
        }
    private:
        LatentAudioVSTAudioProcessor& processor;
    };

    // Envelope Visualization
    class EnvelopeGraph : public juce::Component
    {
    public:
        EnvelopeGraph(LatentAudioVSTAudioProcessor& p) : processor(p) {}

        void paint(juce::Graphics& g) override
        {
            auto w = (float)getWidth();
            auto h = (float)getHeight();

            if (w <= 0.0f || h <= 0.0f) return;

            g.fillAll(juce::Colours::black.withAlpha(0.3f));

            float attack = processor.attackTime.load();
            float decay = processor.decayTime.load();
            float sustain = processor.sustainLevel.load();
            float release = processor.releaseTime.load();

            // Calculate time scaling
            float totalTime = attack + decay + 0.5f + release;
            float scale = w / totalTime;

            // Build envelope path
            juce::Path envPath;

            float x = 0.0f;
            float y = h;
            envPath.startNewSubPath(x, y);

            // Attack
            x += attack * scale;
            y = 0.0f;
            envPath.lineTo(x, y);

            // Decay
            x += decay * scale;
            y = h * (1.0f - sustain);
            envPath.lineTo(x, y);

            // Sustain
            x += 0.5f * scale;
            envPath.lineTo(x, y);

            // Release
            x += release * scale;
            y = h;
            envPath.lineTo(x, y);

            g.setColour(juce::Colours::lime);
            g.strokePath(envPath, juce::PathStrokeType(2.0f));

            // Draw stage labels
            g.setFont(10.0f);
            g.setColour(juce::Colours::white.withAlpha(0.6f));

            float labelY = h - 12.0f;
            g.drawText("A", 2, (int)labelY, 20, 12, juce::Justification::left);
            g.drawText("D", (int)(attack * scale) + 2, (int)labelY, 20, 12, juce::Justification::left);
            g.drawText("S", (int)((attack + decay) * scale) + 2, (int)labelY, 20, 12, juce::Justification::left);
            g.drawText("R", (int)((attack + decay + 0.5f) * scale) + 2, (int)labelY, 20, 12, juce::Justification::left);
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

            g.setColour(juce::Colours::darkgrey);
            g.fillEllipse(bounds);

            if (processor.isMidiActive())
            {

                g.setColour(juce::Colours::lime);
                g.fillEllipse(bounds.reduced(1.0f));
            }
        }

    private:
        LatentAudioVSTAudioProcessor& processor;
    };

    FilterGraph filterGraph;
    EnvelopeGraph envelopeGraph;
    MidiActivityIndicator midiIndicator;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LatentAudioVSTAudioProcessorEditor)
};