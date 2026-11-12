# TODO — next steps

## [HIGH · physics] Flight handling acceptance

The chronological Chaos load handoff and the fixed-tail correction are installed
(ABI 21). What remains:

- **Powered takeoff/relaunch** — the 45 s handling, 110 s flight and 180 s endurance
  tests fail at ground level on both the current and the previous DLL: no sustained
  powered height or relaunch. These are pre-existing criteria failures, not regressions
  of the two corrections; the launch physics or the acceptance criteria need a rework.
- **User acceptance** of the new tail handling (banked-tail yaw no longer reverses).
- **Trim at extremes** — large anhedral can develop rapid adverse roll and unconstrained
  glides can steepen into spirals. The correction removed an erroneous stabilising
  force; decide whether a legitimate trim/damping follow-up is warranted.

## [HIGH · physics] Yawed-hinge geometry

Aerodynamic section kinematics use the yawed axis, but `hingeTorque` and
`SpatialDynamics` still contain parallel-axis approximations. Only Kestrel's parallel
shoulder axis is certified; general hinge layouts need the same treatment.

## [MED · renderer] Fidelity follow-ups

- HW-Raytracing + MegaLights are wired through the quality tiers (EPIC) but remain
  unverified at runtime — needs an editor-closed cook/launch to confirm the HW-Lumen path.
- Niagara wind-drift: weather/swarm systems read a `Wind` user vector param but the
  emitters don't yet consume it; the ISM path still carries full wind drift.
- `Flight/Born2FlapFlightPawn.cpp` WindMotes ISM not yet migrated to Niagara.

## Done (renderer foundation)

- Niagara particle migration — rain/snow/insect-swarm via
  `UBorn2FlapNiagaraBuilderCommandlet` (LOW/MEDIUM keeps the ISM fallback).
- Nanite regeneration — `SM_Rock0..3` via corrected `MeshNaniteSettings`
  (`create_nature_assets.py` + `regen-nature.ps1`).
