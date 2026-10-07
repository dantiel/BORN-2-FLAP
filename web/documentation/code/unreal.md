# Unreal Engine C++

The game layer: the flight pawn, the fullscreen menu, the UMG renderer and the audio synthesiser. Lives under `Unreal/Born2Flap/Source/Born2Flap/`.

## Important files

### `Flight/Born2FlapFlightPawn.{h,cpp}` - the bird
The heart of the game. Bridges input to the Haskell DLL and back.
- `ApplyTuning` / `ApplyBodyMass` - push tuning + mass into the physics.
- `SetBodyMassKg` / `GetBirdModel` / `SetBirdModel` - bird identity.
- `UpdateAeroAudio` - drives the aero-audio synth (wind/wing/servo/leaves).
- `SaveFlightPreferences` - persists the tuning state.
- `WingbeatVolume` / radio volume accessors - per-voice audio gain.

### `Game/Born2FlapGameMode.{h,cpp}` - level/menu lifecycle
- `InitGame` - decides menu mode vs in-level; handles boot/loading.
- `BeginPlay` / `Tick` - spawns the menu, handles F3/F10 entry.
- `OpenFlightLevel` / level travel - world loading and the FLY path.

### `UI/Born2FlapMenu.{h,cpp}` - the fullscreen menu
- `Build` - the sidebar rail + content pane.
- `OnAction` - routes all menu actions (worlds, flight desk, settings, RC).
- `BuildFlightDesk` / `BuildPreferences` - tuning and general-settings pages.
- `NavPage` / `PrefsPage` / `SettingsPage` - page/sub-tab state.

### `UI/Born2FlapMenuSettings.{h,cpp}` - settings store
- `born2flap::FMenuSettings` - a pawn-independent store that reads/writes `FlightPreferences.ini`, so settings work on the home screen.

### `Flight/Born2FlapFlightPreferences.{h,cpp}` - persistence
- Load/save of `[Flight]` / `[Audio]` / `[Tuning]` sections in `FlightPreferences.ini`.

### `Input/Born2FlapRcController.{h,cpp}` - RC transmitter
- Device enumeration, calibration wizard, channel mapping.
- Persists calibration to `%LOCALAPPDATA%/Born2Flap/Config/RcControllers.ini`.

### `Flight/Born2FlapRavenCrow.{h,cpp}` - bird mesh
- `ABorn2FlapRaven::Build` - procedurally builds the wing, fuselage, tail and tail pivot (design 1 = ravencrow, 2 = kestrel).

### `Flight/Born2FlapWingMesh.{h,cpp}` - wing mesh
- Procedural wing surface from the planform + twist/camber.

### `Audio/Born2FlapAudioEngine.h` + `Born2FlapAudioSynth` + `Born2FlapAeroAudio` + `Born2FlapSoundscape`
- `Born2FlapAudioEngine` - voice management + per-voice gain clamps.
- `Born2FlapAeroAudio` - the wing/wind/servo synthesis (incl. backlit-independent gain).
- `Born2FlapSoundscape` - ocean/wind/music ambience.

### `UI/Born2FlapUIRenderer.{h,cpp}` - UMG semantic renderer
- Renders semantic nodes (panel, tabs, slider, dropdown, segment, image) to UMG widgets.

### `UI/Born2FlapUIBridge.{h,cpp}` - Brain bridge
- `HandleBrainAction` - dispatches UMGHAML actions into the game.

### `UI/Born2FlapTuning.h` - tuning metadata
- `born2flap::tuning::ETuningField` and `Rows[]` - the slider/field table; `ToDisplayValue`/`FromDisplayValue` unit conversions (kg.cm, s/60deg, grams).

### `Racing/Born2FlapSpirit.{h,cpp}` + `Born2FlapRacing`
- `FB2FSpiritRecording` (with `BirdModel`) and `SetupSpirit` - replay ghosts that rebuild the correct bird model, grayed-out and translucent.
