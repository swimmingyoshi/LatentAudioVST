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

#include "PluginEditor.h"

using namespace LatentPalette;

namespace
{
    // Sections, left to right. Each knob takes its section's accent colour.
    enum Section { morphSection, playbackSection, filterSection, outputSection, numSections };
    const juce::Colour sectionAccents[] = { accentAmber(), accentViolet(), accentCyan(), juce::Colour (0xfff06080) };
    const char* sectionNames[] = { "SOUND", "PLAYBACK", "FILTER", "OUTPUT" };
    const int sectionUnits[] = { 2, 5, 4, 4 }; // relative widths
    const juce::Colour errorColour (0xfff06080);
}

//==============================================================================
void FilterGraph::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth(), h = bounds.getHeight();
    lastCutoff = cutoffParam.load();
    lastReso = resoParam.load();
    lastLowCut = lowCutParam.load();

    g.setColour (darkBg());
    g.fillRoundedRectangle (bounds, 4.0f);

    // dB grid: +12 / 0 / -12 / -24
    const float topDb = 18.0f, bottomDb = -36.0f;
    auto dbToY = [&] (float db) { return juce::jmap (db, topDb, bottomDb, 0.0f, h); };
    g.setColour (juce::Colour (0xff1a1a30));
    for (float db : { 12.0f, 0.0f, -12.0f, -24.0f })
        g.drawHorizontalLine ((int) dbToY (db), 0.0f, w);
    for (float f : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine ((int) (std::log (f / 20.0f) / std::log (1000.0f) * w), 0.0f, h);

    // Same topology as the processor: Butterworth stage + resonant stage.
    const float qa = 0.5412f, qb = 1.3066f + juce::jlimit (0.0f, 1.0f, lastReso) * 7.0f;
    auto stage = [] (float r, float q) { return 1.0f / std::sqrt (juce::square (1.0f - r * r) + juce::square (r / q)); };
    const bool lowCutOn = lastLowCut > 20.5f;

    juce::Path response;
    for (int i = 0; i <= (int) w; i += 2)
    {
        const float f = 20.0f * std::pow (1000.0f, (float) i / w);
        const float r = f / juce::jmax (20.0f, lastCutoff);
        float mag = stage (r, qa) * stage (r, qb);
        if (lowCutOn)
        {
            const float rh = f / lastLowCut; // high-pass = low-pass response mirrored, times r^2 per stage
            mag *= rh * rh * stage (rh, 0.5412f) * rh * rh * stage (rh, 1.3066f);
        }
        const float db = juce::Decibels::gainToDecibels (mag, -80.0f);
        const float y = juce::jlimit (0.0f, h, dbToY (db));
        if (i == 0) response.startNewSubPath ((float) i, y);
        else        response.lineTo ((float) i, y);
    }

    juce::Path fill (response);
    fill.lineTo (w, h);
    fill.lineTo (0.0f, h);
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (accentCyan().withAlpha (0.25f), 0.0f, 0.0f, juce::Colours::transparentBlack, 0.0f, h, false));
    g.fillPath (fill);
    g.setColour (accentCyan().withAlpha (0.9f));
    g.strokePath (response, juce::PathStrokeType (1.5f));

    g.setColour (borderCol());
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
}

//==============================================================================
void MidiLed::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().withSizeKeepingCentre (12.0f, 12.0f);
    g.setColour (lit ? accentGreen() : panelBg());
    g.fillEllipse (b);
    g.setColour (lit ? accentGreen().brighter (0.3f) : borderCol());
    g.drawEllipse (b, 1.0f);
}

