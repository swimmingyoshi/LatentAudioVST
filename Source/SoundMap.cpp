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
#include <cstring>
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

namespace
{
    float halfToFloat (uint16_t h)
    {
        const uint32_t sign = (uint32_t) (h & 0x8000u) << 16;
        uint32_t exp = (h >> 10) & 0x1fu;
        uint32_t mant = h & 0x3ffu;
        uint32_t bits;
        if (exp == 0)
        {
            if (mant == 0)
                bits = sign;
            else
            {
                exp = 127 - 15 + 1;
                while ((mant & 0x400u) == 0) { mant <<= 1; --exp; }
                mant &= 0x3ffu;
                bits = sign | (exp << 23) | (mant << 13);
            }
        }
        else if (exp == 31)
            bits = sign | 0x7f800000u | (mant << 13);
        else
            bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
        float f;
        std::memcpy (&f, &bits, sizeof (f));
        return f;
    }

    inline void catmullRom (float t, float w[4])
    {
        const float t2 = t * t, t3 = t2 * t;
        w[0] = -0.5f * t3 + t2 - 0.5f * t;
        w[1] = 1.5f * t3 - 2.5f * t2 + 1.0f;
        w[2] = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
        w[3] = 0.5f * t3 - 0.5f * t2;
    }
}

void MapGrid::sample (float x, float y, float* out) const
{
    std::fill (out, out + dim, 0.0f);
    if (! isValid())
        return;
    const float last = (float) (res - 1);
    const float u = juce::jlimit (0.0f, last, (x - minX) / std::max (1.0e-9f, maxX - minX) * last);
    const float v = juce::jlimit (0.0f, last, (y - minY) / std::max (1.0e-9f, maxY - minY) * last);
    const int iu = std::min ((int) u, res - 1), iv = std::min ((int) v, res - 1);
    float wu[4], wv[4];
    catmullRom (u - (float) iu, wu);
    catmullRom (v - (float) iv, wv);
    for (int a = 0; a < 4; ++a)
    {
        const int row = juce::jlimit (0, res - 1, iv - 1 + a);
        for (int b = 0; b < 4; ++b)
        {
            const int col = juce::jlimit (0, res - 1, iu - 1 + b);
            const float w = wv[a] * wu[b];
            const float* cell = values.data() + ((size_t) row * (size_t) res + (size_t) col) * (size_t) dim;
            for (int d = 0; d < dim; ++d)
                out[d] += w * cell[d];
        }
    }
}

bool MapGrid::parse (const juce::var& block, MapGrid& grid, juce::String& error)
{
    grid = {};
    const int res = (int) block.getProperty ("res", 0);
    const int dim = (int) block.getProperty ("dim", 0);
    if (res < 2 || res > 1024 || dim <= 0 || dim > 4096)
    {
        error = "invalid grid size";
        return false;
    }
    // Standard base64 (juce::MemoryBlock's own base64 format is different).
    juce::MemoryOutputStream decoded;
    if (! juce::Base64::convertFromBase64 (decoded, block.getProperty ("data", {}).toString()))
    {
        error = "grid data is not base64";
        return false;
    }
    const auto raw = decoded.getMemoryBlock();
    const bool f16 = block.getProperty ("format", "f32").toString() == "f16";
    const size_t count = (size_t) res * (size_t) res * (size_t) dim;
    if (raw.getSize() != count * (f16 ? 2u : 4u))
    {
        error = "grid data has the wrong size";
        return false;
    }
    grid.values.resize (count);
    const auto* bytes = static_cast<const uint8_t*> (raw.getData());
    for (size_t i = 0; i < count; ++i)
    {
        float v;
        if (f16)
            v = halfToFloat ((uint16_t) (bytes[2 * i] | (bytes[2 * i + 1] << 8)));
        else
        {
            uint32_t bits = (uint32_t) bytes[4 * i] | ((uint32_t) bytes[4 * i + 1] << 8)
                          | ((uint32_t) bytes[4 * i + 2] << 16) | ((uint32_t) bytes[4 * i + 3] << 24);
            std::memcpy (&v, &bits, sizeof (v));
        }
        if (! std::isfinite (v))
        {
            error = "grid has non-finite values";
            grid = {};
            return false;
        }
        grid.values[i] = v;
    }
    grid.res = res;
    grid.dim = dim;
    grid.minX = (float) (double) block.getProperty ("min_x", -1.0);
    grid.maxX = (float) (double) block.getProperty ("max_x", 1.0);
    grid.minY = (float) (double) block.getProperty ("min_y", -1.0);
    grid.maxY = (float) (double) block.getProperty ("max_y", 1.0);
    if (! (grid.maxX > grid.minX && grid.maxY > grid.minY))
    {
        error = "grid has empty bounds";
        grid = {};
        return false;
    }
    return true;
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

    if (hasField())
    {
        field.sample (x, y, result.data());
        return result;
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
        if (p.hasProperty ("brightness"))
            sp.brightness = juce::jlimit (0.0f, 1.0f, (float) (double) p.getProperty ("brightness", 0.5));
        if (p.hasProperty ("length"))
            sp.length = juce::jlimit (0.0f, 1.0f, (float) (double) p.getProperty ("length", 0.5));
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

    // Optional grids: a broken one is ignored (the map still works without it).
    for (auto [name, grid] : { std::pair<const char*, MapGrid*> { "field", &map->field },
                               std::pair<const char*, MapGrid*> { "sky", &map->sky } })
    {
        const auto block = root.getProperty (name, {});
        juce::String gridError;
        if (block.isObject() && ! MapGrid::parse (block, *grid, gridError))
            DBG ("Sound map: ignoring \"" << name << "\": " << gridError);
    }
    if (map->field.isValid() && map->field.dim != map->latentDim)
        map->field = {};

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
