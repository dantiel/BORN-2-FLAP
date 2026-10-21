# Physics tuning guide

A field manual for tuning the ornithopter's flight feel. Everything in this
document is a **live source constant** — a `Double` in the Haskell math core
(`MathCore/src/Born2Flap/Math/`). There is no hidden magic: change a value,
rebuild the DLL, relaunch, and the bird changes.

## Where the numbers live

| Concern | File | What it holds |
| --- | --- | --- |
| Wing strip aerodynamics + thrust/drag/lift coefficients | `Math/Vehicle.hs` | The per-strip force solver and every free gain (`flapJetCoeff`, `circulationLiftEfficiency`, `separatedCd`, …) |
| The servo-closed-loop path the **game actually runs** | `Math/FirmwareVehicle.hs` | Firmware mixer → servo → wing → hinge torque feeding back into the servo |
| RC mixer, flap amplitude/cadence, flight profiles | `Math/Firmware.hs` | Stroke geometry, ferocity, elevator/aileron mixing |
| Servo + battery actuator model | `Math/Servo.hs` | No-load speed, stall torque, back-drive |
| Aeroelastic spar / rod stiffness | `Math/Structure.hs` | `siBendScale`, `siTwistScale`, rod diameters |
| Bird models (mass, geometry, model IDs) | `Math/Model.hs` | `wmMassKg`, per-model outline |
| Airfoil section (camber, reflex, membrane) | `Math/Section.hs` | `spMembraneK`, `zeroLiftAngle`, pitch moments |
| Planform (area, aspect ratio, inertia) | `Math/Planform.hs` | Derived wing mass/inertia |

The authoritative flight mass is **not** only in `Model.hs`: the Chaos body mass
`BodyMassKg` (C++ `Born2FlapFlightPawn.h`) is pushed to the solver via the FFI and
overwrites the model's mass. The two must agree or the wing fights the wrong
weight (see "Mass" below).

## The mental model

The wing strip solver computes, per spanwise strip and per step:

- `q = ½·ρ·|v|²` — dynamic pressure from the **local** section velocity
  (body translation + rotation + flap motion + aeroelastic deflection).
- `cl` — lift coefficient, blended between attached and separated flow by the
  separation fraction `f`: `cl = lerp(attachedCl, separatedCl, f) + rotational + LEV`.
- `cd` — drag coefficient, same blend.
- A separate **momentum-jet thrust** `flapThrust` that does not depend on `alpha`
  at all (it is the reaction to the downstroke shedding a backward jet).

So lift/drag are angle-of-attack machinery; **thrust is a flap-speed machine**.
This split is the single most important thing to internalise before turning any
knob: adding thrust does not add lift, and softening drag does not add thrust.

## Thrust

### `flapJetCoeff` (Vehicle.hs) — *the* thrust scalar

The momentum-jet thrust factor. It scales **both** jet components:

```
staticJet  = ½·ρ·area·flapSpeed² · flapJetCoeff · camberBoost      (hover thrust)
advanceJet = ½·ρ·area·chordVelocity·flapSpeed · flapJetCoeff · camberBoost · flapAdvanceThrustGain
```

- `staticJet` ∝ flap-speed² and exists even at zero airspeed — it is the
  hover/recovery thrust.
- `advanceJet` ∝ forward flow × flap speed and only appears once the bird has
  speed; it is what sustains cruise into a headwind.

**In practice `staticJet` dominates at all speeds** (the advance term carries a
~0.04 gain), so `flapJetCoeff` is effectively a *global* thrust multiplier:
raise it and the bird accelerates harder everywhere — cruise and low-speed alike.
This is the correct lever for "a pinch more power", but it is **not** the lever
for "recovers badly from low speed" because it cannot target only the slow end
without also changing cruise. Current value: `1.0` (was `0.9`; a 49 g kestrel at
`0.70` measured *negative* cruise thrust and could never reach flying speed).

### `flapAdvanceThrustGain` (Vehicle.hs)

Cross-term gain between oncoming chordwise flow and flap velocity — thrust that
*only* exists at speed, vanishing at hover. Raise it to make the bird bite harder
into a headwind / at the top of the speed envelope without affecting hover.
Current: `0.04`.

### `aeroCamberThrustGain` (Vehicle.hs)

How much the aeroelastic membrane camber (`camberPrev`, from `membraneCamberTarget`)
amplifies the jet. The deeper the cup the wing holds under load, the more flow it
redirects aft. This is the one term that makes thrust *grow* with the aeroelastic
state — the wing gets stronger as it loads into cruise. Current: `2.2`.

### Ground-effect thrust (`groundEffectThrustGain`, `groundEffectUpstrokeRecover`)

Extra downstroke thrust and upstroke-jet recovery near the deck (image-vortex
cushion). Raise for a stronger ground-effect "pop" on takeoff. Current: `0.20` /
`0.25`.

