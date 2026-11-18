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

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
LatentAudioVSTAudioProcessorEditor::LatentAudioVSTAudioProcessorEditor(LatentAudioVSTAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), nebulaComp(p.soundMap), filterGraph(p), midiIndicator(p)
{
    setLookAndFeel(&laf);
    setSize(900, 750);

    // Nebula callbacks
    nebulaComp.onCursorChanged = [this](float x, float y)
        {
            audioProcessor.triggerInference(x, y);
        };

    nebulaComp.onMouseReleased = [this]()
        {
            audioProcessor.releaseNote();
        };
    addAndMakeVisible(nebulaComp);

    nebulaComp.setCursorPosition(audioProcessor.currentX.load(), audioProcessor.currentY.load());

    addAndMakeVisible(waveformComp);
    addAndMakeVisible(filterGraph);
    addAndMakeVisible(midiIndicator);

    // === PLAYBACK Section ===
    rateSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    rateSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    addAndMakeVisible(rateSlider);
    rateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        *audioProcessor.parameters, "playbackRate", rateSlider);
    rateLabel.setText("Rate", juce::dontSendNotification);
    rateLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(rateLabel);

    tuneSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    tuneSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    addAndMakeVisible(tuneSlider);
    tuneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        *audioProcessor.parameters, "fineTune", tuneSlider);
    tuneLabel.setText("Tune", juce::dontSendNotification);
    tuneLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(tuneLabel);

    crossfadeSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    crossfadeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    addAndMakeVisible(crossfadeSlider);
    crossfadeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        *audioProcessor.parameters, "crossfade", crossfadeSlider);
    crossfadeLabel.setText("X-Fade", juce::dontSendNotification);
    crossfadeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(crossfadeLabel);

    // === FILTER Section ===

    // Cutoff
    cutoffSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    cutoffSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    cutoffSlider.setRange(20.0, 20000.0, 1.0);
    cutoffSlider.setSkewFactorFromMidPoint(2000.0);
    cutoffSlider.setValue(10000.0);
    cutoffAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        *audioProcessor.parameters, "filterCutoff", cutoffSlider);
    cutoffSlider.onValueChange = [this] {
        audioProcessor.setFilterCutoff((float)cutoffSlider.getValue());
        filterGraph.repaint();
        };
    addAndMakeVisible(cutoffSlider);
    cutoffLabel.setText("Cutoff", juce::dontSendNotification);
    cutoffLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(cutoffLabel);

    // Resonance
    resoSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    resoSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    resoSlider.setRange(0.0, 1.0, 0.01);
    resoSlider.setValue(0.0);
    resoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        *audioProcessor.parameters, "filterResonance", resoSlider);
    resoSlider.onValueChange = [this] {
        audioProcessor.setFilterResonance((float)resoSlider.getValue());
        filterGraph.repaint();
        };
    addAndMakeVisible(resoSlider);
    resoLabel.setText("Reso", juce::dontSendNotification);
    resoLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(resoLabel);

    // === MASTER Section ===

    // Gain
    gainSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    gainSlider.setRange(0.0, 2.0, 0.01);
    gainSlider.setValue(1.0);
    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        *audioProcessor.parameters, "outputGain", gainSlider);
    addAndMakeVisible(gainSlider);
    gainLabel.setText("Gain", juce::dontSendNotification);
    gainLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(gainLabel);

    // Mix
    mixSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    mixSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    mixSlider.setRange(0.0, 1.0, 0.01);
    mixSlider.setValue(1.0);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        *audioProcessor.parameters, "mixAmount", mixSlider);
    addAndMakeVisible(mixSlider);
    mixLabel.setText("Mix", juce::dontSendNotification);
    mixLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(mixLabel);

    // Length
    lengthSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    lengthSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    lengthSlider.setRange(0.1, 1.0, 0.01);
    lengthSlider.setValue(1.0);
    lengthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        *audioProcessor.parameters, "sampleLength", lengthSlider);
    addAndMakeVisible(lengthSlider);
    lengthLabel.setText("Length", juce::dontSendNotification);
    lengthLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lengthLabel);

    // Start
    startSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    startSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    startSlider.setRange(0.0, 0.9, 0.01);
    startSlider.setValue(0.0);
    startAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        *audioProcessor.parameters, "sampleStart", startSlider);
    addAndMakeVisible(startSlider);
    startLabel.setText("Start", juce::dontSendNotification);
    startLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(startLabel);

    // === TOGGLES & BUTTONS ===

    // Hold
    holdToggle.setButtonText("Hold");
    holdToggle.setTooltip("Loop Mode - Sample loops continuously");
    latchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        *audioProcessor.parameters, "latchMode", holdToggle);
    addAndMakeVisible(holdToggle);

    // Trigger on Note
    triggerToggle.setButtonText("Play on Note");
    triggerToggle.setTooltip("Enable MIDI notes to trigger sound generation/playback");
    triggerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        *audioProcessor.parameters, "triggerOnMidi", triggerToggle);
    addAndMakeVisible(triggerToggle);

    // Snap
    snapToggle.setButtonText("Snap");
    snapToggle.setTooltip("Snap to nearest point");
    snapAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        *audioProcessor.parameters, "snapMode", snapToggle);
    snapToggle.onStateChange = [this] { nebulaComp.setSnapping(snapToggle.getToggleState()); };
    addAndMakeVisible(snapToggle);

    // Random
    randomButton.setButtonText("Random");
    randomButton.setTooltip("Jump to random position");
    randomButton.onClick = [this] {
        audioProcessor.triggerRandomPosition();
        };
    homeButton.setButtonText("Home");
    homeButton.onClick = [this] {
        nebulaComp.resetView();
        // Also reset cursor position to center
        audioProcessor.currentX.store(0.0f);
        audioProcessor.currentY.store(0.0f);
        nebulaComp.setCursorPosition(0.0f, 0.0f);
        };
    resetButton.setButtonText("Reset Settings");
    resetButton.setTooltip("Reset all parameters to defaults");
    resetButton.onClick = [this] {
        resetAllKnobs();
        };

    loadModelFolderButton.setButtonText("Load Model Folder");
    loadModelFolderButton.setTooltip("Select folder containing ONNX model and JSON sound map");
    loadModelFolderButton.addListener(this);

    unloadModelButton.setButtonText("Unload Model");
    unloadModelButton.setTooltip("Unload the currently loaded model from memory");
    unloadModelButton.addListener(this);

    modelFolderPathLabel.setText("No model loaded", juce::dontSendNotification);
    modelFolderPathLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8888aa));
    modelFolderPathLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(randomButton);
    addAndMakeVisible(resetButton);
    addAndMakeVisible(homeButton);
    addAndMakeVisible(loadModelFolderButton);
    addAndMakeVisible(unloadModelButton);
    addAndMakeVisible(modelFolderPathLabel);
    addAndMakeVisible(modelStatusLabel);
    addAndMakeVisible(soundMapStatusLabel);
    addAndMakeVisible(xCoordLabel);
    addAndMakeVisible(yCoordLabel);

    providerLabel.setJustificationType(juce::Justification::centredLeft);
    providerLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8888aa));
    providerLabel.setText("Provider: None", juce::dontSendNotification);
    addAndMakeVisible(providerLabel);

    setResizable(true, true);
    setResizeLimits(700, 500, 1920, 1080);

    startTimer(30);
}

