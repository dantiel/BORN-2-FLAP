# Coupled wing/body roll implementation

## Runtime changes

`RollDynamics.hs` solves body roll and both parallel wing hinges together.
Internal motor torque and joint-stop impulses preserve the body-coordinate
angular momentum equation. The old post-hoc recoil term is removed.

The existing servo controller supplies a bounded motor-torque demand instead
of prescribing the physical joint trajectory. Its implicit velocity feedback
reproduces the former fixed-base reference when the base is held and the load
is constant, while allowing the free body and opposite wing to participate.
Motor limits still depend on voltage and stall torque. The backdrive setting
controls dissipative resistance under overpowering loads; a dead battery
supplies no motor torque, but airflow can move its wings.

The implicit solve evaluates nonlinear aerodynamic forces at each candidate
body roll and wing velocity. Aerodynamic memory and the stroke-phase drag
regime stay fixed during the solve; otherwise trial velocities can trigger
discontinuous force jumps at reversal. Memory and structural deformation are
advanced once afterward. Newton iteration reuses its numerical Jacobian for
up to three iterations while still evaluating the full nonlinear residual.
Unconverged steps retry at smaller time steps, including the intermediate body
roll response, and fail transactionally if subdivision cannot resolve them.
Mirrored strip moments are summed pairwise to avoid seeding artificial roll
from cancellation error in symmetric flight.

ABI 12 adds required `B2F_BodyState.roll_inertia_kg_m2`. Unreal supplies its
actual dry, locked-wing Chaos inertia. The mass matrix adds modeled fluid
inertia once, and the returned roll torque is the equivalent impulse for the
host inertia. It must not receive a second recoil or added-mass correction.
The prediction loop now uses the same CG-adjusted moment as the applied load.
An incompatible DLL is rejected by the existing ABI version check.

## Reproduction

```powershell
$env:CABAL_DIR='V:/Born2FlapTools/cabal'
cabal test math-properties --test-show-details=direct --project-dir=MathCore
& Unreal/Born2Flap/Tools/build.ps1
python Tools/test-roll-coupling.py
```

The DLL probe also accepts `--dll <path>` for isolated builds. It tests invalid
inertial inputs, mirrored roll, finite motion, joint travel, two host inertias,
0/8/15 m/s airflow, and 120/240/480 Hz step sizes. Its translation/pitch/yaw
are constrained like a roll-axis wind-tunnel fixture; it is not full flight.

MathCore tests cover the analytic coupled acceleration, angular momentum,
symmetric strokes, passive dissipation, end-stop energy/momentum, loss of motor
power, controller integration, and aerodynamic roll symmetry. Long integration
tests explicitly reject failed steps rather than averaging rollback outputs.

## Scope and limits

This is a parallel-hinge roll correction, not a complete articulated six-axis
fluid/structure simulator. Fluid heave/pitch/yaw cross-inertia, moving structural
CG, detailed rotor/gear inertia, and exact offset-hinge inertia still need a
shared physical geometry and component mass model. The host's dry inertia is
still derived from its existing collision geometry. No measured mass values
were invented, and wings are not constrained to an artificial world-space pose.

The numerical correction changes handling; existing open-loop maneuver tests
must be reported separately from conservation and roll-fixture results.
Full Unreal verification results are recorded below after execution.

## Verification, 2026-10-15

- `math-properties`: PASS, including conservation, end stops, world-stationary
  wing airflow, and force continuity through trial stroke reversal.
- Actual rebuilt ABI-12 DLL fixture: PASS. Mean step time was 1.95 ms on this
  machine (isolated fixture, not a whole-game frame-time measurement).
- At 8 m/s and dry locked-wing inertia 0.008 kg m2, the 240 Hz roll fixture
  rotated the body 7.29 degrees while each wing moved 1.29 degrees in world
  space. Opposite commands mirrored the result. Body rotation was 7.64 at
  120 Hz and 7.52 at 480 Hz. This is a constrained roll fixture, not a claim
  that wings remain stationary in every flight condition.
