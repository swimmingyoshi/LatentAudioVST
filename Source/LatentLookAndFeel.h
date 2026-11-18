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
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override
    {
        using namespace LatentPalette;

        auto centreX   = (float)x + (float)width  * 0.5f;
        auto centreY   = (float)y + (float)height * 0.5f;
        auto radius    = juce::jmin(width, height) * 0.5f - 5.0f;
        auto angle     = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        if (radius <= 2.0f) return;

        // --- Background arc track ---
        {
            juce::Path bg;
            bg.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
            g.setColour(borderCol());
            g.strokePath(bg, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        }

        // --- Value arc (violet → cyan gradient) ---
        {
            juce::Path val;
            val.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                              rotaryStartAngle, angle, true);
            juce::ColourGradient grad(accentViolet(), centreX - radius, centreY,
                                      accentCyan(),   centreX + radius, centreY, false);
            g.setGradientFill(grad);
            g.strokePath(val, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }

        // --- Inner circle ---
        auto innerR = radius * 0.58f;
        g.setColour(panelBg());
        g.fillEllipse(centreX - innerR, centreY - innerR, innerR * 2.0f, innerR * 2.0f);
        g.setColour(borderCol());
        g.drawEllipse(centreX - innerR, centreY - innerR, innerR * 2.0f, innerR * 2.0f, 1.0f);

        // --- Indicator line + tip dot ---
        {
            auto tipR  = radius * 0.76f;
            auto baseR = innerR * 0.32f;
            juce::Point<float> tip (centreX + std::sin(angle) * tipR,
                                    centreY - std::cos(angle) * tipR);
            juce::Point<float> base(centreX + std::sin(angle) * baseR,
                                    centreY - std::cos(angle) * baseR);

            g.setColour(textPrimary().withAlpha(0.85f));
            g.drawLine(base.x, base.y, tip.x, tip.y, 1.8f);

            g.setColour(accentCyan());
            g.fillEllipse(tip.x - 2.5f, tip.y - 2.5f, 5.0f, 5.0f);
        }
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

        g.setColour(isOn ? textPrimary() : textSecondary());
        g.setFont(juce::FontOptions(12.5f));
        g.drawFittedText(button.getButtonText(), button.getLocalBounds(),
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
