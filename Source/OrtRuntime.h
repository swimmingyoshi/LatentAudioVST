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

#include <string>

//==============================================================================
// Loads the onnxruntime.dll that ships next to the plugin binary.
//
// onnxruntime.dll is delay-loaded (linker: /DELAYLOAD:onnxruntime.dll), so the
// plugin itself loads even when the runtime is missing. Windows 11 also ships
// an unrelated onnxruntime.dll in System32; a delay-load hook makes sure the
// plugin always binds to its own copy, loaded by full path.
//
// Must succeed before any ONNX Runtime API is used.
namespace OrtRuntime
{
    // Thread-safe, runs once. Returns false and fills `error` on failure.
    bool load (std::wstring& error, std::wstring& loadedPath);
}