void LatentAudioVSTAudioProcessorEditor::resetAllKnobs()
{
    // PLAYBACK defaults
    rateSlider.setValue(1.0, juce::sendNotificationSync);
    tuneSlider.setValue(0.0, juce::sendNotificationSync);
    crossfadeSlider.setValue(0.05, juce::sendNotificationSync);

    // FILTER defaults
    cutoffSlider.setValue(10000.0, juce::sendNotificationSync);
    resoSlider.setValue(0.0, juce::sendNotificationSync);

    // MASTER defaults
    gainSlider.setValue(1.0, juce::sendNotificationSync);
    mixSlider.setValue(1.0, juce::sendNotificationSync);
    lengthSlider.setValue(1.0, juce::sendNotificationSync);

    // Toggles
    holdToggle.setToggleState(false, juce::sendNotificationSync);
    snapToggle.setToggleState(false, juce::sendNotificationSync);
    triggerToggle.setToggleState(false, juce::sendNotificationSync);
}

LatentAudioVSTAudioProcessorEditor::~LatentAudioVSTAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

//==============================================================================
void LatentAudioVSTAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Deep dark background
    g.fillAll(juce::Colour(0xff0d0d1a));

    // Very subtle dot-grid texture over the top (toggle/model/coord rows)
    {
        auto area    = getLocalBounds().reduced(10);
        auto ctrlH   = 400;
        auto waveH   = 80;
        auto topArea = juce::Rectangle<int>(area.getX(), area.getY(),
                                            area.getWidth(),
                                            area.getHeight() - ctrlH - waveH);
        g.setColour(juce::Colour(0xff141428));
        g.fillRect(topArea);
        g.setColour(juce::Colour(0xff1a1a30));
        for (int gx = topArea.getX(); gx < topArea.getRight(); gx += 24)
            for (int gy = topArea.getY(); gy < topArea.getBottom(); gy += 24)
                g.fillRect(gx, gy, 1, 1);
        g.setColour(juce::Colour(0xff2a2a4a));
        g.drawRect(topArea, 1);
    }

    // Waveform background strip
    {
        auto area      = getLocalBounds().reduced(10);
        auto ctrlArea  = area.removeFromBottom(400);
        auto waveArea  = area.removeFromBottom(80);
        g.setColour(juce::Colour(0xff0d0d1a));
        g.fillRect(waveArea);
    }

    // Section panels
    {
        auto area         = getLocalBounds().reduced(10);
        auto controlsArea = area.removeFromBottom(400);
        int  sectionWidth = controlsArea.getWidth() / 3;

        struct SectionInfo { const char* name; juce::Colour accent; };
        SectionInfo sections[] = {
            { "PLAYBACK", juce::Colour(0xff7b50f0) },
            { "FILTER",   juce::Colour(0xff38bdf8) },
            { "MASTER",   juce::Colour(0xfff06080) },
        };

        auto secArea = controlsArea;
        for (int i = 0; i < 3; ++i)
        {
            auto sec = (i < 2) ? secArea.removeFromLeft(sectionWidth) : secArea;
            auto fb  = sec.toFloat().reduced(2.0f);

            // Panel fill
            g.setColour(juce::Colour(0xff141428));
            g.fillRoundedRectangle(fb, 8.0f);

            // Top accent stripe
            juce::Rectangle<float> bar(fb.getX() + 8.0f, fb.getY() + 4.0f,
                                       fb.getWidth() - 16.0f, 2.5f);
            g.setColour(sections[i].accent);
            g.fillRoundedRectangle(bar, 1.5f);

            // Panel border
            g.setColour(juce::Colour(0xff2a2a4a));
            g.drawRoundedRectangle(fb.reduced(0.5f), 8.0f, 1.0f);

            // Section title
            g.setFont(juce::Font(9.5f, juce::Font::bold));
            g.setColour(sections[i].accent.withAlpha(0.85f));
            g.drawText(sections[i].name, sec.reduced(5, 12),
                       juce::Justification::centredTop, true);
        }
    }
}

void LatentAudioVSTAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(10);

    // Bottom controls (larger now)
    auto controlsArea = area.removeFromBottom(400);

    // Waveform above controls
    auto waveformArea = area.removeFromBottom(80);

    // Nebula takes remaining space
    auto nebulaArea = area.withTrimmedBottom(10);

    // Add toggle buttons above nebula
    auto toggleArea = nebulaArea.removeFromTop(30);
    int toggleWidth = 80;
    int spacing = 10;
    holdToggle.setBounds(toggleArea.removeFromLeft(toggleWidth));
    toggleArea.removeFromLeft(spacing);
    snapToggle.setBounds(toggleArea.removeFromLeft(toggleWidth));
    toggleArea.removeFromLeft(spacing);
    triggerToggle.setBounds(toggleArea.removeFromLeft(toggleWidth));
    toggleArea.removeFromLeft(spacing);
    randomButton.setBounds(toggleArea.removeFromLeft(toggleWidth));
    toggleArea.removeFromLeft(spacing);
    homeButton.setBounds(toggleArea.removeFromLeft(toggleWidth));
    toggleArea.removeFromLeft(spacing);
    midiIndicator.setBounds(toggleArea.removeFromLeft(20));

    // Add model loading buttons and status labels below toggles
    auto modelArea = nebulaArea.removeFromTop(50);
    int buttonWidth = 110;  // Made smaller to fit both buttons
    int labelWidth = 180;
    loadModelFolderButton.setBounds(modelArea.removeFromLeft(buttonWidth));
    modelArea.removeFromLeft(spacing);
    unloadModelButton.setBounds(modelArea.removeFromLeft(buttonWidth));
    modelArea.removeFromLeft(spacing);

    // Add path label taking remaining space
    modelFolderPathLabel.setBounds(modelArea.removeFromLeft(modelArea.getWidth() - labelWidth * 2 - spacing * 2));
    modelArea.removeFromLeft(spacing);

    modelStatusLabel.setBounds(modelArea.removeFromLeft(labelWidth));
    modelArea.removeFromLeft(spacing);
    soundMapStatusLabel.setBounds(modelArea);

    // Add coordinate display below model area
    auto coordArea = nebulaArea.removeFromTop(30);
    int coordWidth = 100;
    xCoordLabel.setBounds(coordArea.removeFromLeft(coordWidth));
    coordArea.removeFromLeft(spacing);
    yCoordLabel.setBounds(coordArea.removeFromLeft(coordWidth));
    coordArea.removeFromLeft(spacing);
    providerLabel.setBounds(coordArea.removeFromLeft(130));

    // Position reset button in upper right corner
    int resetButtonWidth = 120;
    int resetButtonHeight = 25;
    resetButton.setBounds(getWidth() - resetButtonWidth - 10, 10, resetButtonWidth, resetButtonHeight);

    nebulaComp.setBounds(nebulaArea);
    waveformComp.setBounds(waveformArea);

    // Layout controls in 3 sections
    int sectionWidth = controlsArea.getWidth() / 3;
    int labelHeight = 20;

    // === PLAYBACK Section (3 vertical knobs: Rate, Tune, X-Fade) ===
    auto playbackSection = controlsArea.removeFromLeft(sectionWidth).reduced(5);
    playbackSection.removeFromTop(20); // Space for section label

    int pbItemHeight = playbackSection.getHeight() / 3;

    auto rateArea = playbackSection.removeFromTop(pbItemHeight);
    rateLabel.setBounds(rateArea.removeFromTop(labelHeight));
    rateSlider.setBounds(rateArea.reduced(2));

    auto tuneArea = playbackSection.removeFromTop(pbItemHeight);
    tuneLabel.setBounds(tuneArea.removeFromTop(labelHeight));
    tuneSlider.setBounds(tuneArea.reduced(2));

    auto xfadeArea = playbackSection;
    crossfadeLabel.setBounds(xfadeArea.removeFromTop(labelHeight));
    crossfadeSlider.setBounds(xfadeArea.reduced(2));

    // === FILTER Section ===
    auto filterSection = controlsArea.removeFromLeft(sectionWidth).reduced(5);
    filterSection.removeFromTop(20);

    // Graph at top
    auto graphArea = filterSection.removeFromTop(80);
    filterGraph.setBounds(graphArea.reduced(5));

    int filterItems = 2;
    int filterItemHeight = filterSection.getHeight() / filterItems;

    // Cutoff
    auto cutoffArea = filterSection.removeFromTop(filterItemHeight);
    cutoffLabel.setBounds(cutoffArea.removeFromTop(labelHeight));
    cutoffSlider.setBounds(cutoffArea.reduced(2));

    // Reso
    auto resoArea = filterSection.removeFromTop(filterItemHeight);
    resoLabel.setBounds(resoArea.removeFromTop(labelHeight));
    resoSlider.setBounds(resoArea.reduced(2));

    // === MASTER Section ===
    auto masterSection = controlsArea.reduced(5);
    masterSection.removeFromTop(20);

    // 2x2 Grid for master controls (like envelope and morph)
    int masterRowHeight = masterSection.getHeight() / 2;
    int masterColWidth = masterSection.getWidth() / 2;

    auto masterRow1 = masterSection.removeFromTop(masterRowHeight);
    auto masterRow2 = masterSection;

    // Gain (Row 1 Col 1)
    auto gainArea = masterRow1.removeFromLeft(masterColWidth);
    gainLabel.setBounds(gainArea.removeFromTop(labelHeight));
    gainSlider.setBounds(gainArea.reduced(2));

    // Mix (Row 1 Col 2)
    auto mixArea = masterRow1;
    mixLabel.setBounds(mixArea.removeFromTop(labelHeight));
    mixSlider.setBounds(mixArea.reduced(2));

    // Start (Row 2 Col 1)
    auto startArea = masterRow2.removeFromLeft(masterColWidth);
    startLabel.setBounds(startArea.removeFromTop(labelHeight));
    startSlider.setBounds(startArea.reduced(2));

    // Length (Row 2 Col 2)
    auto lengthArea = masterRow2;
    lengthLabel.setBounds(lengthArea.removeFromTop(labelHeight));
    lengthSlider.setBounds(lengthArea.reduced(2));
}

