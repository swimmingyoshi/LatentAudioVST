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

//==============================================================================
// Plugin-private log (Documents/LatentAudioVST_debug.log).
//
// Deliberately does NOT install itself as juce::Logger's global logger: in a
// DAW all plugins share one process, so replacing the global logger would
// hijack the host's (and other plugins') logging.
//
// Never call from the audio thread - it does file I/O.
namespace LatentLog
{
    void write (const juce::String& message);
}
