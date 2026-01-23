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
#include <vector>

//==============================================================================
struct SoundPoint
{
    float x = 0.0f;
    float y = 0.0f;
    std::vector<float> latent;
};

//==============================================================================
class SoundMap
{
public:
    SoundMap();
    ~SoundMap();

    // Loads points from the JSON file
    bool loadFromFile (const juce::File& file);

    // Calculates the interpolated 128D latent vector for a given 2D position
    // using Inverse Distance Weighting (IDW)
    std::vector<float> getInterpolatedVector (float x, float y, int kNearest = 5) const;

    // Find the nearest point to the given coordinates
    const SoundPoint* getNearestPoint (float x, float y) const;

    // Accessors
    const std::vector<SoundPoint>& getPoints() const { return points; }
    int getLatentDim() const { return latentDim; }
    int getSampleRate() const { return sampleRate; }

    // Bounds for normalization
    float getMinX() const { return minX; }
    float getMaxX() const { return maxX; }
    float getMinY() const { return minY; }
    float getMaxY() const { return maxY; }

private:
    std::vector<SoundPoint> points;
    int latentDim = 128;
    int sampleRate = 44100;

    float minX = 0.0f, maxX = 0.0f;
    float minY = 0.0f, maxY = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundMap)
};
