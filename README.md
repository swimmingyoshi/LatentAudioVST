PROJECT WAS BUILT WITH PROJUCER/JUCE. - https://juce.com/download/

Project was made in Visual Studio 2022 but i assume other versions will work (check juce) - https://visualstudio.microsoft.com/downloads/

USES ONNX RUNTIME 1.16.3 - https://github.com/microsoft/onnxruntime/releases/tag/v1.16.3
- CPU build: `onnxruntime-win-x64-1.16.3.zip`
- GPU build (adds CUDA): `onnxruntime-win-x64-gpu-1.16.3.zip`

At model load the plugin tries CUDA -> DirectML -> CPU and uses the first provider that works; the active provider is shown in the UI. Without the CUDA provider DLLs (or a CUDA-capable GPU) it falls back to CPU automatically.

If Building yourself, you need to have the onnx folder in Libs (extract either build's contents into this folder name): 
`.\Libs\onnxruntime-win-x64-1.16.3`

The `.jucer` expects the JUCE `modules` folder at `../../VST_Plugins/JUCE/modules` relative to this project folder. Change the module paths in Projucer if your JUCE lives elsewhere.

Once built, Onnx dlls will go into the Built VST3 folder, next to the nested vst3 file.
`LatentAudioVST.vst3\Contents\x86_64-win\LatentAudioVST.vst3` <-- dlls here

# LatentAudioVST

LatentAudioVST is a JUCE-based VST3 audio plugin that provides real-time interactive exploration of latent audio representations. It allows musicians and sound designers to navigate a 2D latent space where each position corresponds to a unique audio sample, enabling continuous and creative audio generation through visual exploration.

[Watch the demo video on YouTube](https://www.youtube.com/watch?v=94xgIDpDgX0)

## How It Works

LatentAudioVST uses machine learning models trained on audio samples to create a compressed "latent" representation of sound. Each point in a 2D coordinate system maps to a high-dimensional latent vector that, when decoded by an ONNX neural network model, produces audio.

### Core Concept
- **Latent Space**: A 2D map where similar sounds are positioned close together
- **ONNX Model**: Neural network that converts latent vectors into audio waveforms
- **Sound Map**: JSON file containing coordinate-to-latent vector mappings
- **Real-time Decoding**: Instantaneous audio generation as you navigate the space

### Data Flow
1. User loads a model folder containing:
   - `latent_decoder.onnx`: The neural network model
   - `sound_map.json`: Coordinate mappings and metadata

2. User interacts with the visual interface to select positions

3. Plugin interpolates latent vectors between nearby points

4. ONNX model decodes the latent vector into audio

5. Generated audio is processed and played back

## Installation & Setup

### Prerequisites
- DAW supporting VST3 plugins (Ableton Live, Logic Pro, FL Studio, etc.)
- Windows 10/11 (currently Windows-only)


## Prebuilt Binary

A precompiled VST binary is available in the [GitHub Releases](https://github.com/swimmingyoshi/LatentAudioVST/releases) section.  
This binary includes third-party software listed in `THIRD_PARTY_LICENSES.md`.  
Source code is always available in this repository.

Model and soundmap json can be found in huggingface [repo](https://huggingface.co/Meowmeow42/LatentAudioVST/tree/main)

### Installation
1. Build the project using the provided Visual Studio solution
2. Copy the resulting `.vst3` file to your VST3 directory:
   - Default: `C:\Program Files\Common Files\VST3\`
3. Restart your DAW
4. Scan for new plugins

### Model Setup
1. Obtain or train a latent audio model (ONNX format)
2. Create a sound map JSON file with the following structure:
```json
{
  "latent_dim": 128,
  "sample_rate": 44100,
  "points": [
    {
      "label": "Sample_0",
      "x": -0.596,
      "y": 0.117,
      "latent": [1.187, 0.912, -0.362, ...]
    }
  ]
}
```
3. Place both files in a folder and load via the plugin's "Load Model Folder" button

## User Interface

### Layout
- **Header**: model name and status (sound count, CPU/GPU), *Load Model...* and *Unload*. You can also **drag a model folder (or its .onnx / .json) onto the plugin** to load it. Loading runs in the background, so the DAW never freezes.
- **Toolbar**: *Play on Note*, *Hold*, *Snap*, *Key Track*, *Random*, *Center*, MIDI activity light, cursor coordinates, *Reset*.
- **Sound map**: click/drag to play, right/middle-drag to pan, mouse wheel zooms toward the pointer, double-click resets the view. Hover a dot to see its name. Orange lines show which sounds the cursor is blending and how strongly.
- **Waveform**: the generated sound, the Start-End region that plays, and the playhead.
- **Knobs**: double-click any knob to reset it; hover for a description.

### Parameter Controls

#### Playback
- **Rate**: playback speed (0.1x - 2.0x, also changes pitch)
- **Tune**: fine tune (-100 to +100 cents)
- **Loop X-Fade**: crossfade at the loop point when Hold is on
- **Root**: with Key Track on, the MIDI note that plays at the original pitch

#### Filter
- **Cutoff** / **Reso**: 4-pole low-pass with resonance, smoothed so automation doesn't zipper. The graph shows the real response in dB.

#### Output
- **Start** / **End**: the part of the sound that plays (and loops, with Hold)
- **Velocity**: how much MIDI velocity affects volume (0 = ignore velocity)
- **Gain**: output level (shown in dB)

#### Modes
- **Play on Note**: only MIDI notes start sounds; moving the cursor or automating the position changes the sound without playing it
- **Hold**: loop between Start and End until released (turning Hold off lets the loop finish)
- **Snap**: snap the cursor to the nearest sound
- **Key Track**: MIDI notes transpose the sound relative to Root

#### Automation-only parameters
- **Position X / Position Y**: cursor position (0-1, Y = 1 is the top of the map)
- **Inference Rate**: maximum decodes per second while the position is automated (0 = unlimited)
- **Level**: extra output level (kept for older projects)

## Features

### Real-time Exploration
Inference runs on a background thread and always decodes the newest position, so fast drags or automation never queue up. New sounds crossfade in over 50 ms; restarting a playing note is declicked.

### Correct Pitch at Any Sample Rate
Sounds play at the model's sample rate regardless of the DAW's (44.1, 48, 96 kHz...).

### GPU Inference
Decoding runs on CUDA or DirectML when available and falls back to CPU otherwise.

### MIDI Integration
Sample-accurate note timing, velocity sensitivity, optional key tracking. Overlapping notes are handled legato: releasing an old note does not cut a newer one.

### Envelope
A fixed, very short attack/release gate (1 ms / 20 ms) declicks note starts and stops; the generated sample's own shape is preserved.

### Projects Keep Their Sound
The current sound is saved in the project, so it still plays if the model folder is missing on another machine.

## Technical Architecture

### Core Components

#### LatentDecoder (`LatentDecoder.cpp/h`)
- Manages ONNX Runtime session (shared across plugin instances)
- Picks the execution provider (CUDA -> DirectML -> CPU)
- Handles model loading and inference
- Converts latent vectors to audio waveforms

#### SoundMap (`SoundMap.cpp/h`)
- Parses JSON sound map files
- Provides latent vector interpolation
- Manages coordinate-to-vector mappings

#### PluginProcessor (`PluginProcessor.cpp/h`)
- Main audio processing logic
- Manages playback state, crossfades, and the live filter
- Coordinates inference and UI updates

#### InferenceThread (`InferenceThread.h`)
- Asynchronous inference execution
- Prevents audio thread blocking
- Thread-safe result handling

#### NebulaComponent (`NebulaComponent.cpp/h`)
- 2D visualization of latent space
- User interaction handling
- Position mapping and cursor display

#### WaveformComponent (`WaveformComponent.cpp/h`)
- Audio waveform visualization
- Playback position indication
- Real-time display updates

### Audio Architecture
- **Double Buffering**: Seamless crossfading between generated samples
- **Thread Safety**: Lock-free communication between threads
- **Real-time Priority**: Audio thread optimizations for low latency
- **Memory Management**: Efficient buffer handling for continuous playback

### Data Structures
- **Latent Vectors**: 128-dimensional float arrays representing compressed audio features
- **Sound Points**: Coordinate positions with associated latent vectors and labels
- **Audio Buffers**: Stereo float buffers for generated and processed audio

## Usage Tips

### Getting Started
1. Load a model folder with compatible ONNX model and JSON sound map
2. Click around the nebula view to explore different sounds
3. Use the filter, playback rate, and sample start/length to shape the sound
4. Automate the hidden Position X/Y parameters for movement over time

### Creative Techniques
- **Automated Exploration**: Draw automation for Position X/Y for evolving textures
- **MIDI Sequencing**: Use with a DAW to create sequenced latent space journeys
- **Layering**: Combine multiple instances with different models for complex sound design
- **Filtering**: Use the filter to create movement within static positions

### Performance Optimization
- Higher inference rates provide more responsive playback but use more CPU
- Model size affects loading time and memory usage
- Sound map density impacts interpolation quality and responsiveness

## Troubleshooting

### Common Issues

#### Plugin Won't Load
- Ensure VST3 file is in the correct directory
- Check that ONNX Runtime dependencies are available
- Verify JUCE framework compatibility

#### No Audio Output
- Confirm model is loaded successfully (check log file)
- Verify sound map contains valid data
- Check audio interface settings in your DAW

#### Crackling/Popping Sounds
- Lower the Inference Rate parameter
- Check for CPU overload

#### Model Loading Errors
- Verify ONNX model file is not corrupted
- Check JSON sound map format matches expected structure
- Ensure latent dimensions are consistent

#### Performance Issues
- Lower the inference rate parameter
- Use smaller models or sparser sound maps
- Close other CPU-intensive applications

### Debug Information
The plugin writes detailed logs to:
`C:\Users\[username]\Documents\LatentAudioVST_debug.log`

Check this file for detailed error information and troubleshooting data.

## Development

### Building
See `AGENTS.md` for detailed build instructions using Projucer and Visual Studio.

### Tests
`Tests/` builds a console test runner from the same sources with CMake:

```bash
cmake -S Tests -B Tests/build -G "Visual Studio 17 2022" -A x64
cmake --build Tests/build --config Release
Tests/build/LatentAudioTests_artefacts/Release/LatentAudioTests.exe
```

They use a tiny deterministic model in `Tests/data/tiny_model` (regenerate with `Tests/tools/make_tiny_model.py`). Set `LATENTAUDIO_REAL_MODEL` to an exported model folder to also run an end-to-end test (with decode timing) on a real model.

### Model Training
LatentAudioVST is designed to work with autoencoder-style models that compress audio into latent representations. The plugin expects:
- Input: Latent vector (typically 128 dimensions)
- Output: Audio waveform (mono, target sample rate)

### Contributing
This is an open-source project. Contributions for new features, bug fixes, and model compatibility are welcome.

## License

This project is licensed under the GNU Affero General Public License Version 3 (AGPLv3). See the [LICENSE](LICENSE) and [LICENSE INTENT](LICENSE-INTENT.md) file for details.

## Third-Party Libraries

See `THIRD_PARTY_LICENSES.md` for attribution and licensing of libraries included in the binary.

## Credits

Built with JUCE framework and ONNX Runtime.