- Full 110-second Unreal tests at 30, 60 and 144 fps all completed with
  `mathFailures=0`, `safety=0`, and healthy physics. They loaded the new DLL
  through the development-only `B2FPhysicsTestLibrary` override.
- Overall flight acceptance remains FAIL: the scripted turn and pull-up
  speed reduction fail at all three frame rates; landing fails at 30 fps;
  peak speed exceeds 25 m/s at 60 and 144 fps. Do not interpret numerical
  stability as complete flight-model validation or loosen those assertions.

| FPS | Powered/coast/boost altitude (m) | Peak speed (m/s) | Pull-up speed (m/s) | Landing |
| --- | --- | --- | --- | --- |
| 30 | 16.19 / 14.76 / 89.11 | 23.83 | 13.04 to 17.54 | fail |
| 60 | 15.72 / 14.12 / 82.31 | 25.60 | 12.82 to 17.36 | pass |
| 144 | 15.62 / 13.91 / 78.50 | 26.09 | 12.73 to 16.90 | pass |

Evidence: `Unreal/Born2Flap/Saved/Logs/roll-math-tests.log`,
`roll-dll-test.json`, and `coupled-roll-flight-{30,60,144}.log`.
The wider handling failures still require investigation of the complete
pitch/yaw/translation model and maneuver trajectories. The correction does
not establish a fully calibrated six-axis aircraft simulation.

The DLL was installed and hash-verified against the tested build (SHA256
`0A099B60A114AAFEA9C23345FC3397BA77751A793FF8542EF6060AA66CF00A23`).
The installed DLL fixture also passed. Unreal Development build succeeded.
The final rendered 60-fps run completed all 110 seconds, including reset and
relaunch, with the same maneuver results as the headless 60-fps test and zero
physics failures. The flight screenshot was visually inspected.

The first rendered run exposed a separate reset-time `GateTex` invalid-texture
crash. The gate actor/material/texture arrays now have transient UPROPERTY
ownership so unused artwork survives garbage collection. The rebuilt rendered
run completed without that crash. Evidence:
`Saved/Logs/coupled-roll-installed-rendered.log` and
`Saved/Screenshots/WindowsEditor/AerodynamicFlight.png` under the Unreal project.

## In-game integration correction, 2026-10-16

The previous isolated fixture missed a host timing defect and did not use the
user's saved aircraft. The current preferences specify a 10 g membrane bird,
750 deg/s servos and 100% aileron/elevator throws. The earlier reference fixture
used the default 450 g aircraft and smaller throws.

`FChaosScene::SetUpForFrame` caps physical time to
`MaxSubsteps * MaxSubstepDeltaTime` when substepping is enabled. Here this is
8 * 1/240 s, about 33 ms. StepMath previously advanced its wing states by up to
100 ms and supplied averaged torque to a body that advanced only 33 ms. At
12 fps, wings advanced about 83 ms while their body's reaction advanced 33 ms.
The fixed-step remainder also meant forces and hinges did not always represent
the same duration at high frame rates.

StepMath now mirrors Chaos's configured time budget and divides exactly that
duration into steps no longer than about 1/240 s. The hinges, predicted body
motion and applied mean loads therefore cover the same physical interval.
A numerical tolerance prevents float rounding from creating a spurious extra
step at 60/30 fps. If rendering falls below the engine's physical time budget,
both body and hinges slow together; this is not an unlimited catch-up loop.

Fixed strip geometry is now prepared once per coupled solve, with explicit
strict bindings before the candidate-rate closure. Merely partially applying
the former function did not reliably cache its geometry. The full nonlinear
force evaluation and all solver checks remain. Installed-DLL fixture cost fell
from roughly 1.95 ms to 0.63 ms per step (about 3.1x faster). The saved-settings
fixture measured 0.62 ms. These are physics costs, not whole-level FPS promises.

Verification:

- MathCore properties and installed-DLL tests: PASS.
- Saved-settings DLL tests: PASS for numerical validity, mirrored input and
  timestep comparison. Large sustained commands can still rotate the complete
  aircraft; those checks do not assert world-locked wings.
