# Free wing attitude and F5 strip diagnostics

The later [yaw lift calibration](yaw-lift-calibration-2026-11-01.md) corrects the
loaded-cycle reversal missed by the initial yaw checks below, retunes Kestrel
torsion and stroke trim, and extends F5 with signed averages. Its trim and test
results supersede the calibration figures in this report.

This revision removes the positional pressure support that made the wing plane
appear fixed in space. It supersedes the supported-wing behavior described in
[the earlier repair report](flight-physics-2026-11-01.md).

## What was wrong

The solver already evaluated each strip separately and included its individual
`position cross force` moment. Adding those moments a second time would have
double-counted aerodynamic torque.

An additional cross-wing pressure spring was the problem. It integrated the
difference between paired strip velocities, remembered a surface direction for
approximately 12 seconds, and applied opposing forces proportional to that
stored displacement. Although it had no literal world-horizontal target, it
strongly resisted departure from the remembered wing plane. It could mask an
unstable airframe as a stable one.

That force path is removed. Strip field displacement remains a reserved zero
in the diagnostic ABI. The local flow, circulation, pressure drag, stall and
membrane history, elastic structure, servo reactions and added-air inertia
remain active. Ground-effect height now uses the actual strip position dotted
with world-up expressed in body coordinates; body-local Z was incorrect when
banked or inverted.

## Force and moment accounting

For each strip, in body coordinates:

```text
local velocity = body velocity - local wind
               + body angular velocity cross deformed strip position
               + hinge velocity + elastic velocity

strip force  = flow-relative lift + drag + normal pressure
strip moment = position cross strip force + section pitching couple
forward component = dot(strip force, body-forward)
```

The forward component is what the display calls thrust/drag. It is part of the
resultant, not an additional propulsion force. Lift can point sideways, forwards
or downwards as the local surface and airflow change. The same equations apply
in gliding and flapping.

The coupled body/hinge/elastic solve receives these individual loads. The host
roll torque includes the solved inertial reaction; it is consequently different
from the raw sum of wing aerodynamic moments. Tail and fuselage forces also
contribute. Gravity is applied once by the host.

