# Shiomori Bay

> **Design intent & narrative:** see [world-design.md](world-design.md) — what
> this world *is*, why it exists, and what could be developed next.

## CAMPUS LOCATION AND ORIENTATION

The university occupies a repurposed late-1980s seaside hotel on the WEST side of Shiomori Bay, appearing on the RIGHT when viewing the beach from the sea. The iconic volcanic island and sheltered bathing lagoon are also on this same west/right side.

Use the level's actual geographic orientation when placing the campus. Do not place the hotel on the opposite side of the bay.

The coastal ground between the hotel and volcanic island forms part of the university campus. Connect the hotel forecourt, seaside research grounds, and island approach into one coherent place. Preserve the volcanic island's distinctive silhouette and the sheltered lagoon.

## CAMPUS GROUNDS AND ISLAND APPROACH

Extend the campus along the shoreline toward the volcanic island. Include a modest coastal pine garden, low stone retaining walls, shaded worktables, benches, and a pedestrian path following the sheltered water.

Place a traditional torii gate at the transition from the campus garden to the island approach. Frame the volcanic rock through its opening from an important ground-level viewpoint. Use weathered stone or restrained vermilion timber, with proportions appropriate to a small coastal shrine.

The torii marks the approach to a small, older Shinto shrine associated with the island; it is not the university's entrance sign. Let the campus visibly accommodate this pre-existing sacred place. Place the shrine on a suitable natural ledge or near the island's foot, reached by a modest stone path and steps. Avoid a large temple complex or pagoda.

Inspect the existing water gap before designing access. If a crossing fits the terrain and composition, use a narrow, understated pedestrian bridge. Otherwise keep the torii and shrine approach on the mainland shore, facing the island. Preserve water circulation, the bathing lagoon, and useful flight passages.

Keep this shoreline primarily pedestrian. Locate the launch field where it has adequate clearance, away from the shrine approach and bathers. Place the hangar behind the hotel on the inland/service side, with a practical aircraft-handling route to the launch area.

Position the founder's statue in the hotel's former fountain forecourt. Keep it visually separate from the shrine approach so each place has its own identity.

The coastal composition should read as: the bulky adapted hotel, a quieter campus garden and torii beside the lagoon, then the majestic volcanic island. Keep the island visible from the beach and bay, and preserve clear flight space around these landmarks.

Launch the level directly with `Unreal/Born2Flap/Tools/play.ps1 -Level Shiomori`
(or select it from the main menu, **F10**). **F8** opens the POI overlay to
re-place the bird at launch points and scenic cameras; **F3** opens the RC
settings; **F7** hides the flight HUD.

A 3.2 m/s onshore breeze produces a small localized updraft above the stair wall.
This enters the normal air-relative aerodynamic calculation, rather than applying
an extra lift force to the bird. Water landings reset to the beach launch point.

The saved map is `/Game/Shiomori/Maps/SHIOMORI`. Regenerate its authored geometry,
materials and camera viewpoints with `Tools/create_shiomori_map.py` in Unreal's
Python commandlet. `Tools/test-shiomori.ps1` checks collisions, water classification,
wall lift, key objects and captures three rendered views. All content is generated
locally; no downloaded environment assets are required.

The default morning parhelion profile uses a full display: the atmosphere sun, two bright sun dogs,
a 22-degree halo, upper arcs, and a double rainbow opposite the sun. Effects are
world-direction aligned and masked by scene depth, so buildings and terrain
occlude them. The post-process material explicitly preserves PostProcessInput0;
surface additive blending alone would replace the scene with black.

To repair the sky and FPV materials without regenerating the level, run
`Tools/repair_shiomori_sky.py` through Unreal's Python commandlet. Use forward
slashes in the `-script=` path. Restart an already running game to reload the
saved materials.

After building the editor target, run `Tools/test-shiomori.ps1 -Sky` for rendered
sun/halo, opposite-sun rainbow, and FPV captures, followed by the coastal views
and world checks. This mode rejects shader compilation failures; inspect
`Saved/Screenshots/WindowsEditor/SHIOMORI_0.png` through `SHIOMORI_2.png` for
appearance and occlusion. The repaired display was visually checked on 2026-10-04
with `ShiomoriTest PASS` and no material compilation failures.