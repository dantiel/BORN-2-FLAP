# Physical flight correction, 2026-10-23

This is a solver correction in progress, not a validated flight release. Sustained forward flight, climbing and inverted trim are not yet demonstrated with the corrected model. Hover is neither commanded nor guaranteed.

## Changes

- Removed independent forward momentum jets, positive-only downstroke lift, phase-selected feathering, duplicated pressure resistance and supplementary body-roll damping. Local flow now supplies lift, drag and spanwise resistance through one force law in glide and flap.
- The same local force supplies both the body moment and the full servo hinge load. Removed the elastic-state multiplier that made part of the load free.
- Incidence and elastic twist rotate the surface frame once; the aerodynamic centre moves with incidence, sweep, stroke and bending. Point velocity uses angular velocity crossed with its actual offset. Leading-edge offsets use the same aft-positive convention as the mass model.
- Removed the solver's fixed 16 degree dihedral to match the other agent's removal of that render offset.
- Stall delay now depends on signed growth of absolute angle of attack, and angle differences wrap across pi. This removes a positive/negative-lift history asymmetry.
- Torsion uses the actual force lever about the leading edge. Bending loads are projected into the wing plane, rather than always taking body Z. Removed force-dependent relaxation delays; saturation applies to targets before relaxation, and slopes follow the resulting deflection.
- Corrected brace rotational-stiffness to distributed-GJ units and fourth-power geometric stiffness scaling. These remain homogenized beam approximations.
- Replaced the artificially weakened circulation slope with a finite-aspect-ratio baseline, and paired separated lift/drag with a 1.2 normal-pressure coefficient. These are generic polars, not measured polars for this membrane.
- The motor has a torque-speed envelope: maximum positive shaft power cannot exceed stall torque times no-load angular speed divided by four. Battery drain includes acceleration/reversal motor torque, with a mechanical-power lower bound on electrical consumption. Stall current (5 A per servo) and efficiency (70 percent for the lower bound) remain assumed.
- Removed mass-based multiplication of commanded frequency: adding battery mass to unchanged wings no longer changes the requested waveform frequency behind the tuning control.
- Tuning changes persist immediately, with load/test guards and rejection of non-finite inputs. Saved vertical incidence is currently 0 degrees; an old saved 15 was not the constructor default.

## Verification

`cabal test math-properties --project-dir=MathCore --test-show-details=direct` passes. New checks cover local-flow passivity across stroke/incidence, two-second mirrored unsteady stall and structural histories on a symmetric reference wing, unchanged external hinge reaction through elastic states, torsional scaling, and motor torque-speed power bounds.

`Tools/test-roll-coupling.py` now uses the actual ABI 14 tuning layout. The standalone DLL roll fixture passed during this work; it constrains pitch and translation and is not full flight validation.

`Tools/audit-flight-force.py` is a repeatable read-only servo-coupled wind-tunnel audit using a copy of saved tuning in each context. It reports signed force, pitch moment, actual stroke travel, battery consumption and rejected solver steps. Three seconds of prescribed body velocity and attitude do not establish free-flight trim. The final scan retained net drag at 3, 5 and 8 m/s with zero rejected steps. At 8 m/s and zero mount incidence, mean force was -0.637 N forward and +1.224 N upward against 0.491 N weight: ample lift but insufficient propulsion. No successful forward-flight tune is claimed. The final DLL roll fixture passed at approximately 1.37 ms per step on this machine.

Logs and CSV results are under `.setup/physical-flight-20261023/`. The C++ changes compiled, but Unreal linking failed because two open editor processes held the module. The corrected backend has not been installed into the running game.

## Remaining accuracy limits

This is a blade-element model with empirical dynamic-stall memory and quasi-static elastic relaxation. It is not CFD, a resolved wake, or a dynamic structural resonance solver. First-order relaxation cannot represent physical elastic resonance. Pitch/yaw inertia, moving centre of mass and products of inertia still require a full coupled rigid-body audit. Ground effect has only scalar clearance and cannot correctly resolve arbitrary attitude near terrain. A scalar 0..1 spring diagnostic is not stored elastic energy. Servo current/efficiency, membrane polars, torsional stiffness, damping and reflected gearing inertia need measurements before the model can be called calibrated.

A membrane with rest camber and geometric washout is not exactly inversion-symmetric. The regression checks the governing law on mirrored geometry; it does not claim identical trim with unchanged camber and controls.

