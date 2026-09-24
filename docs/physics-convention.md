# Physics convention and sign rules

Single source of truth for reference frames, signs, and the current reduced
real-time model. Both the Haskell MathCore and the C++ OrniCore must conform.

Every entry carries a status marker:

- **REQUIRED** — settled owner requirement.
- **EVIDENCE** — owner's experimental observation; not yet encoded as data.
- **PROVISIONAL** — uncalibrated placeholder constant/formula.
- **TESTED** — asserted by an existing regression test.

## Reference frames

| Frame | Definition |
|---|---|
| Body | +x forward, +y right, +z up: Unreal's left-handed coordinate layout. Components are passed unchanged at the ABI. |
| Wing local (OrniCore) | x chordwise toward trailing edge, y root→tip, z wing normal. |

Units are SI everywhere internally. The Unreal boundary converts metres → cm.

## Sign conventions

- **Sweep** — positive = swept back, negative = swept forward.
  `sin(sweep)` therefore flips sign across zero sweep. **REQUIRED / TESTED**.
- **Crossflow `v_span`** — positive sweeps toward the tip, negative toward the
  root. Reverses with sweep sign. **EVIDENCE / TESTED** (both cores).
- **Angle of attack** — `alpha = twist + atan2(-v_normal, v_chord)`. Positive
  alpha produces lift in +z. **PROVISIONAL**.
- **Lift** — perpendicular to the local planar flow, +z for positive alpha.
- **Drag** — opposes the local planar flow (dissipative, does negative work).
- **Separation `f ∈ [0,1]`** — 0 attached, 1 separated, continuous per element.
  **REQUIRED** (partial stall).

## Reduced real-time model (current)

### Discretisation

- 16 spanwise strips. Haskell simulates two wings (full vehicle); C++
  `WingSimulation` simulates a single isolated wing as the research reference.
- Runtime Haskell geometry: 0.72 m half-span, chord 0.220 / 0.185 / 0.090 m
  at shoulder / elbow / tip, incidence 5° / 2° / −2°. Sweep is derived from
  the planform leading edge (about 15° inboard, 24° outboard).
  C++ research defaults are sweep 20° → 20°, twist 18° → 8°; the `first_flap`
  CLI overrides them. These models are not geometrically reconciled.

### Crossflow baseline

```
v_span = gain * sin(sweep) * |v_local| * F(phase)
```

- `gain = 0.32` — **PROVISIONAL**.
- `F(phase) = 0.65 + 0.35 * |sin(phase)|` — **PROVISIONAL**. Present in C++.
  Not yet applied in Haskell (see reconciliation table).
- `|v_local|` — local planar speed `hypot(v_chord, v_normal)`. **PROVISIONAL**.

### Stall / separation

```
f_target = smoothstep(stallAngle - 3°, stallAngle + 4°, |alpha| - dynamicDelay)
df/dt    = (f_target - f) / tau
tau      = 0.045 s separating, 0.090 s reattaching
```

- `stallAngle` depends on Reynolds number. **PROVISIONAL**.
- `dynamicDelay = clamp(-0.16, 0.16, 0.018 * dalpha/dt)`. **PROVISIONAL**.

### Force coefficients

```
Cl_attached   = clamp(2*pi*alpha, -1.9, 1.9)      (C++ clamps to 1.8)
Cl_separated  = 1.05 * sin(2*alpha)
Cl            = (1 - f) * Cl_attached + f * Cl_separated
                + rotational lift + LEV term        (Haskell only)
Cd_attached   = 0.025 + induced                     (induced = Cl^2 / (pi*0.82*AR))
Cd_separated  = 0.20 + 1.20 * sin^2(alpha)
Cd            = (1 - f) * Cd_attached + f * Cd_separated
```

All coefficients are **PROVISIONAL**; none are calibrated against experiment.

### Transport

Upwind advection + mild diffusion couple adjacent strips. Direction is the
sign of `v_span` (i.e. the sign of local sweep). **PROVISIONAL**.

## Reconciliation status (Haskell ↔ C++)

