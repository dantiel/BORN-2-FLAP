# Captured recovery: shoulder travel erased elevator commands

This continues `tail-control-investigation-2026-11-13.md`. The user capture
is `.setup/tail-control-20261113-013536`, including its DLL, preferences and
normal-play telemetry. The accepted chronological Haskell-to-Chaos handoff
and the fixed, body-mounted fan are preserved.

## What the flight actually shows

The capture loaded ABI 21, SHA256
`7E35EE4541DD3238C757B60FE898154CBBE2B1A35852782F00AEE78A4E1A3835`.
Its active settings were tail incidence -3.8 degrees, CG x=+0.016 m,
AileronScale=ElevatorScale=YawScale=100 and GlideAngle=-4 degrees.
The older `[Tuning.Model2]` section is not the active profile.

In the last recovery, roll and yaw input track each other because desktop
mouse steering intentionally drives both channels. The old glide mixer
added both to static wing differential:

```
c = 4 - 60 * pitch
d = 175 * roll + 12 * yaw
left = clamp(c - d, -80, 80)
right = clamp(c + d, -80, 80)
```

At t=399.774 s, the pilot requested full pitch and approximately -0.65 roll
and yaw. The requested common angle was -56 degrees, the clipped target
was about -7.2 degrees, and the actual mean shoulder angle was -10.4 degrees.
Thus increasing steering discarded most of the pitch request. The fuselage
was banked only about 41 degrees in world Euler coordinates; the actual
shoulder half-difference was nearly -70 degrees. World bank alone does not
describe the relative orientation of fuselage and supporting wings.

The tail also genuinely unloaded during this interval. At t=398.781 s its
surface-relative angle of attack was -2.72 degrees and its aerodynamic
moment magnitude about CG was 0.11084 N m. At t=399.659 s these were -0.20
degrees and 0.01191 N m. Tail speed increased, so this is not simply loss of
dynamic pressure. These observations do not establish that all of the
angle-of-attack change was caused by clipping. Samples after approximately
399.8 s encounter a sharp contact-related velocity discontinuity and are
excluded from the recovery analysis.

`Tools/analyze-tail-control-flight.py` reconstructs commands from that
snapshot and probes the captured DLL. Holding body-relative flow fixed,
changing world-up through banks 0/15/30/60/90/180 degrees changes tail
moment by less than 1.8e-6 relative. This covers both signs of mounting
incidence and 0/30/75-degree sideslip. The residual is the tiny finite-step
gravity response; there is no standalone world-bank attenuation factor.
This is a rotational invariance check, not a claim that airflow stays
constant during real banking.

Independently of the simulator, rotating a moment vector preserves its
magnitude. Its pitch projection onto the original transverse axis scales
as cos(bank), retaining 96.6% at 15 degrees and 86.6% at 30 degrees, while
developing a yaw component. That geometric projection alone cannot explain
the captured 80% loss of requested common shoulder travel. A fixed tail's
force also depends on local angle of attack, including angular velocity at
its aft lever arm; negative mounting incidence does not guarantee negative
lift for every flight path and pitch rate.

## Correction

`Firmware.hs` now uses the actual +/-80-degree shoulder travel for full-scale
glide roll. The previous 175-degree gain was explicitly documented as a
compensation for the old glued-wing assumption. That assumption is obsolete.
Yaw no longer adds static shoulder bank in glide; it retains powered
amplitude steering. Desktop input mapping itself is unchanged.

For common pitch c and differential roll d, the physically feasible command
region is exactly `abs(c) + abs(d) <= 80`. If both commands do not fit, one
shared scale brings them inside the limits while preserving their ratio.
Independent per-wing clipping previously exchanged pitch for more roll.

This changes glide steering sensitivity: with AileronScale=100, full-scale
differential is 80 degrees rather than a 175-degree request that saturates
early. It must not be presented as an aerodynamic-force improvement. Tail
polars, force magnitudes, lever arms, inertia, servo torque, powered mixing
and the accepted wing-air coupling are unchanged.

For the captured full-pitch, -0.65-steering example, the corrected command
is approximately -41.5 degrees common and -38.5 degrees differential. This
is a counterfactual target, not a replayed measurement of the failed flight.

## Validation and limits

Artifacts: `.setup/tail-travel-20261113`. Paired libraries:

- Before: `BC3E9F5AB1BF047D8563A5A57BEFAAA46EE5DBF29C7FB0CC7F0928F113E37D27`.
- Candidate: `D7EE12650947BCC329C035915A9D9AC0D87355F3809A2F55F698A494939E30DB`.

