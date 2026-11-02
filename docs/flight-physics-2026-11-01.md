# Elastic-wing flight repair — 2026-11-01

Historical snapshot: the pressure-support behavior and verification below are
superseded by [free wing attitude and F5 strip diagnostics](flight-physics-free-attitude-2026-11-01.md).

Future work: [tandem-wing aerodynamics and independent phase control](tandem-wing-wake-design.md).

The Windows Editor build and physics DLL are installed. The saved Kestrel profile
uses 50 g total mass and a 240 Hz physics solver. Automated full-flight, handling,
loaded-roll, airfield ABI and yaw/lift checks pass. These are headless/solver
measurements, not a claim of rendered playtesting or measured aerodynamic accuracy.

## Resulting behavior

- Each strip uses its deformed application point and surface frame. Local flow
  includes wind at that point, body angular motion, hinge motion, bending and
  twist. Circulatory lift remains perpendicular to local flow.
- Glide and flap share the same pressure resistance. A relaxing pressure memory
  retains spanwise differential loading without adding aircraft weight to the
  strips. Paired wake reactions sum to zero force, so they cannot suspend the bird.
  The memory sheds strain rapidly when through-flow disappears.
- Body roll, both hinges and bend/twist velocities are solved together. Motor
  torque limits and reaction forces allow a slow differential command to rotate
  the fuselage while the loaded wings move little in the world.
- Hidden roll-to-tail-rudder mixing is disabled. The remembered pressure normal
  is transported without repeatedly projecting it onto changing flow, which had
  introduced bank drift through pitch/yaw motion. Explicit rudder input and
  physical wing, tail and aft-body yaw effects remain.
- Increased torsional stiffness prevents the excessive feathering that weakened
  lift and thrust. Bending remains elastic. The rendered common tail incidence
  now reads the angle used by the force model.
- Cached strip geometry and coupled-solver tangents reduce physics cost. The
  12-second powered slow-roll fixture takes about 5.4 seconds on this machine;
  this is a solver timing, not a rendered frame-rate measurement.

## Verification

| Check | Result |
| --- | --- |
| Unreal Editor Development build | Succeeded |
| MathCore properties, including new yaw checks | Pass |
| 110-second Unreal FlightTest | Pass: turn, landing, reset, relaunch, climb and pull-up; zero safety resets or math failures |
| Unreal HandlingTest | Pass: recovered banks 0.30° / 9.80°; zero safety resets or math failures |
| Slow roll in Unreal, 30 FPS / 240 Hz | Body 10.271°, tail 10.279°, left/right world surface changes 0.252° / 0.246° |
| Powered slow ramp, free heave/lateral/roll | Body −12.343°, mean world surface +0.101°; mirrored command passes |
| Same powered ramp at 480 Hz | Body −12.360°, surface +0.086° |
| 20-second glide roll ramp | Body −17.161°, surface +0.358° |
| Strip-field ABI | Bounded reads, atomic invalid-input handling, frame transport, local wind and tail incidence pass |
| Yaw coupling at 240 and 480 Hz | Both yaw signs, glide and powered flap pass |

The corrected constrained-body force audit at 80% throttle measures:

| Forward speed | Net forward force | Vertical host force | Weight | Maximum elastic twist |
| --- | --- | --- | --- | --- |
| 5 m/s | +0.1630 N | 0.1727 N | 0.4905 N | 9.73° |
| 8 m/s | +0.2190 N | 0.6893 N | 0.4905 N | 16.22° |

These are tunnel averages at zero mount incidence, not free-flight trim or hover
guarantees. The tunnel now uses a large fixture roll inertia: resetting the roll
rate after solving a freely rotating fuselage retained an inconsistent hinge
reaction and corrupted earlier averages. `force-final.csv` and
`force-final-reconciled.csv` are invalid intermediate audits; use
`.setup/force-final-tunnel.csv`.

## Yaw makes the advancing wing rise

Body axes are +X forward, +Y right, +Z up. A positive yaw rate increases the left
wing's forward speed by the `omega × position` contribution. Its greater lift
initially makes a negative roll moment, raising that outside wing. Negative yaw
mirrors this result. No yaw-to-roll torque or corrective pilot command was added.

