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
// Plots the 4-pole low-pass response (in dB) for the current cutoff/resonance.
class FilterGraph : public juce::Component
{
public:
    FilterGraph (std::atomic<float>& cutoff, std::atomic<float>& resonance) : cutoffParam (cutoff), resoParam (resonance) {}

    bool needsUpdate() const { return cutoffParam.load() != lastCutoff || resoParam.load() != lastReso; }
    void paint (juce::Graphics&) override;

private:
    std::atomic<float>& cutoffParam;
    std::atomic<float>& resoParam;
    float lastCutoff = -1.0f, lastReso = -1.0f;
};

//==============================================================================
class MidiLed : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setLit (bool shouldBeLit) { if (lit != shouldBeLit) { lit = shouldBeLit; repaint(); } }
    void paint (juce::Graphics&) override;

private:
    bool lit = false;
};

//==============================================================================
class LatentAudioVSTAudioProcessorEditor : public juce::AudioProcessorEditor,
                                           public juce::FileDragAndDropTarget,
                                           private juce::Timer
{
public:
    explicit LatentAudioVSTAudioProcessorEditor (LatentAudioVSTAudioProcessor&);
    ~LatentAudioVSTAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { setDragHover (true); }
    void fileDragExit (const juce::StringArray&) override { setDragHover (false); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    // Exposed for tests
    NebulaComponent& getNebula() { return nebula; }

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        int section = 0;
    };

    Knob& addKnob (const juce::String& paramId, const juce::String& name, const juce::String& tooltip, int section);
    void addToggle (juce::ToggleButton& button, const juce::String& paramId, const juce::String& text, const juce::String& tooltip);
    void chooseModelFolder();
    void setDragHover (bool hover) { if (dragHover != hover) { dragHover = hover; repaint(); } }
    void timerCallback() override;

    LatentAudioVSTAudioProcessor& processor;
    LatentLookAndFeel laf;
    juce::TooltipWindow tooltipWindow { this, 600 };

    NebulaComponent nebula;
    WaveformComponent waveform;
    FilterGraph filterGraph;
    MidiLed midiLed;

    juce::Label titleLabel, modelLabel, statusLabel, providerLabel, coordLabel;
    juce::TextButton loadButton { "Load Model..." }, unloadButton { "Unload" };
    juce::TextButton randomButton { "Random" }, centerButton { "Center" }, resetButton { "Reset" };
    juce::ToggleButton playOnNoteToggle, holdToggle, snapToggle, keyTrackToggle;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<Knob>> knobs;
    Knob* rootKnob = nullptr;

    std::unique_ptr<juce::FileChooser> fileChooser;

    // UI state polled from the processor (per editor instance)
    std::shared_ptr<const SoundMapData> shownMap;
    uint32_t lastDisplayVersion = 0xffffffffu;
    uint32_t lastMidiCount = 0;
    juce::uint32 midiLitUntil = 0;
    bool dragHover = false;
    std::array<juce::Rectangle<int>, 3> sectionAreas;
    juce::Rectangle<int> headerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LatentAudioVSTAudioProcessorEditor)
};
