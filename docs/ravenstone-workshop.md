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

The expanded field has 130,692 deterministic, jittered grass clumps, ranging from 36 to 90 cm outside the 6-10 cm mown launch strip. The user subsequently requested meadow flowers: 32,290 white, yellow and purple flower heads now form uneven colonies, with grass-dominated areas between them. This implements the mixed-grass-and-flowers option rather than a cereal crop. A textured ground layer maintains coverage at a distance and fades at the field edges. Radio, signs, river margin, road and footpath retain clearance; vegetation has no collision.

The tree exclusion now covers the field plus a buffer (X -20000 to 38500, Y -14000 to the river's far margin). A roughly two-metre pedestrian path winds from the southern woodland through the meadow to the near riverbank. It has a matching vegetation clearance and an opening in the old fence.

## Roof and Autobahn refinement

The main barn now has a half-hipped roof: clipped upper gables, a shorter ridge, sloping end planes, hip caps and a timber underside. The assembly hall and gallery remain intact.

The bridge uses neutral gray concrete materials. Its old staircase-like approach blocks have been replaced by connected road polygons following a cubic grade, with terrain-connected embankments. Each approach now reaches 390 m from the river centre, with a further 10 m transition into the ground. Road markings and guardrails follow the slope. Collision samples along both approaches pass, with 7.1 cm minimum centreline clearance above the underlying terrain.

## Maintenance and verification

The workshop generator is `Source/Born2Flap/World/Born2FlapRavenWorkshop.cpp`; field details are in `Born2FlapRavenMeadow.cpp`, bridge geometry in `Born2FlapRavenLandmarks.cpp`, and vegetation placement/checks in `Born2FlapValley.cpp`. `RavenSurface.h` builds the authored polygon surfaces. World generator version is 6.

After rebuilding Born2FlapEditor, run `Tools/rebuild_raven_workshop.py` through Unreal Editor Python. It loads the existing map and rebuilds only the valley actor's generated components, preserving independent actors. It also creates/validates the grass material. The saved map contains the generated scenery for editor use.

`Tools/test-ravenstonefield.ps1` checks the existing terrain/water/radio contract, upstairs floor collision, a 6 m-wide aircraft sweep through the barn door and the road approaches. Validation runs after GameMode radio initialization. `-Workshop` captures nine rendered views including the interiors, meadow, footpath, field overview and bridge. Refinement evidence is retained under `.setup/raven-refinement-20261010`.

The editor build and collision/world checks pass. Six rendered views completed after switching the grass to conventional instancing; an earlier Nanite foliage run caused a GPU memory fault, retained in the evidence folder. Missing Nanite usage flags on existing Ravenstonefield foliage materials were also repaired. This does not establish long-run GPU stability or packaged Shipping performance.

Follow-up lighting verification: the exposure-bounds fix was built and saved. Barn, gallery and hangar interior views show the aircraft and workspaces clearly; runtime diagnostics confirm the interior camera regions. Exterior views remain outside them. Earlier runs encountered intermittent GPU startup faults; the latest refinement run completed all nine views successfully. Long-run GPU stability remains outside these checks.

Final refinement verification: editor build succeeded, world/workshop/road checks passed, all nine captures completed, and roof, meadow, path, overview and bridge images were visually inspected. Evidence is in `.setup/raven-refinement-20261010/verified`. Independent actors and unrelated concurrent project changes were preserved.
