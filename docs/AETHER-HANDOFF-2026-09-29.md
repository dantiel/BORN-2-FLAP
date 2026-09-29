# BORN 2 FLAP — handoff to Aether agent

Date: 2026-09-29. Workspace: `V:\BORN 2 FLAP`. Shell: Windows PowerShell.

## Immediate objective and user direction

The user likes the layout and size of the newly implemented **Shiomori Bay**, but
explicitly wants **naturalistic / photorealistic graphics, comparable in intent to
TRYP FPV**, rather than the current stylized primitive scenery. Keep the existing
level dimensions. Improve models, textures, terrain, water, vegetation, lighting
and effects. The last conversation outlined this approach; **the photorealistic
art pass has NOT been implemented yet**. This handoff is requested because the
current agent's usage window is ending.

Suggested first implementation: finish one representative stretch of Shiomori
(beach + surf + stairs + shelter + shrubs + industrial frontage) at convincing
close-up and flight-camera quality, then apply the same asset/material standard
throughout the existing map. This is a recommendation, not a user-imposed limit.
Do not expand the world to compensate for poor detail. Preserve sparse beach
clutter and subdued industry: realism comes from construction and material detail,
not adding indiscriminate objects.

## Repository safety / current status

- Verified now: branch `main`, local tracking display `main...origin/main`, HEAD
  `e4582d7` (merge of upstream main). No fresh remote fetch was performed for this
  briefing; tracking display does not establish that GitHub has no newer commits.
- **All work described below is local and uncommitted**, including important
  compile repairs. Many new source, documentation and binary asset files are
  untracked. Preserve the entire worktree before any merge/reset/cleanup.
- Use `git -c safe.directory='V:/BORN 2 FLAP' ...` on this checkout.
- No AGENTS.md was found during this work. Recheck if project instructions change.
- No new release was uploaded for these changes. Earlier Windows prerelease:
  https://github.com/dantiel/BORN-2-FLAP/releases/tag/alpha-20260927.1
  That release predates the wing and Shiomori changes. Its earlier upload was
  reported complete; release state was not queried again during this handoff.
- Git LFS already covers `.uasset`, `.umap`, and other large binaries. Do not
  accidentally omit the new `Content/Shiomori` directory or wing material.
- User prefers direct implementation and actual verification, without repeated
  confirmation requests for already authorized work. No permission to buy paid
  assets has been supplied. No need to send messages to external services.

## Completed: compile repairs after upstream merge

Preserve these repairs in the dirty worktree:
- `Flight/Born2FlapRavenMesh.h` and `Born2FlapRavenCrow.cpp`: shared Build outputs
  use `TObjectPtr<USceneComponent>&`, matching pawn and replay-spirit callers.
- `Racing/Born2FlapRacing.cpp`: `.Pop(false)` -> `.Pop(EAllowShrinking::No)`.
- Restored missing `ABorn2FlapFlightPawn::SelectBirdModel` implementation.
- `SetBlindFlight` reapplies model selection after recursively changing visibility,
  preventing multiple models appearing together when leaving blind flight.

Paths below are relative to `Unreal/Born2Flap/Source/Born2Flap` unless specified.

## Completed: aeroelastic visuals and three wing designs

User explicitly wanted the old silhouettes preserved. F8 now selects:
1. Ravencrow (index 0): folded dark body, broad inner wing, seven slotted parallel
   pinions. Do NOT replace these slots with a continuous membrane again.
2. Prototype (index 1): original rounded body and overlapping elliptical feathers,
   with teal/ivory base colours. Do NOT remove the elliptical feathers.
3. Manta (index 2): continuous tapered membrane wings, currently on a raven-like
   body. It is the third selectable model, not a replacement for either original.

All six live wing meshes deform independently using the actual Haskell solver's
16 stations per wing: body-Z bending displacement, geometric + elastic twist,
and membrane camber. No aerodynamic force retuning was done for model selection.
This remains a reduced beam/section model, NOT a cloth/FEM simulation.

Main files:
- `Flight/Born2FlapWingMesh.h/.cpp`: shared procedural surface renderer. Different
  subdivided rest shapes preserve slots and elliptical feather overlaps. Rest
  vertices deform; normals/tangents update; UVs stay fixed on the surface.