//==============================================================================
LatentAudioVSTAudioProcessorEditor::LatentAudioVSTAudioProcessorEditor (LatentAudioVSTAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      filterGraph (*p.getAPVTS().getRawParameterValue ("filterCutoff"),
                   *p.getAPVTS().getRawParameterValue ("filterResonance"),
                   *p.getAPVTS().getRawParameterValue ("lowCut"))
{
    setLookAndFeel (&laf);

    // --- Header ---
    titleLabel.setText ("LATENT AUDIO", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, textPrimary());
    modelLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    modelLabel.setColour (juce::Label::textColourId, textPrimary());
    statusLabel.setFont (juce::FontOptions (12.0f));
    providerLabel.setJustificationType (juce::Justification::centredRight);
    providerLabel.setTooltip ("Where the model runs. GPU needs the CUDA build of ONNX Runtime and a CUDA GPU.");
    for (auto* l : { &titleLabel, &modelLabel, &statusLabel, &providerLabel, &coordLabel })
        addAndMakeVisible (l);

    loadButton.setTooltip ("Choose a model folder (latent_decoder.onnx + sound_map.json). You can also drag a folder onto the plugin.");
    loadButton.onClick = [this] { chooseModelFolder(); };
    unloadButton.setTooltip ("Unload the model. Memory is freed once no other instance uses it.");
    unloadButton.onClick = [this] { processor.unloadModel(); };
    addAndMakeVisible (loadButton);
    addAndMakeVisible (unloadButton);
    loadButton.setComponentID ("load");
    unloadButton.setComponentID ("unload");

    // --- Toolbar ---
    addToggle (playOnNoteToggle, "triggerOnMidi", "Play on Note", "Only MIDI notes start sounds. Moving the cursor changes the sound without playing it.");
    addToggle (holdToggle, "latchMode", "Hold", "Loop the sound between Start and End until released.");
    addToggle (snapToggle, "snapMode", "Snap", "Snap the cursor to the nearest sound on the map.");
    addToggle (keyTrackToggle, "keyTrack", "Key Track",
               "MIDI notes set the pitch. Pitched models play the real note; others transpose relative to Root.");

    randomButton.setTooltip ("Jump to a random sound.");
    randomButton.onClick = [this] { processor.triggerRandomPosition(); };
    centerButton.setTooltip ("Reset the view and move the cursor to the centre of the map.");
    centerButton.onClick = [this]
    {
        nebula.resetView();
        if (auto map = processor.getMapSnapshot())
            processor.moveCursorFromUI (map->minX + map->rangeX() * 0.5f, map->minY + map->rangeY() * 0.5f);
    };
    resetButton.setTooltip ("Reset all controls to their defaults (keeps the position).");
    resetButton.onClick = [this] { processor.resetParametersToDefaults(); };
    for (auto* b : { &randomButton, &centerButton, &resetButton })
        addAndMakeVisible (b);
    randomButton.setComponentID ("random");
    centerButton.setComponentID ("center");
    resetButton.setComponentID ("reset");

    midiLed.setTooltip ("MIDI activity");
    addAndMakeVisible (midiLed);
    coordLabel.setJustificationType (juce::Justification::centredRight);
    coordLabel.setTooltip ("Cursor position (automatable as Position X / Position Y)");

    // --- Map + waveform ---
    nebula.onCursorChanged = [this] (float x, float y, bool isDrag) { processor.triggerFromUI (x, y, ! isDrag); };
    nebula.onMouseReleased = [this] { processor.releaseFromUI(); };
    nebula.onMorphTargetChanged = [this] (float x, float y) { processor.setMorphTargetFromUI (x, y); };
    addAndMakeVisible (nebula);
    addAndMakeVisible (waveform);
    nebula.setComponentID ("nebula");
    waveform.setComponentID ("waveform");
    waveform.renderExport = [this] { return processor.renderForExport(); };
    waveform.exportName = [this]
    {
        juce::String name;
        if (auto map = processor.getMapSnapshot(); map != nullptr && ! map->points.empty())
        {
            const auto c = processor.getCursor();
            const auto& p = map->points[(size_t) map->nearestIndex (c.x, c.y)];
            name = p.label.isNotEmpty() ? p.label.upToLastOccurrenceOf (".", false, false) : juce::String ("sound");
        }
        return name;
    };

    // --- Knobs ---  (drag up/right, Shift = fine, wheel, double-click = reset)
    blendKnob = &addKnob ("blend", "Blend",
             "Area mode: how many sounds around the cursor are mixed into the one you hear. 0% = the map's own blend "
             "(about ten nearby sounds, exactly the sound when you are on a star). Turn it up to mix in more distant "
             "sounds. The map shows which sounds are in the mix.", morphSection);
    morphKnob = &addKnob ("morph", "Morph A-B",
             "A/B mode: blends the sound under the cursor (A) into the sound at B. Shift-click (or Alt-click) "
             "the map to place B anywhere - far-apart sounds make the most unusual hybrids.", morphSection);
    morphKnob->sharesPrevCell = true;

    // Blend mode switch (two segments)
    for (auto* b : { &areaModeButton, &morphModeButton })
    {
        b->setColour (juce::TextButton::buttonOnColourId, accentAmber());
        b->setColour (juce::TextButton::textColourOnId, textPrimary());
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }
    areaModeButton.setTooltip ("Area: mix the sounds around the cursor (Blend knob).");
    morphModeButton.setTooltip ("A/B: morph the cursor's sound (A) into another sound (B). Shift-click the map to place B.");
    areaModeButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    morphModeButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    areaModeButton.onClick = [this] { setBlendMode (false); };
    morphModeButton.onClick = [this] { setBlendMode (true); };
    areaModeButton.setComponentID ("modeArea");
    morphModeButton.setComponentID ("modeMorph");

    addKnob ("playbackRate", "Rate", "Playback speed (also changes pitch).", playbackSection);
    addKnob ("fineTune", "Tune", "Fine tune in cents.", playbackSection).slider.getProperties().set ("bipolar", true);
    addKnob ("crossfade", "Loop X-Fade", "Crossfade at the loop point when Hold is on.", playbackSection);
    rootKnob = &addKnob ("rootNote", "Root", "With Key Track on, this note plays at the original pitch.", playbackSection);
    addKnob ("polyphony", "Voices", "How many notes can play at once (1 = mono: a new note cuts the last).", playbackSection);

    addAndMakeVisible (filterGraph);
    addKnob ("lowCut", "Low Cut",
             "High-pass: removes rumble and boom below this frequency. Try 60-120 Hz before OTT or heavy EQ. "
             "Off at the far left (sub-20 Hz rumble is always removed).", filterSection);
    addKnob ("filterCutoff", "Cutoff", "Low-pass cutoff frequency.", filterSection);
    addKnob ("filterResonance", "Reso", "Low-pass resonance.", filterSection);

    addKnob ("sampleStart", "Start", "Where playback starts in the sound.", outputSection);
    addKnob ("sampleLength", "End", "Where playback ends (and loops, with Hold).", outputSection);
    addKnob ("velocitySens", "Velocity", "How much MIDI velocity affects volume.", outputSection);
    addKnob ("outputGain", "Gain", "Output level.", outputSection);

    setResizable (true, true);
    setResizeLimits (860, 580, 2400, 1600);
    setSize (1080, 720);

    timerCallback();
    startTimerHz (30);
}

LatentAudioVSTAudioProcessorEditor::~LatentAudioVSTAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

LatentAudioVSTAudioProcessorEditor::Knob& LatentAudioVSTAudioProcessorEditor::addKnob (
    const juce::String& paramId, const juce::String& name, const juce::String& tooltip, int section)
{
    auto knob = std::make_unique<Knob>();
    knob->section = section;
    knob->slider.setTooltip (tooltip + "\n(Drag up/right, Shift for fine, double-click to reset.)");
    knob->slider.setColour (juce::Slider::rotarySliderFillColourId, sectionAccents[section]);
    knob->slider.setComponentID (paramId);
    knob->slider.setPopupDisplayEnabled (false, false, nullptr);
    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.getAPVTS(), paramId, knob->slider);
    // Double-click a knob to reset it to its default.
    if (auto* param = processor.getAPVTS().getParameter (paramId))
        knob->slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

    knob->label.setText (name, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.setTooltip (tooltip);
    knob->label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (knob->slider);
    addAndMakeVisible (knob->label);
    // After parenting, so the value box is created with our look-and-feel.
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knobs.push_back (std::move (knob));
    return *knobs.back();
}

void LatentAudioVSTAudioProcessorEditor::setBlendMode (bool morph)
{
    if (auto* param = processor.getAPVTS().getParameter ("blendMode"))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (morph ? 1.0f : 0.0f);
        param->endChangeGesture();
    }
}

