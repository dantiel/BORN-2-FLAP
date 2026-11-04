# Pitch and spar audit, 3 November 2026

Historical intermediate audit. The installed geometry and solver have since been superseded by [the original-visible-wing repair](visible-wing-physics-audit-2026-11-03.md), which records the final build and remaining failures.

The pitch problem is **not fully fixed**. This change installs a verified collision-history repair and a moderate increase in Kestrel spar bending. It does not install the experimental inertia changes that failed flight checks.

## Installed changes

- Clear previous wing-sweep and water-contact samples on reset and hand-launch teleports; skip wing collision sweeps while the bird is held. Previously a ground-to-air teleport could generate a ground impulse at the airborne location. At 30 FPS a captured test registered a ground sweep at 100 m, followed one frame later by a 62.8 rad/s pitch safety reset. The repaired launch has no such contact or safety reset after teleporting.
- Kestrel effective bending scale changes from 8 to 6. Torsion remains 20. Material moduli and rod diameters are unchanged; this is a provisional skin/brace stiffness calibration, not measured material data. It changes the beam solution and aerodynamic geometry, not just the mesh.
- Force audits now record mean absolute tip bending and elastic twist. The sagittal pitch audit records settled pitch-rate RMS and pitch range.

Your saved preferences are unchanged: CG -4 mm, tail incidence -8.5 degrees, wing mount -5 degrees, base frequency 3 Hz and wheel 1. Zero-incidence comparisons use a separate profile file.

## Evidence

Artifacts and frozen profiles are in `.setup/pitch-flex-20261103/`. Logs are in `Unreal/Born2Flap/Saved/Logs/`.

At 5 m/s, throttle 0.72, wheel 1, zero wing/tail incidence, using the prescribed-motion force audit:

| Stroke centre | Before / after mean absolute tip bend | Before / after wing vertical force | Before / after wing forward force |
|---|---|---|---|
| -15 degrees | 23.3 / 32.7 mm | 0.742 / 0.748 N | 0.181 / 0.179 N |
| 0 degrees | 23.6 / 33.0 mm | 0.797 / 0.805 N | 0.185 / 0.182 N |
| +15 degrees | 23.1 / 32.4 mm | 0.814 / 0.811 N | 0.176 / 0.175 N |

Native tests using the rebuilt collision code and the accepted physics candidate:

- Powered yaw, both directions: PASS; heading changes about +/-104 degrees, mean banks about +/-17.3 degrees, initial bank zero, no solver errors or safety resets. Logs `pitch-accepted-powered-*`.
- Glide yaw, both directions: PASS; heading changes +/-63.35 degrees and mean banks +/-5.86 degrees, no solver errors or safety resets. Logs `pitch-bend-only-glide-*`.
- Launch at 30 FPS, zero incidence, centred CG: PASS; peak pitch 23.57 degrees, final altitude 110.47 m, speed 5.12 m/s, no solver errors. Log `pitch-accepted-launch-0`.
- Launch with the saved negative incidences: **FAIL**, peak pitch 89.66 degrees. The false ground impulse is gone; the remaining large excursion is real in this simulation. Log `pitch-bend-only-launch--4`.
- Haskell properties: PASS (`test-installed.log`).

CG is connected in the current solver. At 1.95 seconds in the same zero-incidence sagittal fixture, CG -100 / 0 / +100 mm gives pitch -6.59 / +1.95 / +11.39 degrees. This demonstrates a response, not universal stability or a trim recommendation. The fixture constrains roll/yaw; native tests remain necessary.

Settled flap pitching barely changed with the accepted spar adjustment: approximately 75 degrees/s pitch-rate RMS in the zero-incidence sagittal case. Do not describe the bending change as a cure for flap-induced pitch.

## Rejected experiments and remaining work

The fixed fan currently has aerodynamic loads but no added-fluid inertia. Adding acceleration-pressure inertia reduced isolated pitch-rate RMS, but the native glide turns then rolled over. Including steady through-flow momentum as permanently attached tail fluid also caused large trim changes and loss of lateral stability. Neither variant is installed.

Elastic bending/torsion also lacks reciprocal body/hinge cross-inertia. A candidate added those blocks, dry elastic gravity and rotating-frame terms to the existing implicit solve. Glide checks passed, but pitch oscillation increased. Combining it with tail acceleration inertia still failed glide turns. The experimental source diff is retained in `.setup/pitch-flex-20261103/rejected-inertia-experiments.patch`; it is not production code.

The next repair needs a consistent unsteady surface-load and elastic-inertia model, including the geometry derivatives of the deformed modes, with longitudinal and lateral acceptance tests together. Acceleration inertia alone is not the entire unsteady aerodynamic response: the reference equations distinguish acceleration and pitch-rate terms as well as circulation/wake response. See [DTU Risoe-R-1663, sections 2.2.1 and 2.2.3](https://backend.orbit.dtu.dk/ws/portalfiles/portal/3315978/ris-r-1663.pdf). These are modelling references, not validation of this game's implementation.

No horizon constraint, artificial pitch damper, lift bonus or hidden tail actuator was added. Prior broader flight/handling failures remain open; the tests above do not establish full flight acceptance.
