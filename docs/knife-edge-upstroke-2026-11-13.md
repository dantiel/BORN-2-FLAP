# Knife-edge recovery and upstroke resistance, 13 November 2026

## Status and installed changes

**Knife-edge recovery is not accepted as fixed.** Two concrete defects were
corrected and tested in the real Unreal/Chaos path:

1. The servo reference subtracted the magnitude of external load from its
   acceleration budget even when that load helped the requested acceleration.
2. The concurrent twist draft multiplied elastic torque without the matching
   aerodynamic force/couple, and its phase gate interpreted left upstroke as
   downstroke. This draft had reached the installed physics DLL and produced
   a substantial left/right bias under symmetric input.

The final correction retains the existing controller, motor torque/speed
limits, stiffness, mass, mixer, fixed tail and chronological Chaos handoff.
It changes the servo acceleration budget to
`max 0 (stall + sign(rateError)*externalLoad)` and restores the physical twist
load `F dot dr/dtwist + sectionCouple dot angularJacobian`. No force floor,
artificial stabilization, extra actuator or visual twist multiplier was added.

The tested final DLL is
`D742B24B48BDF2380B4D09927FC2503B9FE110A1985FB3A0E6B4FE2C89AF0681`,
installed at `Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll`.
`installed-recovery.log` confirms ABI 21 from that normal path, without a
test-library override, in a rendered powered knife-edge trajectory.
The captured frame was inspected. Rendering and successful solver execution
do not establish successful recovery.

Artifacts and source snapshots: `.setup/knife-upstroke-20261113`.
Active preferences were captured from `[Tuning]`, not stale `[Tuning.Model2]`:
Kestrel, 50 g, CG x=+0.016 m, tail incidence -10 degrees, 857.142883 deg/s,
0.74 N m nominal motor, 8.4 V battery, glide angle -4 degrees. No player
preferences were edited. The native module used throughout was the existing
13 November 08:18:15 build; this investigation did not rebuild it.

## Servo defect and independent motor reference

At 7.4 V, zero shaft speed, a distant +60-degree target and +0.7 N m external
torque helping upward travel, the old controller commanded **-0.62 N m** of
motor braking. Net driving torque was only +0.08 N m. At helpful load equal
to stall torque it commanded -0.74 N m and cancelled the load completely.
The signed budget commands +0.74 N m in that first case, giving +1.44 N m
net torque. At a stationary target it still holds with -0.7 N m. Driving
against that same load still uses the original -0.74 N m motor limit.

`ServoAssistanceAudit.hs` checks both signs and compares integrated spin-up
against the independent constant-load DC motor solution:

```
I * dw/dt = Tstall * (1 - w/wfree) + load
w(t) = wfree * (1 + load/Tstall) * (1 - exp(-Tstall*t/(I*wfree)))
```

The comparison covers the initial full-drive interval, before position or
speed limiting. Holding, braking and motor-envelope limits remain active.
The test passes; its spin-up reference is not another implementation of the
servo reference tracker.

An initial broader experiment removed load from the reference altogether.
It increased realized stroke and thrust but materially changed handling.
It was rejected in favor of the narrower signed-budget correction. Files
named `servo-candidate` or `powered-*-candidate` retain that rejected
experiment. **The final variant is named `servo-signed`.**

## Upstroke force measurements

`UpstrokeAudit.hs` settles a loaded wing, freezes its geometry and stall/LEV
history, then varies shoulder rate independently. At 8 m/s forward flow and
0.5 m/s descent, a horizontal wing has approximately +0.05 N m shoulder load
at rest. A slow +0.2 rad/s upstroke still receives helpful positive torque.
At +2 rad/s the circulation reverses; at +8 rad/s its vertical contributions
are -2.1572 N circulation, -0.44065 N pressure and -0.05565 N drag, with total
hinge torque -0.95817 N m. These are frozen instantaneous loads, not wingbeat
averages or measured bird performance.

Thus raising a loaded wing initially reduces its positive lift, as the user
described. That does not imply that a faster rising wing cannot subsequently
develop negative lift. The model must feather, fold or change incidence to
avoid that part of its local angle-of-attack excursion.

`PoweredUpstrokeAudit.hs` uses the real mixer, servo and elastic solver with
prescribed body motion, 480 Hz steps and averages over seconds 2-5. Body recoil
is constrained with very large body inertia; this is a wind-tunnel diagnostic,
not free flight. It records signed circulation, pressure, drag, hinge torque,
motor torque/work, twist, angle, rate and commanded travel. The final signed
servo correction produces these results at throttle 0.8, yaw differential
0.65; the actual oscillator is approximately 4.089 Hz:

| Flow | Wing | Mean forward force | Mean vertical force | Upstroke vertical force | Downstroke vertical force |
|---|---|---:|---:|---:|---:|
| 8 m/s | Larger stroke | 0.1863 N | 0.4215 N | -0.6238 N | 1.3858 N |
| 8 m/s | Smaller stroke | 0.0416 N | 0.4793 N | 0.0836 N | 0.8413 N |
| 2 m/s | Larger stroke | 0.0682 N | 0.0929 N | -0.2891 N | 0.4737 N |
| 2 m/s | Smaller stroke | 0.0485 N | 0.1009 N | -0.0722 N | 0.2646 N |

