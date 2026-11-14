# Fixed-tail control investigation, 13 November 2026

## Status

**The reported tail-control problem is not accepted as fixed.** The user
confirmed the earlier wing-grip correction, then rejected the first tail
correction as insufficient. Roll input banks the fixed tail. Yaw input is not
used in these tail-control experiments. No control mapping, tail geometry,
minimum aerodynamic force, artificial attitude torque or stabilization gain
has been added.

The continuing investigation found another real omission and implemented a
3D tail-flow candidate. It passes its force/geometry checks and changes some
handling results, but the knife-edge comparisons have not demonstrated the
requested improvement. A normal-play recording is now needed to identify the
exact state in which the user sees the reversal. This is not evidence that the
user's report is mistaken or that the remaining simulation is complete.

## Implemented tail-flow correction

`kestrelTailLoads` previously computed circulation speed and angle of attack
from body X/Z velocity. Body Y flow contributed friction but was discarded
when calculating circulation. For a finite fan moving mainly sideways, a
small physical angle of attack could therefore be interpreted as zero
circulation speed or fully stalled flow.

The candidate resolves each patch's complete local velocity into components
normal and tangent to the actual tail surface:

```
vn = dot(Vlocal, n)
Vt = Vlocal - vn*n
alpha = atan2(-vn, |Vt|)
```

It calculates circulation perpendicular to that complete velocity. The
effective aspect ratio uses the actual polygon's span perpendicular to the
in-plane flow, divided by its area. Existing pressure, friction, separation
and finite-wing coefficients are retained. Each force still acts at its
physical patch position, with `M = (r-rCG) cross F`. A perfectly tangent flow
does not acquire a normal-force floor. This is a quasi-steady finite-plate
approximation; it does not resolve a free wake or feather vortices.

The native airflow display and trace now use the same surface-relative angle.
`TailAirTrace` also retains `alphaXZ` for comparison with older logs. Earlier
logs' `alpha` field used the X/Z definition and must not be relabeled as the
new 3D angle.

## Independent aerodynamic reference

