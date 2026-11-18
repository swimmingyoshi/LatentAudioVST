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

#include "LatentLog.h"

namespace LatentLog
{
    void write (const juce::String& message)
    {
        // Function-local static: created on first use, thread-safe initialisation,
        // shared by every plugin instance in the process. FileLogger::logMessage
        // is internally locked.
        static juce::FileLogger logger (
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                .getChildFile ("LatentAudioVST_debug.log"),
            "LatentAudioVST debug log",
            1024 * 1024); // trim to ~1 MB on open so it never grows unbounded

        logger.logMessage (message);
    }
}
