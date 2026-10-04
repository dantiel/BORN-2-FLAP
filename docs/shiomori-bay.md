# Shiomori Bay

A fictional near-future waterfront on a quiet industrial coast.
The 900 m beach faces an open bay, with a basalt island sheltering its western end.
Six continuous shallow steps rise 1.5 m to the promenade. Nine cloud-canopied
art shelters (soft translucent white puffs on slim white piers) line the
pavement; service roads, sterile white buildings and scrub sit behind it, with a
grey-yellowed industrial yard.
bushes concentrated at its eastern end.

Press **F4** to cycle Ravenstonefield ? Shiomori Bay ? Training. For direct launch:
`Unreal/Born2Flap/Tools/play.ps1 -Level Shiomori`.

A 3.2 m/s onshore breeze produces a small localized updraft above the stair wall.
This enters the normal air-relative aerodynamic calculation, rather than applying
an extra lift force to the bird. Water landings reset to the beach launch point.

The saved map is `/Game/Shiomori/Maps/SHIOMORI`. Regenerate its authored geometry,
materials and camera viewpoints with `Tools/create_shiomori_map.py` in Unreal's
Python commandlet. `Tools/test-shiomori.ps1` checks collisions, water classification,
wall lift, key objects and captures three rendered views. All content is generated
locally; no downloaded environment assets are required.

The sky uses a persistent full display: the atmosphere sun, two bright sun dogs,
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
