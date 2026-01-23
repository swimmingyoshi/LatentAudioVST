# LatentAudioVST - Agent Guidelines

This document provides essential information for AI agents working in the LatentAudioVST repository. Follow these guidelines to ensure consistency and maintainability across the codebase.

## 1. Project Overview

LatentAudioVST is a JUCE-based audio VST Plugin designed to interact with a latent space representation of audio samples. It utilizes ONNX for model inference and a JSON-based map for latent space navigation.

- **Core Tech:** C++17, JUCE Framework, ONNX Runtime.
- **Data:** Latent weights and sound maps are stored in `Source/VST_Export/`.
- **Goal:** Provide a visual interface for exploring and decoding latent audio representations.

## 2. Build and Test Commands

The project is managed via Projucer.

### Build Commands (Windows / VS2022)
- **Generate Solution:** Open `LatentAudioVST.jucer` in Projucer and click "Save and Open in IDE" to regenerate the Visual Studio solution. This is required whenever new files are added to the `Source` directory.
- **Build via CLI (Debug):**
  ```bash
  "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Builds\VisualStudio2022\LatentAudioVST.sln /p:Configuration=Debug /p:Platform=x64
  ```
- **Build via CLI (Release):**
  ```bash
  "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Builds\VisualStudio2022\LatentAudioVST.sln /p:Configuration=Release /p:Platform=x64
  ```

### Running the Application
- When building use : 
cd Builds/VisualStudio2022 && "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" LatentAudioVST.sln /property:Configuration=Debug /property:Platform=x64

- Ensure that the working directory for the executable has access to the `VST_Export` folder if paths are relative.

### Testing
- Currently, no automated tests are implemented. 
- **Adding Tests:** Use `juce::UnitTest` for logic testing. New test files should be placed in `Source/Tests/`.
- **Running Tests:** If implemented, tests should be triggered via a separate test runner target or a `--test` command-line flag. Use `juce::UnitTestRunner` to execute and log results.

## 3. Code Style Guidelines

### File Structure & Formatting
- **Indentation:** 4 spaces. No tabs.
- **Line Endings:** LF (preferred) or CRLF.
- **Encoding:** UTF-8.
- **Bracing:** Allman style (opening brace on a new line).
  ```cpp
  void function()
  {
      if (condition)
      {
          // logic
      }
  }
  ```
- **Separators:** Use JUCE-style separators to delineate sections:
  `//==============================================================================`

### Naming Conventions
- **Classes/Structs:** `PascalCase` (e.g., `MainComponent`, `AudioProcessor`).
- **Methods:** `camelCase` (e.g., `paint`, `resized`, `processBlock`).
- **Variables (Member):** `camelCase` (e.g., `mainWindow`, `latentDecoder`).
- **Variables (Local):** `camelCase` (e.g., `numSamples`, `pointIndex`).
- **Constants/Enums:** `PascalCase` or `kConstantName`.
- **JUCE Macros:** Always use uppercase (e.g., `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR`).

### Imports and Namespacing
- **Namespacing:** Do NOT use `using namespace juce;`. Always use the `juce::` prefix.
- **Headers:** Use `#pragma once` in all headers.
- **Includes:**
  1. Primary header (e.g., `MainComponent.h` in `MainComponent.cpp`).
  2. JUCE headers (`#include <JuceHeader.h>`).
  3. Standard library headers (`<vector>`, `<memory>`, etc.).
  4. Local project headers.

### Memory Management
- **Modern C++:** Use `std::unique_ptr` for owned objects.
- **JUCE Ownership:** Use `juce::Component::addChildAndSetID` or `addAndMakeVisible`.
- **Safety:** Always include `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClassName)` in private sections.
- **Smart Pointers:** Prefer `juce::ComponentSafePointer` or `juce::WeakReference` for non-owning UI references that might outlive their target.

### Error Handling
- **Assertions:** Use `jassert(condition)` for developer-facing logic checks.
- **Runtime Errors:** Use `juce::Logger::writeToLog()` for logging.
- **Audio Thread Safety:** Avoid all memory allocation (`new`, `malloc`), file I/O, or blocking synchronization (locks) inside `processBlock`.

