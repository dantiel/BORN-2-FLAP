# TODO — high priority (base)

Renderer foundation. These gate the core goal: lean on UE5.8 shader paths as much as
possible, keep a sensible software fallback where it makes sense. `Base` = the hardware
path must land before the settings-driven quality tiers can actually deliver their promise.

## [HIGH · base] Niagara particle migration — DONE
- Code strand: `Born2FlapGraphics::SupportsNiagara()` gate (HIGH/EPIC) + `Niagara` module
  dep in `Born2Flap.Build.cs`. `ABorn2FlapWeather::PrecipitationFX` and
  `ABorn2FlapInsectSwarm::SwarmFX` replace the rain/snow + insect-swarm ISM layers, with
  the ISM path kept as the LOW/MEDIUM software fallback (auto-fallback when the system
  asset is absent or emitter-less). Hero insects stay real meshes — the detailed,
  catchable individuals, not background particles.
- Assets: `NS_Rain`/`NS_Snow` (`/Game/Weather`) + `NS_InsectSwarm` (`/Game/Nature/Insects`)
  are authored programmatically by `UBorn2FlapNiagaraBuilderCommandlet` (module
  `Born2FlapEditor`) — not in-editor. Run:
  `UnrealEditor-Cmd.exe Born2Flap.uproject -run=Born2FlapNiagaraBuilder -nullrhi`.
- Follow-up (optional): Niagara wind-drift — the systems read a `Wind` user vector param
  (set by the weather/swarm actors) but the emitters don't yet consume it; ISM path still
  carries full wind drift.
- `Flight/Born2FlapFlightPawn.cpp` WindMotes ISM not yet migrated.

## [HIGH · base] Nanite asset regeneration — DONE
- `SM_Rock0..3` are Nanite-enabled: `create_nature_assets.py` uses the correct UE5.8
  Python class `unreal.MeshNaniteSettings` (`enabled=True`) — NOT the nonexistent
  `StaticMeshNaniteSettings`, which silently failed before.
- Assets regenerated into project Content via `regen-nature.ps1` (now `-ExecutePythonScript`,
  matching regen-shiomori.ps1 — the old `-run=pythonscript` pattern redirected `/Game/Nature`
  to the AppData detour).
- `SM_ContinuousBeach` stays non-Nanite on purpose (complex-as-simple collision on
  critical terrain; Nanite there would be risky and marginal).

## Backlog (deferred by design)
- HW-Raytracing + MegaLights are already wired through the quality tiers (EPIC) but
  remain unverified at runtime — needs an editor-closed cook/launch to confirm the
  HW-Lumen path, since the final link is blocked while the editor is running.