- New `-B2FRollResponseTest -B2FFlightTest -B2FUseSavedFlightSettings` checks
  actual Chaos body, attached tail and both visible wing transforms during a
  short roll command in free flight. Saved-settings diagnostics never write
  the user's preferences. All three runs passed:

| Render rate | Body roll change | Left/right wing world roll change | Tail roll |
| --- | --- | --- | --- |
| 12 fps | 6.00 deg | 1.94 / 1.94 deg | 5.83 deg |
| 60 fps | 5.05 deg | 0.96 / 0.96 deg | 4.91 deg |
| 144 fps | 5.48 deg | 1.42 / 1.42 deg | 5.32 deg |

The 60-fps test rendered successfully and `RollResponse.png` was inspected.
Logs: `roll-transforms-{12,60,144}.log`, `roll-integration-properties.log`,
`roll-optimized-dll.json`, `roll-saved-settings.json` in Saved/Logs.
Longer saved-settings flight scripts also completed without math failures or
safety resets, but their overall maneuver acceptance remains FAIL. In particular,
script timing is wall-clock based whereas physics slows below 30 fps, so those
long scripts are not frame-rate equivalence tests. The focused roll test uses
physical elapsed time and checks the rendered hierarchy directly.

Rebuilt and installed DLL SHA256:
`5B64BBE04B42CD1954C34238F04C0A2C6EBB125E936159AC362D9CBF4517364C`.

## 50 g aircraft and modest glide resistance increase

At the user's request, the saved aircraft mass is now 0.05 kg. Glide-only
normal pressure drag is multiplied by 1.25 (effective coefficient 3.75 instead
of 3.0). This is an empirical handling adjustment opposing full air-relative
normal motion, including sink; it is not a world-space constraint. Powered
flapping retains its former pressure-drag coefficients.

Build, MathCore properties, and the 50 g saved-settings DLL probe pass. The
Unreal roll-transform test passes at 60 fps: body 2.788 degrees, tail 2.765,
wing world changes -1.445 degrees each. DLL cost measured 0.65 ms per step.
The large-command fixture at 8 m/s showed a modest reduction in wing world
rotation, 54.52 to 53.84 degrees; this change does not lock wings in the air.
Evidence: roll-50g-build.log, roll-50g-properties.log, roll-50g-before.json,
roll-50g-after.json, roll-50g-transforms.log under Saved/Logs. Installed build.

## Strong glide response candidate (requested 2026-10-16)

Glide pressure-drag gain is now 40.0, compared with the previous 1.25. This is
explicitly strong empirical handling tuning, not a measured aerodynamic
coefficient. It remains a passive force against real surface-relative flow;
powered-flapping coefficients and the saved 50 g mass are unchanged.

With the same 50 g setup and fast roll at 8 m/s, the constrained DLL fixture
shows wing world movement reduced from 53.84 to 34.00 degrees (37%), with
body rotation increasing from 6.16 to 26.00 degrees in magnitude. At 15 m/s,
wing world movement drops from 75.76 to 49.29 degrees. This is a meaningful
response increase, not a claim of world-locked wings in every condition.

Math properties and actual DLL checks pass. Runtime cost is about 0.78 ms per
step. The full 110-second Unreal flight script PASSES with the saved settings:
turn, landing, reset, relaunch; safety resets 0, math failures 0. The 12 fps
Unreal transform test also passes (body 5.060 deg, wings 0.846/0.847 deg,
tail 5.035 deg). Evidence: roll-strong40-properties.log,
roll-strong40-after.json, roll-strong40-flight.log, roll-strong40-transforms.log.

Tested DLL SHA256:
6ABFB81ACA9343C09EB604E9CB832458C5BA2A422466D6A9B7DBF2C108321B2D.
Installed after the game was closed. The installed DLL SHA256 matches the
tested candidate above; the installed-DLL saved-settings probe also PASSES
(roll-strong40-installed.json). Restart the game to use this build.
