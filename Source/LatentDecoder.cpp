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
#include "LatentLog.h"
#include "OrtRuntime.h"

std::mutex LatentDecoder::cacheMutex;
std::map<std::string, std::weak_ptr<const LatentDecoder::Handle>> LatentDecoder::cache;
std::weak_ptr<Ort::Env> LatentDecoder::sharedEnv;

//==============================================================================
namespace
{
    Ort::SessionOptions makeBaseOptions()
    {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads (1);
        opts.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_BASIC);
        return opts;
    }
}

//==============================================================================
bool LatentDecoder::ensureRuntime (juce::String& error)
{
    // Caller holds cacheMutex.
    static bool initialised = false;
    if (initialised)
        return true;

    std::wstring loadError, path;
    if (! OrtRuntime::load (loadError, path))
    {
        error = juce::String (loadError.c_str());
        return false;
    }

    const OrtApi* api = OrtGetApiBase()->GetApi (ORT_API_VERSION);
    if (api == nullptr)
    {
        error = "onnxruntime.dll is too old (needs API version " + juce::String (ORT_API_VERSION) + ")";
        return false;
    }
    Ort::InitApi (api);
    initialised = true;
    LatentLog::write ("[LatentDecoder] ONNX Runtime " + juce::String (OrtGetApiBase()->GetVersionString())
                      + " loaded from " + juce::String (path.c_str()));
    return true;
}

std::shared_ptr<const LatentDecoder::Handle> LatentDecoder::createHandle (const juce::File& file, juce::String& error)
{
    if (! ensureRuntime (error))
        return nullptr;

    auto h = std::make_shared<Handle>();
    h->file = file;

    // Caller holds cacheMutex, so the shared Env is created at most once.
    h->env = sharedEnv.lock();
    if (h->env == nullptr)
    {
        h->env = std::make_shared<Ort::Env> (ORT_LOGGING_LEVEL_WARNING, "LatentAudioVST");
        sharedEnv = h->env;
    }

    const std::wstring path = file.getFullPathName().toWideCharPointer();

    // Provider probe: CUDA -> DirectML -> CPU. Each attempt gets fresh options
    // because a failed AppendExecutionProvider can leave options unusable.
    try
    {
        auto opts = makeBaseOptions();
        OrtCUDAProviderOptions cuda {};
        cuda.device_id = 0;
        opts.AppendExecutionProvider_CUDA (cuda);
        h->session  = std::make_unique<Ort::Session> (*h->env, path.c_str(), opts);
        h->provider = "GPU: CUDA";
    }
    catch (const std::exception& e)
    {
        LatentLog::write ("[LatentDecoder] CUDA unavailable: " + juce::String (e.what()));
    }

    if (h->session == nullptr)
    {
        try
        {
            auto opts = makeBaseOptions();
            opts.SetExecutionMode (ExecutionMode::ORT_SEQUENTIAL);
            opts.DisableMemPattern();
            opts.AppendExecutionProvider ("DML", { { "device_id", "0" } });
            h->session  = std::make_unique<Ort::Session> (*h->env, path.c_str(), opts);
            h->provider = "GPU: DML";
        }
        catch (const std::exception& e)
        {
            LatentLog::write ("[LatentDecoder] DirectML unavailable: " + juce::String (e.what()));
        }
    }

    if (h->session == nullptr)
    {
        try
        {
            h->session  = std::make_unique<Ort::Session> (*h->env, path.c_str(), makeBaseOptions());
            h->provider = "CPU";
        }
        catch (const std::exception& e)
        {
            error = "Could not load model: " + juce::String (e.what());
            return nullptr;
        }
    }

    try
    {
        Ort::AllocatorWithDefaultOptions allocator;

        if (h->session->GetInputCount() < 1 || h->session->GetOutputCount() < 1)
        {
            error = "Model must have one input and one output";
            return nullptr;
        }

        for (size_t i = 0; i < h->session->GetInputCount(); ++i)
            h->inputNames.emplace_back (h->session->GetInputNameAllocated (i, allocator).get());
        for (size_t i = 0; i < h->session->GetOutputCount(); ++i)
            h->outputNames.emplace_back (h->session->GetOutputNameAllocated (i, allocator).get());
        for (auto& n : h->inputNames)  h->inputNamePtrs.push_back (n.c_str());
        for (auto& n : h->outputNames) h->outputNamePtrs.push_back (n.c_str());

        // Expected shape [batch, latentDim]; the last dim is -1 when dynamic.
        auto shape = h->session->GetInputTypeInfo (0).GetTensorTypeAndShapeInfo().GetShape();
        if (! shape.empty())
            h->latentDim = shape.back();
    }
    catch (const std::exception& e)
    {
        error = "Could not inspect model: " + juce::String (e.what());
        return nullptr;
    }

    return h;
}

