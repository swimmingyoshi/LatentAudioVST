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

#include "LatentDecoder.h"

// Static members
std::map<std::string, std::shared_ptr<Ort::Session>> LatentDecoder::sharedSessions;
std::map<std::string, std::string> LatentDecoder::sharedProviders;
std::shared_ptr<Ort::Env> LatentDecoder::sharedEnv;
std::mutex LatentDecoder::sharedMutex;
std::atomic<int> LatentDecoder::instanceCount{ 0 };

//==============================================================================
LatentDecoder::LatentDecoder()
{
    incrementInstances();
    juce::Logger::writeToLog("[LatentDecoder] === CONSTRUCTOR ===");
    juce::Logger::writeToLog("[LatentDecoder] Instance count after increment: " + juce::String(getInstanceCount()));
    juce::Logger::writeToLog("[LatentDecoder] this=" + juce::String::toHexString((juce::int64)this));
}

//==============================================================================
LatentDecoder::~LatentDecoder()
{
    juce::Logger::writeToLog("[LatentDecoder] === DESTRUCTOR ===");
    juce::Logger::writeToLog("[LatentDecoder] this=" + juce::String::toHexString((juce::int64)this));
    juce::Logger::writeToLog("[LatentDecoder] Instance count before decrement: " + juce::String(getInstanceCount()));
    decrementInstances();
    juce::Logger::writeToLog("[LatentDecoder] Instance count after decrement: " + juce::String(getInstanceCount()));

    // CRITICAL: Only clear shared sessions if this is the LAST instance
    if (getInstanceCount() == 0)
    {
        std::lock_guard<std::mutex> lock(sharedMutex);
        juce::Logger::writeToLog("[LatentDecoder] Last instance - clearing shared sessions, cache size: " + juce::String((int)sharedSessions.size()));
        sharedSessions.clear();
        sharedProviders.clear();
        sharedEnv.reset();
        juce::Logger::writeToLog("[LatentDecoder] Shared sessions and env cleared");
    }
    else
    {
        juce::Logger::writeToLog("[LatentDecoder] Not last instance - keeping shared sessions");
    }
}

