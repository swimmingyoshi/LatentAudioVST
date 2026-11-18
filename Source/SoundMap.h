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
#include <memory>
#include <vector>

//==============================================================================
struct SoundPoint
{
    float x = 0.0f;
    float y = 0.0f;
    juce::String label;
    std::vector<float> latent;
};

//==============================================================================
// Immutable once built. Shared between threads via std::shared_ptr<const ...>.
struct SoundMapData
{
    std::vector<SoundPoint> points;
    int latentDim = 0;
    int sampleRate = 44100;
    float minX = 0.0f, maxX = 0.0f;
    float minY = 0.0f, maxY = 0.0f;

    float rangeX() const { return maxX - minX > 1.0e-6f ? maxX - minX : 1.0f; }
    float rangeY() const { return maxY - minY > 1.0e-6f ? maxY - minY : 1.0f; }

    // Inverse-distance-weighted blend of the kNearest points' latents.
    std::vector<float> interpolate (float x, float y, int kNearest = 5) const;

    // Indices of the kNearest points, closest first.
    std::vector<int> nearestIndices (float x, float y, int kNearest) const;

    // Index of the closest point, or -1 if the map is empty.
    int nearestIndex (float x, float y) const;

    // Parses and validates a sound_map.json. Returns nullptr and fills `error` on failure.
    static std::shared_ptr<const SoundMapData> parse (const juce::String& json, juce::String& error);
    static std::shared_ptr<const SoundMapData> load (const juce::File& file, juce::String& error);
};

//==============================================================================
// Thread-safe holder of the current map. Readers take a snapshot; loading builds
// a new SoundMapData and swaps it in, so readers never see a half-loaded map.
// The swap uses a SpinLock that is held only for a pointer copy.
class SoundMap
{
public:
    std::shared_ptr<const SoundMapData> snapshot() const;
    void setData (std::shared_ptr<const SoundMapData> newData);
    void clear() { setData (nullptr); }

    // Convenience: parse + swap. On failure the current map is left untouched.
    bool loadFromFile (const juce::File& file, juce::String* error = nullptr);

    bool isLoaded() const { auto d = snapshot(); return d != nullptr && ! d->points.empty(); }

private:
    mutable juce::SpinLock lock;
    std::shared_ptr<const SoundMapData> data;
};