void LatentAudioVSTAudioProcessorEditor::timerCallback()
{
    bool needsRepaint = false;

    if (audioProcessor.newWaveformAvailable.load())
    {
        waveformComp.setWaveform(audioProcessor.getGeneratedWaveform());
        audioProcessor.newWaveformAvailable.store(false);

        nebulaComp.setCursorPosition(audioProcessor.currentX.load(), audioProcessor.currentY.load());
        needsRepaint = true;
    }

    // Check for cursor position changes (parameters control cursor)
    static float lastCursorX = -999.0f;
    static float lastCursorY = -999.0f;

    float cursorX = audioProcessor.currentX.load();
    float cursorY = audioProcessor.currentY.load();

    if (std::abs(cursorX - lastCursorX) > 0.001f ||
        std::abs(cursorY - lastCursorY) > 0.001f)
    {
        nebulaComp.setCursorPosition(cursorX, cursorY);
        lastCursorX = cursorX;
        lastCursorY = cursorY;
        needsRepaint = true;
    }

    float pos = audioProcessor.getPlaybackPositionNormalized();
    static float lastPos = -2.0f; // Initialize to invalid value

    // FIXED: Only update if position changed significantly
    if (std::abs(pos - lastPos) > 0.001f)
    {
        waveformComp.setPlaybackPosition(pos);
        lastPos = pos;
        needsRepaint = true;
    }

    // FIXED: Cache these values and only update on change
    static float lastLoopLen = -1.0f;
    static float lastSampleStart = -1.0f;

    float loopLen = audioProcessor.sampleLength.load();
    float sampleStart = audioProcessor.sampleStart.load();

    if (loopLen != lastLoopLen || sampleStart != lastSampleStart)
    {
        waveformComp.setLoopLength(loopLen);
        waveformComp.setSampleStart(sampleStart);
        lastLoopLen = loopLen;
        lastSampleStart = sampleStart;
        needsRepaint = true;
    }

    // Update MIDI activity indicator
    static bool lastMidiActive = false;
    bool currentMidiActive = audioProcessor.isMidiActive();
    if (currentMidiActive != lastMidiActive)
    {
        midiIndicator.repaint();
        lastMidiActive = currentMidiActive;
        needsRepaint = true;
    }

    // Update coordinate display
    static float lastX = -999.0f;
    static float lastY = -999.0f;
    float currentX = audioProcessor.currentX.load();
    float currentY = audioProcessor.currentY.load();
    // Calculate normalized coordinates for display
    float minX = audioProcessor.soundMap.getMinX();
    float maxX = audioProcessor.soundMap.getMaxX();
    float minY = audioProcessor.soundMap.getMinY();
    float maxY = audioProcessor.soundMap.getMaxY();
    // Guard against division by zero
    float xRange = maxX - minX;
    float yRange = maxY - minY;
    if (xRange <= 0.0f) xRange = 1.0f;
    if (yRange <= 0.0f) yRange = 1.0f;
    float normalizedX = juce::jlimit(0.0f, 1.0f, (currentX - minX) / xRange);
    float normalizedY = juce::jlimit(0.0f, 1.0f, (maxY - currentY) / yRange);  // Inverted for Y mapping
    if (std::abs(normalizedX - lastX) > 0.001f || std::abs(normalizedY - lastY) > 0.001f)
    {
        xCoordLabel.setText("X: " + juce::String(normalizedX, 3), juce::dontSendNotification);
        yCoordLabel.setText("Y: " + juce::String(normalizedY, 3), juce::dontSendNotification);
        lastX = normalizedX;
        lastY = normalizedY;
    }

    // Update execution provider label (cheap string compare at 30fps)
    static juce::String lastProvider;
    juce::String currentProvider = audioProcessor.decoder.getActiveProvider();
    if (currentProvider != lastProvider)
    {
        providerLabel.setText("Provider: " + currentProvider, juce::dontSendNotification);
        if (currentProvider.startsWith("GPU"))
            providerLabel.setColour(juce::Label::textColourId, juce::Colour(0xff4ade80));
        else
            providerLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8888aa));
        lastProvider = currentProvider;
    }
}