bool LatentDecoder::loadModel(const juce::File& modelFile)
{
    juce::Logger::writeToLog("[LatentDecoder::loadModel] === START ===");
    juce::Logger::writeToLog("[LatentDecoder::loadModel] this=" + juce::String::toHexString((juce::int64)this));
    juce::Logger::writeToLog("[LatentDecoder::loadModel] Loading model: " + modelFile.getFullPathName());
    juce::Logger::writeToLog("[LatentDecoder::loadModel] Current latentDim: " + juce::String(latentDim));

    if (!modelFile.existsAsFile())
    {
        juce::Logger::writeToLog("[LatentDecoder::loadModel] FAIL: Model file does not exist!");
        return false;
    }

    std::string  modelPathStr = modelFile.getFullPathName().toStdString();
    std::wstring modelPathW   = modelFile.getFullPathName().toWideCharPointer();

    // Helper: build fresh SessionOptions with common base settings.
    // Must be called fresh for each provider attempt — a partially-configured
    // SessionOptions after a failed AppendExecutionProvider is not reusable.
    auto makeBaseOptions = []() -> Ort::SessionOptions
    {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
        return opts;
    };

    try
    {
        Ort::AllocatorWithDefaultOptions allocator;
        std::shared_ptr<Ort::Session> targetSession;
        std::string chosenProvider;

        // ---- Check shared session cache (holds lock for entire probe) ----
        {
            std::lock_guard<std::mutex> lock(sharedMutex);
            juce::Logger::writeToLog("[LatentDecoder::loadModel] Cache size: " + juce::String((int)sharedSessions.size()));

            auto it = sharedSessions.find(modelPathStr);
            if (it != sharedSessions.end())
            {
                // Cache hit: reuse session and restore the recorded provider
                juce::Logger::writeToLog("[LatentDecoder::loadModel] CACHE HIT - reusing existing session");
                session = it->second;
                env     = sharedEnv;

                auto pit = sharedProviders.find(modelPathStr);
                activeProvider = (pit != sharedProviders.end())
                                 ? juce::String(pit->second) : "CPU";

                size_t numIn  = session->GetInputCount();
                size_t numOut = session->GetOutputCount();
                inputNodeNames.clear();  inputNodeNameStrings.clear();
                for (size_t i = 0; i < numIn; ++i)
                {
                    auto name = session->GetInputNameAllocated(i, allocator);
                    inputNodeNameStrings.push_back(name.get());
                    inputNodeNames.push_back(inputNodeNameStrings.back().c_str());
                }
                outputNodeNames.clear(); outputNodeNameStrings.clear();
                for (size_t i = 0; i < numOut; ++i)
                {
                    auto name = session->GetOutputNameAllocated(i, allocator);
                    outputNodeNameStrings.push_back(name.get());
                    outputNodeNames.push_back(outputNodeNameStrings.back().c_str());
                }
                juce::Logger::writeToLog("[LatentDecoder::loadModel] CACHE HIT SUCCESS, provider=" + activeProvider);
                return true;
            }

            // Cache miss — ensure shared Env exists
            if (!sharedEnv)
            {
                juce::Logger::writeToLog("[LatentDecoder::loadModel] Creating new shared Ort::Env");
                sharedEnv = std::make_shared<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "LatentAudioVST");
            }
            env = sharedEnv;

            // ---- Provider probe: CUDA -> DirectML -> CPU ----------------
            // Each attempt uses freshly constructed SessionOptions so a failed
            // append on one attempt doesn't poison the next attempt's options.

            // Attempt 1: CUDA (NVIDIA GPU — requires onnxruntime_providers_cuda.dll)
            {
                juce::Logger::writeToLog("[LatentDecoder::loadModel] Trying CUDA provider...");
                try
                {
                    Ort::SessionOptions cudaOpts = makeBaseOptions();
                    OrtCUDAProviderOptions cudaProvOpts{};
                    cudaProvOpts.device_id = 0;
                    cudaOpts.AppendExecutionProvider_CUDA(cudaProvOpts);
                    targetSession  = std::make_shared<Ort::Session>(*env, modelPathW.c_str(), cudaOpts);
                    chosenProvider = "GPU: CUDA";
                    juce::Logger::writeToLog("[LatentDecoder::loadModel] CUDA provider: SUCCESS");
                }
                catch (const Ort::Exception& e)
                {
                    juce::Logger::writeToLog("[LatentDecoder::loadModel] CUDA provider: unavailable (" + juce::String(e.what()) + ")");
                    targetSession.reset();
                }
                catch (...)
                {
                    juce::Logger::writeToLog("[LatentDecoder::loadModel] CUDA provider: unavailable (unknown error)");
                    targetSession.reset();
                }
            }

            // Attempt 2: DirectML (any Windows GPU/iGPU — requires onnxruntime_providers_dml.dll)
            if (!targetSession)
            {
                juce::Logger::writeToLog("[LatentDecoder::loadModel] Trying DirectML provider...");
                try
                {
                    Ort::SessionOptions dmlOpts = makeBaseOptions();
                    // DML requires sequential execution and disabled memory pattern optimisation
                    dmlOpts.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
                    dmlOpts.DisableMemPattern();
                    dmlOpts.AppendExecutionProvider("DML", { {"device_id", "0"} });
                    targetSession  = std::make_shared<Ort::Session>(*env, modelPathW.c_str(), dmlOpts);
                    chosenProvider = "GPU: DML";
                    juce::Logger::writeToLog("[LatentDecoder::loadModel] DirectML provider: SUCCESS");
                }
                catch (const Ort::Exception& e)
                {
                    juce::Logger::writeToLog("[LatentDecoder::loadModel] DirectML provider: unavailable (" + juce::String(e.what()) + ")");
                    targetSession.reset();
                }
                catch (...)
                {
                    juce::Logger::writeToLog("[LatentDecoder::loadModel] DirectML provider: unavailable (unknown error)");
                    targetSession.reset();
                }
            }

            // Attempt 3: CPU (always available — may throw only if model file is corrupt)
            if (!targetSession)
            {
                juce::Logger::writeToLog("[LatentDecoder::loadModel] Using CPU provider (fallback)");
                Ort::SessionOptions cpuOpts = makeBaseOptions();
                targetSession  = std::make_shared<Ort::Session>(*env, modelPathW.c_str(), cpuOpts);
                chosenProvider = "CPU";
                juce::Logger::writeToLog("[LatentDecoder::loadModel] CPU provider: SUCCESS");
            }

            // Commit both session and provider to the shared cache atomically
            sharedSessions[modelPathStr]  = targetSession;
            sharedProviders[modelPathStr] = chosenProvider;
            session = targetSession;

            juce::Logger::writeToLog("[LatentDecoder::loadModel] Session cached, provider=" + juce::String(chosenProvider));
        } // lock released

        activeProvider = juce::String(chosenProvider);

        // Populate node names for the new session (outside lock — read-only on targetSession)
        size_t numIn  = targetSession->GetInputCount();
        size_t numOut = targetSession->GetOutputCount();

        inputNodeNames.clear();  inputNodeNameStrings.clear();
        for (size_t i = 0; i < numIn; ++i)
        {
            auto name = targetSession->GetInputNameAllocated(i, allocator);
            inputNodeNameStrings.push_back(name.get());
            inputNodeNames.push_back(inputNodeNameStrings.back().c_str());
        }
        outputNodeNames.clear(); outputNodeNameStrings.clear();
        for (size_t i = 0; i < numOut; ++i)
        {
            auto name = targetSession->GetOutputNameAllocated(i, allocator);
            outputNodeNameStrings.push_back(name.get());
            outputNodeNames.push_back(outputNodeNameStrings.back().c_str());
        }

        juce::Logger::writeToLog("[LatentDecoder::loadModel] SUCCESS: provider=" + activeProvider
            + ", latentDim=" + juce::String(latentDim)
            + ", inputNodes=" + juce::String((int)numIn)
            + ", outputNodes=" + juce::String((int)numOut));
        return true;
    }
    catch (const Ort::Exception& e)
    {
        juce::Logger::writeToLog("[LatentDecoder::loadModel] ONNX Runtime Error: " + juce::String(e.what()));
        return false;
    }
}