Dihedral produces its rolling response through sideslip and local flow. Anhedral
reverses that geometric contribution. Neither creates a direct attitude target.
This is the distinction illustrated by [NASA's introduction to flight
aerodynamics](https://appel.nasa.gov/wp-content/uploads/2018/08/NASA-SP-367-Intro-to-Aero-of-Flight.pdf).

Yaw makes the advancing wing faster and changes its strip lift and torque. It
does not guarantee that the complete craft will bank in the same direction:
the inverted-V tail, sideslip, hinge motion and elastic deformation also act.
The force-isolation checks and whole-airframe response are reported separately.

## F5 controls

F5 cycles through two views and then off. Arrows begin at each of the 32 actual
deformed strip positions and use linear, labelled scales.

| View | Colors and meaning |
| --- | --- |
| 1: strip forces | Blue: flow-relative lift; green: signed body-forward force; white: complete strip resultant |
| 2: flow and moments | Cyan: normal pressure; orange: drag; yellow: air velocity relative to the moving strip; magenta: aerodynamic torque axis about the body origin; white line: surface normal |
| Body reference, both views | White: aggregate host aerodynamic force; red: gravity |

Strip forces use 800 cm/N. Body reference arrows use 100 cm/N. Airflow uses
2 cm per m/s, and torque axes use 2500 cm/Nm. A torque arrow is an axial vector,
not another force. Do not add a resultant to its displayed components.

The lower legend displays instantaneous left/right lift magnitudes, forward
forces and aerodynamic roll moments. These are the last solved strip loads;
the body reference is averaged over the physics steps in the frame. The new
`b2f_math_get_wing_loads` getter is read-only and leaves existing ABI-17 layouts
unchanged. Insufficient buffers or invalid pointers do not partially write.

## Stability and tested trim

Removing the spring changes handling materially. The saved flat-centre Kestrel
configuration fails the old free-flight/automatic bank-recovery tests, with no
math failures or safety resets. It can develop a growing bank after disturbance.
That result is not relabelled as a passing handling test.

A physical +12 degree dihedral centre passes both tests. In the existing mixer
sign convention this is **FlapCentre = -12**. The test copy is
`.setup/strip-physics-20261101/dihedral.ini`; the user's saved preferences were
not changed. This setting moves the real hinges and redirects forces throughout
the flap cycle. It does not enable stabilization software or a horizon lock.

Inverted orientation is allowed. A cambered airframe still needs the appropriate
angle of attack and control trim to support its weight upside down. Retaining
an inverted pose in a constrained fixture is not proof of sustained inverted
free flight under gravity.

## Verification

| Check | Result |
| --- | --- |
| MathCore properties | Pass: passive force work, coupled mechanical conservation, yaw lift, dihedral/anhedral sideslip reversal, inverted reflection, force decomposition and absence of attitude/old-patch force dependence |
| Actual DLL diagnostics | Pass: bounded/atomic reads, independent wind input, force closure, lift perpendicular to local velocity, passive drag |
| Yaw strip-force checks, 240 and 480 Hz | Pass for both yaw signs in glide and powered flap |
| 45 degree attitude step with unchanged air-relative flow | Old spring: 18.85 degree body bank after the hold; revised: 45.0000 degrees |
| Inverted powered tow with unchanged air-relative flow | Retains 180.0000 degrees; no artificial upright torque |
| 110-second flight test, +12 degree dihedral, 240 Hz | PASS: turn, landing, reset, relaunch, climb and pull-up; zero math failures/safety resets |
| Bank recovery, same trim | PASS: measured recovery banks 11.41 / 8.47 degrees; zero math failures/safety resets |
| Slow differential roll in Unreal's constrained tow fixture | PASS: body rotates -27.99 degrees relative to the wing surfaces; fixed tail follows body within 0.02 degrees. Absolute wing attitude is free and reaches the inverted region in this fixture |
| Rendered F5 views | Both views captured and inspected in the Murex world: strip origins, changing force directions, torque axes and readable lower legend are visible |

The former requirement that the wing remain within three degrees of its initial
world orientation is now an optional legacy diagnostic, not a physical
acceptance condition. `test-strip-fields.py --expect-level-support` and Unreal's
`-B2FLegacyLevelSupport` retain it explicitly. Differential-command checks use
body motion relative to wing motion instead. `test-yaw-lift.py
--expect-outside-bank` retains the optional whole-airframe bank-direction gate;
the default checks isolate the advancing wing's speed, lift and rolling moment.

Reports, candidate/baseline DLLs and logs are under
`.setup/strip-physics-20261101/`. Reproduction:

```powershell
python Tools/test-airfields-api.py
python Tools/test-yaw-lift.py --hz 240
python Tools/test-yaw-lift.py --hz 480
python Tools/test-strip-fields.py --roll 0 --perturb-bank 45 --ramp 2 --hold 1
python Tools/test-strip-fields.py --roll 0 --perturb-bank 180 --throttle .72 --ramp 2 --hold 1
powershell -NoProfile -ExecutionPolicy Bypass -File Unreal/Born2Flap/Tools/test-flight.ps1 -FrameRates 60 -SkipSoak -BirdModel 2 -Preferences .setup/strip-physics-20261101/dihedral.ini -PhysicsHz 240
```

`-B2FFlightTest -B2FVectorCapture` captures both F5 views and exits after 20
simulated seconds. That is a visual diagnostic run, not the 110-second flight
acceptance test. Use an actual world map and `-B2FLevel` when capturing so the
title menu does not obscure the bird.

Inspected captures are saved as
`.setup/strip-physics-20261101/f5-forces.png` and `f5-airflow.png`. The renderer
also reports a pre-existing competing-directional-lights warning; the vector
overlay remains visible. These captures verify the diagnostics, not the
appearance or completion of the Murex level.

The aerodynamic coefficients remain provisional. There is no explicit shed
wake velocity solver or tandem-wing wake interaction yet; see the
[future tandem-wing design](tandem-wing-wake-design.md).