MathCore tests isolate this effect on identical instantaneous geometry, including
bent/twisted strips, at 5, 8 and 12 m/s. They check both yaw signs in glide and
across 32 flap phases. The cycle-mean lift difference and rolling moment have the
expected signs, and local speed differences match the actual strip positions.

The DLL test imposes ±0.6 rad/s yaw during an 8 m/s curved tow, leaving heave,
lateral motion, roll and elastic wings free, with neutral pilot roll/rudder.
After 1.5 seconds at 240 Hz:

| Mode | Mean outside-wing speed advantage | Outside-wing rise, measured from actual span positions | Body bank in the same direction |
| --- | --- | --- | --- |
| Glide | 0.3134 m/s | 0.886° | 0.769° |
| 72% throttle | 0.2787 m/s | 0.896° | 0.923° |

Opposite yaw mirrors these results; neutral cases remain symmetric. At 480 Hz the
outside-wing rise is 0.977° / 0.982°. Subsequent sideslip, elastic twist and wake
reaction change each wing's operating point. In particular, instantaneous lift
during an upstroke need not increase upwards. The regression checks the initial
lift response, isolated cycle average and actual ensuing motion separately.

## Reproduction and artifacts

Run from the repository root, except the Cabal command from `MathCore`:

```powershell
$env:CABAL_DIR='V:\Born2FlapTools\cabal'
cabal test all --offline

python Tools/test-airfields-api.py
python Tools/test-strip-fields.py --throttle .72 --free-translation
python Tools/test-strip-fields.py --throttle .72 --roll -.25 --free-translation
python Tools/test-strip-fields.py --throttle .72 --hz 480 --free-translation
python Tools/test-strip-fields.py --ramp 20 --free-translation
python Tools/test-yaw-lift.py --output .setup/yaw-lift-final-240.json
python Tools/test-yaw-lift.py --hz 480 --output .setup/yaw-lift-final-480.json
python Tools/audit-flight-force.py --dll Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll --preferences Unreal/Born2Flap/Saved/Config/FlightPreferences.ini --speed 5 8 --incidence 0 --sample-seconds 3 --throttle .8

powershell -NoProfile -ExecutionPolicy Bypass -File Unreal/Born2Flap/Tools/test-flight.ps1 -FrameRates 60 -SkipSoak -BirdModel 2 -Preferences Unreal/Born2Flap/Saved/Config/FlightPreferences.ini -PhysicsHz 240
```

For the isolated Unreal loaded-roll gate, launch the Editor on `/Engine/Maps/Entry`
with `-game -nullrhi -nosound -unattended -benchmark -fps=30 -B2FFlightTest
-B2FRollResponseTest -B2FTestBirdModel=2 -B2FPhysicsHz=240
-B2FUseSavedFlightSettings` and the same absolute `-B2FFlightPreferences` path.

Evidence: `.setup/physics-tail-pose-unreal-build.log`,
`.setup/physics-yaw-tests.log`, `.setup/physics-wake-recovery-flight.log`,
`.setup/loaded-roll-final-30.log`, `.setup/strip-wake-recovery*.json`,
`.setup/strip-final-glide20.json`, `.setup/yaw-lift-final-*.json`, and
`.setup/force-final-tunnel.csv`.

Installed DLL SHA-256:
`D36A3220C50070F6682391BBC0E7AA70AA7F9074298FC0EF8786EF81C4296597`.
The flight-tested `.setup/physics-wake-recovery.dll` has SHA-256
`FE8D98E59D84B6B102DB253F14282F41B77C1E5CE158C2C5D864EEF51E6A8AF5`;
the two binaries differ only in their PE build timestamps. New yaw and slow-roll
checks were run against the installed binary.

## Remaining model limits

The coherent pressure memory is a reduced, calibrated wake model, not CFD.
Structural stiffness and motor electrical assumptions still need measured data
for hardware accuracy. Added-air coupling is strongest in the roll/heave system;
this is not a complete six-axis deformable-fluid inertia model. The retained
inverted-V aerodynamic tail was an approximation to the displayed fan. The
Kestrel now uses the actual fixed fan; see the
[November 2 tail and control correction](tail-control-correction-2026-11-02.md)
for current behavior, changed setting semantics, and failing integration gates.
Windows Editor behavior is verified here; Shipping, mobile and macOS builds and
rendered flight acceptance are not established by these checks.
