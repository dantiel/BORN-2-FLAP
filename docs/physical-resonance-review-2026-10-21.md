# Physical resonance and servo wave shaping

The user clarified that resonance means a physical mechanical response, not
servo waveform shaping or phase locking. These mechanisms must remain separate.

## What the code actually implements

- `Waveform.hs`: a servo target generator, including the PteronautOS
  `sketches/yoshimitsu/src/MotionWaveform.h` skewed dwell plateau. Its internal
  washboard oscillator controls phase timing; it is not the wing structure.
- The former `Resonance.hs`: a tracking-error filter and a synthetic phase-lock
  equation driven by injected wind phase noise. It had no elastic mass,
  mechanical spring coordinate, or hinge reaction. It was not physical resonance.
- `Structure.hs` / `Vehicle.applyStructure`: static cantilever bending and
  torsion targets followed by first-order relaxation. Such a response can lag
  and settle, but cannot ring freely or produce a mechanical resonance peak.
- `RollDynamics.hs`: coupled body/hinge inertia. This is useful mechanical
  infrastructure, but does not contain an elastic wing mode.

## Correction to the current work

The proposed automatic lag-to-skew coupling was withdrawn. The remaining
tracking estimator is named `ResponsePhase.hs` and has no controller output.
Firmware does not change skew or cadence from those diagnostics. The existing
phase-error telemetry layout remains compatible. The legacy stabilization flag
and wind-phase-noise setter no longer imply that a mechanical resonator exists.
The PteronautOS waveform correction remains servo command shaping only.

## Required physical model (not yet implemented)

Elastic coordinates need displacement and velocity, with a consistent equation
`M q'' + C q' + K q = aerodynamic forcing + base-motion forcing`.
Mass/inertia must come from the wing mass distribution; stiffness from EI/GJ and
geometry; damping must dissipate energy. The elastic coordinates must couple to
the existing body/hinge inertia and return their reaction to the servo and body.
Merely filtering a static target with an oscillator and adding its force to the
existing inertia would risk double-counting inertia or injecting energy.

Dwell and skew determine the forcing spectrum. They may excite or suppress a
physical mode, but must not define its natural frequency or create lift/energy.
A reduced modal model is a practical first implementation; a full flexible beam
is not necessary to demonstrate a real resonance.

Acceptance checks: unforced ring-down; mass/stiffness frequency scaling;
forced frequency sweep; passive energy balance and hinge reaction; timestep
convergence; and full Unreal flight tests. Stored strain-energy diagnostics and
passing scalar waveform tests alone do not establish physical resonance.

## Runtime findings from the broader audit experiment

The pre-change installed DLL and the experimental DLL both failed the full
60 Hz flight, Raven flight and handling scenarios. The experiment introduced
additional safety resets, so it must not be described as a validated flight fix.
The anhedral fixture retains positive lift, but this does not prove controllable
inverted flight. Logs and the pre-change DLL are preserved under `.setup`.

The pre-experiment installed DLL was restored after comparison. The updated
source passes `math-properties`; its newly built DLL is retained in the Cabal
build directory, not installed as a validated flight build.
