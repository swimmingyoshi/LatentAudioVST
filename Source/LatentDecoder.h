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
#include <memory>
#include <map>
#include <mutex>
#include <string>

// ONNX Runtime Headers
// Suppress warnings from external headers
#pragma warning(push)
#pragma warning(disable: 4267) // Conversion warning
#pragma warning(disable: 4244)
#include <onnxruntime_cxx_api.h>
#pragma warning(pop)

//==============================================================================
class LatentDecoder
{
public:
    static std::map<std::string, std::shared_ptr<Ort::Session>> sharedSessions;
    static std::shared_ptr<Ort::Env> sharedEnv;
    static std::mutex sharedMutex;
    static std::atomic<int> instanceCount;
    static void incrementInstances() { instanceCount.fetch_add(1); }
    static void decrementInstances() { instanceCount.fetch_sub(1); }
    static int getInstanceCount() { return instanceCount.load(); }
    LatentDecoder();
    ~LatentDecoder();

    bool loadModel (const juce::File& modelFile);

    // Run inference: 128D input -> Audio output
    // Returns true on success
    bool decode (const std::vector<float>& latentVector, std::vector<float>& audioOutput);

    void setLatentDim(int dim) { latentDim = dim; }

    bool isModelLoaded() const { return session != nullptr; }

    int getExpectedLatentDim() const { return latentDim; }

    // Manual unload functionality
    static bool unloadModelFromCache(const juce::String& modelPath);
    static std::vector<juce::String> getLoadedModels();
    static int getCacheSize();

private:
    std::shared_ptr<Ort::Env> env;
    std::shared_ptr<Ort::Session> session;
    
    // Model Info
    int latentDim = 128;
    
    // Use unique_ptr for these to avoid header dependency issues if possible, 
    // but Ort classes are small handles.
    // We need to store input/output names for the Run call.
    std::vector<const char*> inputNodeNames;
    std::vector<const char*> outputNodeNames;
    
    // We also need to keep the strings alive if we used `GetInputNameAllocated`
    std::vector<std::string> inputNodeNameStrings;
    std::vector<std::string> outputNodeNameStrings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LatentDecoder)
};
