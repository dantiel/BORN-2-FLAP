# TODO — high priority (base)

Renderer foundation. These gate the core goal: lean on UE5.8 shader paths as much as
possible, keep a sensible software fallback where it makes sense. `Base` = the hardware
path must land before the settings-driven quality tiers can actually deliver their promise.

## [HIGH · base] Niagara particle migration
- Replace the ISM particle effects — rain, snow, insect swarm — with GPU-Niagara systems.
- Keep the existing ISM path as the software fallback (quality tiers LOW/MEDIUM).
- Asset authoring: build the Niagara systems procedurally in `Unreal/Born2Flap/Tools/`,
  regenerate assets via the import commandlet (editor closed).
- Sources: `Born2FlapFlightPawn.cpp` (InsectSwarm, WindLeaves ISM),
  `Born2FlapWorld/Weather` (rain/snow spawn).

## [HIGH · base] Nanite asset regeneration
- Code is in: `SM_Rock0..3` are flagged Nanite-enabled in `create_nature_assets.py`.
- Assets must be re-imported (commandlet) for the rocks to actually receive Nanite.
- Editor must be closed during import — it holds the module DLL / asset lock.
- `SM_ContinuousBeach` stays non-Nanite on purpose (complex-as-simple collision on
  critical terrain; Nanite there would be risky and marginal).

## Backlog (deferred by design)
- HW-Raytracing + MegaLights are already wired through the quality tiers (EPIC) but
  remain unverified at runtime — needs an editor-closed cook/launch to confirm the
  HW-Lumen path, since the final link is blocked while the editor is running.
