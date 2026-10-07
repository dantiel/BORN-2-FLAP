# Native C/C++

The ABI boundary between the Haskell physics and the rest of the system, plus the RC/desktop input shims.

## Important files

### `include/born2flap_math.h` - the C ABI
The single header that declares the physics interface.
- `B2F_MATH_ABI_VERSION` (currently `8u`) - bumped whenever the struct layout changes.
- `B2F_VehicleInput` / `B2F_VehicleOutput` - raw vehicle step in/out.
- `B2F_PilotInput` / `B2F_TuningConfig` - firmware-emulation input and the tuning struct.
- `b2f_math_*` - `abi_version`, `runtime_init/shutdown`, `create/destroy/step_vehicle`, `reconfigure_firmware_vehicle`, `step_firmware_vehicle`.

### `src/born2flap_math_bridge.cpp` - the C++ shim
Loads `born2flap_math.dll` and exposes a thin C++ wrapper over the C ABI. This is the only place that touches the DLL.

### `include/born2flap_rc_input.h` / `born2flap_rc_calibration.h` / `born2flap_rc_windows.h`
RC transmitter input, calibration structures, and the Windows joystick/RawInput backend.

### `include/born2flap_desktop_input.h`
Desktop keyboard/mouse input abstraction.

## ABI discipline

The Haskell exports (`hs_b2f_math_*` in `FFI.hs`) and the C header must agree exactly. `MathCore/cbits/bridge.c` also carries the ABI version constant. When you add a field to `B2F_TuningConfig`, bump the version in all three places or the game will refuse to load the DLL (the "expecting new physics engine version" error).
