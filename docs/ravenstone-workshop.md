# Ravenstone Ornithopter Works

A fictional farm workshop that has gradually become an established flight-development centre. It borrows the broad timber-barn character of early aviation workshops; it is not a reconstruction of Lilienthal's property.

## Site and construction

- Replaces the first old barn at world XY (-16000, -15500), with an 18-degree rotation. The other two farm barns remain.
- Main building: 32 x 18 m footprint, two working levels, 8.4 m eaves and 14 m ridge. Open assembly nave, a gallery at 4.16 m, stairs, bridge, workbenches and sliding doors.
- Smaller 21 x 16 m hangar on the adjacent lower pad, with an open 16 m front, clerestory and handling apron. Barn floor is Z 416.5 cm; hangar floor is Z 387.5 cm. The terrain is only gently sloped here.
- Three bespoke static experimental airframes: a suspended full-size study, a larger prototype on maintenance stands in the hangar and a tabletop model. Timber spars, cambered membranes, ribs, tail surfaces and bracing. These are scenery, not new flyable vehicles.
- Native instanced construction reuses the project's timber, plaster, stone, metal and roof materials. No third-party assets were downloaded.
- Terrain-sampled foundations and connections; foliage excluded from the enlarged working grounds. Interior lights and bounded exposure adaptation are installed. Exposure bounds use query-only collision with every response ignored, allowing Unreal to detect the camera inside without obstructing flight.

## Flight meadow

The main field has 63,270 deterministic, jittered grass clumps using the existing grass meshes and a new green masked foliage material. Productive sward varies from 24 to 60 cm. A 155 x 11 m launch strip is cut to 6-10 cm. The radio, signs, river margin and road have clearance; grass has no collision. Instancing and distance culling limit rendering cost. This is managed grassland, without an ornamental wildflower treatment.

## Maintenance and verification

The generator is `Source/Born2Flap/World/Born2FlapRavenWorkshop.cpp`; meadow placement and checks are in `Born2FlapValley.cpp`. World generator version is 5.

After rebuilding Born2FlapEditor, run `Tools/rebuild_raven_workshop.py` through Unreal Editor Python. It loads the existing map and rebuilds only the valley actor's generated components, preserving independent actors. It also creates/validates the grass material. The saved map contains the generated scenery for editor use.

`Tools/test-ravenstonefield.ps1` checks the existing terrain/water/radio contract, upstairs floor collision and a 6 m-wide aircraft sweep through the barn door. Validation runs after GameMode radio initialization. `-Workshop` captures six rendered views, including the two interiors and the flight meadow at eye height. Evidence is retained under `.setup/raven-workshop-20261010`.

The editor build and collision/world checks pass. Six rendered views completed after switching the grass to conventional instancing; an earlier Nanite foliage run caused a GPU memory fault, retained in the evidence folder. Missing Nanite usage flags on existing Ravenstonefield foliage materials were also repaired. This does not establish long-run GPU stability or packaged Shipping performance.

Follow-up lighting verification: after the user closed the editor, the exposure-bounds fix was built and saved. Rendered barn interior and gallery views now show the aircraft, beams and workspaces clearly; runtime diagnostics confirm the camera is inside the barn exposure region. Exterior views remain outside it. The hangar has the same corrected bounds, but its dedicated capture remains an exterior doorway view. One further GPU fault occurred during startup; retrying the unchanged build completed all six captures. GPU startup stability remains a separate unresolved limitation.