void LatentAudioVSTAudioProcessorEditor::addToggle (juce::ToggleButton& button, const juce::String& paramId,
                                                    const juce::String& text, const juce::String& tooltip)
{
    button.setButtonText (text);
    button.setTooltip (tooltip);
    button.setComponentID (paramId);
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.getAPVTS(), paramId, button));
    addAndMakeVisible (button);
}

//==============================================================================
void LatentAudioVSTAudioProcessorEditor::chooseModelFolder()
{
    auto start = processor.getLoadStatus().folder;
    fileChooser = std::make_unique<juce::FileChooser> ("Select a model folder", start.isDirectory() ? start : juce::File());
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis = juce::Component::SafePointer<LatentAudioVSTAudioProcessorEditor> (this)] (const juce::FileChooser& fc)
        {
            if (safeThis == nullptr)
                return;
            const auto chosen = fc.getResult();
            if (chosen != juce::File())
                safeThis->processor.loadModelFolderAsync (chosen);
        });
}

bool LatentAudioVSTAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (files.size() != 1)
        return false;
    const juce::File f (files[0]);
    return f.isDirectory() || f.hasFileExtension ("onnx;json");
}

void LatentAudioVSTAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    setDragHover (false);
    if (files.size() == 1)
        processor.loadModelFolderAsync (juce::File (files[0]));
}

