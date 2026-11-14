# Modest spring/twist increase and preservation audit

Artifacts: `.setup/wing-release-20261113`. Captured preferences are unchanged:
active stroke ferocity 68, return 67, tail -10 degrees, CG x=+16 mm and
speed modifier 0.82. Earlier reports using ferocity 50 are different cases.

## What survived, and what changed elsewhere

The source comparison against the saved session version confirms these
corrections remain: complete 3D fixed-tail airflow; physical tail patch
moments; glide pitch/roll travel allocation; no static glide-yaw articulation;
signed assisting servo load; chronological Chaos force application; SI/Chaos
inertia conversion; distributed chord pressure; parabolic airfoil moment;
independent left/right camber controls. The obsolete hang fields remain ABI
placeholders, explicitly zeroed. The artificial glue/dihedral-lift/phase-stiffness
mechanisms have not returned.

The pre-change installed DLL, SHA256
`C7F99E2987942E93FB8217A9D9CD3D6A3EB4A62D761B2192792F26345B543D6B`,
gave exactly identical tail coefficients to the last verified F4B330 DLL in
all 15 cases: flow bearings 0/15/30/60/90 degrees and alpha -3/0/+3 degrees.
The new installed DLL matches to roundoff. This verifies preservation of the
tail correction, not successful knife-edge handling.

A later mixer edit added `aileronAmpDiff=clamp(-.45,.45,.50*roll*scale)` and
subtracted it from yaw's left-wing amplitude while adding it to the right.
It opposed yaw when desktop steering supplied both channels with the same
sign. Removed that addition to restore this session's yaw-owned amplitude
steering. Kept shoulder-centre roll and explicitly configured skew/timing
controls, and retained the full-yaw zero-amplitude endpoint. A new full-cycle
test checks that changing roll does not cancel yaw's commanded stroke arc.

## Spring, twist and force direction

Kestrel torsional scale 7 -> 6.3: 10% less stiffness and 11.1% greater static
compliance for the same torque. Structural damping ratio .15 -> .12: less
internal loss, while damping remains dissipative. These are requested design
adjustments, not newly measured material constants or aerodynamic gains.
For a freely vibrating linear mode, the first opposite-peak amplitude ratio
changes from exp(-pi*.15/sqrt(1-.15^2))=0.621 to 0.684. Actual aerodynamic
loading adds its own damping; this analytic ratio is not a flight prediction.

Circulation direction is now expressed explicitly as local velocity crossed
with the consistently oriented bound-vortex axis. For the existing Kestrel
geometry this is algebraically equivalent to the previous section-frame
formula. It does not create a new always-outward force. Bank, bending, twist,
body rates and shoulder motion already enter the local apparent wind.
Reference: [MIT circulation force notes](https://web.mit.edu/fluids-modules/www/potential_flows/LecturesHTML/lec1011/node45.html).
New tests rotate both the wing and apparent wind and require the entire force
to rotate identically, including lateral force, while circulation does zero
translational work.

The Kestrel renderer consumes physical bend/twist directly; no visual multiplier
was introduced. In the prescribed-flow DLL experiment (8 m/s, sink .5 m/s,
throttle .8), symmetric tip twist changed from -14.559/+7.938 degrees to
-15.907/+8.722 degrees. Mean per-wing lift changed .3278 -> .3020 N, while
thrust was .1862 -> .1864 N. Increased washout reduces downstroke lift as well
as negative upstroke lift. It is not a free lift/thrust improvement.

## Validation

`before.dll` is the captured installed version; `candidate.dll` isolates
spring/twist/vector changes; `restored.dll` additionally removes the opposing
roll-amplitude mix. Paired native tests include crossed controls and the newly
added coordinated same-sign roll/yaw cases. Do not mix those maneuvers.
All completed recovery comparisons reported zero math failures, but do not
demonstrate reliable knife-edge recovery. Both old and new broad handling
tests still fail their bank-recovery requirements.

Math-properties passes, including the new frame-covariance and control-arc
checks. Native chronological torque-pulse test passes: expected rotation
0.005594057103 rad, observed 0.005594056750 rad, zero final momentum/energy.
Slow and fast loaded-wing grip pass: body rotation 18.754 and 18.056 degrees.

Installed SHA256:
`C4913731B67A75851E7CE9364C9C4256AE88D288774CF7A003017E6805915CCB`.
`verified-files.json` records the physics/native source hashes and installed
DLL hash for future comparison. Changed hashes identify changes, not by
themselves regressions. No user preferences or unrelated level edits were
overwritten.

The normal-path rendered run confirmed ABI 21 and both +0.25% camber controls
applied; its screenshot was inspected. Compared with the captured baseline,
the final 110-second flight scenario reached 59.917 rather than 54.217 m but
still failed turn/landing criteria. Handling bank errors were 55.78/46.72
degrees versus 46.55/53.53 degrees before: a mixed result, not a recovery fix.
The spring-only candidate passed the 180-second soak at altitude 34.08 m;
the captured baseline failed at ground level (peak 13.43 m). All these runs
reported zero solver failures and zero safety resets.
The final restored-mapping build also passed that 180-second soak at 34.08 m,
with zero solver failures and zero safety resets.
