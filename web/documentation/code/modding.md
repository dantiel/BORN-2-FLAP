# Modding Guide

Everything is open source and designed to be changed without a fork or a pull request. This page is the map of where to touch what.

## Quick reference

- Change a bird model: `Flight/Born2FlapRavenCrow.cpp` (`Build`) + the CRAFT tab in `UI/Born2FlapMenu.cpp`.
- Add a servo preset: presets in `UI/Born2FlapTuning.h` + the dropdown in the menu.
- Tweak physics: `MathCore/src/Born2Flap/Math/FFI.hs` (`applyTuning`) and `Vehicle.hs`.
- Add a UI panel: `Brain/lib/born2flap/ui/views/<name>/<name>.umghaml` + `.rb` + a route in `router.rb`.
- Add a world: a new map + entries in `Game/Born2FlapGameMode.cpp` and the menu world list.
- Persist a setting: `Config/FlightPreferences.ini` via `Flight/Born2FlapFlightPreferences.cpp`.

## Rebuild workflow

1. Physics: `Tools/build.ps1` runs `cabal build` and copies the DLL. If you change the C struct layout, bump `B2F_MATH_ABI_VERSION` in `Native/include/born2flap_math.h` and the matching ABI in `MathCore/cbits/bridge.c` and `FFI.hs`.
2. Unreal: the same `build.ps1` runs UnrealBuildTool.
3. UI text: regenerate `UI/Born2FlapI18n.h` via `Brain/bin/gen_i18n_header` after editing locale JSON.

## Adding a bird model

A bird is built procedurally. `ABorn2FlapRaven::Build` (design 1 = ravencrow, 2 = kestrel) emits the mesh parts (wing, fuselage, tail, tail pivot). The model list lives in the menu CRAFT segment and is persisted as `[Flight] BirdModel`. Add a new design by extending `Build`, giving it a design id, and adding it to the CRAFT segment and to `GetBirdModel`/`SetBirdModel`.

## Adding a servo preset

Servo presets are a table of `{name, speed, torque, backdrive, voltage}` in `born2flap::tuning`. Loading a preset writes those into the tuning config. Add a row to the preset table and it appears in the SERVO PRESET dropdown. Slider edits mark the selection as CUSTOM.

## Tweaking physics

The ABI struct `B2F_TuningConfig` (currently ABI v8) carries body mass, CG, mount angle, servo speed/torque, flap frequency and wing parameters. `applyTuning` in `FFI.hs` maps those to the firmware/vehicle. Wing planform and aeroelastic stiffness scale from body mass (see `Planform.hs` and `Structure.hs`). The force model lives in `Vehicle.hs`.

## Adding a UI panel

UMGHAML is a declarative tree that the Brain turns into semantic actions. A panel has a `.umghaml` (the tree) and a `.rb` (the builder + action routes). The Unreal side renders the semantic nodes (see `UI/Born2FlapUIRenderer.cpp`). New actions are routed through `HandleBrainAction` in `Born2FlapUIBridge`.

## Persistence locations

- Flight/tuning/audio settings: `Saved/Config/FlightPreferences.ini`.
- RC controller calibration: `%LOCALAPPDATA%/Born2Flap/Config/RcControllers.ini` (survives recompiles and Steam updates).

## Regenerating the reference

Run `python Tools/gen_code_reference.py` to refresh `web/_code_ref/*.md` after structural changes.
