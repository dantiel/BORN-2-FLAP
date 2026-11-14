# Review for the concurrent aeroelastic-twist task

The knife-edge investigation reviewed the shared twist draft. After verifying
that the installed DLL had a substantial neutral-input asymmetry, it removed
the torque-only gains from `Vehicle.hs`. `Model.hs` and wing rendering remain
untouched by this investigation. The removed draft is preserved as
`.setup/knife-upstroke-20261113/Vehicle-twist-draft.hs`.

The first observed draft adds `aeroTwistGainBase=1.3` and
`aeroTwistUpstrokeGain=2.4`. These were definitions only when reviewed.
The subsequent draft now applies these gains to `sectionTwistMoment`.
Multiplying the aerodynamic generalized twist load by either gain does not
constitute a passive compliance change. For an unactuated section its load is fixed
by `Q_twist = F dot dr/dtwist + M_section dot angularJacobian`. A larger
elastic RHS with the same actual force and body moment introduces unmatched
work. A structural integrator conserving energy for that fictitious RHS
does not repair the full-system mismatch.

There is also a concrete left/right sign error in the new gate:
`tanh(featherSharpness * side * strokeRate)`. The public shoulder coordinate
in `Vehicle.hs` has **positive strokeRate raising BOTH wings**; `side` is
already applied in `flapVelocity = side*strokeRate*(hingeAxis cross r)`.
Multiplying by `side` again makes the left upstroke use the downstroke gain
and vice versa. Symmetric flapping therefore receives asymmetric physical
properties and can develop a spurious roll/yaw bias. A mirrored-motion test
must compare both forces and elastic twist, not just solver convergence.

For more passive twist, change the declared torsional stiffness/compliance
with a stated design/calibration rationale and use the unchanged actual
load. Direction-dependent compliance needs a real one-sided structural law
and stored-energy accounting; it cannot be inferred solely from upstroke
versus downstroke phase. Active feathering instead needs an actuator, its
reaction and power. Either physical design can be useful; a phase-selected
force multiplier is not a substitute.

`MathCore/test/UpstrokeAudit.hs` includes an independent finite-difference
virtual-work check for the actual section geometry at loaded dihedral,
horizontal and anhedral positions. It will expose a torque-only multiplier.
Render the same `stripTwistAero` values used by the physical section, without
a separate visual amplification.

Twist can reduce negative upstroke lift and improve thrust, but passive
twist does not guarantee both at every speed or phase. Independent wind
tunnel evidence for controlled twist/folding is Fan, Gehrke and Breuer:
https://arxiv.org/abs/2408.15577 . Compare signed lift, forward force and
actuator work over complete wingbeats, plus yaw amplitude differentials.
Do not claim reduced resistance simply from larger plotted twist angles.

The installed baseline and latest preferences are snapshotted in
`.setup/knife-upstroke-20261113`. Knife-edge recovery remains unaccepted;
please preserve the chronological Chaos handoff and fixed Kestrel tail.

## Measured installed-library defect

`Tools/audit-powered-symmetry.py` loads the actual DLL with the captured active
Tuning section and symmetric 0.8 throttle, zero steering, 8 m/s forward and
0.5 m/s descending prescribed body flow. It averages seconds 2 through 5.
The body is constrained, not a free-flight test.

The installed `9C838C2C...` library gave mean vertical forces 0.262117 N left
and 0.502265 N right, maximum shoulder difference 6.407260 degrees and maximum
tip-twist difference 0.105800 rad. The reviewed, unamplified load with the
signed servo correction gives 0.437084 / 0.436758 N, maximum shoulder difference
0.465719 degrees and tip-twist difference 0.007781 rad. Numerical/dynamic
symmetry is not exact; the large directional bias is removed.

The finite-difference twist virtual-work regression is now part of
`MathCore/test/Main.hs`, covering both wings, both stroke directions,
horizontal/dihedral/anhedral positions and parallel/yawed hinge models.

## Passive stiffness and wake sensitivity

At 8 m/s and 0.65 amplitude differential, decreasing the declared torsion scale
from 8 to 4, 2 and 1 reduced the stronger wing's upstroke downward force, but
also reduced its mean thrust: 0.2284, 0.1891, 0.1028 and -0.0476 N. These are
isolated prescribed-flow experiments using the initial unloaded-reference
servo experiment, not installed tuning recommendations. The stronger wing
still did not gain a mean lift advantage over the smaller-stroke wing.

A separate Jones/Wagner sensitivity prototype reduced upstroke downward force
from 0.6849 to 0.4988 N at 8 m/s while reducing thrust from 0.2284 to 0.2070 N.
It does not validate a free 3D wake or large-amplitude separated flight and
was not installed. Added mass is not a substitute for circulation memory.

`FluidGeometryAudit.hs` also identifies a limitation relevant to substantially
increasing twist: the present spatial added-air matrix uses the undeformed
wing normal. At 45 degrees uniform elastic twist, direct thin-plate geometry
quadrature gives only 0.559 of that hinge inertia; at 90 degrees it gives
0.0037. The existing rest washout explains the nonzero remainder. In the
settled loaded glide, however, the discrepancy is only about 0.8% of the
added-air hinge term, so it does not explain the original large loss of grip.
Large-twist changes need consistent fluid/elastic inertia and cross-coupling,
not just changes to aerodynamic surface orientation. This audit has not
silently replaced the mass matrix with an unbalanced partial correction.
