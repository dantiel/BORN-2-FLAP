# Fixed Kestrel tail and effective incidence control

The Tail Elevator Angle setting previously changed the wings' stroke centre.
The tail itself used a hidden throttle/pitch trim and differential ruddervators
on two inverted-V panels, although the rendered Kestrel has a flat fan.

The setting now specifies the fan's actual mounting incidence, in degrees. The
fan remains fixed to the fuselage at that angle, as requested. Pitch input still
changes the wing motion; pitch, throttle and yaw no longer independently tilt
the Kestrel tail. Flap Centre controls the wings separately. The menu exposes
tail incidence in 0.1-degree increments because a full degree is significant.

## Physical model

- The visible fan and aerodynamic fan use the same symmetric polygon, pivot
  and triangulation. Tiny asymmetries in the authored outline were mirrored in
  both representations to avoid an unintended permanent roll bias.
- Eighteen quadrature patches evaluate their own position and local airspeed,
  including body angular velocity crossed with patch position. Each contributes
  lift, pressure drag, skin/induced drag, and its own `position cross force`
  moment. The lifting polar uses the complete fan's aspect ratio.
- Tail mounting incidence rotates both its surface normal and application
  points. Body bank then rotates the entire fixed tail and its forces into world
  space. In forward flight with negative tail lift, a right bank produces a
  leftward tail force and a rightward yaw moment. There is no extra yaw torque.
- The existing implicit roll solve includes the tail loads. The flat fan has
  no hidden vertical-fin area for spanwise flow. Other bird models retain their
  previous inverted-V approximation.
- The differential wing servo had been strongly attenuating, and in an unloaded
  five-Hz test phase-reversing, the commanded difference in flap amplitude. Its
  controller now tracks commanded differential velocity as well as position.
  Motor torque/speed limits and the six-radian-per-second controller bandwidth
  remain. This produces a turn through wing forces and body/tail motion.

F5 includes the eighteen tail patches in both views: lift/resultant in view 1;
pressure, drag, local airflow and torque in view 2. Its tail line reports the
actual incidence, world vertical force and world yaw moment. The optional
bounded ABI-17 `b2f_math_get_tail_fields` getter exposes these exact solved loads.

## Verification and limits

Evidence is under `.setup/tail-yaw-20261102/`.

| Check | Result |
| --- | --- |
| Haskell properties, including differential motor saturation/dead battery/mirror symmetry | PASS, `properties-final.log` |
| Installed DLL force signs, local flow, passive drag, exact patch moments, live incidence and bounded reads | PASS, `tail-forces-final.log`, `abi-final.log` |
| Actual native setting to solver to visible pivot, at -8 and +8 degrees | PASS, `tail-capture.log` |
| Rendered F5 lift and airflow views | Both inspected, `StripForces.png`, `StripAirflow.png`; tail vectors and text visible |
| Native yaw, -0.8-degree tail / zero wing incidence, right command | PASS: heading +38.530 degrees, mean bank +10.337 degrees |
| Same native test, left command | PASS: heading -38.584 degrees, mean bank -10.340 degrees |
| Final cycle-averaged yaw/sideslip sweep, same preset | **55/57 pass**; both full-throttle sideslip outside-wing lift checks fail, `yaw-trim-final.json` |
| Full 110-second flight sequence at that trim | **FAIL**, `flight-tracking-unreal.log` |
| Neutral-control bank recovery at that trim | **FAIL**, `handling-tracking-unreal.log` |

Without differential velocity tracking, the same fixed-tail yaw fixture barely
turned: -0.071 degrees heading and -0.016 degrees bank on a right command.
The corrected yaw runs ended about 17.08 m above the ground at 4.63 m/s, with
zero numerical failures or safety resets. These isolated passes do not establish
reliable general handling.

The longer test still fails its turn, relaunch and pull-up deceleration gates.
Powered altitude is 8.314 m, coasting altitude 3.922 m, and boosted altitude
13.681 m; pull-up raises altitude from 13.33 to 14.37 m but increases speed from
5.35 to 6.67 m/s. The recovery test leaves banks of 27.66 and 18.59 degrees.
These failures remain visible; no thresholds or attitude targets were changed.
The final sweep passes the yaw-rate-induced outside-wing speed, lift and rolling
moment checks in both directions at zero, 72% and full throttle. Its two failing
full-throttle sideslip checks concern total outside-minus-inside lift; the
corresponding lift rolling-moment checks still pass. This is not a universal
validation of sideslip stability.
Removing the fictitious pitch/yaw tail actuator changes available control
authority, so the remaining trim and pitch/control behavior needs further work.

## Existing settings

Old large negative values are not compatible trim: they used to move the wings.
The saved profile at the start of this investigation had tail -11 degrees and
wing mount +5 degrees. With a real -11-degree tail it failed both yaw runs and
reached ground level. Changing only its tail to -0.8 degrees did not validate
that profile either: one yaw direction and both broad flight checks failed.
Saved user preferences were not overwritten.

[KestrelBalanced.ini](../Unreal/Born2Flap/Config/FlightProfiles/KestrelBalanced.ini)
is now an explicit **yaw reproduction preset**, with tail -0.8 degrees and wing
mount zero. Despite its historical filename, it is not a fully validated flight
calibration. The preset header records that limitation.

The installed math DLL is the velocity-tracking candidate, SHA256
`C9896429A43AFC0185B9956FD7F8BB766B5026D3C1CD41948AA164E77FFE3D2E`.
The pre-change DLL remains at `.setup/tail-yaw-20261102/before.dll`, SHA256
`FEA0BCD0B28C13A8D9743B80823A8315F69BE0365F748B093EE343279EC4CBAA`.
Windows Editor has been rebuilt. Shipping and other platforms are unverified.

## Reproduction

```powershell
$env:CABAL_DIR='V:\Born2FlapTools\cabal'
# From MathCore:
cabal test all --offline
# From the repository root:
python Tools/test-tail-airflow.py
python Tools/test-airfields-api.py
python Tools/test-yaw-trim.py
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/test-yaw-bank.ps1 -Preferences Unreal/Born2Flap/Config/FlightProfiles/KestrelBalanced.ini
powershell -NoProfile -ExecutionPolicy Bypass -File Unreal/Born2Flap/Tools/test-flight.ps1 -FrameRates 60 -SkipSoak -BirdModel 2 -Preferences Unreal/Born2Flap/Config/FlightProfiles/KestrelBalanced.ini -PhysicsHz 240
```

For the native angle/visual check, launch `UnrealEditor-Cmd.exe` with the project,
`/Game/Murex/Maps/MUREX -B2FLevel=Murex -game -unattended -nosplash -nosound
-RenderOffscreen -benchmark -fps=60 -B2FFlightTest -B2FVectorCapture
-B2FTailAngleTest -B2FPhysicsHz=240 -B2FTestBirdModel=2
-B2FUseSavedFlightSettings -B2FFlightPreferences="<absolute preset path>"`.
It changes incidence to -8 and +8 degrees only in the test, logs solver/pivot
agreement, saves both F5 screenshots, and exits after twenty simulated seconds.
