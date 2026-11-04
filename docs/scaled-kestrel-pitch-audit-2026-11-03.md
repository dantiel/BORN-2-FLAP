# Scaled Kestrel pitch audit, 3 November 2026

This supersedes the earlier full-size-wing audit. The user's corrected visible size is retained: **55 cm wing half-span**, the uniformly scaled fuselage/tail, and the existing shoulder at (3, +/-1, 4) cm. Carbon rod lengths and diameters are unchanged.

## What was wrong

The visible tail had been scaled by 0.55, but its aerodynamic polygon, root position and added-air inertia still used the larger tail. The wing solver had also reverted to a different, strongly swept outline with a +6.5 degree root-incidence offset. These were real geometry mismatches, not merely insufficient elasticity.

At identical level flow, the oversized tail produced **3.31 times the correct force and 6.01 times the correct pitching moment about the body origin**. The factor about a displaced CG differs because that reference point does not scale with the tail.

## Installed changes

- The 32 wing strips follow the current scaled SVG chords and leading edges, including the actual shoulder offset. Their forces retain individual application points and directions.
- The wing root is the zero-incidence reference; the existing relative washout is retained. `VerticalMountAngle` sets the actual root incidence instead of adding to a legacy +6.5 degree offset. Rendering consumes these same physical section angles.
- Tail area, force positions, mounting pivot, dry component inertia and added-air inertia now follow the visible scaled polygon. The fan remains fixed to the fuselage.
- Bending calibration changes from 6 to 4; torsion remains 20. This increases load-dependent bending without lengthening the rods or multiplying the rendered displacement. Material calibration remains provisional.
- The attached membrane root again suppresses cupping smoothly over the inner 16% of span.
- F5 now separates pitch about the actual CG into wing forward-force, vertical-force, section-couple and tail contributions. Positive means nose-down. Green forward-force arrows remain projections of the resultant, not additional thrust forces.
- The native pitch fixture can independently select throttle and pitch input, so glide and powered cases can be compared with identical saved geometry/settings.

Unrelated concurrent input work changed yaw to reciprocal flap-amplitude control with no yaw actuator in glide. That work is preserved. Mixer regression tests now check that contract; the old glide-yaw command test is no longer an acceptance test for this control scheme. Passive yaw-rate/sideslip aerodynamic tests remain separate.

## Evidence

Reproduction settings: 50 g, CG -60 mm (aft-positive UI), tail -4.7 degrees, wing mount 0, servo stall torque 2.02017 Nm, wheel 0.98, stroke ferocity 50, return ferocity 0. The player's preferences are preserved. A copy is saved in `Unreal/Born2Flap/Config/FlightProfiles/KestrelScaled.ini`.

| Native check | Result |
|---|---|
| Before repair, neutral powered input | FAIL: peak absolute pitch 90 degrees |
| Corrected geometry but legacy +6.5 degree root offset | FAIL: peak absolute pitch 90 degrees |
| Final neutral powered flight, same saved settings | PASS: peak 36.57 degrees, late mean +24.45 degrees |
| Final normal launch, 30 FPS | PASS: peak 37.91 degrees, late mean +24.97 degrees |
| Moderate dihedral input, -0.3 pitch command | PASS: peak 38.83 degrees, late mean +18.86 degrees |
| Full dihedral input held for the whole test | Steep nose-down dive, ground reached; intentionally does not pass the neutral-flight bound |
| Glide, equivalent final geometry/incidence | PASS: peak 25.97 degrees, late mean -11.00 degrees |
| Actual mounted CG slider and alternate settings action | PASS; -50/0/+50 mm moves Chaos COM +5/0/-5 cm |
| Deformed native wing geometry | PASS, both scaled wings |
| DLL wing geometry against renderer SVG | PASS, max chord/forward-arm error 0.000000110 m |
| DLL tail patch positions against renderer polygon | PASS, max error below 0.000000000001 m |
| Tail force and banked-yaw sign at identical airflow | PASS |
| Angular impulse checks | 24/24 PASS |
| Passive yaw-rate/sideslip and local-speed derivatives | 57/57 PASS at 5 m/s, 5 degrees airflow angle, 240 Hz |
| Haskell property suite, including current mixer contract | PASS |

Native flight checks reported no solver failures or safety resets. The two F5 views were rendered and inspected with the integrated build; the pitch ledger is readable and the patch origins follow the scaled wing/tail. Screenshots and logs are under `.setup/pitch-scaled-20261103/`.

In the fixed-flow audit at 5 m/s and 5 degrees body airflow angle, extreme dihedral produced about **+0.102 Nm nose-down from wing forward force**, while anhedral produced **-0.066 Nm nose-up**. This contribution is `z * Fx` about the CG; the separate vertical contribution is `-(x - cgX) * Fz`. Downstroke/upstroke forces are recorded separately in `final-flap.json`. The force decomposition closes to floating-point precision.

## Limits

This is not a level-flight autopilot or a claim of complete aerodynamics. The saved high-power configuration still climbs strongly with neutral pitch. Full held dihedral can dive into the ground. The tail's mounting sign alone does not determine its force sign; local airflow and body rotation determine angle of attack.

Changing tail trim alone to zero did not cure the original powered oscillation. A +3 degree tail experiment bounded powered pitch but made the glide steeper, so it was not silently installed as a universal trim. The final repair preserves the saved -4.7 degree setting.

The fixed-flow force audit constrains body motion and reports moments shifted to the requested CG; it does not enable the free-translation spatial solve and then discard its velocity increment. Free-flight acceptance comes from the native tests. Elastic deformation still lacks complete reciprocal body/hinge/elastic inertia coupling and a convected wake model. Long-duration handling and player acceptance remain separate from these checks.

Integrated ABI-18 math DLL SHA256: `1B30CE081BB6E23FCFAF61428601562178AE3211DB754E615C1EE2163F451DC1`. Native editor build: `.setup/pitch-scaled-20261103/editor.log`. Earlier candidate `final.dll` has identical neutral-pitch physics but predates the concurrent yaw-only mixer change; `integrated.dll` includes it.