- `Flight/Born2FlapRavenCrow.cpp`: builds raven and Manta roots with distinct names.
- `Flight/Born2FlapFlightPawn.h/.cpp`: creates prototype wings, updates all three
  pairs after shoulder rotation, model roots and painting entry points.
- `Flight/Born2FlapFlightSettings.cpp`: three F8 choices and existing persistence.
- `Flight/Born2FlapDesktopTest.cpp`: selects all three, verifies all six meshes.

Solver transport:
- `Native/include/born2flap_math.h`: optional ABI-v4 extension
  `b2f_math_get_wing_shape(context, capacity, left, right)`;
  `B2F_WING_STATIONS=16`, five doubles per `B2F_WingSection`:
  span_fraction, chord_m, bend_m, twist_rad, camber.
- `MathCore/src/Born2Flap/Math/FFI.hs` reads existing FirmwareVehicle strip states;
  `MathCore/cbits/bridge.c` exports the C wrapper.
- Existing ABI output layouts/version remain unchanged. Telemetry is read-only.
- `Native/src/born2flap_math_bridge.cpp` fallback returns 0 (no structural solver).
- `Math/Born2FlapMathBridge.h/.cpp` loads the optional accessor; old DLLs keep
  undeformed rest wings and warn when the export is absent.
- **Bend is BODY Z, not flap-local Z**. Preserve the body-to-wing conversion.

Painting:
- Pawn `WingPaint` property / Blueprint `SetWingPaint`; individual component
  `SetPaintTexture`. Null restores base colours.
- Material: `/Game/Birds/M_WingMembrane` (new binary asset).
- Reproducible generator: `Tools/create_wing_membrane_material.py`.
- UV coordinates are rest-space U=(30-X)/95, V=abs(Y)/105, centimetres. Artwork
  continues across separate feathers while their gaps remain open.
- Two-sided surfaces currently share the same artwork on both sides.
- Replay spirits still store only flap angles, so they have slotted raven rest
  geometry with recorded flapping, not reconstructed aeroelastic deformation.
- Details: `docs/wing-membrane-rendering.md`.

## Completed: third level, Shiomori Bay

User request: fictional coastal-city name reminiscent of a Pokemon town, with a
quiet industrial seaside. Long sparse beach, bushes
at one end, raised pavement with stairs and some uplift, slightly futuristic
bus-shelter-like picnic tables, boring industry and bushes behind, volleyball net,
small volcanic island to the left creating a sheltered bay.

Implementation:
- Saved map: `/Game/Shiomori/Maps/SHIOMORI` (~1.5 MB).
- Content: `Unreal/Born2Flap/Content/Shiomori/Maps` and `Materials`.
- Generator: `Unreal/Born2Flap/Tools/create_shiomori_map.py`.
- F4 cycles Ravenstonefield -> Shiomori -> Training -> Ravenstonefield.
- `Tools/play.ps1 -Level Shiomori` launches directly.
- Approximately 794 authored scenery actors, with saved camera viewpoints.
- 900 m beach along world X; sea is +Y, promenade and industry are -Y.
- Beach X +/-45000 cm, Y -1200..10000, top Z=0.
- Six continuous 25 cm stair risers from Y=-1200 to -1800; promenade top Z=150.
- Nine picnic shelters, industrial warehouses/service road, backshore scrub,
  bushes concentrated at the eastern beach end, regulation-width 9 m net and a
  few stray objects. Shelter roofs have solar-coloured panels and orange fascia.
- Island centre (-37000,26000), horizontal radii (8500,13500), base sphere centre
  Z=-600 with vertical radius 2200. Its water channel is deliberately separated
  from the beach; do not move it back onto the shore. Uses imported irregular
  rock meshes `/Game/Nature/SM_Rock0..3`, with dark basalt material.
- Sea surface Z=-50, shallow-water margin and sparse foam marks.
- Ocean and surf are currently simple static surfaces, **not finished realistic
  water**. Beach is still a large flat slab; island base still a smooth ellipsoid.
  Buildings and bushes remain primitive geometry. These are the main art gaps.

Gameplay integration:
- `Game/Born2FlapGameMode.h/.cpp`: coast detection, ground-height/water queries,
  level cycle, sea breeze, test capture/validation.
- Onshore wind approx 3.2 m/s toward -Y; updraft peaks around .85 m/s at the
  1.5 m stair wall, decaying with altitude and distance. Goes through air-relative
  flight dynamics, not an arbitrary extra force.