LatentDecoder::LoadResult LatentDecoder::loadModel (const juce::File& modelFile, int expectedLatentDim)
{
    LoadResult result;
    if (! modelFile.existsAsFile())
    {
        result.error = "Model not found: " + modelFile.getFullPathName();
        return result;
    }

    const auto key = modelFile.getFullPathName().toStdString();
    std::shared_ptr<const Handle> newHandle;
    {
        // Held across creation so two instances loading the same file share
        // one session instead of both loading the (large) model.
        std::lock_guard<std::mutex> lock (cacheMutex);
        newHandle = cache[key].lock();
        if (newHandle == nullptr)
        {
            LatentLog::write ("[LatentDecoder] Loading " + modelFile.getFullPathName());
            newHandle = createHandle (modelFile, result.error);
            if (newHandle == nullptr)
            {
                cache.erase (key);
                LatentLog::write ("[LatentDecoder] " + result.error);
                return result;
            }
            cache[key] = newHandle;
            LatentLog::write ("[LatentDecoder] Loaded, provider=" + juce::String (newHandle->provider));
        }
    }

    if (expectedLatentDim > 0 && newHandle->latentDim > 0 && newHandle->latentDim != expectedLatentDim)
    {
        result.error = "Model expects latent size " + juce::String ((int) newHandle->latentDim)
                     + " but the sound map has " + juce::String (expectedLatentDim);
        return result;
    }

    std::shared_ptr<const Handle> old;
    {
        std::lock_guard<std::mutex> lock (handleMutex);
        old = std::exchange (handle, newHandle);
    }
    // `old` is released here: if this was its last user, the session is freed now.
    result.ok = true;
    return result;
}

void LatentDecoder::unload()
{
    std::shared_ptr<const Handle> old;
    {
        std::lock_guard<std::mutex> lock (handleMutex);
        old = std::move (handle);
        handle.reset();
    }
}

std::shared_ptr<const LatentDecoder::Handle> LatentDecoder::getHandle() const
{
    std::lock_guard<std::mutex> lock (handleMutex);
    return handle;
}

bool LatentDecoder::decode (const std::vector<float>& latentVector, std::vector<float>& audioOutput) const
{
    auto h = getHandle();
    if (h == nullptr || latentVector.empty())
        return false;

    if (h->latentDim > 0 && (int64_t) latentVector.size() != h->latentDim)
        return false;

    try
    {
        auto memoryInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 2> inputShape { 1, (int64_t) latentVector.size() };

        auto input = Ort::Value::CreateTensor<float> (memoryInfo,
                                                      const_cast<float*> (latentVector.data()),
                                                      latentVector.size(),
                                                      inputShape.data(), inputShape.size());

        auto outputs = h->session->Run (Ort::RunOptions { nullptr },
                                        h->inputNamePtrs.data(), &input, 1,
                                        h->outputNamePtrs.data(), 1);

        const auto count = outputs.front().GetTensorTypeAndShapeInfo().GetElementCount();
        const float* data = outputs.front().GetTensorData<float>();
        audioOutput.assign (data, data + count);
        return true;
    }
    catch (const std::exception& e)
    {
        LatentLog::write ("[LatentDecoder] Inference error: " + juce::String (e.what()));
        return false;
    }
}

juce::String LatentDecoder::getActiveProvider() const
{
    auto h = getHandle();
    return h != nullptr ? juce::String (h->provider) : juce::String ("None");
}

int LatentDecoder::getModelLatentDim() const
{
    auto h = getHandle();
    return h != nullptr ? (int) h->latentDim : -1;
}

juce::File LatentDecoder::getModelFile() const
{
    auto h = getHandle();
    return h != nullptr ? h->file : juce::File();
}

int LatentDecoder::getCacheSize()
{
    std::lock_guard<std::mutex> lock (cacheMutex);
    int n = 0;
    for (auto& [key, weak] : cache)
        if (! weak.expired())
            ++n;
    return n;
}