| Concern | Haskell | C++ | Decision |
|---|---|---|---|
| Crossflow phase factor `F` | absent | `0.65+0.35·|sin φ|` | use `F` in both |
| Load/transport ordering | loads then transport | transport then loads | transport first (C++) |
| Transport speed source | body speed | local planar speed | local planar speed |
| Diffusion `dt`/`dr` scaling | constant 0.018 | constant 0.018 | make `dt`,`dr` dependent |
| Reverse flow | signed section velocity, dissipative drag | chord clamped positive | C++ research core still needs reconciliation |
| Airspeed source | body velocity | fixed `forwardAirspeedMS` | vehicle body velocity |

## Flight stability correction (2026-09-24)

The runtime firmware path now uses the section velocity **through the air**:

```
sectionVelocity = bodyVelocity + cross(omega, position) + flapVelocity
v_chord = sectionVelocity.x
v_normal = dot(sectionVelocity, wingNormal)
v_span = dot(sectionVelocity, spanAxis)
```

`wingNormal` and `spanAxis` rotate with the flap. With normalized planar flow
`(u,w)`, the implemented force pair is `Fx = -D*u - L*w`, `Fn = -D*w + L*u`.
The lift direction is perpendicular to section motion; drag removes energy.
The native regression checks all 27 combinations of (-5, 0, +5) m/s on the
three body axes, including diagonal and reverse flow.

The former negation of body vertical velocity made drag accelerate a falling
bird. The explicit finite-difference added-mass term also amplified acceleration
and ground-contact impulses. It is now omitted pending an implicit fluid/body
inertia solve; this reduces physical completeness but removes an artificial
energy source. Servo backdrive has finite response time, a speed limit and
mechanical travel limits of +/-80 degrees. The battery model limits its assumed
two-servo stall current to 10 A. These actuator constants remain provisional.

`Native/tests/flight_regression.py` exercises the actual shared library with
passivity checks and gravity/contact feedback. It runs three 60-second level-body
trajectories with measurements held at 30, 60 and 144 Hz. This is separate from
Unreal's six-component rigid-body and contact integration.

The current playable Unreal mode applies native aerodynamic forces directly,
with Chaos gravity and contact. The earlier velocity/altitude compensator and
rotation locks have been removed. A bounded, optional attitude torque assists
bank, pitch and coordinated turns; it supplies no translational force.
Both linear and angular state are predicted between the native 240 Hz evaluations,
then mean loads are submitted once per game frame to Chaos. This avoids holding
angular-rate feedback constant across a whole low-rate rendering frame.
Chaos remains authoritative for the actual body and contacts.

The wing now includes **PROVISIONAL**, quasi-static passive feathering:

```
feather = (0.55 + 0.20 * spanFraction)
          * atan2(radius * strokeRate, max(1.5, abs(v_chord)))
alpha = geometricIncidence + aeroelasticTwist + feather
        + atan2(-v_normal, v_chord)
```

This follows the flap-induced inflow, reducing excessive incidence during the
stroke. It vanishes at zero flap rate. It is a prescribed approximation, not a
solved torsional dynamic model. Section lift remains perpendicular to section
motion and drag remains dissipative; there is no added lift/thrust multiplier.

The native firmware vehicle profile now mixes throttle into both amplitude and
cadence (0.5–3.2 Hz). The game selects a provisional 1200°/s, 8 N·m hinge actuator
and an 11.1 V, 1.3 Ah battery with 0.08 ohm internal resistance. These are
vehicle-side linkage/actuator parameters, not a verified commercial servo rating.
Only opposing hinge load reduces tracking speed; overload still backdrives the
wing. The firmware mixer equations and ABI v2 layout remain unchanged.
See [wing-powered flight](wing-powered-flight.md) for tests and limits;
the [earlier stability report](flight-stability-2026-09-24.md) records the
superseded altitude-assisted trainer.

Outstanding research work: implicit added mass, tail forces for arbitrary flow,
calibrated motor/servo/linkage data, full pitch/roll trim, cross-core reconciliation
and aerodynamic comparison with measured experiments. The legacy `stepVehicle`
oscillator still flaps at zero throttle; the runtime `stepFirmwareVehicle` uses
firmware glide mode and holds neutral at zero throttle.