- `Flight/Born2FlapFlightPawn.cpp` uses level-aware wind and water reset queries.
- `Game/Born2FlapHUD.cpp` uses Shiomori title/location text; hides HUD in coast test.
- `docs/shiomori-bay.md` describes usage and layout.

Important generator lessons:
- Planar mesh bounds have near-zero (not exactly zero) Z extent. The generator
  now uses `bounds.z > .001` before dividing; otherwise Z scale is 1. Earlier
  division produced ~6e13 scale and made the ocean disappear. Keep the scale
  assertion (<10000) to catch this regression.
- Imported rock pivots are offset. The generator recentres world bounds before
  placement; without it rocks float. Rock bases should remain embedded.
- Regenerating the map recreates it from scratch. Move hand-authored changes into
  the generator or preserve a separate authored map before regeneration.
- Ground/water queries must track terrain edits. Current analytic boundaries
  include the raised industrial hinterland and the island channel.

## Verified results and evidence

Verified in the working tree, not a newly packaged release:
- Latest editor build: `.setup/coast-editor-final.log` -> Result: Succeeded.
- Latest Shipping build: `.setup/wing-shipping-build.log` -> Result: Succeeded.
- `Saved/Logs/desktop-input-test.log` -> DesktopInputTest PASS and WingVisualTest
  PASS for all three models / six meshes (bending, finite normals, stable UVs).
- `Saved/Logs/shiomori-test.log` -> ShiomoriTest PASS, latest 2026-09-28 22:18 UTC:
  sand/stair/promenade collisions, bay/island/channel water mask, stair lift,
  nine shelters, two net posts, sixty volcanic outcrops.
- `Native/tests/wing_shape.py` previously passed actual Haskell DLL tests:
  capacity/null handling, buffer sentinel, finite asymmetric deformation,
  changing membrane camber, and unchanged physics when reading telemetry.
  Observed peak bending in its scenario: .0567 m.
- Existing `Native/tests/abi_smoke.py` passed after ABI extension.
- `git diff --check` passed; line-ending normalization warnings are expected.
- A full fresh cook/package of these changes has NOT been done.

Screenshots actually inspected:
`Unreal/Born2Flap/Saved/Screenshots/WindowsEditor/`
- `RAVENCROW_MODEL.png`: restored slotted raven.
- `PROTOTYPE_MEMBRANE.png`: restored elliptical feather silhouette.
- `MANTA_MEMBRANE.png`: third continuous membrane model.
- `SHIOMORI_0.png`: latest bay overview, separated island and visible ocean.
- `SHIOMORI_1.png`: promenade, shelter/table, stairs and industry.
- `SHIOMORI_2.png`: basalt island detail.

These pictures establish the current stylized baseline, not photorealistic quality.
Preview capture was delayed slightly for Manta to avoid capturing the previous
model on the render thread. Keep this timing if modifying automated previews.

## Commands / environment

Engine: `V:\UE_5.8` (installed engine reports 5.8.2).
GHC: `V:\Born2FlapTools\ghcup\bin`; CABAL_DIR: `V:\Born2FlapTools\cabal`.
Python that works: `V:\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe`.
Default WindowsApps Python may not work.

From repo root, normal combined build:
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Unreal/Born2Flap/Tools/build.ps1
```

Unreal-only editor build (useful when no Haskell changes):
```powershell
& 'V:\UE_5.8\Engine\Build\BatchFiles\Build.bat' Born2FlapEditor Win64 Development -Project='V:\BORN 2 FLAP\Unreal\Born2Flap\Born2Flap.uproject' -WaitMutex
```

Shipping compile:
```powershell
& 'V:\UE_5.8\Engine\Build\BatchFiles\Build.bat' Born2Flap Win64 Shipping -Project='V:\BORN 2 FLAP\Unreal\Born2Flap\Born2Flap.uproject' -WaitMutex -NoHotReloadFromIDE
```

Map generation:
```powershell
& 'V:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'V:\BORN 2 FLAP\Unreal\Born2Flap\Born2Flap.uproject' -run=pythonscript '-script=V:\BORN 2 FLAP\Unreal\Born2Flap\Tools\create_shiomori_map.py' -unattended -nosplash -nullrhi -nosound '-abslog=V:\BORN 2 FLAP\.setup\shiomori-generation.log'
```
Check `SHIOMORI_MAP_READY` and Python errors in the log; commandlet exit status
alone is not enough to establish that a Python script completed.

Tests:
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Unreal/Born2Flap/Tools/test-desktop-input.ps1 -Capture -BirdPreview
powershell -NoProfile -ExecutionPolicy Bypass -File Unreal/Born2Flap/Tools/test-shiomori.ps1
& 'V:\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe' Native/tests/wing_shape.py 'V:\BORN 2 FLAP\Unreal\Born2Flap\Binaries\ThirdParty\born2flap_math.dll'
```
Add `-WingPaintPreview` to the desktop test for temporary checker paint.
The test scripts launch hidden/offscreen windows and enforce timeouts.