//==============================================================================
void LatentAudioVSTAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (darkBg());

    g.setColour (panelBg());
    g.fillRect (headerArea);
    g.setColour (borderCol());
    g.drawHorizontalLine (headerArea.getBottom(), 0.0f, (float) getWidth());

    for (size_t i = 0; i < sectionAreas.size(); ++i)
    {
        const auto fb = sectionAreas[i].toFloat();
        // Panel with a faint wash of the section colour at the top.
        g.setGradientFill (juce::ColourGradient (panelBg().interpolatedWith (sectionAccents[i], 0.07f), 0.0f, fb.getY(),
                                                 panelBg(), 0.0f, fb.getY() + fb.getHeight() * 0.6f, false));
        g.fillRoundedRectangle (fb, 8.0f);
        g.setColour (borderCol());
        g.drawRoundedRectangle (fb.reduced (0.5f), 8.0f, 1.0f);

        // Title with short accent rules either side.
        const auto title = juce::String (sectionNames[i]);
        const auto font = juce::Font (juce::FontOptions (10.5f, juce::Font::bold)).withExtraKerningFactor (0.12f);
        g.setFont (font);
        const float tw = juce::GlyphArrangement::getStringWidth (font, title);
        const float ty = fb.getY() + 13.0f, cx = fb.getCentreX();
        g.setColour (sectionAccents[i].withAlpha (0.95f));
        g.drawText (title, juce::Rectangle<float> (cx - tw * 0.5f - 4.0f, ty - 7.0f, tw + 8.0f, 14.0f), juce::Justification::centred);
        const float rule = juce::jmin (36.0f, (fb.getWidth() - tw) * 0.5f - 18.0f);
        if (rule > 4.0f)
        {
            g.setColour (sectionAccents[i].withAlpha (0.45f));
            g.fillRoundedRectangle (cx - tw * 0.5f - 8.0f - rule, ty - 1.0f, rule, 2.0f, 1.0f);
            g.fillRoundedRectangle (cx + tw * 0.5f + 8.0f, ty - 1.0f, rule, 2.0f, 1.0f);
        }
    }
}

void LatentAudioVSTAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dragHover)
        return;
    g.setColour (accentViolet().withAlpha (0.18f));
    g.fillAll();
    g.setColour (accentViolet());
    g.drawRect (getLocalBounds(), 3);
    g.setColour (textPrimary());
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("Drop to load model", getLocalBounds(), juce::Justification::centred);
}

void LatentAudioVSTAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Header
    headerArea = area.removeFromTop (44);
    {
        auto h = headerArea.reduced (12, 8);
        titleLabel.setBounds (h.removeFromLeft (130));
        unloadButton.setBounds (h.removeFromRight (80));
        h.removeFromRight (6);
        loadButton.setBounds (h.removeFromRight (120));
        h.removeFromRight (10);
        providerLabel.setBounds (h.removeFromRight (110));
        modelLabel.setBounds (h.removeFromLeft (juce::jmin (220, h.getWidth() / 2)));
        statusLabel.setBounds (h);
    }

    area.reduce (10, 8);

    // Toolbar
    {
        auto t = area.removeFromTop (28);
        for (auto* b : { &playOnNoteToggle, &holdToggle, &snapToggle, &keyTrackToggle })
        {
            b->setBounds (t.removeFromLeft (b == &playOnNoteToggle ? 112 : (b == &keyTrackToggle ? 92 : 80)));
            t.removeFromLeft (6);
        }
        t.removeFromLeft (10);
        randomButton.setBounds (t.removeFromLeft (70));
        t.removeFromLeft (6);
        centerButton.setBounds (t.removeFromLeft (70));
        t.removeFromLeft (8);
        midiLed.setBounds (t.removeFromLeft (20));
        resetButton.setBounds (t.removeFromRight (64));
        t.removeFromRight (8);
        coordLabel.setBounds (t.removeFromRight (150));
    }
    area.removeFromTop (8);

    // Controls (bottom), waveform, then the map takes what remains.
    auto controls = area.removeFromBottom (juce::jlimit (150, 200, area.getHeight() / 3));
    area.removeFromBottom (8);
    waveform.setBounds (area.removeFromBottom (juce::jlimit (48, 90, area.getHeight() / 6)));
    area.removeFromBottom (8);
    nebula.setBounds (area);

    // Sections side by side: sound (blend), playback, filter (graph + 2), output.
    int totalUnits = 0;
    for (int u : sectionUnits) totalUnits += u;
    const float unit = (float) controls.getWidth() / (float) totalUnits;
    {
        float x = (float) controls.getX();
        for (int s = 0; s < numSections; ++s)
        {
            const float w = unit * (float) sectionUnits[s];
            sectionAreas[(size_t) s] = juce::Rectangle<int> (juce::roundToInt (x), controls.getY(),
                                                             juce::roundToInt (x + w) - juce::roundToInt (x), controls.getHeight()).reduced (3, 0);
            x += w;
        }
    }

    std::array<juce::Rectangle<int>, numSections> inner;
    for (size_t i = 0; i < inner.size(); ++i)
        inner[i] = sectionAreas[i].reduced (6, 0).withTrimmedTop (26).withTrimmedBottom (6);

    {
        auto modeRow = inner[morphSection].removeFromTop (22).withSizeKeepingCentre (juce::jmin (inner[morphSection].getWidth(), 130), 22);
        areaModeButton.setBounds (modeRow.removeFromLeft (modeRow.getWidth() / 2));
        morphModeButton.setBounds (modeRow);
    }

    filterGraph.setBounds (inner[filterSection].removeFromLeft (inner[filterSection].getWidth() / 4).reduced (2, 14));

    // One cell per knob, then every knob gets the same size (the largest that fits all).
    std::array<int, numSections> counts {};
    for (auto& k : knobs) if (! k->sharesPrevCell) ++counts[(size_t) k->section];
    std::array<int, numSections> placed {};
    std::vector<juce::Rectangle<int>> cells;
    int knobSize = 1000;
    for (auto& k : knobs)
    {
        if (k->sharesPrevCell)
        {
            cells.push_back (cells.back());
            continue;
        }
        const auto s = (size_t) k->section;
        const int width = inner[s].getWidth() / juce::jmax (1, counts[s] - placed[s]);
        cells.push_back (inner[s].removeFromLeft (width));
        ++placed[s];
        knobSize = juce::jmin (knobSize, cells.back().getWidth() - 6, cells.back().getHeight() - 16 - 20);
    }
    knobSize = juce::jlimit (24, 96, knobSize);
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        const auto cell = cells[i];
        const int blockH = 16 + knobSize + 20;
        auto block = cell.withSizeKeepingCentre (cell.getWidth(), blockH);
        k->label.setBounds (block.removeFromTop (16));
        k->slider.setBounds (block.withSizeKeepingCentre (juce::jmax (knobSize, 80), block.getHeight()));
    }
}

