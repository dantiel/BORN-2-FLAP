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