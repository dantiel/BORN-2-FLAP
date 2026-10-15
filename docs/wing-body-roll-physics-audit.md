# Wing/body roll coupling audit — 2026-10-15

This records the pre-change diagnosis. The coupled roll solver is now implemented;
see [implementation and verification](wing-body-roll-implementation.md).

## Finding

This is not adequately described as insufficient wing drag. Air-relative wing
motion and inertial recoil already exist, but the actuator and body integrations
do not solve the same coupled dynamics. No flight coefficients or runtime
physics were changed during this audit.

The desired tendency is physically reasonable: a low-roll-inertia fuselage can
rotate substantially between broad wings. Wings are not anchored to the air,
however. Structural inertia, fluid added mass, aerodynamic damping, forward
speed, joint travel, and motor torque determine their world-space movement.
Near rest, quadratic pressure drag is weak; increasing its coefficient cannot
replace acceleration-dependent added mass or correct joint/body coupling.

## What exists and what is missing

* `Vehicle.hs:stepStrip` uses body translation, `omega cross position`, and
  flap velocity together. Pressure drag opposes full normal surface velocity.
  Wing motion through air is therefore already represented.
* `FirmwareVehicle.hs` adds `J*(alphaLeft-alphaRight)` recoil to body roll
  moment. Recoil is not absent. `Planform.hs:shapeFlapInertia` includes structural
  mass and a stripwise flat-plate approximation for added air mass.
* `Servo.hs:stepServo` accelerates each relative joint independently using
  `Jwing + Jrotor`. It receives aerodynamic torque, but neither body inertia nor
  body angular acceleration. It consequently models a fixed actuator base even
  when that base is free to recoil. The other wing is absent from this solve.
* `FlightPawn.cpp:StepMath` subsequently integrates the returned moment using
  Chaos inertia. The comment beside `InertiaTensorScale` explicitly intends to
  include an approximate wing contribution. Simply adding wing inertia again
  would risk double counting. The actual tensor comes from a collision box
  with half-extents 42,12,12 cm and scale factors, not a component mass model.
  The recent 450 g runtime log reports approximately 0.008 kg m² about roll.
* Added air mass enters servo acceleration and recoil but has no consistently
  assembled body/joint/translational mass matrix. The aerodynamic solver
  explicitly omits the old acceleration-differencing force because it was
  unstable. Do not restore that delayed force as a shortcut.
* Aero-to-servo load uses the previous step. Raising pressure drag also raises
  this load, slows or stalls the servo, and can aggravate explicit feedback.
* The physics hinge height is 10 cm; the kestrel visual shoulders are at 4 cm.
  Span geometry also folds shoulder offset into flap radius. These require a
  shared physical geometry definition before calling the result accurate.

## Reproducible evidence

From `MathCore`, run:

```powershell
$env:CABAL_DIR='V:/Born2FlapTools/cabal'
cabal exec -- runghc -isrc test/RollCouplingAudit.hs
```

The reference planform gives one-wing structural inertia 0.000430744 kg m²;
including the current added-air approximation gives 0.001906572 kg m².
The probe calls the actual servo function at zero aerodynamic load and confirms
570.358 rad/s², exactly the fixed-base prediction for its 2 Nm motor and
0.0016 kg m² reflected rotor inertia.

A simplified simultaneous roll/joint solve with the same numbers and a rounded
0.008 kg m² **locked-wing total** roll inertia gives 769.877 rad/s² relative
joint acceleration and -366.956 rad/s² body acceleration. This is about 35%
more joint acceleration for the same torque. It demonstrates the missing
coupling; it is not a predicted in-game maneuver or a calibrated replacement.
Its body and joint equation residuals are below 1e-10 Nm.

The inertial-only body/joint motion ratio in that model is 0.477. Thus the
present assumptions do not themselves predict nearly stationary wings and an
almost entirely moving fuselage. Aerodynamic damping can change that response;
the physical mass distribution also needs calibration. Making the ratio one
by fiat would be another handling assist, not a physical correction.

## Recommended implementation

Use generalized coordinates `phi, qLeft, qRight`, where both positive joint
angles raise their wing. For the simplest coincident, parallel hinge model:

```text
M = [ Ilocked  -J       J     ]
    [ -J       J+R     0     ]
    [ J        0       J+R   ]

M * [phiDD, qLeftDD, qRightDD] =
    [externalRollMoment, motorLeft + aeroLeft, motorRight + aeroRight]
```

`Ilocked` already includes both wing contributions; `R` is reflected drive
inertia. Motor torques are internal and must not also be added to the total
body-coordinate generalized force. This follows the standard floating-base
[multibody equations](https://underactuated.mit.edu/multibody.html).

1. Separate motor control from mechanical integration. Produce signed,
   voltage/speed/torque-limited drive torque; then integrate both joints and the
   fuselage together. Preserve actuator braking, backdrive and joint limits.
2. Derive mass/inertia from one shared geometry and mass distribution for the
   fuselage, battery, drive, tail and wing structure. Keep collision bounds
   independent. Include reflected drivetrain inertia consistently.
3. Assemble added-mass contributions from strip normal-velocity Jacobians.
   This naturally includes body roll, heave and joint cross terms. Use an
   implicit or sufficiently converged coupled integration for aerodynamic
   damping. Do not independently apply added-mass recoil a second time.
4. Make the Unreal bridge exchange the required body inertial data and coherent
   joint/body impulses. Preserve Chaos contact response. Its current mean-load
   application and predicted substeps need checking at 30/60/120 FPS.
5. Only then calibrate drag and membrane/servo behavior against measurements.

## Acceptance tests before replacement

* In vacuum, internal servo motion conserves total angular momentum and motor
  work accounts for kinetic energy. Equal symmetric strokes cancel roll.
* Opposite roll commands mirror each other; neutral unpowered wings receive no
  artificial pose-restoring torque.
* A locked body reproduces the existing fixed-base actuator limit. A light free
  body changes joint acceleration according to the coupled mass matrix.
* Exact cancellation of body rotation and joint motion removes their local
  aerodynamic velocity contribution; retain real root-translation effects.
* Sweep airspeed, body mass, wing mass, drive torque, wind, and integration step.
  Log fuselage angle plus both wing world-space angles, not only servo angles.
* Verify hand launch, glide roll, powered roll, stalls, ground contacts, and
  recovery in full Unreal 3D. A passing scalar probe cannot certify these.

The diagnostic added here deliberately leaves current flight behavior intact.
It establishes a concrete reason to replace the sequential actuator/recoil
approximation rather than continue increasing the air-resistance parameter.