void LatentAudioVSTAudioProcessorEditor::updateUIFromProcessor()
{
    juce::Logger::writeToLog("[updateUIFromProcessor] Called");

    // APVTS handles all parameter updates automatically via attachments!
    // Just need to update non-parameter UI elements

    float x = audioProcessor.currentX.load();
    float y = audioProcessor.currentY.load();

    juce::Logger::writeToLog("[updateUIFromProcessor] Setting cursor to (" + juce::String(x) + ", " + juce::String(y) + ")");
    nebulaComp.setCursorPosition(x, y);

    // Update status labels - clear if not loaded, set if loaded
    if (audioProcessor.decoder.isModelLoaded() && audioProcessor.modelPath.exists())
    {
        modelStatusLabel.setText("Model: " + audioProcessor.modelPath.getFileName(), juce::dontSendNotification);
    }
    else
    {
        modelStatusLabel.setText("No model loaded", juce::dontSendNotification);
    }

    if (!audioProcessor.soundMap.getPoints().empty() && audioProcessor.soundMapPath.exists())
    {
        soundMapStatusLabel.setText("Sound Map: " + audioProcessor.soundMapPath.getFileName(), juce::dontSendNotification);
    }
    else
    {
        soundMapStatusLabel.setText("", juce::dontSendNotification);
    }

    // ADD THIS: Update folder path display
    if (audioProcessor.modelPath.exists() && audioProcessor.soundMapPath.exists())
    {
        juce::File modelFolder = audioProcessor.modelPath.getParentDirectory();
        juce::String folderName = modelFolder.getFileName();

        if (audioProcessor.decoder.isModelLoaded())
        {
            // Model successfully loaded
            modelFolderPathLabel.setText("Folder: " + folderName, juce::dontSendNotification);
            modelFolderPathLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8888aa));
        }
        else
        {
            // Paths exist but model not loaded (needs reload)
            modelFolderPathLabel.setText("You had \"" + folderName + "\" model loaded", juce::dontSendNotification);
            modelFolderPathLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff5a623));
        }

        modelFolderPathLabel.setTooltip(modelFolder.getFullPathName());
    }
    else
    {
        modelFolderPathLabel.setText("No model loaded", juce::dontSendNotification);
        modelFolderPathLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8888aa));
    }

    // Sync provider label
    juce::String prov = audioProcessor.decoder.getActiveProvider();
    providerLabel.setText("Provider: " + prov, juce::dontSendNotification);
    if (prov.startsWith("GPU"))
        providerLabel.setColour(juce::Label::textColourId, juce::Colour(0xff4ade80));
    else
        providerLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8888aa));

    filterGraph.repaint();
}