MathCore properties pass, including combined-command travel, direction
preservation, and no yaw-induced glide articulation. Four seconds of
prescribed-flow DLL comparison gives exactly identical outputs for powered
flight with roll/pitch/yaw and for neutral-roll glide with pitch input.

Twelve real Unreal/Chaos runs compare slow (8 s) and fast (0.25 s) steering
with common shoulder requests -15, 0 and +15 degrees, including four free
trajectories released from an 80-degree bank. In the eight constrained
loaded runs, the old +/-15-degree common request ends at approximately
+/-4.42 degrees; the candidate retains +/-15 degrees within 0.004 degrees.
The fixture's raw steering -0.65 passes through the saved 0.42 expo, so its
logical roll is approximately -0.4923. It is not the same input as the
capture's logical -0.65. All twelve runs report zero math failures.

These runs do not establish a recovered knife-edge maneuver. The negative
common-angle tow cases turn toward an inverted attitude. In the sustained
80-degree-bank, negative-common case the candidate eventually contacts the
ground while the baseline remains airborne at the fixture endpoint. This
is a material handling result, not hidden by the improved command tracking.
The old `RollResponseTest` pass/fail criterion also assumes held input and
specific response magnitude; its result cannot certify the new allocation.

The broad existing tests were rerun with the captured active preferences:

| Test | Before | Candidate |
|---|---|---|
| Handling | FAIL: bank errors 13.24/14.89 deg, on ground | Identical FAIL |
| 110 s flight | FAIL: turn=0, relaunch=0 | FAIL: turn=1, relaunch=0 |
| 180 s soak | FAIL: altitude 0, speed 0.59 m/s | Identical FAIL |

All report zero safety resets and zero math failures. Both flight tests peak
at only 1.351 m; these ground-dominated results do not validate airborne
handling. A successful numerical solve is not being substituted for it.

The native module was rebuilt with read-only normal-play telemetry restored,
including altitude, position, actual tail incidence and physical frame
duration. Optional controlled-fixture pitch, yaw and pulse-duration inputs
permit paired combined-control experiments. They have no effect in normal
play.

Six further paired Unreal runs start at 0/+80/-80-degree banks, 8 m/s
forward velocity and 2 m/s descent. They apply a 1.5 s combined-control
pulse (0.25 s ramp, raw roll/yaw +/-0.78, pitch +1), then release controls.
Keyboard slew and the saved expo remain enabled. At t=4 s the measured
logical inputs are roll/yaw +/-0.651712 and pitch +1:

| Initial bank | Common angle before / corrected | Tail moment magnitude before / corrected |
|---|---|---|
| 0 deg | -7.1 / -41.4 deg | 0.0036 / 0.0277 N m |
| +80 deg | -7.1 / -41.4 deg | 0.0030 / 0.0277 N m |
| -80 deg | -7.1 / -41.4 deg | 0.0030 / 0.0277 N m |

This verifies a changed force-bearing configuration and increased tail load
in the real engine, not merely changed mixer targets. It does not verify a
successful pilot recovery: both versions eventually contact the ground in
these fixed-pulse trajectories. At t=4 s in the +80-degree case, the corrected
tail yaw moment about the wing normal is -0.01675 N m, opposed by a wing
moment of +0.01038 N m. The tail retains the commanded yaw direction while
the coupled aircraft still develops the wrong recovery trajectory. Further
handling work must address that complete transient, not boost tail force
until an Euler-angle metric passes.

Reproduce with `Tools/test-glide-travel.ps1 -Combined`; the ordinary mode
runs the 12 travel fixtures, and `-Regressions` runs the six broad tests.
Results and full traces are retained under the artifact directory.

The candidate was installed at
`Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll`, with the SHA256
listed above. The native rebuild succeeded after the user closed the other
game window that had locked the module. This is a confirmed control-travel
correction, not acceptance of the complete tail/knife-edge handling issue.

`installed-combined.log` confirms that Unreal loaded ABI 21 from that normal
installed path, without a test-library override. At t=4 s it reproduces the
candidate's -41.429-degree common position and 0.027725 N m tail moment.

The original modest slow-roll grip fixture also passes for both libraries
(`grip-before.log`, `grip-candidate.log`). The smaller commanded shoulder
differential reduces body rotation from 29.115 to 13.959 degrees as expected
from the changed sensitivity. Differential world-wing drift remains small:
0.219 degrees before and 0.060 degrees after. This checks that the previously
accepted wing-air grip was retained rather than masking a return of its
integration defect.
