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
#include "LatentLookAndFeel.h"

WaveformComponent::WaveformComponent()
{
    setTooltip ("Drag onto a track in your DAW to use this sound (with Rate, Tune, Start/End, Filter and Gain "
                "applied). Right-click to save it as a WAV.");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

juce::File WaveformComponent::exportFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("LatentAudio").getChildFile ("Exports");
}

bool WaveformComponent::writeWav (const juce::File& file, const std::vector<float>& audio, int rate,
                                  float start, float end, juce::String& error)
{
    const int n = (int) audio.size();
    const int from = juce::jlimit (0, n, (int) std::floor (juce::jlimit (0.0f, 1.0f, start) * (float) n));
    const int to = juce::jlimit (from, n, (int) std::ceil (juce::jlimit (0.0f, 1.0f, end) * (float) n));
    if (to - from < 4)
    {
        error = "No sound to export";
        return false;
    }

    const auto dir = file.getParentDirectory();
    if (! dir.createDirectory())
    {
        error = "Cannot create " + dir.getFullPathName();
        return false;
    }
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk())
    {
        error = "Cannot write " + file.getFullPathName();
        return false;
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), (double) rate, 1, 24, {}, 0));
    if (writer == nullptr)
    {
        error = "WAV writer failed";
        return false;
    }
    stream.release(); // owned by the writer now

    // Short fades so a trimmed region does not click.
    std::vector<float> region (audio.begin() + from, audio.begin() + to);
    const int len = (int) region.size();
    const int fadeIn = from > 0 ? std::min (64, len / 4) : 0;
    const int fadeOut = to < n ? std::min (256, len / 4) : 0;
    for (int i = 0; i < fadeIn; ++i)
        region[(size_t) i] *= (float) i / (float) fadeIn;
    for (int i = 0; i < fadeOut; ++i)
        region[(size_t) (len - 1 - i)] *= (float) i / (float) fadeOut;

    const float* channels[] = { region.data() };
    if (! writer->writeFromFloatArrays (channels, 1, len))
    {
        error = "Write failed";
        return false;
    }
    return true;
}

bool WaveformComponent::writeExport (const juce::File& file, juce::String& error) const
{
    if (renderExport != nullptr)
        return writeWav (file, renderExport(), sampleRate, 0.0f, 1.0f, error);
    return writeWav (file, waveform, sampleRate, regionStart, regionEnd, error);
}

juce::File WaveformComponent::exportToFolder (juce::String& error) const
{
    auto stem = exportName != nullptr ? exportName() : juce::String();
    stem = juce::File::createLegalFileName (stem.trim()).replaceCharacter (' ', '_');
    if (stem.isEmpty())
        stem = "sound";
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
    const auto file = exportFolder().getNonexistentChildFile ("LatentAudio_" + stem + "_" + stamp, ".wav", false);
    if (! writeExport (file, error))
        return {};
    return file;
}

void WaveformComponent::mouseDown (const juce::MouseEvent& e)
{
    dragStarted = false;
    if (! e.mods.isPopupMenu() || waveform.empty())
        return;

    juce::PopupMenu menu;
    menu.addItem (1, "Save as WAV...");
    menu.addItem (2, "Save to exports folder");
    menu.addItem (3, "Open exports folder");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [safe = juce::Component::SafePointer<WaveformComponent> (this)] (int choice)
        {
            auto* self = safe.getComponent();
            if (self == nullptr || choice == 0)
                return;
            juce::String error;
            if (choice == 2)
            {
                const auto f = self->exportToFolder (error);
                if (f != juce::File())
                    f.revealToUser();
            }
            else if (choice == 3)
            {
                exportFolder().createDirectory();
                exportFolder().startAsProcess();
            }
            else
            {
                auto stem = self->exportName != nullptr ? self->exportName() : juce::String ("sound");
                self->chooser = std::make_unique<juce::FileChooser> (
                    "Save sound as WAV",
                    exportFolder().getChildFile (juce::File::createLegalFileName (stem) + ".wav"), "*.wav");
                self->chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                    [safe] (const juce::FileChooser& fc)
                    {
                        auto* me = safe.getComponent();
                        const auto target = fc.getResult();
                        if (me == nullptr || target == juce::File())
                            return;
                        juce::String err;
                        if (! me->writeExport (target.withFileExtension ("wav"), err))
                            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save failed", err);
                    });
            }
            if (error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export failed", error);
        });
}

void WaveformComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.mods.isPopupMenu() || waveform.empty() || e.getDistanceFromDragStart() < 6)
        return;
    dragStarted = true;

    juce::String error;
    const auto file = exportToFolder (error);
    if (file == juce::File())
        return;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void WaveformComponent::setWaveform (std::vector<float> audioData)
{
    waveform = std::move (audioData);
    peaks.clear();
    repaint();
}

void WaveformComponent::setPlaybackPosition (float normalizedPosition)
{
    if (std::abs (normalizedPosition - playbackPos) < 0.0005f)
        return;
    playbackPos = normalizedPosition;
    repaint();
}

void WaveformComponent::setRegion (float normalizedStart, float normalizedEnd)
{
    if (normalizedStart == regionStart && normalizedEnd == regionEnd)
        return;
    regionStart = normalizedStart;
    regionEnd = normalizedEnd;
    repaint();
}

void WaveformComponent::computePeaks (int width)
{
    peaks.assign ((size_t) width, { 0.0f, 0.0f });
    const size_t n = waveform.size();
    if (n == 0 || width <= 0)
        return;

    for (int x = 0; x < width; ++x)
    {
        const size_t from = (size_t) x * n / (size_t) width;
        const size_t to = std::max (from + 1, (size_t) (x + 1) * n / (size_t) width);
        float lo = waveform[from], hi = waveform[from];
        for (size_t i = from; i < to && i < n; ++i)
        {
            lo = std::min (lo, waveform[i]);
            hi = std::max (hi, waveform[i]);
        }
        peaks[(size_t) x] = { lo, hi };
    }
}

void WaveformComponent::paint (juce::Graphics& g)
{
    using namespace LatentPalette;
    const auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth(), h = bounds.getHeight();
    const float mid = h * 0.5f, scale = h * 0.45f;

    g.setColour (darkBg());
    g.fillRect (bounds);

    // Region that actually plays
    const float rx0 = regionStart * w, rx1 = std::max (rx0 + 1.0f, regionEnd * w);
    g.setColour (accentAmber().withAlpha (0.06f));
    g.fillRect (rx0, 0.0f, rx1 - rx0, h);

    g.setColour (juce::Colour (0xff1a1a30));
    g.drawHorizontalLine ((int) mid, 0.0f, w);

    if (waveform.empty())
    {
        g.setColour (textSecondary().withAlpha (0.5f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Generated sound appears here", bounds, juce::Justification::centred);
    }
    else
    {
        if ((int) peaks.size() != (int) w)
            computePeaks ((int) w);

        juce::Path shape;
        shape.startNewSubPath (0.0f, mid - peaks.front().second * scale);
        for (size_t x = 1; x < peaks.size(); ++x)
            shape.lineTo ((float) x, mid - peaks[x].second * scale);
        for (size_t x = peaks.size(); x-- > 0;)
            shape.lineTo ((float) x, mid - peaks[x].first * scale);
        shape.closeSubPath();

        g.setColour (accentCyan().withAlpha (0.55f));
        g.fillPath (shape);
        g.setColour (accentCyan().withAlpha (0.9f));
        g.strokePath (shape, juce::PathStrokeType (0.8f));

        // Dim what lies outside the region
        g.setColour (darkBg().withAlpha (0.55f));
        g.fillRect (0.0f, 0.0f, rx0, h);
        g.fillRect (rx1, 0.0f, w - rx1, h);
    }

    // Region markers
    g.setColour (accentGreen().withAlpha (0.8f));
    g.drawVerticalLine ((int) rx0, 0.0f, h);
    g.setColour (accentAmber().withAlpha (0.8f));
    g.drawVerticalLine ((int) std::min (rx1, w - 1.0f), 0.0f, h);

    if (playbackPos >= 0.0f && playbackPos <= 1.0f)
    {
        const float x = playbackPos * w;
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawLine (x, 0.0f, x, h, 1.5f);
        juce::Path arrow;
        arrow.addTriangle (x - 4.0f, 0.0f, x + 4.0f, 0.0f, x, 5.0f);
        g.fillPath (arrow);
    }

    g.setColour (borderCol());
    g.drawRect (bounds, 1.0f);
}
