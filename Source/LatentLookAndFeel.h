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

//==============================================================================
// Palette constants (hex ARGB)
namespace LatentPalette
{
    static juce::Colour darkBg()        { return juce::Colour(0xff0d0d1a); }
    static juce::Colour panelBg()       { return juce::Colour(0xff141428); }
    static juce::Colour borderCol()     { return juce::Colour(0xff2a2a4a); }
    static juce::Colour accentViolet()  { return juce::Colour(0xff7b50f0); }
    static juce::Colour accentCyan()    { return juce::Colour(0xff38bdf8); }
    static juce::Colour accentGreen()   { return juce::Colour(0xff4ade80); }
    static juce::Colour accentAmber()   { return juce::Colour(0xfff5a623); }
    static juce::Colour textPrimary()   { return juce::Colour(0xffe0e0ff); }
    static juce::Colour textSecondary() { return juce::Colour(0xff8888aa); }
}

//==============================================================================
class LatentLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LatentLookAndFeel()
    {
        using namespace LatentPalette;

        setColour(juce::ResizableWindow::backgroundColourId, darkBg());

        // Sliders
        setColour(juce::Slider::rotarySliderFillColourId,    accentViolet());
        setColour(juce::Slider::rotarySliderOutlineColourId, borderCol());
        setColour(juce::Slider::thumbColourId,               accentCyan());
        setColour(juce::Slider::textBoxTextColourId,         textSecondary());
        setColour(juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId,    accentViolet().withAlpha(0.4f));

        // Labels
        setColour(juce::Label::textColourId,                 textSecondary());
        setColour(juce::Label::backgroundColourId,           juce::Colours::transparentBlack);
        setColour(juce::Label::outlineColourId,              juce::Colours::transparentBlack);

        // TextButton
        setColour(juce::TextButton::buttonColourId,          panelBg());
        setColour(juce::TextButton::buttonOnColourId,        accentViolet().withAlpha(0.4f));
        setColour(juce::TextButton::textColourOffId,         textPrimary());
        setColour(juce::TextButton::textColourOnId,          accentCyan());

        // ToggleButton
        setColour(juce::ToggleButton::textColourId,          textSecondary());
        setColour(juce::ToggleButton::tickColourId,          accentViolet());
        setColour(juce::ToggleButton::tickDisabledColourId,  borderCol());

        // PopupMenu
        setColour(juce::PopupMenu::backgroundColourId,       panelBg());
        setColour(juce::PopupMenu::textColourId,             textPrimary());
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accentViolet().withAlpha(0.5f));
        setColour(juce::PopupMenu::highlightedTextColourId,  textPrimary());

        // ScrollBar
        setColour(juce::ScrollBar::thumbColourId,            borderCol());
    }

    //==========================================================================
    // Knob: thick arc in the slider's accent colour (with a soft glow), a raised
    // body and a pointer. Brightens on hover/drag. "bipolar" property: arc from 12 o'clock.
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override
    {
        using namespace LatentPalette;

        const auto area   = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height).reduced(2.0f);
        const auto centre = area.getCentre();
        const float radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.5f - 2.0f;
        if (radius < 8.0f) return;

        const bool enabled = slider.isEnabled();
        const bool hot     = enabled && slider.isMouseOverOrDragging();
        const auto accent  = slider.findColour(juce::Slider::rotarySliderFillColourId);
        const float track  = juce::jlimit(3.0f, 6.5f, radius * 0.14f);
        const float angle  = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
        const bool bipolar = (bool) slider.getProperties()["bipolar"];
        const float from   = bipolar ? (rotaryStartAngle + rotaryEndAngle) * 0.5f : rotaryStartAngle;
        const juce::PathStrokeType stroke(track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        // Track
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(0xff23233f));
        g.strokePath(arc, stroke);

        // Value arc + glow
        if (std::abs(angle - from) > 0.01f)
        {
            juce::Path val;
            val.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                              juce::jmin(from, angle), juce::jmax(from, angle), true);
            g.setColour(accent.withAlpha(hot ? 0.30f : 0.16f));
            g.strokePath(val, juce::PathStrokeType(track * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(hot ? accent.brighter(0.25f) : accent);
            g.strokePath(val, stroke);
        }

        // Body: shadow, top-lit gradient, rim
        const float bodyR = radius - track * 1.8f;
        const auto body = juce::Rectangle<float>(bodyR * 2.0f, bodyR * 2.0f).withCentre(centre);
        g.setColour(juce::Colours::black.withAlpha(0.40f));
        g.fillEllipse(body.translated(0.0f, 2.0f).expanded(1.0f));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2e2e56), centre.x, body.getY(),
                                               juce::Colour(0xff15152b), centre.x, body.getBottom(), false));
        g.fillEllipse(body);
        g.setColour(hot ? accent.withAlpha(0.75f) : juce::Colour(0xff3a3a64));
        g.drawEllipse(body.reduced(0.5f), 1.0f);

        // Pointer
        const juce::Point<float> dir(std::sin(angle), -std::cos(angle));
        juce::Path pointer;
        pointer.startNewSubPath(centre + dir * (bodyR * 0.25f));
        pointer.lineTo(centre + dir * (bodyR * 0.80f));
        g.setColour(enabled ? textPrimary() : textSecondary());
        g.strokePath(pointer, juce::PathStrokeType(juce::jlimit(2.0f, 3.5f, bodyR * 0.11f),
                                                   juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* l = juce::LookAndFeel_V4::createSliderTextBox(slider);
        l->setFont(juce::FontOptions(12.0f));
        l->setColour(juce::Label::textColourId, LatentPalette::textPrimary().withAlpha(0.85f));
        l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        return l;
    }

    //==========================================================================
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour&, bool isHighlighted, bool isDown) override
    {
        using namespace LatentPalette;

        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto corner = 5.0f;

        juce::Colour fill   = panelBg();
        juce::Colour border = borderCol();

        if (isDown)             { fill = accentViolet().withAlpha(0.45f); border = accentViolet(); }
        else if (isHighlighted) { border = accentViolet().withAlpha(0.7f); }

        g.setColour(fill);
        g.fillRoundedRectangle(bounds, corner);
        g.setColour(border);
        g.drawRoundedRectangle(bounds, corner, 1.0f);
    }

    //==========================================================================
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool isHighlighted, bool /*isDown*/) override
    {
        using namespace LatentPalette;

        auto bounds     = button.getLocalBounds().toFloat();
        auto cornerSize = bounds.getHeight() * 0.35f;
        bool isOn       = button.getToggleState();

        juce::Colour fill   = isOn ? accentViolet().withAlpha(0.45f) : panelBg();
        juce::Colour border = isOn ? accentViolet() : borderCol();
        if (isHighlighted) border = border.brighter(0.35f);

        g.setColour(fill);
        g.fillRoundedRectangle(bounds, cornerSize);
        g.setColour(border);
        g.drawRoundedRectangle(bounds.reduced(0.5f), cornerSize, 1.0f);

        // Status light on the left
        auto textArea = button.getLocalBounds();
        if (bounds.getWidth() > 60.0f)
        {
            const auto led = juce::Rectangle<float>(7.0f, 7.0f).withCentre({ bounds.getX() + 13.0f, bounds.getCentreY() });
            if (isOn)
            {
                g.setColour(accentCyan().withAlpha(0.30f));
                g.fillEllipse(led.expanded(3.0f));
            }
            g.setColour(isOn ? accentCyan() : juce::Colour(0xff2c2c4c));
            g.fillEllipse(led);
            textArea.removeFromLeft(14);
        }

        g.setColour(isOn ? textPrimary() : textSecondary());
        g.setFont(juce::FontOptions(12.5f));
        g.drawFittedText(button.getButtonText(), textArea,
                         juce::Justification::centred, 1);
    }

    //==========================================================================
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override
    {
        return juce::Font(juce::FontOptions(juce::jmin(13.0f, (float)buttonHeight * 0.6f)));
    }

    juce::Font getLabelFont(juce::Label& label) override
    {
        return juce::Font(juce::FontOptions(11.5f));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LatentLookAndFeel)
};
