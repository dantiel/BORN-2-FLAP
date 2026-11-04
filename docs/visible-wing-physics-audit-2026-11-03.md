# Original visible wing: physics alignment, 3 November 2026

**Historical intermediate build.** The user subsequently corrected the whole Kestrel to 55% scale. The current repair preserves that size and is documented in [Scaled Kestrel pitch audit](scaled-kestrel-pitch-audit-2026-11-03.md). Dimensions, DLL hashes and acceptance results below apply only to the earlier full-size experiment.

This supersedes the earlier same-day pitch/spar audit. The original visible Kestrel outline, **100 cm half-span and shoulder at (3, +/-1, 4) cm**, is restored. The 55 cm render experiment was rejected after the user clarified that the visible dimensions must remain unchanged. The installed solver now follows the original SVG outline, including its leading-edge offset from the shoulder, streamwise chords and root-relative resting washout.

Installed math DLL SHA256: `2AF8FA664F0D4DA047204330D332F3A97FBFF8A2D1B6E66E3D0B62C1DB74EEEA` (ABI 18). Native editor build succeeded. Tests and candidate history are in `.setup/cg-wing-20261103`; native logs are also under `Unreal/Born2Flap/Saved/Logs`.

## Changes

- Each strip uses the visible outline's chord and application point. Leading-edge sweep no longer rotates an already streamwise chord a second time. The original mesh size and shoulder placement are retained; changing aircraft mass does not resize the wing.
- Rendering uses actual geometric/elastic section incidence, local spar bending and the solver's LE axis. Mount incidence is applied to the membrane section, avoiding an extra parent Euler rotation of the swept outline. Collision probes use the deformed, full-size mesh.
- Root attachment suppresses membrane cupping smoothly over the inner 16% of span. This reduces the rounded inner bulge without suppressing outboard bending. Effective bending/torsion calibrations are 4/10, versus the previous 6/20; rod lengths follow the full-size outline. These remain provisional material calibrations. A still softer 2/5 candidate was rejected for free-flight instability.
- The fixed fan now contributes normal added-air inertia, including its actual polygon, mounting incidence and CG-relative lever arms. It remains fixed to the fuselage; no hidden tail actuator was introduced.
- Wing and fan inertia no longer add a second `v cross p_fluid` pitching/yawing couple on top of the complete quarter-chord section polar. The previous combination could create a pitch bias at constant translation. Acceleration, rotating-basis and joint terms remain. Tests cover steady-flow cancellation, CG-reference invariance and angular response.
- Positive yaw gives the outside wing the larger commanded stroke, with all resulting thrust/roll/yaw still calculated from surface forces. Glide yaw hinge authority increases from 8 to 12 degrees at full scale. Neither is a prescribed body torque or attitude constraint.
- F5 now distinguishes tail mounting angle from actual mean airflow angle of attack, and shows normal force and yaw moment about the CG.

The unsteady-load convention is informed by the separation of quasi-steady quarter-chord loads and non-circulatory acceleration/pitch-rate terms in [Li et al., Wind Energy Science 7 (2022), sections 2 and 3](https://wes.copernicus.org/articles/7/1341/2022/). That reference is not validation of this particular simulation.

## Native results with the 50 g reproduction profile

Profile: `Unreal/Born2Flap/Config/FlightProfiles/KestrelVisibleWing.ini`. It reproduces the earlier saved settings: tail -8 degrees, wing incidence 0, CG -4 mm, wheel 1. The player's current preferences were not overwritten.

| Check | Result |
|---|---|
| Mounted native CG slider, -50 / 0 / +50 mm | PASS: actual Chaos COM moves +5 / 0 / -5 cm |
| Alternate Brain settings action | PASS |
| Deformed native wing geometry | PASS, both original-size wings |
| DLL strip geometry vs rendered SVG | PASS, all 32 strips; maximum chord/forward-arm discrepancy 0.000000199 m |
| Fixed-tail signs, glide and flap | PASS: +/-8 degree mounting reverses normal force and banked yaw at identical level airflow |
| Angular impulse response | 24/24 PASS |
| Haskell properties | PASS |
| Powered yaw, both directions | PASS: heading +/-16.26 degrees, mean bank +/-7.05 degrees |
| Glide yaw, both directions | PASS: heading +/-10.05 degrees, mean bank +/-1.14 degrees |
| Normal launch at 30 FPS | PASS: peak pitch 25.56 degrees; no solver errors or safety resets |

CG changes produce a clear native response at otherwise identical settings:

| CG setting, aft positive | Peak absolute pitch | Late-window mean pitch |
|---|---:|---:|
| -100 mm | 8.59 degrees | 0.52 degrees |
| 0 mm | 16.93 degrees | 11.13 degrees |
| +100 mm | 30.31 degrees | 16.67 degrees |

The constrained roll-response check passes its relative body/wing and fixed-tail tests, but reaches nearly inverted flight under a held input. It does **not** establish gentle handling or horizon support.

Rendered captures `StripForces.png` and `StripAirflow.png` were inspected after running the controlled airborne fixture with `-B2FNoMenu -UseFixedTimeStep`. They show the original-width wing, per-strip force origins and new tail-airflow diagnostics. A first capture was covered by the menu and is not visual evidence. The useful captures are copied into `.setup/cg-wing-20261103/`.

## Limits and remaining issues

- The later saved settings (13 g, CG -59 mm, tail +5 degrees, wing mount +5 degrees) still fail the native pitch bound, reaching 90 degrees. Do not call that configuration stable or overwrite it silently with the passing profile.
- Tail mounting sign alone does not determine lift sign. In the controlled test, a 2 m/s upward body-relative flow at 8 m/s forward speed makes the effective angle negative even with +8 degree mounting; both +/-8 settings then produce downward tail force. F5 now exposes this distinction.
- The motor can lose stroke travel at high requested cadence under the full-size wing's air inertia. At 5 m/s and 10 degree body airflow angle, wheel 1 had less useful forward force than wheel 0.5 in the fixed-body audit. Cadence selection is not yet a torque/load-optimal controller.
- A zero-angle tail is not a universal trim for the original root-relative washout. Do not reuse neutral-incidence test expectations from the smaller, positively pitched solver outline.
- Elastic modes still approximate deformation and do not contain the complete reciprocal body/hinge/elastic mass coupling. A convected inter-surface wake and full unsteady circulation model are also absent. Broad handling and long-duration flight acceptance remain separate from the checks above.

The main repair is geometry/force consistency and removal of an identified pitch bias, not a claim that every aerodynamic effect is complete or every tuning combination must self-stabilize.
