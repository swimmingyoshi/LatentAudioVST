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

    try
    {
        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(1);
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);

        // Convert path to string
        std::string modelPathStr = modelFile.getFullPathName().toStdString();

        std::shared_ptr<Ort::Session> targetSession;
        Ort::AllocatorWithDefaultOptions allocator;
        size_t numInputNodes, numOutputNodes;

        // Check shared sessions
        juce::Logger::writeToLog("[LatentDecoder::loadModel] Checking shared sessions cache...");
        {
            std::lock_guard<std::mutex> lock(sharedMutex);
            juce::Logger::writeToLog("[LatentDecoder::loadModel] Cache size: " + juce::String((int)sharedSessions.size()));
            juce::Logger::writeToLog("[LatentDecoder::loadModel] sharedEnv=" + juce::String(sharedEnv ? "VALID" : "NULL"));

            auto it = sharedSessions.find(modelPathStr);
            if (it != sharedSessions.end())
            {
                juce::Logger::writeToLog("[LatentDecoder::loadModel] CACHE HIT - reusing existing session");
                juce::Logger::writeToLog("[LatentDecoder::loadModel] Cached session ptr=" + juce::String::toHexString((juce::int64)it->second.get()));
                session = it->second;
                targetSession = session;
                env = sharedEnv;
                // Get input/output names from existing session
                numInputNodes = session->GetInputCount();
                numOutputNodes = session->GetOutputCount();
                // Populate names
                inputNodeNames.clear();
                inputNodeNameStrings.clear();
                for (size_t i = 0; i < numInputNodes; i++)
                {
                    auto name = session->GetInputNameAllocated(i, allocator);
                    inputNodeNameStrings.push_back(name.get());
                    inputNodeNames.push_back(inputNodeNameStrings.back().c_str());
                }
                outputNodeNames.clear();
                outputNodeNameStrings.clear();
                for (size_t i = 0; i < numOutputNodes; i++)
                {
                    auto name = session->GetOutputNameAllocated(i, allocator);
                    outputNodeNameStrings.push_back(name.get());
                    outputNodeNames.push_back(outputNodeNameStrings.back().c_str());
                }
                juce::Logger::writeToLog("[LatentDecoder::loadModel] CACHE HIT SUCCESS");
                return true;
            }
            else
            {
                juce::Logger::writeToLog("[LatentDecoder::loadModel] CACHE MISS - creating new session");

                // Create shared env if needed
                if (!sharedEnv)
                {
                    juce::Logger::writeToLog("[LatentDecoder::loadModel] Creating new shared Ort::Env");
                    sharedEnv = std::make_shared<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "LatentAudioVST");
                }

                env = sharedEnv;

                // Load new session
                // Convert path to wstring for Windows
                std::wstring modelPathW = modelFile.getFullPathName().toWideCharPointer();

                juce::Logger::writeToLog("[LatentDecoder::loadModel] Creating new Ort::Session...");
                auto newSession = std::make_shared<Ort::Session>(*env, modelPathW.c_str(), sessionOptions);
                juce::Logger::writeToLog("[LatentDecoder::loadModel] New session created, ptr=" + juce::String::toHexString((juce::int64)newSession.get()));

                sharedSessions[modelPathStr] = newSession;
                session = newSession;
                targetSession = newSession;

                // Get input/output names for new session
                numInputNodes = targetSession->GetInputCount();
                numOutputNodes = targetSession->GetOutputCount();

                juce::Logger::writeToLog("[LatentDecoder::loadModel] Session added to cache, new cache size: " + juce::String((int)sharedSessions.size()));
            }
        }

        // Populate names for new session
        inputNodeNames.clear();
        inputNodeNameStrings.clear();

        for (size_t i = 0; i < numInputNodes; i++)
        {
            auto name = targetSession->GetInputNameAllocated(i, allocator);
            inputNodeNameStrings.push_back(name.get());
            inputNodeNames.push_back(inputNodeNameStrings.back().c_str());
        }

        outputNodeNames.clear();
        outputNodeNameStrings.clear();

        for (size_t i = 0; i < numOutputNodes; i++)
        {
            auto name = targetSession->GetOutputNameAllocated(i, allocator);
            outputNodeNameStrings.push_back(name.get());
            outputNodeNames.push_back(outputNodeNameStrings.back().c_str());
        }

        juce::Logger::writeToLog("[LatentDecoder::loadModel] SUCCESS: Model loaded, latentDim=" + juce::String(latentDim) + ", inputNodes=" + juce::String(numInputNodes) + ", outputNodes=" + juce::String(numOutputNodes));
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
    juce::Logger::writeToLog("[LatentDecoder::decode] Called with latentVector.size()=" + juce::String(latentVector.size()) + ", expected latentDim=" + juce::String(latentDim));
    juce::Logger::writeToLog("[LatentDecoder::decode] session is " + juce::String(session ? "VALID" : "NULL"));

    if (!session || latentVector.size() != static_cast<size_t> (latentDim))
    {
        juce::Logger::writeToLog("[LatentDecoder::decode] FAIL: session null or latentVector size mismatch");
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

        juce::Logger::writeToLog("[LatentDecoder::decode] SUCCESS: Generated audio with " + juce::String(outputSize) + " samples");
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