## Drag

### Body parasitic drag CdA — `0.0004 m²` (Vehicle.hs **and** FirmwareVehicle.hs)

The fuselage's `CdA`. This is the "air feels like water" knob. A 0.42 m × 0.03 m
streamlined body at Cd ≈ 0.44 is ~4e-4 m². It must be kept identical in both files
(the old 0.011 m² was ~27× too high and ate a third of the bird's weight at 7 m/s;
a later drift to 0.0030 made the air feel "watery"). **This is the floatiness /
terminal-velocity lever**, not lift.

### `attachedCd` (Vehicle.hs) — wing profile drag

`0.03 + inducedCd`. The `0.03` is the feathered membrane's parasitic drag. Low on
purpose: the live membrane sheds pressure drag a rigid plate would take. Raise it
to make glide sink faster without touching lift.

### `separatedCd` (Vehicle.hs) — stall drag

`0.18 + 1.05·sin²α`. The drag spike when the wing is stalled. This is the
"stalls a bit too much" knob: a high coefficient turns an incipient stall into a
hard sink (drag jumps ~10× as α rises). Soften it (lower the `1.05`) to let a
brief stall be survivable; harden it to make stalls punishing. Current: `1.05`
(was `1.20`).

### `flapDampingCd` (Vehicle.hs) — flap pressure drag

Flat-plate drag opposing the wing's **normal** (flapping/roll) motion. This is the
lateral/roll-damping channel. **Do not raise it to chase thrust or roll authority**:
the hinge torque it produces is `½·ρ·Cd·ω²·∫chord·r³dr` — at full flap rate on a
1 m wing that's ~5.4·Cd N·m, so large values back-drive even a strong servo into
its end-stop and latch it there. Current: `1.5`.

### `flapUpstrokeDampingFraction` (Vehicle.hs)

Fraction of `flapDampingCd` kept on the upstroke. A real bird feathers the recovery
stroke so the downstroke bites full air and the upstroke sheds nearly all of it.
This asymmetry is what turns symmetric pressure drag into net lift + roll authority.
Current: `0.35`.

### `circulationLiftEfficiency` (Vehicle.hs) — lift + induced drag together

Bounds-vortex lift efficiency relative to a rigid thin airfoil. **This is not the
floatiness lever.** It scales the effective lift slope
(`2π·AR/(AR+2)·circulationLiftEfficiency`), and induced drag is `cl²/(π·0.82·AR)`.
Lowering it makes the bird sink (drag) rather than making it float — and it
collapses roll damping, because the torque that holds the wings level is this lift
slope responding to roll rate. Keep it high; make the bird sink with **drag**, not
lift. Current: `0.30`.

## Stall & separation (the low-speed recovery machinery)

The separation fraction `f` is a first-order lag toward a smooth-stepped target:

```
f_target = smoothStep(stallAngle − 3°, stallAngle + 4°, |α_eff| − dynamicDelay)
df/dt    = (f_target − f) / tau
```

- `stallAngle` = `16° − 1.4·log10(150000/Re)`, clamped 10°..18°. Low Reynolds
  (low speed) stalls *earlier* — physically correct.
- `dynamicDelay` = `0.018·αRate` (±0.16 rad) — dynamic-stall lag; the faster the
  pitch-up, the later (in α) the stall sets in.
- `tau` — **asymmetric hysteresis**: `0.045 s` entering separation, `0.060 s`
  reattaching (was `0.090`). This is the lever for "stalls a little, then can't
  recover": a long reattachment tau keeps the wing separated (no lift → no pitch
  authority, high separated drag → no net thrust) through several flap cycles
  after α has already dropped back below stall. Shorten it for snappier stall
  recovery; lengthen it for a more punishing, lingering stall.

`separatedCl = 0.12·sin(2α)` and the `0.05·lev` leading-edge-vortex lift are the
residual lift available while stalled. The rotational lift term
`0.20·chord·αRate/safeSpeed` is the Kramer effect.

## Aeroelasticity (wing flex)

In `Math/Structure.hs`:

- `siBendScale` — spanwise bending stiffness gain. **This is the resonant-range
  lever**: the wing must flex to the ideal airfoil *at the bird's design weight*,
  not too soft (flag-flapping, no thrust, nose-heavy) and not too stiff (plank).
  The invariant is *effective stiffness ∝ design mass*: at 50 g the kestrel uses
  `siBendScale 260`; at 25 g the half-weight would flex 2× too much, so the light
  variant halves the *rod diameter* (EI/GJ ∝ d⁴) instead.
- `siTwistScale` — torsional stiffness gain (elastic washout). Matched the same
  way (`4.0` at 50 g).
- `kestrelLightRodScale` = `0.5^(1/4)` ≈ `0.841` — every rod diameter in the
  Kestrel Light is thinned by this factor, halving EI/GJ to cancel the half weight,
  so it flexes to the *identical* ideal airfoil as the 50 g bird.

