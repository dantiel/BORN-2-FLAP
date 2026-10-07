# Shiomori lava, liquid water and wooded bay

The rejected procedural basalt columns have been replaced with licensed lava
photoscans. `shiomori_volcanic.py` arranges overlapping, tilted scoria and lava
meshes into reefs with tidal channels, beach rocks on both flanks and submerged
fragments. Grass and three small trees are planted using actual mesh raycasts.
Flight ground/water queries likewise trace the scanned surfaces, including gaps.

Sources and attribution are recorded in `Unreal/Born2Flap/ThirdPartyNotices.txt`,
which is staged with packaged builds. `fetch_lava_assets.py` downloads the public
Objaverse copies, retains original license metadata/checksums and normalizes GLBs.
Run `import_lava_assets.py` with the full Unreal editor's `-ExecutePythonScript`
entry point to create `/Game/Shiomori/LavaScans` and wet/dry charcoal materials.

Close the game/editor and back up the saved map before running
`repair_shiomori_volcanic.py` with `-ExecutePythonScript`. It replaces only actors
with volcanic tags and preserves other map work. The full map generator calls
the same placement module. Do not use the Python commandlet for the placement
or mesh-import steps; they require editor subsystems and scene collision.

`water_material()` now uses Single Layer Water: physical absorption/scattering,
reflection and refraction, with the existing MPC-driven Gerstner displacement.
It renders before translucent bird membranes. `repair_shiomori_water.py` updates
only the ocean materials. Breaking surf retains its curling geometry and foam;
its vertices now follow the displaced liquid surface to avoid depth occlusion.
`repair_bird_water_rendering.py` preserves ocean refraction when rerun.

`ABorn2FlapBayTerrain` supplies continuous collision terrain behind the developed
shore and around the two headlands, replacing decorative non-colliding distant
hills at runtime. Its height function also drives native ground queries and
upwind terrain shelter. Protection fades above the headlands and offshore.
Mature trees form groves to the east and behind the buildings, with smaller
western groves. `repair_coastal_tree_canopies.py` enables Nanite area preservation
and runtime-safe bark materials so foliage does not disappear at distance.

Validation commands, run sequentially after a native build:

```powershell
./Unreal/Born2Flap/Tools/test-shiomori.ps1 -NoRender
./Unreal/Born2Flap/Tools/test-weather.ps1 -Volcanic -Weather sunny -DayTime noon
./Unreal/Born2Flap/Tools/test-weather.ps1 -BirdWater -Weather sunny -DayTime noon
```

The volcanic test captures five views: reef overview, water among rocks, east
beach/headland, rear woodland and the bay outline. Images are saved as
`Saved/Screenshots/WindowsEditor/VOLCANIC_Shiomori_sunny_*.png`. Collision tests
cover the new land, actual reef surfaces, a tidal gap and terrain wind exposure.