The diagnostic uses the unmodified Windows executable of
[MIT AVL 3.52](https://web.mit.edu/drela/Public/web/avl/), downloaded into the
ignored `.setup` directory. No AVL implementation was copied into the game.
`Tools/reference-tail-avl.py` generates the fan and settled wing geometries.
AVL is an attached-flow reference, not a validation of stalled or rapidly
flapping flight. Its documentation explicitly limits its normal formulation
to small alpha/beta and explains that SECTION incidence changes the tangency
condition without rotating chord geometry.

For the isolated fan, the polygon is rotated into the incident in-plane flow
before running AVL at 3 degrees angle of attack. Thus the 90-degree case does
not misuse AVL with a 90-degree freestream sideslip. Refining from 6 chordwise /
48 spanwise panels to 12 / 96 changes CL by at most 0.00015 in this sweep.

The table compares dimensionless circulation coefficients. The DLL probe
normalizes with the simulator's actual density, **1.05 kg/m3**, not standard
sea-level density. There is no fitted gain in this comparison.

| In-plane flow bearing | Previous DLL CL | 3D candidate CL | Independent AVL CL |
|---:|---:|---:|---:|
| 0 deg | 0.14142 | 0.14142 | 0.12128 |
| 15 deg | 0.13660 | 0.13657 | 0.11571 |
| 30 deg | 0.12249 | 0.12700 | 0.10561 |
| 60 deg | 0.07081 | 0.13212 | 0.11402 |
| 90 deg | 0 | 0.11012 | 0.09267 |

The independent model supports finite crossflow circulation, and therefore
the existence of the omission. It also shows that the retained simple polar
overestimates these attached-flow coefficients by roughly 16-20%. This is a
quantitative limitation, not an exact aerodynamic calibration. Both incidence
signs and the zero-incidence limit were checked through the actual ABI-21 DLL.

The complete-airframe reference also supports a substantial anhedral
sideslip-roll moment. At 8 m/s forward speed, 2 m/s descent, 80-degree wing-plane
bank, -25-degree common shoulders and -15-degree relative body bank, the
production frozen-geometry circulation roll moment is +0.3895 N m. AVL gives
approximately +0.3284 N m at the same air density; refining 6x32 to 12x64 panels
preserves that result to the printed precision. Thus the production result is
about 19% larger, but the independent calculation does not support removing
that large adverse-roll mechanism. These signs use the Haskell right-hand
bank convention; the Unreal fixture bank convention is mirrored.

The production yaw-to-wing-speed chain was checked separately against the
rectangular-wing result `dMx/dr = -2 rho U c CL b^3/3`. At 0/80-degree bank,
the measured/reference ratios are 0.849/0.860, consistent with the existing
0.85 circulation calibration and small root-height geometry effects. Yaw does
create different local wing speeds and lift. In strong anhedral slip one wing
can have negative lift; increasing its speed then increases downward force.
That is why this mechanism alone cannot guarantee bank recovery.

## Wake experiments were not installed

The game still omits wing-induced flow at the tail. Several diagnostic
horseshoe / lifting-line / two-mode reconstructions were compared with AVL.
They did not reproduce the tail field reliably across horizontal, dihedral
and anhedral cases. None was added to production physics. Their sources and
outputs are retained under `.setup/knife-edge-20261112` as experiments.

Two reference-setup errors were found and corrected before using the final
AVL tables: dimensionalizing with 1.225 rather than the game's 1.05 kg/m3, and
tilting the fan's swept leading edge in Z as well as using AVL's Ainc. The
latter manufactured cant in AVL's linearized geometry. Final `summary.json`,
`control-summary.json`, and fan/DLL comparisons supersede earlier interim
numbers. A missing wake remains a model limitation; the unsuccessful wake
approximations are not a justification for adding a coefficient to force the
desired turn.

## Actual Unreal comparisons

The paired libraries are `polar-before.dll` and `polar-candidate.dll` in
`.setup/knife-edge-20261112`. Both were built with identical Servo, FFI and
Vehicle sources; the fixed-fan circulation calculation is the difference.
The native fixture settles first, initializes an 80-degree bank at t=3 s,
then releases every body axis. All tail tests use yaw input zero. The severe
fixture deliberately uses CG x=0; it is not a replay of the user's saved
CG x=+0.016 m setting.

52 fixture runs (26 matched before/after cases) cover common shoulders -25/0/+25 degrees, tail mounting
angles -2.3/-8.4 degrees, both roll directions and neutral input, 8 or 2 m/s
initial forward speed, and 2 or 8 m/s initial descent. The large-slip runs
develop high rotation rates; their later bank/heading must not be interpreted
as a small-disturbance derivative.

At 8 m/s, 2 m/s descent, -25-degree common angle, -8.4-degree tail and -0.15
recovery input, wing-bank at t=3.5 s changes from 120.71 to 121.43 degrees.
The tail yaw moment about the instantaneous wing normal is -0.008838 versus
-0.008861 N m. This does **not** demonstrate the requested fix. At 2 m/s and
8 m/s descent, the corresponding bank changes from 53.00 to 50.91 degrees,
also insufficient to establish the requested general recovery behavior.

Eight further before/after runs preserve the user's saved CG x=+0.016 m and
cover initially level and 80-degree-bank cases, with horizontal/anhedral
wings. In the initially level horizontal-wing run, the wing-plane angular
displacement at t=3.5 s is -19.62 / -19.75 degrees: yaw authority is substantial
in that fixture. In the anhedral run it is -16.89 / -17.03 degrees at t=3.5 s,
but +5.92 / +5.80 degrees at t=4 s. The tail moment at t=4 s is still negative,
-0.005747 / -0.005815 N m. The net reversal therefore cannot be described as
the tail force simply acquiring the opposite sign in this reproduction.

Eight additional candidate-only design experiments move CG forward to +40
and +70 mm, without editing user preferences. They increase the tail yaw load
but do not cure the reversal. For example, the initially level anhedral case
at t=4 s has tail yaw -0.010774 / -0.013917 N m, yet accumulated wing-plane turn
+10.98 / +11.78 degrees. A forward-CG tuning recommendation is therefore not
being offered as a fix. There are 68 controlled trajectories in total, plus
the broad regressions below.

Actual Chaos angular rates agree with the previous frame's predicted
three-axis rates to roughly 1e-6 rad/s in these contact-free comparisons.
All these trajectories report zero solver failures. This rules out a large
host-integration discrepancy in these fixtures, not every possible runtime
defect. The accepted chronological force handoff is preserved.

## Broad regressions

The existing tests were rerun against both paired libraries, with the same
saved tuning snapshot. These are the current paired results, not the older
ground-dominated tests in the first tail report.

| Test | Before | 3D candidate |
|---|---|---|
| Handling | FAIL; recovered-bank errors 22.17 / 24.75 deg | FAIL; 19.13 / 12.14 deg |
| 110 s flight | FAIL; landing flag 0, turn and relaunch flags 1 | FAIL; same flags |
| 180 s soak | FAIL; altitude 294.50 m, speed 6.14 m/s | FAIL; altitude 290.86 m, speed 4.79 m/s |

All six report zero safety resets and zero math failures. Some handling
measures improve; the flight and endurance acceptance gates still fail. The
candidate's flight-test peak altitude falls from 71.01 to 68.91 m and its peak
speed from 13.73 to 11.73 m/s. These changes are retained as possible
regressions, not hidden behind solver success.

## Normal-play capture and installed state

The native module was rebuilt successfully with `-B2FTailControlTrace`, an
opt-in normal-play trace. It records pilot inputs, actual pose and rates,
shoulder positions, world wing normal, local tail airflow, physical tail and
wing moments about CG, yaw moments about the wing plane, host angular
acceleration, inertia, motor torques and failure count. It does not set any
state or controls. A 780-frame native smoke run verified the trace and parser.

Run from PowerShell:

```
powershell -ExecutionPolicy Bypass -File "V:\BORN 2 FLAP\Tools\capture-tail-control.ps1"
```

The launcher records the installed DLL hash and copies that DLL and preferences
beside the flight log. It does not rebuild, install or change tuning. Analysis:

```
python Tools/summarize-tail-control.py <capture-directory>/flight.log
```

Follow-up: the user completed a capture in
`.setup/tail-control-20261113-013536`. Its analysis found excessive glide
differential commands clipping away common pitch travel; see
`tail-travel-correction-2026-11-13.md` for the correction, paired real-engine
results and remaining recovery failures. This supersedes the capture-pending
status below, without turning the earlier 3D-tail candidate into a claimed
complete handling fix.

The user was asked to reproduce the exact reversal once and reply when done.
The first interactive launch closed its viewport during startup and recorded
no flight; it is not evidence of a reproduced user case.

At capture preparation the installed DLL was ABI 21, SHA256
`B6C82A334ADB6B96B20AAD6A4986D4D8CFB2F1FBC67F5A3213483BC13CE15824`.
It already contains the 3D fan behavior, verified by its force fields against
the isolated candidate. It was not replaced during this capture preparation.
Other tasks are building in this shared workspace, so artifacts are identified
by hash and actual force behavior rather than assuming the installed path
still contains an earlier candidate. Woshi edits were left to the other task.