## 4. UI & Graphics
- **Layout:** Implement layout logic in `resized()`. Avoid hardcoded coordinates; use `getLocalBounds()` and `removeFromTop()`, `removeFromLeft()`, etc.
- **Painting:** Keep `paint()` strictly for drawing. Do not perform complex calculations or state updates here.
- **Look and Feel:** Use `getLookAndFeel().findColour(...)` to remain consistent with JUCE's color system.

## 5. Assets & Weights
The `Source/VST_Export/` directory is critical. 
- `latent_decoder.onnx`: The inference model.
- `sound_map.json`: Contains `latent_dim`, `sample_rate`, and a list of `points` with `x, y` coordinates and `latent` vectors.

When accessing these files:
- Use `juce::File::getSpecialLocation(juce::File::currentExecutableFile)` to find the relative path to assets.
- Do not commit large binary weights to git unless explicitly requested.

### Audio Architecture (Future VST)
While currently a `guiapp`, the "VST" suffix suggests a transition to a plugin.
- **AudioProcessor:** Handles the audio signal and processing logic. Keep it separate from UI state.
- **AudioProcessorEditor:** Handles the visual interface. Access the processor via the `processor` member.
- **Parameters:** Use `juce::AudioProcessorValueTreeState` for managing plugin parameters and ensuring thread-safe communication between UI and Audio threads.

### Handling Sound Map Data
The `sound_map.json` can be large. 
- **Parsing:** Use `juce::JSON::parse` carefully. For very large files, consider background loading to avoid UI freezes.
- **Data Structures:** Store latent vectors in a way that allows fast distance calculations (e.g., `juce::Array<float>` or `std::vector<float>`).

## 6. Development Workflow for Agents
1. **Analyze:** Examine `LatentAudioVST.jucer` to see which modules and files are included. Check for external dependencies like ONNX Runtime.
2. **Plan:** Propose structural changes (new classes, modules) before editing the `.jucer` file. If adding a new UI component, define its ownership clearly.
3. **Implement:** Maintain the Allman bracing and `//========` separators. Ensure all JUCE classes are prefixed with `juce::`.
4. **Verify:** Use `msbuild` to verify that the project still compiles. If you add files, remind the user to re-save the Projucer project to update the Visual Studio solution.
5. **Self-Correction:** If you encounter "file not found" errors for `JuceHeader.h`, it usually means the Projucer project hasn't been saved recently.

### Audio Thread Safety & Performance
The `processBlock` method in any `AudioProcessor` (or future audio handling code) runs on a high-priority real-time thread.
- **NO Allocations:** Never use `new`, `malloc`, or any container resizing (like `std::vector::push_back`) inside the audio callback.
- **NO Locks:** Avoid `std::mutex` or any blocking synchronization. Use lock-free data structures or `juce::Atomic` for thread-safe communication.
- **NO Logging/IO:** Do not write to logs or files from the audio thread. Use a FIFO/RingBuffer to pass data to a background thread for logging or disk writing.
- **Math:** Use `juce::FloatVectorOperations` for optimized SIMD-based audio processing where possible.

## 7. Useful JUCE Macros & Classes
- `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClassName)`: Essential for all Component/Processor classes.
- `jassert(condition)`: Use liberally to catch logic errors during development.
- `juce::ScopedPointer`: Deprecated. Use `std::unique_ptr`.
- `juce::Rectangle<int>`: Use for all bounds and layout math.
- `juce::Colour`: Access via `juce::Colours::...` or `juce::Colour::fromFloatRGBA(...)`.

## 8. Git & Version Control
- **Binary Files:** ONNX and PTH files are tracked. Avoid committing frequent changes to these unless the model is updated.
- **Generated Files:** Do NOT commit the `Builds/` folder contents (except the `.sln` and `.vcxproj` if they are manually tuned, though Projucer usually manages them).
- **Projucer:** Always update the `.jucer` file when adding new source files.

## 9. Troubleshooting
- **Missing Header:** If `JuceHeader.h` is missing, open the `.jucer` file in Projucer and click "Save Project".
- **Linker Errors:** Ensure that the ONNX Runtime library paths are correctly set in the Projucer's "External Libraries to Link" section for the VS2022 exporter.
