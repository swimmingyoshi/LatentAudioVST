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

// Deliberately does not include JuceHeader.h: it only needs Win32.
#include "OrtRuntime.h"

#ifdef _WIN32

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <delayimp.h>
#include <mutex>

namespace
{
    HMODULE ortModule = nullptr;

    // Bind the delay-loaded "onnxruntime.dll" import to the module we loaded by
    // full path, never to a same-named DLL found by the default search order.
    FARPROC WINAPI delayLoadHook (unsigned notify, PDelayLoadInfo info)
    {
        if (notify == dliNotePreLoadLibrary && ortModule != nullptr && _stricmp (info->szDll, "onnxruntime.dll") == 0)
            return reinterpret_cast<FARPROC> (ortModule);
        return nullptr;
    }

    std::wstring ownModuleDirectory()
    {
        HMODULE self = nullptr;
        GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR> (&delayLoadHook), &self);
        wchar_t path[MAX_PATH * 4] = {};
        const DWORD len = GetModuleFileNameW (self, path, (DWORD) (sizeof (path) / sizeof (path[0])));
        std::wstring dir (path, len);
        const auto slash = dir.find_last_of (L"\\/");
        return slash == std::wstring::npos ? std::wstring() : dir.substr (0, slash);
    }
}

extern "C" const PfnDliHook __pfnDliNotifyHook2 = delayLoadHook;

bool OrtRuntime::load (std::wstring& error, std::wstring& loadedPath)
{
    static std::once_flag once;
    static bool ok = false;
    static std::wstring storedError, storedPath;

    std::call_once (once, []
    {
        storedPath = ownModuleDirectory() + L"\\onnxruntime.dll";
        if (GetFileAttributesW (storedPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            storedError = L"onnxruntime.dll is missing next to the plugin (" + storedPath + L")";
            return;
        }

        // Altered search path: its own dependencies resolve from the same folder.
        ortModule = LoadLibraryExW (storedPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (ortModule == nullptr)
        {
            storedError = L"Could not load " + storedPath + L" (error " + std::to_wstring (GetLastError()) + L")";
            return;
        }
        ok = true;
    });

    error = storedError;
    loadedPath = storedPath;
    return ok;
}

#else

bool OrtRuntime::load (std::wstring&, std::wstring&) { return true; }

#endif