The force decomposition follows [NASA Glenn's aerodynamic-force definitions](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/aerodynamic-force/): lift is perpendicular to relative flow, drag is along the opposing direction. Forward propulsion must result from the moving surfaces and the drive's energy, not an extra body-forward force.

Final standalone DLL SHA-256: `D8484D4E863895AEDF10B699D3B81A369910BEF4302E245321621F310A4419B7`. Final audit: `.setup/physical-flight-20261023/final-wind-tunnel.csv`; roll fixture: `.setup/physical-flight-20261023/final-roll-test.json`.

## Pressure and propulsion follow-up

The original replacement still delayed broadside pressure through stall memory. Pressure is now an explicit signed normal-velocity reaction (`Cp = 1.2`) at the real section normal, shared by glide, roll and flap. It replaces the separated pressure polar rather than being added on top. Circulation and its induced drag fade with separation. The pressure term always dissipates local relative-flow energy.

A second error drove membrane camber from the hypothetical fully attached Cl even after stall. It now follows actual solved normal pressure. In the prescribed 2 Hz / 35 degree wing test at 5 m/s, the pressure-only correction produced 0.0151 N forward force per wing; using actual pressure for camber increased that to 0.0355 N without changing membrane stiffness.

With the saved 50 g configuration (1132.07544 degrees/s servo speed, 3.77556038 Nm stall torque), identical before/after DLL wind-tunnel fixtures at zero mount incidence gave:

| Speed | Previous forward force | Corrected forward force |
|---|---:|---:|
| 3 m/s | -0.169171 N | +0.121275 N |
| 5 m/s | -0.330037 N | +0.127092 N |
| 8 m/s | -0.580137 N | +0.051746 N |

At a constrained +2 rad/s body roll and 8 m/s, mean resisting torque changed from -0.098648 to -0.111375 Nm (about 13 percent stronger). Near-zero-speed steady resistance is essentially unchanged; the new broadside-load regression specifically protects the immediate pressure response before separation develops. These are constrained tests, not proof of full-flight stability or climb.

The full math suite and DLL roll-coupling fixture pass, with zero rejected steps in these audits. Results and the preference snapshot are in `.setup/pressure-audit-20261023/`. Installation was attempted but blocked by the running editor holding the physics DLL; its live simulation still uses the previous backend.


## Kestrel pressure, travel and elastic dynamics follow-up

This section supersedes the earlier quasi-static elasticity and installation status above.

- Normal pressure now has one implementation (`surfacePressure`, Cp 1.8) for wings and tail, as does skin drag (0.03). Tail circulation and induced drag now use the wing's finite-aspect-ratio equations with the tail's own geometry, replacing its unrelated 1.1/3.5 lift/pressure polar. Tail incidence was re-trimmed after correcting its lift slope. These remain effective, unmeasured aerodynamic coefficients.
- Spanwise wing resistance increased from 0.018 + 0.08 separation to 0.10 + 0.12 separation. The signed forces oppose surface-relative motion in glide and flap and remain fully charged to the servos.
- Travel ceiling increased from +/-35 to +/-55 degrees. Ferocity no longer silently reduces travel. The game setting now controls BOTH stroke and return ferocity; previously return remained at 50 even when the slider was zero. One analytic waveform-derivative bound limits cadence using the actual shaped arc, replacing duplicate full-arc caps.
- The 50 g Kestrel has two coupled structural modes per wing, bending and LE torsion. Static beam compliance supplies mode shapes and stiffness; distributed dry wing mass and flat-plate added-air inertia supply a symmetric mass matrix. Midpoint integration stores kinetic/elastic energy and dissipates it through material damping. Bend/twist velocities enter the actual local air-relative velocity. Resonance follows inertia and stiffness, independently of firmware dwell/phase shaping.
- Adding the displaced-air inertia was essential: a dry-mass-only trial rang too quickly and became unstable in free flight. Dry-only trial results are not the delivered model.
- Bending stiffness increased 25%; membrane billow compliance decreased modestly. Torsional stiffness was recalibrated for dynamic response rather than relying on the former 15-degree deformation clamp. A prescribed 43-degree / 3.64 Hz stroke at 5 m/s retained 0.477 N combined wing thrust while reducing peak twist from 33.8 to 16.4 degrees. This is an isolated prescribed-wing comparison, not full-aircraft thrust.
- Tail roll resistance is now inside the implicit roll residual, evaluated at the candidate roll rate rather than held at the old rate. The host resolves explicit pitch/yaw/elastic feedback at 960 Hz; the previous 240 Hz step could overshoot the stronger damping. The extra accuracy costs approximately 11-14 ms of physics per 60 Hz frame on this machine, versus about 3 ms at 240 Hz.

### Reproducibility and limits

`Tools/audit-flight-force.py` now accepts sample duration, side velocity, roll rate, integration rate and temporary servo/trim overrides. It reports actual shaft travel, force, battery power and peak elastic deformation. The audit is a constrained wind tunnel, not a free-flight test.

`test-flight.ps1 -BirdModel 2 -Preferences <ini> -PhysicsHz 960` selects the Kestrel explicitly and loads an isolated tuning file. The old test otherwise selected Raven model 0 regardless of the desired coast bird. Normal saved preferences are not overwritten by these tests.

The reviewed tuning snapshot is `.setup/stroke-pressure-20261023/kestrel-tune.ini`: 50 g Kestrel, 1200 degrees/s, 3.77556 Nm stall torque, zero ferocity/incidence/CG offset, -5-degree stroke-centre setting, 25% aileron and 55% elevator. This is a simulator tune, not a claim that a real 50 g aircraft carries the configured servo and 3.46 Ah battery; the simulator currently permits independent total-mass and hardware specifications. The user's live preferences changed during the work and were preserved.

The structure is a reduced two-mode linear model with assumed damping ratio 0.25, not a measured full finite-element model. Added-air cross inertia between bending and torsion is included, but their full cross inertia with the moving hinges and fuselage is not yet assembled into one mass matrix. Flow memory, wake/downwash, ground effect and whole-body inertia retain the earlier documented approximations. No claim of completely accurate or fully calibrated physics is justified.

Energy conservation/dissipation, resonant-frequency response, coupled modal energy, shared passive tail pressure, waveform speed bounds, full commanded travel and the existing math properties pass. Standalone roll-coupling checks pass. Final free-flight results are recorded below after the trim checks; intermediate CSVs/logs are retained under `.setup/stroke-pressure-20261023/`.
