// Console runner for the LatentAudio unit tests (category "LatentAudio").
#include <JuceHeader.h>
#include <iostream>

namespace
{
    // JUCE's default runner logs to the debugger; print to the console instead.
    struct ConsoleRunner : juce::UnitTestRunner
    {
        void logMessage (const juce::String& message) override
        {
            std::cout << message << std::endl;
        }
    };
}

int renderSnapshots (const juce::File& modelDir, const juce::File& outDir); // ProcessorTests.cpp
int hostPlugin (const juce::File& vst3, const juce::File& modelDir, const juce::File& screenshot); // HostTests.cpp

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // LatentAudioTests --snapshot <modelFolder> <outputFolder>
    if (argc == 4 && juce::String (argv[1]) == "--snapshot")
        return renderSnapshots (juce::File (argv[2]), juce::File (argv[3]));

    // LatentAudioTests --host <plugin.vst3> <modelFolder> [screenshot.png]
    if (argc >= 4 && juce::String (argv[1]) == "--host")
        return hostPlugin (juce::File (argv[2]), juce::File (argv[3]), argc > 4 ? juce::File (argv[4]) : juce::File());

    ConsoleRunner runner;
    runner.setAssertOnFailure (false);

    if (argc > 1)
        runner.runTestsInCategory (argv[1]);
    else
        runner.runTestsInCategory ("LatentAudio");

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        failures += runner.getResult (i)->failures;
        passes += runner.getResult (i)->passes;
    }

    std::cout << "\n==== " << passes << " passed, " << failures << " failed ====" << std::endl;
    return failures > 0 ? 1 : 0;
}