The larger stroke provides more thrust, but less cycle-mean lift. This is a
measured weakness of this operating point; amplitude differential cannot be
assumed to produce a favorable roll contribution. At 8 m/s circulation
dominates its negative upstroke load; at 2 m/s pressure dominates. The servo
sign correction makes little difference in these periodic cases, especially
at 2 m/s where the motor is already saturated. It is not being presented as
the dominant explanation of the reported knife-edge failure.

Signed motor and aerodynamic work are recorded, but these prescribed-body
tests are not a complete free-aircraft energy/momentum balance. In particular,
they do not validate the omitted cross-coupling between elastic modes and the
spatial fluid inertia described in the twist review.

## Concurrent twist review and model limits

See [the twist review](twist-review-2026-11-13.md) for the virtual-work defect,
left/right sign error, installed DLL measurements and passive-stiffness sweep.
The faulty installed `9C838C2C...` DLL gave 0.262117 / 0.502265 N mean left/right
lift under identical input and flow. The corrected library gives
0.437084 / 0.436758 N. The older installed payload is preserved as
`installed-latest.dll`; that filename refers to the captured pre-install state.

More passive compliance reduced negative upstroke lift but also reduced
downstroke lift and thrust. It is not a validated recovery fix. Independent
wind-tunnel evidence shows benefits from **controlled twist and folding**;
that result cannot be treated as evidence that arbitrary elastic torque
amplification improves a passive membrane wing:
[Fan, Gehrke and Breuer (2024)](https://arxiv.org/abs/2408.15577).

The production circulation is quasi-steady; added mass does not supply its
missing wake memory. An isolated Jones/Wagner sensitivity prototype reduces
negative upstroke loading but does not recover the desired mean lift
differential. It was not installed: a two-dimensional attached-flow response
with the existing finite-wing slope is not a validated model of this large
amplitude, separated three-dimensional wingbeat. Relevant independent models
are [BYU's Wagner formulation](https://flow.byu.edu/Aeroelasticity.jl/dev/models/aerodynamics/wagner/)
and [Boutet and Dimitriadis (2018)](https://www.mdpi.com/2226-4310/5/3/92).

The added-air spatial matrix also uses an undeformed wing normal. Direct
thin-plate kinetic-energy quadrature exposes a large error at hypothetical
45/90-degree feathering, but only about 0.8% of the added-air hinge diagonal
in the settled loaded glide tested here. It is a real limitation to resolve
before large-twist changes, not evidence for a large arbitrary inertia cut.
The dry-plus-drive hinge diagonal is 0.001958221 kg m2; added air contributes
0.000623104 kg m2. The reflected drive contribution is 0.0016 kg m2; its
calibration is not established by these tests and was not changed.

## Actual Unreal trajectories and regressions

Eighteen powered trajectories compare the clean pre-servo baseline, the
rejected unloaded-reference experiment and final signed-budget correction:
80-degree initial bank, 2 or 8 m/s forward speed, 2 m/s descent, throttle 0.8,
zero roll/pitch input and raw yaw -0.78/0/+0.78 for two seconds. The fixture
releases all body axes at t=3 s; keyboard slew and saved expo remain active.
All have zero solver failures. The measurements do not demonstrate reliable
recovery. At 8 m/s and positive yaw, the wing-normal vertical component at
t=5 s is 0.777 baseline versus 0.771 final; negative yaw gives 0.613 versus
0.607. Later free trajectories can diverge substantially. Euler roll alone
is not the acceptance measure for these pitched, articulated states.

The accepted slow-roll grip check is unchanged to printed precision:
body rotation 18.330 degrees, peak body rate 0.028 rad/s, left/right surface
rotation -6.236/-6.263 degrees, PASS for both baseline and final. The fast
final check also passes. These tests do not certify knife-edge recovery.

Existing broad native tests were run with the same captured preferences:

| Test | Clean baseline | Final signed servo correction |
|---|---|---|
| Handling | FAIL; bank errors 78.42 / 74.45 deg, altitude 17.97 m | FAIL; 71.23 / 16.38 deg, on ground |
| 110 s flight | FAIL; turn=1, land=0, peak altitude 48.461 m | FAIL; turn=0, land=0, peak altitude 27.815 m |
| 180 s soak | FAIL; final altitude 0, peak altitude 11.76 m | FAIL; final altitude 0, peak altitude 16.70 m |

All report zero safety resets and math failures. The lower flight altitude,
lost scripted turn and handling-test ground contact are material regressions;
the fact that the baseline also fails does not excuse them. No tuning change
was made to hide these results. The physical bug corrections are installed,
but complete tail/knife-edge handling acceptance remains outstanding.

Reproduce with `Tools/test-knife-upstroke.ps1 -Versions before,signed` from a
PowerShell session (array argument), adding `-Regressions` for the broad tests.
The numerical probes, exact before/after libraries, traces and JSON summaries
are retained in the artifact directory. `math-properties` passes including
the new signed-load and two-sided twist virtual-work regressions.
