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
#include <limits>
#include <memory>
#include <vector>

//==============================================================================
struct SoundPoint
{
    float x = 0.0f;
    float y = 0.0f;
    juce::String label;
    std::vector<float> latent;
    float midi = std::numeric_limits<float>::quiet_NaN(); // pitched models: the sound's note
    float brightness = std::numeric_limits<float>::quiet_NaN(); // 0 dark .. 1 bright (optional)
    float length = std::numeric_limits<float>::quiet_NaN();     // 0 short .. 1 long ringing (optional)
};

//==============================================================================
// Values on a regular res x res grid over [minX, maxX] x [minY, maxY], row = y.
// Sampled bicubically (Catmull-Rom), clamped at the edges.
struct MapGrid
{
    int res = 0, dim = 0;
    float minX = -1.0f, maxX = 1.0f, minY = -1.0f, maxY = 1.0f;
    std::vector<float> values; // [y][x][dim]

    bool isValid() const { return res >= 2 && dim > 0 && values.size() == (size_t) res * (size_t) res * (size_t) dim; }
    void sample (float x, float y, float* out) const;

    // Parses {"res", "dim", "min_x", ..., "format": "f16"|"f32", "data": base64}.
    static bool parse (const juce::var& block, MapGrid& grid, juce::String& error);
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

    // Pitch-conditioned models: latent[pitchIndex] = (midi - pitchOffset) / pitchScale.
    // The plugin can overwrite that element to ask the model for a specific note.
    int pitchIndex = -1;
    float pitchOffset = 60.0f, pitchScale = 24.0f;
    bool hasPitch() const { return pitchIndex >= 0 && pitchIndex < latentDim; }
    float latentToMidi (float v) const { return v * pitchScale + pitchOffset; }
    float midiToLatent (float midi) const { return (midi - pitchOffset) / pitchScale; }

    // Smooth latent field (newer maps): every position is a smooth blend that passes
    // through the sounds - no creases where the nearest sounds change.
    MapGrid field;
    bool hasField() const { return field.isValid() && field.dim == latentDim; }

    // Sound descriptors everywhere on the map (brightness, level, length; 0..1),
    // decoded by the model at export - drawn as the sky map's nebula.
    MapGrid sky;

    float rangeX() const { return maxX - minX > 1.0e-6f ? maxX - minX : 1.0f; }
    float rangeY() const { return maxY - minY > 1.0e-6f ? maxY - minY : 1.0f; }

    // The latent at a map position: the smooth field when present, otherwise an
    // inverse-distance-weighted blend of the kNearest points. Exactly on a sound: that sound.
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
