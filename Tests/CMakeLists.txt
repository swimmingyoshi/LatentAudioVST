# Unit / integration tests for LatentAudioVST.
#
# The plugin itself is built with Projucer (LatentAudioVST.jucer). This project
# compiles the same Source/ files into a console test runner.
#
#   cmake -S Tests -B Tests/build -G "Visual Studio 17 2022" -A x64
#   cmake --build Tests/build --config Release
#   Tests/build/Release/LatentAudioTests.exe            # fast tests
#   set LATENTAUDIO_REAL_MODEL=D:\path\to\KickModel     # optional: also test a real model
cmake_minimum_required(VERSION 3.22)
project(LatentAudioTests VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../VST_Plugins/JUCE" CACHE PATH "JUCE checkout")
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)

set(ORT_DIR "${CMAKE_CURRENT_LIST_DIR}/../Libs/onnxruntime-win-x64-1.16.3" CACHE PATH "ONNX Runtime")
set(SRC "${CMAKE_CURRENT_LIST_DIR}/../Source")

juce_add_console_app(LatentAudioTests PRODUCT_NAME "LatentAudioTests")
juce_generate_juce_header(LatentAudioTests)

target_sources(LatentAudioTests PRIVATE
    TestMain.cpp
    SoundMapTests.cpp
    DecoderTests.cpp
    ProcessorTests.cpp
    ${SRC}/LatentLog.cpp
    ${SRC}/SoundMap.cpp
    ${SRC}/LatentDecoder.cpp
    ${SRC}/PluginProcessor.cpp
    ${SRC}/PluginEditor.cpp
    ${SRC}/NebulaComponent.cpp
    ${SRC}/WaveformComponent.cpp)

target_include_directories(LatentAudioTests PRIVATE ${SRC} ${ORT_DIR}/include)

target_compile_definitions(LatentAudioTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="LatentAudioVST"
    JucePlugin_IsSynth=1
    JucePlugin_WantsMidiInput=1
    JucePlugin_ProducesMidiOutput=0
    JucePlugin_IsMidiEffect=0
    LATENTAUDIO_TEST_DATA="${CMAKE_CURRENT_LIST_DIR}/data")

target_link_libraries(LatentAudioTests PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    ${ORT_DIR}/lib/onnxruntime.lib
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# The tests need the ONNX Runtime DLL next to the executable.
add_custom_command(TARGET LatentAudioTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ORT_DIR}/lib/onnxruntime.dll
        ${ORT_DIR}/lib/onnxruntime_providers_shared.dll
        $<TARGET_FILE_DIR:LatentAudioTests>)
