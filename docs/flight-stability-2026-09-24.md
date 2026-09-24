# Flight stability investigation — 24 September 2026

This records the initial stability repair and altitude-assisted trainer.
The subsequent [wing-powered flight revision](wing-powered-flight.md) removes
that trainer's speed/altitude compensation. Controls and results below describe
the earlier version, not the current game.

## Reproduction and root cause

The user's aircraft fell, bounced and accelerated away without W. A native
Python harness reproduced the failure with the **actual Haskell shared library**,
independently of Unreal rendering or keyboard input. At zero throttle and a
vertical velocity of -5 m/s, the previous DLL returned about **-49.87 N vertical
force**. Its force did +249.37 W of work on the falling body: it added energy
instead of resisting the fall. Vehicle weight at 0.45 kg is only 4.41 N.

The wing code mixed air-relative and body-relative velocity conventions. Normal
velocity had the wrong sign, and the finite-difference added-mass term reinforced
acceleration/contact impulses. An unbounded backdriven servo could then integrate
extreme angular rates and positions into the next aerodynamic evaluation. The
small old floor and absent sky made leaving the scene appear as a black screen.
Per-step pitch/roll teleports and state resets that did not reset the firmware
were additional problems at the Unreal boundary.

Stale force telemetry after a rejected step was misleading, but **the old code
did not repeatedly apply those stale loads**. That earlier diagnosis was wrong.

## Corrections

- Use full section velocity, including body rotation and flap motion; signed
  chord/normal velocity and rotating wing axes. Drag opposes motion; planar lift
  is perpendicular to it, including diagonal and reverse flow.
- Remove the explicit acceleration-derived added-mass force until a coupled
  implicit inertia solution is available. This is a deliberate model limitation.
- Bound servo backdrive speed and linkage travel (+/-80 degrees), give the
  actuator a 25 ms response time, and cap assumed battery stall current at 10 A.
  These parameters remain provisional rather than experimentally calibrated.
- Reject invalid/excessive native output before applying any force. A rejected
  step disarms once; R recreates the entire firmware context and body state.
- Submit one averaged force per game frame for Chaos to substep. Replace repeated
  orientation teleports with pitch/roll constraints in the training mode.
- Use an unscaled collision root, correct centimetre geometry, zero-restitution
  contact, a large solid floor, daylight sky, and a following camera.

## Playable training mode

The HUD explicitly says **ASSIST ON**. W starts and increases target altitude;
release holds altitude and 8 m/s cruise. A/D turns, S descends and lands, R resets.
The altitude target is limited to 35 m. The controller gently turns back at the
300 m training boundary, before reaching the floor edge. Six golden gates give a
simple flying objective. F1 draws measured aerodynamics in cyan and assistance in
green. Flapping geometry follows the actual firmware servo angles.

This is an assisted flight game, **not a validated unassisted ornithopter trim
solution**. While airborne, the controller compensates measured aero forces and
commands acceleration, gravity support and damping compensation. Physical
pitch/roll are constrained, yaw is controlled, and visible banking is animated.
The aerodynamic core runs throughout, but the trainer's stability is supplied by
that explicit assistance. Free pitch/roll dynamics, propulsion calibration and
measured real-bird performance still need research.

## Evidence

All measurements below use the rebuilt Haskell DLL, not the C++ fallback.

| Check | Result |
|---|---|
| `cabal test all` | Passed |
| Native ABI rollback, multiple contexts, repeated runtime sessions | Passed |
| 27 unpowered initial flow directions, including diagonal/reverse | Force dot velocity <= 0 |
| Corrected zero-throttle -5 m/s fall | +1.775 N vertical force; -8.873 W aerodynamic work |
| Coupled gravity/contact harness, 3 x 60 s at 30/60/144 Hz body feedback | Settled without runaway; peak speed <= 4.491 m/s |
| Unreal/Chaos, 75 s at 30 FPS | Start, hold, turn, land, reset, restart: PASS |
| Unreal/Chaos, 75 s at 60 FPS | Same sequence: PASS |
| Unreal/Chaos, 75 s at 144 FPS | Same sequence: PASS |
| Unreal/Chaos, 300 s at 60 FPS | Sustained flight and automatic boundary return: PASS |
| Steady assisted height error in these engine runs | <= 0.0035 m |
| Maximum speed / altitude in engine runs | <= 8.545 m/s / 12.456 m |
| Safety teleports / native step failures in these engine runs | 0 / 0 |
| Native C++ Release build / CTest | Passed, 1/1 |

The numeric harness is a level-body approximation with inelastic floor contact;
it is not a substitute for the separate actual Chaos tests. Engine tests use
scripted inputs through the same flight controller and are simulated time, not
wall-clock duration. They do not claim exhaustive validation of every hardware,
frame-stall, collision or pilot-input combination.

Reproduce with `Native/tests/flight_regression.py <DLL-or-build-directory>` and
`Unreal/Born2Flap/Tools/test-flight.ps1`. The native regression is included in CI.
See [Windows development](windows-development.md) for exact commands. Engine
logs under `Saved/Logs` contain `FlightTest PASS`, `FlightSoakTest PASS`,
`FlightTelemetry`, and `FlightBoundaryReturn` records.

## Rendered and normal-input validation

The final rendered 75-second run also passed. A real screenshot was inspected:
[training flight](images/training-flight.png). The first daylight setup exposed
an overexposure problem, which was corrected with fixed camera daylight exposure.
The final render showed the bird, animated wings, sky, ground, gates and HUD;
there was no unbuilt-lighting warning in the screenshot or final render log.
Both `Born2FlapEditor` and `Born2Flap` Win64 Development targets built successfully.
Ruby brain tests also passed (4 tests, 6 assertions).

A separate normal game launch, **without the scripted flight-test flag**, received
targeted Windows W/D/S/R key messages through its own window. After a 0.4-second W
press and release it held 7.60 m altitude and 8.00 m/s, passing the first three
gates. D turned the heading to 36 degrees. Holding S landed and stopped the bird.
R recreated the firmware context and returned it to the start; it settled at
0.01 m reported clearance and zero speed. This verifies the window/input/controller
path as well as the deterministic simulation scenario.

Tested Haskell DLL SHA-256:
`D3FF500A22365F693F78A879C91702C040305E3770682BB2CEDB76024F015443`.
Generated binaries and full local logs remain ignored; source, material assets,
reproduction scripts and this report are versioned.
