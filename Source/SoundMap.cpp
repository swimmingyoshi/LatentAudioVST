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

#include "SoundMap.h"
#include <algorithm>
#include <cmath>
#include <limits>

//==============================================================================
std::vector<int> SoundMapData::nearestIndices (float x, float y, int kNearest) const
{
    const int n = (int) points.size();
    kNearest = juce::jlimit (0, n, kNearest);

    std::vector<std::pair<float, int>> dist;
    dist.reserve ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const float dx = x - points[(size_t) i].x;
        const float dy = y - points[(size_t) i].y;
        dist.emplace_back (dx * dx + dy * dy, i);
    }

    std::partial_sort (dist.begin(), dist.begin() + kNearest, dist.end());

    std::vector<int> result;
    result.reserve ((size_t) kNearest);
    for (int i = 0; i < kNearest; ++i)
        result.push_back (dist[(size_t) i].second);
    return result;
}

int SoundMapData::nearestIndex (float x, float y) const
{
    auto idx = nearestIndices (x, y, 1);
    return idx.empty() ? -1 : idx.front();
}

std::vector<float> SoundMapData::interpolate (float x, float y, int kNearest) const
{
    std::vector<float> result ((size_t) juce::jmax (0, latentDim), 0.0f);
    if (points.empty())
        return result;

    const auto nearest = nearestIndices (x, y, kNearest);

    // Exactly on a point: return it unblended.
    {
        const auto& p = points[(size_t) nearest.front()];
        const float dx = x - p.x, dy = y - p.y;
        if (dx * dx + dy * dy < 1.0e-10f)
            return p.latent;
    }

    // Inverse distance weighting, power 2 (weight = 1 / distSq).
    float totalWeight = 0.0f;
    std::vector<float> weights;
    weights.reserve (nearest.size());
    for (int idx : nearest)
    {
        const auto& p = points[(size_t) idx];
        const float dx = x - p.x, dy = y - p.y;
        const float w = 1.0f / (dx * dx + dy * dy);
        weights.push_back (w);
        totalWeight += w;
    }

    for (size_t i = 0; i < nearest.size(); ++i)
    {
        const float w = weights[i] / totalWeight;
        const auto& latent = points[(size_t) nearest[i]].latent;
        for (size_t d = 0; d < result.size(); ++d)
            result[d] += latent[d] * w;
    }
    return result;
}

//==============================================================================
std::shared_ptr<const SoundMapData> SoundMapData::parse (const juce::String& json, juce::String& error)
{
    juce::var root;
    auto parseResult = juce::JSON::parse (json, root);
    if (parseResult.failed())
    {
        error = "Sound map is not valid JSON: " + parseResult.getErrorMessage();
        return nullptr;
    }

    auto map = std::make_shared<SoundMapData>();
    map->latentDim  = (int) root.getProperty ("latent_dim", 0);
    map->sampleRate = (int) root.getProperty ("sample_rate", 44100);

    if (map->latentDim <= 0)
    {
        error = "Sound map has no valid \"latent_dim\"";
        return nullptr;
    }
    if (map->sampleRate < 8000 || map->sampleRate > 384000)
    {
        error = "Sound map has an invalid \"sample_rate\": " + juce::String (map->sampleRate);
        return nullptr;
    }

    if (auto* pitch = root.getProperty ("pitch", {}).getDynamicObject())
    {
        const int index = (int) pitch->getProperty ("index");
        const float scale = (float) (double) pitch->getProperty ("scale");
        if (index >= 0 && index < map->latentDim && std::abs (scale) > 1.0e-6f)
        {
            map->pitchIndex = index;
            map->pitchOffset = (float) (double) pitch->getProperty ("offset");
            map->pitchScale = scale;
        }
    }

    auto* pointsArray = root.getProperty ("points", {}).getArray();
    if (pointsArray == nullptr)
    {
        error = "Sound map has no \"points\" array";
        return nullptr;
    }

    map->points.reserve ((size_t) pointsArray->size());
    map->minX = map->minY = std::numeric_limits<float>::max();
    map->maxX = map->maxY = std::numeric_limits<float>::lowest();
    int skipped = 0;

    for (const auto& p : *pointsArray)
    {
        SoundPoint sp;
        sp.x = (float) p.getProperty ("x", 0.0);
        sp.y = (float) p.getProperty ("y", 0.0);
        sp.label = p.getProperty ("label", {}).toString();
        if (p.hasProperty ("midi"))
            sp.midi = (float) (double) p.getProperty ("midi", 0.0);
        else if (map->pitchIndex >= 0)
            sp.midi = std::numeric_limits<float>::quiet_NaN(); // filled from the latent below

        if (auto* latent = p.getProperty ("latent", {}).getArray())
        {
            sp.latent.reserve ((size_t) latent->size());
            for (const auto& v : *latent)
                sp.latent.push_back ((float) (double) v);
        }

        const bool finite = std::isfinite (sp.x) && std::isfinite (sp.y)
                         && std::all_of (sp.latent.begin(), sp.latent.end(), [] (float v) { return std::isfinite (v); });

        if (! finite || sp.latent.size() != (size_t) map->latentDim)
        {
            ++skipped;
            continue;
        }

        map->minX = std::min (map->minX, sp.x);
        map->maxX = std::max (map->maxX, sp.x);
        map->minY = std::min (map->minY, sp.y);
        map->maxY = std::max (map->maxY, sp.y);
        if (map->hasPitch() && ! std::isfinite (sp.midi))
            sp.midi = map->latentToMidi (sp.latent[(size_t) map->pitchIndex]);
        map->points.push_back (std::move (sp));
    }

    if (map->points.empty())
    {
        error = "Sound map contains no usable points (" + juce::String (skipped)
              + " skipped: wrong latent size or non-finite values)";
        return nullptr;
    }

    // A single point (or a degenerate axis) still needs a usable viewport.
    if (map->maxX - map->minX < 1.0e-6f) { map->minX -= 1.0f; map->maxX += 1.0f; }
    if (map->maxY - map->minY < 1.0e-6f) { map->minY -= 1.0f; map->maxY += 1.0f; }

    return map;
}

std::shared_ptr<const SoundMapData> SoundMapData::load (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "Sound map not found: " + file.getFullPathName();
        return nullptr;
    }
    return parse (file.loadFileAsString(), error);
}

//==============================================================================
std::shared_ptr<const SoundMapData> SoundMap::snapshot() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return data;
}

void SoundMap::setData (std::shared_ptr<const SoundMapData> newData)
{
    std::shared_ptr<const SoundMapData> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = std::exchange (data, std::move (newData));
    }
    // `old` is released here, outside the lock.
}

bool SoundMap::loadFromFile (const juce::File& file, juce::String* error)
{
    juce::String err;
    auto newData = SoundMapData::load (file, err);
    if (error != nullptr)
        *error = err;
    if (newData == nullptr)
        return false;
    setData (std::move (newData));
    return true;
}
