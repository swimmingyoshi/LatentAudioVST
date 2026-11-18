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
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// ONNX Runtime headers (silence conversion warnings from external code).
// The API is initialised explicitly once our bundled onnxruntime.dll is loaded
// (see OrtRuntime.h), so no ORT call can happen at static-initialisation time.
#define ORT_API_MANUAL_INIT
#pragma warning(push)
#pragma warning(disable: 4267)
#pragma warning(disable: 4244)
#include <onnxruntime_cxx_api.h>
#pragma warning(pop)

//==============================================================================
// Wraps one ONNX decoder session: latent vector -> mono audio.
//
// Thread safety: every public method may be called from any thread except the
// audio thread (decode() runs the network and allocates). Loading a new model
// swaps an immutable handle under a short lock, so a decode() already in flight
// keeps using the old session until it finishes - nothing is freed under it.
//
// Sessions are cached per model file and shared between plugin instances via
// weak references, so the (large) model is freed once no instance uses it.
class LatentDecoder
{
public:
    LatentDecoder() = default;
    ~LatentDecoder() = default;

    struct LoadResult
    {
        bool ok = false;
        juce::String error;
    };

    // If expectedLatentDim > 0 and the model's input size differs, nothing is
    // swapped and an error is returned (the current model stays active).
    LoadResult loadModel (const juce::File& modelFile, int expectedLatentDim = -1);
    void unload();

    // Returns false (and leaves audioOutput untouched) on failure.
    bool decode (const std::vector<float>& latentVector, std::vector<float>& audioOutput) const;

    bool isModelLoaded() const { return getHandle() != nullptr; }

    // "None", "CPU", "GPU: CUDA" or "GPU: DML"
    juce::String getActiveProvider() const;

    // Latent size the model expects, or -1 if unknown / dynamic / not loaded.
    int getModelLatentDim() const;

    juce::File getModelFile() const;

    // Number of distinct model files currently resident (across all instances).
    static int getCacheSize();

private:
    struct Handle
    {
        std::shared_ptr<Ort::Env> env; // keeps the Env alive for as long as the session
        std::unique_ptr<Ort::Session> session;
        std::vector<std::string> inputNames, outputNames;
        std::vector<const char*> inputNamePtrs, outputNamePtrs;
        std::string provider;
        int64_t latentDim = -1;
        juce::File file;
    };

    std::shared_ptr<const Handle> getHandle() const;
    static bool ensureRuntime (juce::String& error);
    static std::shared_ptr<const Handle> createHandle (const juce::File& file, juce::String& error);

    mutable std::mutex handleMutex;
    std::shared_ptr<const Handle> handle;

    static std::mutex cacheMutex;
    static std::map<std::string, std::weak_ptr<const Handle>> cache;
    static std::weak_ptr<Ort::Env> sharedEnv;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LatentDecoder)
};
