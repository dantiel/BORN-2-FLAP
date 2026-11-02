# Anhedral lift and force transfer, 2026-11-02

This is an earlier investigation snapshot. For the subsequently installed DLL,
fixed-tail controls and current failing flight gates, see
[the tail-control correction](tail-control-correction-2026-11-02.md).

**Status: investigated and corrected in source; experimental DLL not deployed.**
The original runtime DLL remains installed because the corrected model regresses
the reference preset's native flight sequence. Saved preferences are unchanged.

The missing support was partly in the transfer from aerodynamic forces to host
acceleration, rather than in the lift-strip calculation. Added air was included
in translational inertia, but gravity was not reduced consistently. This made
steady flight require enough lift to support the dry aircraft **and** its added
air mass. Added mass is an acceleration response, not additional aircraft weight;
see [MIT's added-mass equations](https://web.mit.edu/fluids-modules/www/potential_flows/LecturesHTML/lec12/node1.html).

## Correction

Let `m` be dry aircraft mass, `ma` the added air mass of one wing, and
`ratio = m / (m + 2*ma)`. In the modeled lateral/heave coordinates the old host
acceleration was:

```
a = (ratio * F_aero + F_recoil) / m + g
```

The corrected host force includes `m * (ratio - 1) * g`. Consequently,
with stationary joints and `F_aero = -m*g`, acceleration is exactly zero.
Gravity still comes from the host; this correction does not apply weight twice.
The corresponding reduced roll/hinge load is
`B_dry^T*g - ratio*B_full^T*g`. It vanishes when added air is zero.

`RollDynamics.supportGravity` implements both corrections; `FirmwareVehicle`
uses them in its implicit mechanics and returned host force. Neither the wing
polar nor the pressure/drag coefficients change. The correction applies to
gliding and flapping, every wing position, banked and inverted orientations.
The native Void level passes a zero up/gravity reference so it remains free of
gravity coupling. ABI layout/version stay unchanged; the header documents zero.

## Measurements isolating the correction

DLL force ledger, 5 m/s, throttle 0.72, mount incidence 5 degrees, equal symmetric
strokes, stationary body, elastic wings and servos active, 240 Hz. Samples cover
complete measured cycles after three seconds of settling. These are constrained
measurements, not proof of sustained flight.

| Commanded centre | Wing resultant Z before / after (N) | Effective host Z before / after (N) |
|---|---:|---:|
| -30 degrees, anhedral | 1.0281 / 1.0442 | 0.4944 / 0.7299 |
| 0 degrees | 1.1947 / 1.2007 | 0.5842 / 0.8133 |
| +30 degrees, dihedral | 1.0497 / 1.0383 | 0.5064 / 0.7266 |

The roughly 0.23 N restored support is substantial relative to the 50 g dry
weight, 0.4905 N. At 480 Hz, corrected anhedral/dihedral host Z is 0.7211/0.7212 N;
the angle symmetry is retained and the timestep difference is approximately 1%.
Every strip's lift + pressure + drag closes to its resultant to roundoff.

A separate pitch-input experiment (common trim +12 degrees, mount +5 degrees,
pitch input +0.5) changes actual mean wing angle from approximately -14 to -13
degrees. Host vertical force rises from 0.359 to 0.586 N, while raw wing vertical
force changes only from 1.139 to 1.145 N. This experiment includes tail downforce.

Pitch input changes both the common wing position and physical tail incidence.
That tail force must be distinguished from changing wing centre alone. Both
geometric loss of vertical projection at large angles and tail downforce remain
real effects. The F5 green arrow is the body-forward component of the strip
resultant, not a separate jet to add to lift a second time; its world-vertical
contribution comes from the aircraft orientation.

## Reproduction and limits

```
python Tools/audit-anhedral-lift.py --centre -30 0 30 --pitch 0 --speed 5 --output ledger.json
python Tools/audit-anhedral-lift.py --pitch 0 .5 1 --speed 5 --output pitch-ledger.json
```

Use `--dll` to compare builds, `--preferences` for a fixed profile, and `--hz 480`
for timestep convergence. The audit preserves saved preferences. Its default
centre uses the profile unchanged; specifying centre replaces both legacy common
trim fields in the test context only. The Haskell property suite checks exact
weight balance, free-fall acceleration, zero-gravity and vacuum limits, and
anhedral/dihedral, asymmetric, banked and inverted configurations.

Evidence is under `.setup/anhedral-lift-20261102/`, including the original DLL
and preferences, force ledgers, builds and native test logs. The saved profile
(mount +5, legacy common trim -5, flap centre -12) already failed the native
flight sequence with the original DLL. Solver and recovery success must not be
reported as a full flight pass.

Native validation at 60 fps / 240 Hz:

- Original DLL, reference `KestrelBalanced.ini`: flight and recovery PASS.
- Gravity correction, reference profile: recovery PASS; flight FAIL. Powered
  altitude 7.572 m, coast 2.839 m, boost 0.012 m; pull-up loses speed and initially
  gains height but the aircraft subsequently reaches the ground. No math failures
  or safety resets. Relaunch passes.
- Original DLL, saved profile: flight FAIL (turn, boost, pull-up), recovery PASS.
- Gravity correction, saved profile: flight FAIL (powered climb, boost, relaunch),
  recovery PASS, no math failures/safety resets.
- Reference profile's 57 cycle-averaged yaw/sideslip checks PASS after correction.
  The saved profile fails the full-throttle sideslip lift checks both before and
  after; the correction also exceeds its mirror tolerance in that case.
- Three tail-trim experiments and a pitch/elevator-decoupling experiment did not
  pass all flight gates. All are rejected; original tail trim and controls remain
  in the source. The fixed-pitch-tail experiment retained boost and relaunch but
  failed the pull-up speed/height gates.

Do not install `.setup/anhedral-lift-20261102/candidate.dll` as a completed fix.
It isolates the verified gravity error and is retained for further pitch/flap
dynamics work. The force-ledger figures above describe this candidate, not the
restored runtime. When reproducing them, pass that path explicitly via `--dll`.
The source changes are intentionally reviewable, but a future normal MathCore
build includes the candidate correction: repeat native flight validation before
deploying it. The editor build includes the Void zero-gravity reference change,
which is compatible with the original DLL.

This is still a reduced fluid-inertia model: it does not implement a full
orientation-dependent 6-DOF added-mass tensor, inter-wing wake interaction, or
prove sustained hovering/inverted flight.