Build/cache caveats:
- Restricted execution often cannot write Cabal/Unreal external caches or start
  Unreal's Zen service. Use appropriate sandbox escalation when needed.
- Combined build can fail updating `Binaries/ThirdParty/born2flap_math.dll` if an
  editor/game process holds it. Do not kill the user's session indiscriminately.
  For C++-only changes, direct Unreal build avoids replacing the unchanged DLL.
- A failed `.setup/merged-build-editor.log` near the end is from a DLL lock, NOT
  the final editor result; see the successful `coast-editor-final.log` instead.
- `.setup/*.py` transformation scripts are throwaway history, not idempotent
  project tooling. Do not rerun them as a setup procedure.

## Photorealistic upgrade: findings and next work

Current rendering config already enables Lumen GI/reflections, Virtual Shadow
Maps, distance fields and DX12/SM6. The primary deficiency is authored content,
not an absent "photorealism" switch. Do not merely increase bloom, sharpness or
texture resolution on the current primitives.

Existing asset pipeline:
- `Tools/fetch_nature_assets.py`: Poly Haven CC0 source imports, presently mostly
  1K texture/model packages. `create_nature_assets.py` imports them into Unreal.
- `Tools/fetch_ravenstonefield.py`: additional 2K surface textures and tree models.
- Sources/manifests live under ignored `Saved/NatureSource` and
  `Saved/RavenstonefieldSource`; imported Content assets are the portable outputs.
- Useful sources: Poly Haven (CC0) and suitably licensed Megascans/Fab assets.
  Do not assume every Fab listing is free or that all accounts own it.

Priorities for Shiomori:
1. Sculpt beach slope, irregular shoreline, dunes and weathered island geology.
2. Layer scanned dry/wet sand, rock, concrete and weathered wood with correct
   real-world scale, roughness, normal/height detail and broad colour variation.
3. Replace sphere bushes with real coastal vegetation; believable distribution
   and wind, while preserving the open beach and bushes at one end.
4. Build detailed modular stairs/shelters/warehouses: bevels, joints, bolts,
   gutters, drains, recessed windows, salt stains and wear. Industry stays mundane.
5. Implement convincing water and shore interaction: waves, reflections, depth,
   foam, wet-sand transitions; evaluate Unreal Water system as a foundation.
6. Establish coherent daylight/exposure/sky and atmospheric distance. Inspect
   from actual flight cameras, at ground level and during fast motion.
7. Profile frame times and memory on a stated target GPU before committing to
   expensive effects. Nanite is useful for suitable detailed static geometry;
   foliage still needs specific testing and suitable LOD/instancing choices.

Treat selected scanned assets as part of a coherent coastal art direction; random
high-resolution asset mixing will not by itself achieve naturalism. Higher-quality
source art, placement and material transitions are required across the whole scene.
No paid purchases, additional asset downloads or naturalism implementation were
performed after the user asked this last question; only inspection and research.

Sources consulted for the recommendation:
- https://polyhaven.com/license
- https://github.com/Poly-Haven/Public-API/blob/master/README.md
  (asset CC0 terms and live API usage requirements are distinct; retain source manifest)
- https://quixel.com/megascans/home?assetId=vletdha (Megascans now on Fab)
- https://dev.epicgames.com/documentation/unreal-engine/water-system-in-unreal-engine
- https://dev.epicgames.com/documentation/unreal-engine/working-with-naniteenabled-content
- https://store.steampowered.com/app/1881200/ (user's TRYP FPV visual reference)

## Other preserved project direction

Servo mount angles are planned future work: the user's real birds use outward
mounting angles up to about 30 degrees, with axes meeting toward the rear/tail.
The user previously said to keep parallel mounting for now and document angled
mounts for future development. Do not silently change mount physics while doing
an environment art pass.