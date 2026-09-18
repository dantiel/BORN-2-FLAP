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
| Body | +x forward, +y right, +z up, right-handed. Used by `Vehicle.hs` and Unreal. |
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
- Geometry (prototype): span 0.72 m, chord 0.20 → 0.10 m, sweep 24° → −8°,
  twist 19° → 7° (Haskell `stepVehicle` and `first_flap` CLI). C++ defaults are
  sweep 20° → 20°, twist 18° → 8°; the CLI overrides them.

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
| Reverse flow | chord clamped ≥ 0.05 | chord clamped ≥ 0.05 | not modelled yet |
| Airspeed source | body velocity | fixed `forwardAirspeedMS` | vehicle body velocity |

## Not yet modelled (per physics audit)

- reverse flow (chord velocity clamped positive)
- real `omega × r` local spanwise flow
- actuator/servo slew — zero throttle still commands flapping (Haskell)
- added-mass sign and startup transient
- native RTS/ABI load in a real C host (Unreal bridge never exercised)