bool LatentDecoder::decode(const std::vector<float>& latentVector, std::vector<float>& audioOutput)
{
    if (!session || latentVector.size() != static_cast<size_t> (latentDim))
    {
        juce::Logger::writeToLog("[LatentDecoder::decode] FAIL: session null or latentVector size mismatch (dim=" + juce::String(latentDim) + ")");
        return false;
    }

    try
    {
        auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        // Prepare Input Tensor
        std::vector<int64_t> inputShape = { 1, latentDim };

        // Const cast is safe here as CreateTensor doesn't modify input for read-only tensors
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo,
            const_cast<float*> (latentVector.data()),
            latentVector.size(),
            inputShape.data(),
            inputShape.size()
        );

        // Run Inference
        auto outputTensors = session->Run(
            Ort::RunOptions{ nullptr },
            inputNodeNames.data(),
            &inputTensor,
            1, // One input
            outputNodeNames.data(),
            1  // One output
        );

        // Extract Output
        float* floatArr = outputTensors.front().GetTensorMutableData<float>();
        auto typeInfo = outputTensors.front().GetTensorTypeAndShapeInfo();
        size_t outputSize = typeInfo.GetElementCount();

        // Resize buffer if needed (usually 44100)
        if (audioOutput.size() != outputSize)
            audioOutput.resize(outputSize);

        // Copy data
        std::memcpy(audioOutput.data(), floatArr, outputSize * sizeof(float));

        // Success - don't log here (called on inference thread every inference, log = file I/O = latency)
        return true;
    }
    catch (const Ort::Exception& e)
    {
        juce::Logger::writeToLog("[LatentDecoder::decode] Inference Error: " + juce::String(e.what()));
        return false;
    }
}

//==============================================================================
// Manual unload functionality
//==============================================================================

bool LatentDecoder::unloadModelFromCache(const juce::String& modelPath)
{
    std::string modelPathStr = modelPath.toStdString();

    std::lock_guard<std::mutex> lock(sharedMutex);

    auto it = sharedSessions.find(modelPathStr);
    if (it != sharedSessions.end())
    {
        juce::Logger::writeToLog("[LatentDecoder::unloadModelFromCache] Unloading model: " + modelPath);
        juce::Logger::writeToLog("[LatentDecoder::unloadModelFromCache] Cache size before: " + juce::String((int)sharedSessions.size()));

        sharedSessions.erase(it);
        sharedProviders.erase(modelPathStr); // keep companion map in sync

        juce::Logger::writeToLog("[LatentDecoder::unloadModelFromCache] Cache size after: " + juce::String((int)sharedSessions.size()));
        return true;
    }

    juce::Logger::writeToLog("[LatentDecoder::unloadModelFromCache] Model not found in cache: " + modelPath);
    return false;
}

std::vector<juce::String> LatentDecoder::getLoadedModels()
{
    std::vector<juce::String> loadedModels;

    std::lock_guard<std::mutex> lock(sharedMutex);
    for (const auto& pair : sharedSessions)
    {
        loadedModels.push_back(pair.first);
    }

    return loadedModels;
}

int LatentDecoder::getCacheSize()
{
    std::lock_guard<std::mutex> lock(sharedMutex);
    return (int)sharedSessions.size();
}