Membrane cupping (`Section.hs`) adds a load-proportional camber
(`membraneCamberTarget = spRestCamber + spMembraneK·cl`); `spMembraneK` is how much
the membrane billows per unit lift. This feeds both the pitch moment and the
`aeroCamberThrustGain` above.

## Servo & battery

In `Math/Servo.hs`:

- `servoNoLoadSpeedDegPerSec` — no-load slew rate. A slow servo at high cadence
  can only track a few degrees of the commanded stroke, so flap thrust collapses.
  This is the *speed-limited* failure (distinct from torque-limited).
- `servoStallTorqueNm` — stall torque. Aero hinge torque consumes torque, so under
  load the servo slows; past stall it is back-driven (the wing pushes the servo).
  This is the *torque-limited* failure.
- `servoBackdriveDegPerSecNm` — compliance when overpowered.

The real kestrel servo (VOTIK PTK 7465 Plus) is 750°/s / 0.74 N·m. The sim default
carries a ×1.8 fidelity on torque (`1.332 N·m`) to match observed behaviour. The
Blue Arrow AF D43S pairs with the lighter bird and is faster (1500°/s).

## Mass

- `wmMassKg` (`Model.hs`) — per-model spec mass. Current: kestrel/turtle `0.050`,
  raven `0.086`, prototype `0.036`, kestrel-light `0.025` kg.
- `BodyMassKg` (`Born2FlapFlightPawn.h`, C++ default `0.050f`) — the **actual**
  flight mass applied to the Chaos body and pushed to the solver. It overrides the
  model mass, and it **persists to `FlightPreferences.ini`** (`Flight.BodyMassKg`),
  so a saved slider value wins over the source default. If the bird suddenly feels
  like a brick or a feather, check the hangar's BIRD WEIGHT slider first.
- `referenceWingMassKg` (`Planform.hs`) = `0.005` — the 5 g structural wing mass
  the planform-derived flap inertia scales from.

## Mixer & stroke geometry

In `Math/Firmware.hs`:

- Flap frequency `freqHz` = `0.5 + freq01·(fwFlapBaseFreqDh·0.1·speedFactor − 0.5)`
  — base cadence in deci-Hz, scaled by the CH6 speed modifier.
- Flap amplitude `amplitude` = `throttlePct · min(ampMaxDeg, amplitudeSpeedCap)`,
  where the cap `= servo_speed_deg/s / (2π·f)` keeps the commanded stroke one the
  servo can actually track. `ampMaxDeg = 24`.
- `rollAuthorityGain = 2.0` — amplifies the flap-centre deviation into roll.
- `tailTrim = −4.6 − 2.2·throttle²`, tail incidence `= tailTrim − 18·elevator − side·30·rudder`
  (FirmwareVehicle.hs) — pitch trim and ruddervator authority.

## Symptom → knob

| Symptom | Turn |
| --- | --- |
| Can't climb at the top of the envelope / generally weak | `flapJetCoeff` ↑ |
| Too floaty / no terminal velocity / rocket acceleration | body `CdA` ↑ (not lift) |
| Sinks in glide too fast | `attachedCd` ↓ (or body `CdA` ↓) |
| "Air feels like water" | body `CdA` ↓ (both files, in lockstep) |
| Stalls hard, drops a wing, unrecoverable | `separatedCd` ↓ |
| Brief stall lingers, can't re-accelerate after a pull-up | reattachment `tau` ↓ (Vehicle.hs `else` branch) |
| Wing flops like a flag / no thrust at weight | `siBendScale` ↑ (stiffen) |
| Wing feels plank-like / too rigid | `siBendScale` ↓ (soften) |
| Servo can't track the stroke (weak at high cadence) | `servoNoLoadSpeedDegPerSec` ↑ or lower cadence |
| Servo overpowered by hinge load (weak under load) | `servoStallTorqueNm` ↑ |
| Rolls too slow / no roll authority | roll via lift (`circulationLiftEfficiency`), **not** `flapDampingCd` |

## Tuning workflow

1. Edit the constant in the Haskell file.
2. Rebuild the math DLL (`Tools/build-math-static.sh` / the math build) and copy
   `born2flap_math.dll` to `Unreal/Born2Flap/Binaries/ThirdParty/`.
3. **Close Unreal** before copying — the editor locks the loaded DLL.
4. Relaunch and fly.

Haskell-only edits need no Unreal C++ rebuild (no ABI change). Only when the FFI
signature or a C++ default changes must the editor DLL also be rebuilt.

The regression suites live in `MathCore/test/` (math properties) and
`Native/tests/aerodynamic_flight.py` (energy-budget trajectories). Run them after
any thrust/drag change: the unpowered case must never exceed its initial energy,
and powered cases must gain energy and retain late wing travel.
