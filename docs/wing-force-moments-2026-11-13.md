# Wing force and pitch audit, 13 November 2026

This continues the knife-edge investigation. Recovery is not accepted merely
because a moment identity or solver test passes.

## Corrections

- Removed the other task's artificial dihedral lift addition, load-gated extra
  chord drag and phase-dependent torsional softening. Those mechanisms were
  not derived from the physical geometry. Kept its modest, phase-independent
  GJ setting of 7 (previously 8): this is a structural design setting, not a
  measured material calibration.
- Broadside pressure now integrates across the physical chord. Each point
  uses its own velocity. Its force and couple enter body, hinge and elastic
  virtual work together. The prior quarter-chord concentration understated
  pressure's feathering lever and omitted its chordwise rotational resistance.
  Integration splits at zero normal velocity and uses two Gauss points on
  each interval, exactly integrating the quadratic traction and cubic moment.
- Shoulder power includes aerodynamic section couples. Section pitch rate
  uses the actual yawed hinge and bend axes.
- Corrected the parabolic airfoil's quarter-chord moment from -pi*m/2 to
  -pi*m. Removed inherited reflex from Kestrel: its rendered membrane has no
  reflex. For z/c=4*m*x*(1-x), Fourier integration gives A1=4m, A2=0,
  alpha0=-2m and Cm=pi/4*(A2-A1). Reference:
  [MIT thin-airfoil notes](https://courses.edx.org/asset-v1:MITx%2B16.101x_2%2B3T2015%2Btype@asset%2Bblock/mitx_compressed.pdf).
- Added independent left/right airfoil bias controls, in percent of chord,
  default +0.25%, range -1 to +1%. These change rest camber, membrane shape,
  zero-lift angle and pitching moment together. They are not lift multipliers.
  Additive DLL setter preserves the ABI 21 structure layouts.
- Retained the signed servo-assistance correction documented in the earlier
  upstroke report. No new control mapping or attitude assistance was added.

## Force directions and measured limits

With x forward, y right, z up, physical moments about CG are
Mx=y*Fz-z*Fy; My=z*Fx-(x-cgX)*Fz; Mz=(x-cgX)*Fy-y*Fx.
Positive My pitches the nose down. Lift produces roll directly through y.
Pure forward thrust produces yaw and pitch; it cannot directly produce roll.
Its subsequent roll influence depends on changed airflow and wing geometry.

Prescribed-flow measurements at 8 m/s, 0.5 m/s sink, actual active preferences
(stroke ferocity 50, return 67, tail -10 degrees, CG x=+16 mm), full throttle:
cleaned baseline wing pair My=+0.0716 Nm versus tail My=-0.1481 Nm.
Down elevator raises the stroke centre: wing pair My=+0.1366 Nm, of which
+0.1076 Nm comes from the actual thrust-height lever. That lever is present.
The CG shift is also present; moving the reference changes moment by -dcg x F.
These are body-constrained force experiments, not free-flight trim predictions.

There is an additional physical constraint on the proposed upstroke force
direction. For a locally horizontal section translating forward at U and
upward at w, quasi-steady circulation satisfies Fx*U+Fz*w=0. Thus producing
positive forward thrust on an upstroke entails Fz/Fx=-U/w, before drag.
Rotating the force toward pure forward thrust while retaining the same local
velocity would inject energy. A real design can change this through feathering,
folding, stroke-plane geometry or rotational work; it cannot be achieved by
redirecting the resultant independently of section motion. The current
material setting does not feather sufficiently to unload the upstroke fully.

The corrected airfoil moment increases passive washout as well as nose-down
moment. In the first distributed-pressure draft, with the small camber bias,
neutral full-throttle wing-pair My was +0.0982 Nm. Negative upstroke lift
remained substantial. At differential yaw 0.65, the larger wing averaged
Fx=0.1814 N, Fz=0.3117 N, upstroke Fz=-0.6412 N and downstroke +1.2860 N.
The smaller wing averaged Fz=0.3627 N. Consequently differential thrust is
present, but upstroke lift still cancels the larger wing's downstroke advantage.
These data do not justify claiming successful knife-edge recovery.

## Validation and artifacts

Artifacts: `.setup/knife-control-20261113b`. Snapshots preserve the other task's
source and installed DLL before cleanup. `clean-camber.dll` isolates cleanup;
`camber-moment.dll` isolates the airfoil-moment correction;
`pressure-moment.dll` is the first five-point pressure draft; `corrected.dll`
uses exact split integration. Do not confuse the draft with the final build.

The first draft completed 12 actual Unreal trajectories, at 2 and 8 m/s,
80-degree starting bank, zero and both differential-amplitude inputs, compared
with the other task's installed DLL. All had zero reported math failures.
Results changed substantially but were direction-dependent: some recovery
metrics improved and others worsened. Full traces and world wing normals are
in `native-summary.json`; a favorable snapshot is not a recovery guarantee.

Independent tests include numerical thin-airfoil Fourier integration, dense
chord-pressure integration, pressure passivity, finite-difference elastic
virtual work, hinge power, signed motor assistance, and host impulse/CG tests.
The original five-point quadrature missed small cancelling pressure resultants;
the exact split integration fixes that numerical error rather than loosening
the test. Native build succeeded after the game released its module lock.

Known model limits remain: the spatial added-mass matrix uses rigid-wing
geometry and omits elastic/body cross inertia. A measured small loaded-glide
error does not validate it for large feathering. The material stiffness and
separated-flow polar are not experimentally calibrated. Upstroke unloading
does not guarantee zero negative lift once local angle of attack reverses.

## Final build verification

Final SHA256: `F4B330F7CDD420C6D5E52ECE08D100D4EBF2F71EEF041BD9BF62F536631EAEA9`.
Installed at `Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll`.
The math-properties suite passed, including independent pressure integration
and airfoil reference tests. Both native slow and fast loaded-wing grip tests
passed (body rotation 18.950 and 18.231 degrees respectively). The native
handling test still failed: recovered-bank errors 53.99/45.53 degrees,
altitude 20.94 m, zero safety resets and zero math failures. This remains an
open handling issue; the force corrections are not a claim of full recovery.

Normal-path rendered Unreal run confirmed ABI 21 and both biases applied;
the end screenshot was inspected. Physical telemetry recorded no solver
failures. The roll-ramp assertion printed FAIL in this recovery fixture,
which uses a pulse and releases the controls rather than holding the ramp.
It must not be used as a recovery acceptance criterion.

The corrected 180-second soak passed: altitude 200.52 m, peak speed 10.17 m/s,
zero safety resets and zero math failures. The 110-second flight scenario
still failed turn/landing requirements, although it reached 56.467 m and
successfully relaunched. The other task's DLL failed the same scenario,
remaining grounded through powered/coast/boost segments, peak altitude
3.843 m, and no relaunch. Its handling test also failed on the ground.
These are meaningful flight changes, but neither a complete handling pass
nor evidence that the remaining upstroke cancellation is resolved.
The other task's 180-second soak failed at ground level (peak altitude
3.85 m), with zero math failures. Both comparisons used the same saved
preferences and native module, with only the explicitly selected DLL changed.
