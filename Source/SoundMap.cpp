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
#include <limits>
#include <algorithm>
#include <cmath>

//==============================================================================
SoundMap::SoundMap()
{
}

SoundMap::~SoundMap()
{
}

bool SoundMap::loadFromFile (const juce::File& file)
{
    juce::Logger::writeToLog("[SoundMap::loadFromFile] Loading: " + file.getFullPathName());

    if (! file.existsAsFile())
    {
        juce::Logger::writeToLog("[SoundMap::loadFromFile] FAIL: File does not exist!");
        return false;
    }

    auto jsonString = file.loadFileAsString();
    juce::var result;

    if (juce::JSON::parse (jsonString, result).wasOk())
    {
        // Check core properties
        if (result.hasProperty ("latent_dim"))
            latentDim = static_cast<int> (result["latent_dim"]);

        if (result.hasProperty ("sample_rate"))
            sampleRate = static_cast<int> (result["sample_rate"]);

        // Parse points
        if (result.hasProperty ("points") && result["points"].isArray())
        {
            auto* pointsArray = result["points"].getArray();
            points.clear();
            points.reserve (pointsArray->size());

            // Initialize bounds with inverted values
            minX = std::numeric_limits<float>::max();
            maxX = std::numeric_limits<float>::lowest();
            minY = std::numeric_limits<float>::max();
            maxY = std::numeric_limits<float>::lowest();

            for (auto& p : *pointsArray)
            {
                SoundPoint sp;
                sp.x = static_cast<float> (p["x"]);
                sp.y = static_cast<float> (p["y"]);

                // Update bounds
                if (sp.x < minX) minX = sp.x;
                if (sp.x > maxX) maxX = sp.x;
                if (sp.y < minY) minY = sp.y;
                if (sp.y > maxY) maxY = sp.y;

                if (p.hasProperty ("latent") && p["latent"].isArray())
                {
                    auto* vecArray = p["latent"].getArray();
                    sp.latent.reserve (vecArray->size());
                    
                    for (auto& val : *vecArray)
                        sp.latent.push_back (static_cast<float> (val));
                }

                // Verify dimension
                if (sp.latent.size() == static_cast<size_t> (latentDim))
                    points.push_back (sp);
            }
            juce::Logger::writeToLog("[SoundMap::loadFromFile] SUCCESS: Loaded " + juce::String(points.size()) + " points, latentDim=" + juce::String(latentDim));
            return true;
        }
        else
        {
            juce::Logger::writeToLog("[SoundMap::loadFromFile] FAIL: No points array in JSON");
        }
    }
    else
    {
        juce::Logger::writeToLog("[SoundMap::loadFromFile] FAIL: JSON parse failed");
    }

    return false;
}

std::vector<float> SoundMap::getInterpolatedVector (float x, float y, int kNearest) const
{
    // Return empty if no points
    if (points.empty())
        return std::vector<float> (latentDim, 0.0f);

    // Structure to store distance and index
    struct PointDist
    {
        float distSq;
        int index;
    };

    std::vector<PointDist> nearestPoints;
    nearestPoints.reserve (points.size());

    // 1. Calculate squared distances to all points
    for (int i = 0; i < static_cast<int> (points.size()); ++i)
    {
        float dx = x - points[i].x;
        float dy = y - points[i].y;
        float distSq = dx * dx + dy * dy;

        // Optimization: If we hit a point exactly, return its vector immediately
        if (distSq < 1.0e-6f) 
            return points[i].latent;

        nearestPoints.push_back ({ distSq, i });
    }

    // 2. Sort by distance (partial sort is faster)
    kNearest = std::min (kNearest, static_cast<int> (points.size()));
    
    std::partial_sort (nearestPoints.begin(), 
                       nearestPoints.begin() + kNearest, 
                       nearestPoints.end(),
                       [] (const PointDist& a, const PointDist& b) { return a.distSq < b.distSq; });

    // 3. Calculate IDW weights
    std::vector<float> weights;
    weights.reserve (kNearest);
    float totalWeight = 0.0f;
    float p = 2.0f; // Power parameter for IDW (commonly 2)

    for (int i = 0; i < kNearest; ++i)
    {
        float dist = std::sqrt (nearestPoints[i].distSq);
        float weight = 1.0f / std::pow (dist, p);
        weights.push_back (weight);
        totalWeight += weight;
    }

    // 4. Interpolate
    std::vector<float> result (latentDim, 0.0f);

    for (int i = 0; i < kNearest; ++i)
    {
        float normWeight = weights[i] / totalWeight;
        const auto& targetVec = points[nearestPoints[i].index].latent;

        for (int d = 0; d < latentDim; ++d)
            result[d] += targetVec[d] * normWeight;
    }

    return result;
}

const SoundPoint* SoundMap::getNearestPoint (float x, float y) const
{
    if (points.empty()) return nullptr;

    const SoundPoint* nearest = nullptr;
    float minDistSq = std::numeric_limits<float>::max();

    for (const auto& p : points)
    {
        float dx = x - p.x;
        float dy = y - p.y;
        float distSq = dx * dx + dy * dy;

        if (distSq < minDistSq)
        {
            minDistSq = distSq;
            nearest = &p;
        }
    }

    return nearest;
}
