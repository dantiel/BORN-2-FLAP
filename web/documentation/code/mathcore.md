# Haskell MathCore

The physics brain. Pure Haskell, compiled to `born2flap_math.dll` and called through a stable C ABI. Every file here is total and tested.

## Important files

### `FFI.hs` - the C ABI surface
The foreign-function exports and the top-level orchestration.
- `b2f_math_abi_version`, `b2f_math_runtime_init/shutdown` - lifecycle.
- `b2f_math_reconfigure_firmware_vehicle` - applies a `B2F_TuningConfig`.
- `b2f_math_step_firmware_vehicle` / `b2f_math_step_vehicle` - the per-tick advance.
- `applyTuning` - maps tuning fields to `FirmwareParams` and the planform scale.

### `Vehicle.hs` - aerodynamics
The force model: per-strip lift/drag, stall, crossflow, added mass.
- `vehicleForces` - assembles strip forces into the total force/moment.
- `stepStrip` / section incidence - local angle of attack and separation state.
- `computeFlapThrust` - the momentum-jet downstroke thrust term.

### `Firmware.hs` - servo mixer and control surfaces
Pilot sticks -> mixer -> servo commands.
- `computeServoMixer` - throttle/roll/pitch/yaw -> per-servo flap + glide commands.
- `flappingBranch` / `glideBranch` - flap vs glide amplitude logic.
- `FirmwareParams` / `PilotInput` - the tuning and stick records.

### `FirmwareVehicle.hs` - firmware-level vehicle step
- `advanceFirmwareVehicle` - runs mixer -> servo -> wing in one timestep.

### `Servo.hs` - actuator model
- `stepServo` - rate/torque-limited tracking against hinge load.
- `ServoSpec` / `ServoState` / `BatterySpec` - servo and battery records.

### `Wing.hs` - wing kinematics/aero
- Wing strip state, flapping geometry, and the wing-level force assembly.

### `Planform.hs` - wing geometry
- `defaultBirdWing`, `shapeChord`, `shapeTwistRad` - the reference planform.
- planform scale derived from body mass (`(mass/0.45)^(1/4)`).

### `Structure.hs` - aeroelasticity
- `applyStructure` - bending (EI) and torsion (GJ) stiffness, relaxed per strip.
- The global kestrel spar (1.6 mm inner / 1.2 mm outer) lives here as the reference.

### `Section.hs` - blade-element section
- Per-section aerofoil coefficients and stall behaviour.

### `Waveform.hs` - flap waveform
- Asymmetric dwell / pointed, beat-locked flap waveform.

### Supporting modules
- `PhaseEnvelope.hs` - phase coverage of the flap cycle.
- `Resonance.hs` - phase error / resonance tracking.
- `Control.hs` - control law helpers.
- `OrderTracker.hs` - order tracking.
- `FerocityPrice.hs` - stroke ferocity cost model.
- `Simulation.hs` - top-level simulation glue.
- `Types.hs` - shared records (`Vec3` etc.).