void LatentAudioVSTAudioProcessorEditor::buttonClicked(juce::Button* button)
{
    if (button == &loadModelFolderButton)
    {
        fileChooser = std::make_unique<juce::FileChooser>("Select a model folder", juce::File{});

        auto folderChooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;

        fileChooser->launchAsync(folderChooserFlags, [this](const juce::FileChooser& fc)
            {
                juce::File chosenFolder = fc.getResult();
                if (chosenFolder == juce::File{})
                    return;

                if (audioProcessor.loadModelFolder(chosenFolder))
                {
                    // Store folder name for display
                    juce::String folderName = chosenFolder.getFileName();
                    modelFolderPathLabel.setText("Folder: " + folderName, juce::dontSendNotification);
                    modelFolderPathLabel.setTooltip(chosenFolder.getFullPathName()); // Full path in tooltip

                    // Update status labels based on what was found
                    juce::Array<juce::File> modelFiles, jsonFiles;
                    chosenFolder.findChildFiles(modelFiles, juce::File::findFiles, false, "*.onnx");
                    chosenFolder.findChildFiles(jsonFiles, juce::File::findFiles, false, "*.json");

                    if (modelFiles.size() > 0)
                        modelStatusLabel.setText("Model: " + modelFiles[0].getFileName(), juce::dontSendNotification);
                    if (jsonFiles.size() > 0)
                        soundMapStatusLabel.setText("Sound Map: " + jsonFiles[0].getFileName(), juce::dontSendNotification);

                    nebulaComp.repaint();
                }
                else
                {
                    modelStatusLabel.setText("No model files found in folder", juce::dontSendNotification);
                }
             });
     }
     else if (button == &unloadModelButton)
     {
         // Get the currently loaded model path
         juce::String currentModelPath = audioProcessor.modelPath.getFullPathName();

         if (currentModelPath.isNotEmpty())
         {
             // Attempt to unload the model
             if (audioProcessor.unloadModel(currentModelPath))
             {
                 // Clear the UI status
                 modelFolderPathLabel.setText("No model loaded", juce::dontSendNotification);
                 modelStatusLabel.setText("Model unloaded", juce::dontSendNotification);
                 soundMapStatusLabel.setText("", juce::dontSendNotification);

                 // Clear stored paths
                 audioProcessor.modelPath = juce::File{};
                 audioProcessor.soundMapPath = juce::File{};

                 nebulaComp.repaint();
             }
             else
             {
                 modelStatusLabel.setText("Failed to unload model", juce::dontSendNotification);
             }
         }
         else
         {
             modelStatusLabel.setText("No model to unload", juce::dontSendNotification);
         }
     }
 }