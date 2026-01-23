PROJECT WAS BUILT WITH PROJUCER/JUCE. - https://juce.com/download/

Project was made in Visual Studio 2022 but i assume other versions will work (check juce) - https://visualstudio.microsoft.com/downloads/

USES ONNX RUNTIME (CPU) - https://onnxruntime.ai/

Once built, Onnx dlls will go into the Built VST3 folder, next to the nested vst3 file.
`LatentAudioVST.vst3\Contents\x86_64-win\LatentAudioVST.vst3` <-- dlls here

# LatentAudioVST

LatentAudioVST is a JUCE-based VST3 audio plugin that provides real-time interactive exploration of latent audio representations. It allows musicians and sound designers to navigate a 2D latent space where each position corresponds to a unique audio sample, enabling continuous and creative audio generation through visual exploration.

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

### Main Components

#### Nebula View (Top)
- **Visual Map**: 2D representation of the latent space
- **Points**: Individual audio samples positioned by similarity
- **Cursor**: Current playback position (white circle)
- **Navigation**: Click and drag to explore different sounds

#### Waveform Display (Middle)
- **Generated Audio**: Real-time visualization of current sound
- **Position Indicator**: Shows playback progress through the sample

#### Control Panel (Bottom)
- **Transport Controls**: Play/pause, position scrubbing
- **Parameter Sliders**: Audio processing controls
- **Model Loading**: Load different audio models

### Parameter Controls

#### Playback
- **Playback Rate**: Speed control (0.1x to 2.0x)
- **Fine Tune**: Pitch adjustment (-100 to +100 cents)

#### Audio Processing
- **Filter Cutoff**: Low-pass filter frequency (20Hz - 20kHz)
- **Filter Resonance**: Filter emphasis (0.0 - 1.0)

#### Envelope (ADSR)
- **Attack**: Time to reach full volume (0.001 - 2.0s)
- **Decay**: Time to reach sustain level (0.001 - 2.0s)
- **Sustain**: Volume during hold (0.0 - 1.0)
- **Release**: Time to fade out (0.01 - 5.0s)

#### Morphing
- **Morph Speed**: Automation rate when enabled (0.0 - 5.0)
- **Morph Rate**: Oscillation frequency (0.1 - 20.0 Hz)
- **Morph Depth X/Y**: Movement range in each dimension (0.0 - 1.0)

#### Master
- **Output Gain**: Overall volume (-∞ to +6dB)
- **Dry/Wet Mix**: Blend between processed and unprocessed audio (0% - 100%)
- **Sample Length**: Portion of generated audio to play (10% - 100%)
- **Sample Start**: Starting position within the sample (0% - 90%)

#### Triggering
- **Latch Mode**: Toggle between one-shot and sustained playback
- **Trigger on MIDI**: Enable MIDI note triggering
- **Snap Mode**: Quantize position to nearest sound map point

## Features

### Real-time Exploration
Navigate the latent space continuously, with smooth interpolation between audio samples creating organic transitions between different sounds.

### Morphing
Enable automated movement through the latent space with customizable speed, depth, and oscillation patterns for evolving soundscapes.

### MIDI Integration
Trigger audio generation via MIDI notes, allowing synchronization with sequencers and musical performance.

### Envelope Shaping
Full ADSR envelope control for shaping the amplitude of generated sounds, from percussive to sustained tones.

### Audio Processing
Built-in low-pass filtering with resonance control for timbral modification of the generated audio.

### Crossfading
Seamless transitions between different positions with adjustable crossfade time to eliminate audio artifacts.

### Playback Control
Variable speed playback and fine pitch tuning for creative manipulation of the generated audio.

### Model Flexibility
Support for different ONNX models and sound maps, allowing exploration of various audio domains and styles.

## Technical Architecture

### Core Components

#### LatentDecoder (`LatentDecoder.cpp/h`)
- Manages ONNX Runtime session
- Handles model loading and inference
- Converts latent vectors to audio waveforms

#### SoundMap (`SoundMap.cpp/h`)
- Parses JSON sound map files
- Provides latent vector interpolation
- Manages coordinate-to-vector mappings

#### PluginProcessor (`PluginProcessor.cpp/h`)
- Main audio processing logic
- Manages playback state and envelopes
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
3. Use the envelope controls to shape the sound's character
4. Enable morphing for automated exploration

### Creative Techniques
- **Morphing Exploration**: Set morph speed > 0 and adjust depth for evolving textures
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
- Reduce inference rate for smoother playback
- Increase crossfade amount
- Check for CPU overload

#### Model Loading Errors
- Verify ONNX model file is not corrupted
- Check JSON sound map format matches expected structure
- Ensure latent dimensions are consistent

#### Performance Issues
- Reduce morph complexity or inference rate
- Use smaller models or sparser sound maps
- Close other CPU-intensive applications

### Debug Information
The plugin writes detailed logs to:
`C:\Users\[username]\Documents\LatentAudioVST_debug.log`

Check this file for detailed error information and troubleshooting data.

## Development

### Building
See `AGENTS.md` for detailed build instructions using Projucer and Visual Studio.

### Model Training
LatentAudioVST is designed to work with autoencoder-style models that compress audio into latent representations. The plugin expects:
- Input: Latent vector (typically 128 dimensions)
- Output: Audio waveform (mono, target sample rate)

### Contributing
This is an open-source project. Contributions for new features, bug fixes, and model compatibility are welcome.

## License

This project is licensed under the GNU Affero General Public License Version 3 (AGPLv3). See the [LICENSE](LICENSE) file for details.

## Credits

Built with JUCE framework and ONNX Runtime.