LatentAudioVST - quick guide

SETUP
- Add the LatentAudioVST.vst3 folder to your VST3 plugins folder and rescan in your DAW (FL: scan in the plugin manager).
- Load a model: click "Load Model..." and pick the model folder (e.g. KickModel),
  or just DRAG THE FOLDER ONTO THE PLUGIN.
  The folder needs an ONNX model (.onnx) and a sound map (.json, preferably sound_map.json).
- Loading happens in the background. A large model takes a few seconds and uses a lot of memory
  (about 1-3 GB). Several instances using the same model share one copy.
- Uses the GPU (CUDA, then DirectML) when available, otherwise the CPU. The header shows which.

PLAYING
- Click the map to play a sound, drag to morph between sounds while it plays.
- Orange lines show which sounds the cursor is blending (thicker = stronger).
- Right/middle drag pans, mouse wheel zooms toward the mouse, double-click resets the view.
- Hover a dot to see its name.

BUTTONS
- PLAY ON NOTE: only MIDI notes play sounds. Moving the cursor (or automating it) changes the sound
  without playing it, so muting your MIDI pattern really makes it silent.
- HOLD: loops the Start-End region until you let go / the note ends. Turning Hold off lets it finish.
- SNAP: the cursor snaps to the nearest sound.
- KEY TRACK: MIDI notes set the pitch.
  * Pitched models (guitar/strings; the header says "plays any note with Key Track"): the model
    generates the actual note you play. You hear the note instantly, and the model's real version
    fades in a moment later. ROOT is not used.
  * Other models: the sound is transposed; ROOT is the note that plays at original pitch.
- RANDOM: jumps to a random sound.
- CENTER: resets the view and puts the cursor in the middle of the map.
- RESET: puts all knobs back to default (keeps the position).
- Double-click any knob to reset just that knob.

AUTOMATION
- Position X / Position Y are automatable (Y = 1 is the top of the map). Dragging on the map
  records automation if your DAW is recording automation.
- The hidden "Inference Rate" parameter limits how often automated movement re-generates the
  sound (0 = as fast as possible). Find it in FL's "Browse parameters".

EXPORTING
- During export (offline render) the plugin generates synchronously, so exports are deterministic.
  Large models on CPU can make exports slower than real time; recording the track live is an
  alternative.

PROJECTS
- The current sound is saved in the project, so it still plays on a machine without the model.
  Load the model folder there to explore again.
