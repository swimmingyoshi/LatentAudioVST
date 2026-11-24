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
    FilterGraph (std::atomic<float>& cutoff, std::atomic<float>& resonance, std::atomic<float>& lowCut)
        : cutoffParam (cutoff), resoParam (resonance), lowCutParam (lowCut) {}

    bool needsUpdate() const
    {
        return cutoffParam.load() != lastCutoff || resoParam.load() != lastReso || lowCutParam.load() != lastLowCut;
    }
    void paint (juce::Graphics&) override;

private:
    std::atomic<float>& cutoffParam;
    std::atomic<float>& resoParam;
    std::atomic<float>& lowCutParam;
    float lastCutoff = -1.0f, lastReso = -1.0f, lastLowCut = -1.0f;
};

//==============================================================================
// A knob that is easy to turn: drag up or right to raise it (about 200 px for the
// whole range, wherever you grab it), hold Shift or Ctrl for fine steps, use the
// mouse wheel, or double-click to reset.
class LatentKnob : public juce::Slider
{
public:
    LatentKnob() : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow)
    {
        setRepaintsOnMouseActivity (true);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        rightClick = e.mods.isPopupMenu();
        if (rightClick)
            return; // the editor shows the automation menu; no gesture, no value change
        juce::Slider::mouseDown (e); // starts the host gesture
        lastDragPos = e.position;
        dragProportion = valueToProportionOfLength (getValue());
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! isEnabled() || rightClick)
            return;
        const float delta = (lastDragPos.y - e.position.y) + (e.position.x - lastDragPos.x);
        lastDragPos = e.position;
        const bool fine = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();
        dragProportion = juce::jlimit (0.0, 1.0, dragProportion + (double) delta / (fine ? 1200.0 : 200.0));
        setValue (proportionOfLengthToValue (dragProportion), juce::sendNotificationSync);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! rightClick) // what the press was: the other button may be down by now
            juce::Slider::mouseUp (e);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            juce::Slider::mouseDoubleClick (e);
    }

private:
    juce::Point<float> lastDragPos;
    double dragProportion = 0.0;
    bool rightClick = false;
};

//==============================================================================
// A button that ignores right clicks (they open the automation menu instead).
template <typename ButtonType>
class LeftClickOnly : public ButtonType
{
public:
    using ButtonType::ButtonType;
    void mouseDown (const juce::MouseEvent& e) override
    {
        rightClick = e.mods.isPopupMenu();
        if (! rightClick) ButtonType::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! rightClick) ButtonType::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override   { if (! rightClick) ButtonType::mouseUp (e); }

private:
    bool rightClick = false;
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
        LatentKnob slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        int section = 0;
        bool sharesPrevCell = false; // drawn in the previous knob's place (mode-dependent knobs)
    };

    Knob& addKnob (const juce::String& paramId, const juce::String& name, const juce::String& tooltip, int section);
    void addToggle (juce::ToggleButton& button, const juce::String& paramId, const juce::String& text, const juce::String& tooltip);
    void chooseModelFolder();

    // Right-click: the host's menu for a parameter (FL: "Create automation clip"...).
    void addParameterMenu (juce::Component& component, const juce::StringArray& paramIds);
    void showParameterMenu (const juce::StringArray& paramIds, juce::Point<int> editorPos);
    void showHostMenu (const juce::String& paramId, juce::Point<int> editorPos);
    void showMapMenu();
    void mouseDown (const juce::MouseEvent&) override; // right clicks on registered components
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
    LeftClickOnly<juce::ToggleButton> playOnNoteToggle, holdToggle, snapToggle, keyTrackToggle;
    LeftClickOnly<juce::TextButton> areaModeButton { "Area" }, morphModeButton { "A / B" };
    Knob* blendKnob = nullptr;
    Knob* morphKnob = nullptr;
    void setBlendMode (bool morph);

    std::vector<std::pair<juce::Component*, juce::StringArray>> parameterMenus;
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
    std::array<juce::Rectangle<int>, 4> sectionAreas;
    juce::Rectangle<int> headerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LatentAudioVSTAudioProcessorEditor)
};
