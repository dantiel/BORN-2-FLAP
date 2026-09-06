# Physics audit and hermetic Haskell state

## Verified scope of this revision

`Simulation s e a` is a pure state/error monad. It has no IO, clock, random
generator or global state. Its constructor is hidden; an aborted transaction
returns only an error. `stepVehicle` preserves its public interface and returns
the original vehicle state on rejected input or nonfinite loads.

Every scalar input is checked for NaN/infinity before computation. Phase is
integrated from instantaneous commanded frequency and wrapped to [-pi, pi].
This removes the previous frequency-times-total-time discontinuity. It does
not yet provide servo dynamics: amplitude changes can still jump.

Tail control forces now scale with squared local airspeed, including angular
velocity at the tail arm. They vanish at rest and are oriented to produce the
commanded right-hand-rule moment. Coefficients remain experimental placeholders;
this is not a validated tail polar or complete tail damping model.

Regression tests cover transactional failure, nonfinite input, phase changes,
bilateral force/moment symmetry, bounded separation and zero-flow tail behavior.
Compilation and these tests do not establish aerodynamic accuracy.

## Remaining correctness work before flight validation

- Establish a single signed flow convention and test lift perpendicularity,
  dissipative drag, reverse flow, and local wing-frame projection.
- Replace ideal amplitude changes with continuous actuator state; zero throttle
  currently still commands flapping.
- Check added-mass sign and initialization; finite differences currently produce
  startup transients and use an arbitrary force clamp.
- Correct angle wrapping and mirrored dynamic-stall delay; calibrate or disable
  speculative LEV and rotational lift contributions.
- Transport separation before evaluating loads and make diffusion depend on
  timestep and strip spacing, with numerical convergence tests.
- Implement actual local spanwise flow and full omega-cross-position velocity.
- Move geometry, density and model coefficients into validated configuration.
- Test native RTS initialization, multiple vehicle lifetimes, ABI offsets and
  library unloading in an actual C host. Linking alone does not verify these.
- Couple aero updates to Chaos physics substeps; the current render-tick
  accumulator repeats the body state during catch-up and is not a coupled
  240 Hz rigid-body integrator.
- Measure latency and allocation over long runs. Bounded phase is not proof of
  indefinitely stable, real-time operation.

Experimental force measurements and profile polars are required to establish
accuracy. Existing LEV, crossflow and stall coefficients must not be presented
as calibrated results.