//==============================================================================
void LatentAudioVSTAudioProcessorEditor::timerCallback()
{
    using LoadState = LatentAudioVSTAudioProcessor::LoadState;
    const auto status = processor.getLoadStatus();
    auto map = processor.getMapSnapshot();

    // Header
    modelLabel.setText (status.folder != juce::File() ? status.folder.getFileName() : juce::String ("No model"),
                        juce::dontSendNotification);
    modelLabel.setTooltip (status.folder.getFullPathName());
    statusLabel.setText (status.message, juce::dontSendNotification);
    statusLabel.setTooltip (status.message);
    statusLabel.setColour (juce::Label::textColourId, status.state == LoadState::Failed ? errorColour : textSecondary());
    unloadButton.setEnabled (status.state != LoadState::Empty);

    const auto provider = processor.getActiveProvider();
    providerLabel.setText (provider == "None" ? juce::String() : provider, juce::dontSendNotification);
    providerLabel.setColour (juce::Label::textColourId, provider.startsWith ("GPU") ? accentGreen() : textSecondary());

    // Map
    if (map != shownMap)
    {
        shownMap = map;
        nebula.setMap (map);
    }

    if (status.state == LoadState::Loading)
        nebula.setOverlayMessage ("Loading " + status.folder.getFileName() + "...");
    else if (map == nullptr && status.state == LoadState::Failed)
        nebula.setOverlayMessage (status.message, true);
    else if (map == nullptr)
        nebula.setOverlayMessage ("Drop a model folder here\nor click Load Model...");
    else
        nebula.setOverlayMessage ({});

    nebula.setSnapping (snapToggle.getToggleState());
    nebula.setBlend (processor.getAPVTS().getRawParameterValue ("blend")->load());

    // Blend mode: switch state, which knob shows, and B on the map.
    const bool morph = processor.isMorphMode();
    areaModeButton.setToggleState (! morph, juce::dontSendNotification);
    morphModeButton.setToggleState (morph, juce::dontSendNotification);
    if (blendKnob != nullptr && morphKnob != nullptr && morphKnob->slider.isVisible() != morph)
    {
        blendKnob->slider.setVisible (! morph);
        blendKnob->label.setVisible (! morph);
        morphKnob->slider.setVisible (morph);
        morphKnob->label.setVisible (morph);
    }
    nebula.setMorph (morph, processor.getMorphTarget(),
                     processor.getAPVTS().getRawParameterValue ("morph")->load());
    const auto cursor = processor.getCursor();
    nebula.setCursorPosition (cursor.x, cursor.y);

    if (map != nullptr)
    {
        const auto n = LatentAudioVSTAudioProcessor::mapToNormalized (*map, cursor.x, cursor.y);
        coordLabel.setText ("X " + juce::String (n.x, 3) + "   Y " + juce::String (n.y, 3), juce::dontSendNotification);
    }
    else
    {
        coordLabel.setText ({}, juce::dontSendNotification);
    }

    // Waveform
    const auto version = processor.getDisplayVersion();
    if (version != lastDisplayVersion)
    {
        lastDisplayVersion = version;
        std::vector<float> wave;
        processor.copyDisplayWaveform (wave);
        waveform.setWaveform (std::move (wave));
        waveform.setSampleRate (processor.getModelSampleRate());
    }
    auto& params = processor.getAPVTS();
    const float start = params.getRawParameterValue ("sampleStart")->load();
    const float end = juce::jmax (start + 0.01f, params.getRawParameterValue ("sampleLength")->load());
    waveform.setRegion (start, end);
    waveform.setPlaybackPosition (processor.getPlayheadNormalized());

    // MIDI LED stays lit briefly after each event
    const auto midiCount = processor.getMidiActivityCount();
    const auto now = juce::Time::getMillisecondCounter();
    if (midiCount != lastMidiCount)
    {
        lastMidiCount = midiCount;
        midiLitUntil = now + 150;
    }
    midiLed.setLit (now < midiLitUntil);

    if (rootKnob != nullptr)
    {
        // Pitched models play the actual note, so the root note only applies to others.
        const bool on = keyTrackToggle.getToggleState() && ! processor.isPitchedModel();
        rootKnob->slider.setEnabled (on);
        rootKnob->slider.setAlpha (on ? 1.0f : 0.4f);
        rootKnob->label.setAlpha (on ? 1.0f : 0.4f);
    }

    if (filterGraph.needsUpdate())
        filterGraph.repaint();
}
