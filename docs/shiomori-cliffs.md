# Shiomori: outer campus cliffs

The cliff coast is at the seaward end of the western campus peninsula (negative
world X, positive world Y). It is runtime terrain, not placed volcanic boulders.
The volcanic island remains separate and unchanged.

`Born2FlapBayCliffs.cpp` defines a continuous eroded height field, irregular
ledges, a small recovery strand, crest grass and woodland behind the launch
area. The face tiles use 50 cm spacing; adjacent land uses the existing coarser
mesh. Collision is generated from these same vertices. The native height query
also drives bird ground interaction, vegetation placement and wind shelter.

`Tools/create_cliff_material.py` rebuilds `/Game/Shiomori/Materials/M_BayCliffs`.
It copies the existing headland material and blends a local world-projected
rock, moss, turf, pine-litter and wet-sand surface into it. This avoids rectangular
material boundaries and preserves the existing campus ground treatment.
Run with the project-qualified Unreal Python command while the editor is closed.

The user's NatureManufacture Coast & Dunes video is a quality reference only;
that commercial pack was not purchased or imported. Current geometry remains a
height field, so it has no true overhangs or caves and does not reproduce the
scanned cliff assets in the reference.

Wind lift depends on incoming wind projected onto the local cliff normal,
distance from the crest, and height. It vanishes in calm/offshore conditions
and below ground. Woodland adds directional shelter behind the high ground.

Verification, 2026-10-13:

- Development Editor build passed.
- `Tools/test-shiomori.ps1 -NoRender` passed. Twelve cliff collision probes hit;
  maximum sampled native-height/triangle discrepancy was 24.3 cm on the steep
  face and at most 0.1 cm on the sampled plateau/strand/seabed locations.
- With 4 m/s onshore wind, three crest probes produced 3.10, 3.10 and 3.71 m/s
  lift; offshore and calm probes produced zero.
- `Tools/test-weather.ps1 -Cliffs -Weather sunny -DayTime noon` captures the sea
  approach, crest view and woodland overview for visual inspection.

These checks use the editor game runtime, not a new Shipping package.

## Detailed surface / vegetation pass

`Tools/fetch_cliff_assets.py` downloads 4K diffuse, normal and roughness maps,
verifies their checksums and retains a source manifest in `Saved/CliffSource`.
`create_cliff_material.py` imports portable assets to `/Game/Shiomori/Cliffs`.
The photographed source surfaces are CC0:

- https://polyhaven.com/a/rock_face_03
- https://polyhaven.com/a/cliff_side
- https://polyhaven.com/a/leafy_grass
- https://polyhaven.com/a/forrest_ground_03

Grass uses two existing scanned meshes in irregular colonies. Ferns occupy the
sheltered woodland floor; periwinkle fills selected outer patches. Existing
photographed moss-rock meshes form deeply embedded low outcrops. A clear path
and launch corridor are excluded from taller vegetation and outcrops.
Hierarchical instances are built once after placement, with distance culling
for small vegetation. Final runtime placement: 65,971 total detail instances,
including 348 woodland trees. The Development Editor build, coast collision /
wind test and rendered weather test all passed on 2026-10-13. Four views were
inspected: seaward approach, crest, woodland overview and a ground-level view.
The final material fades into the existing headland shader without a tile edge.
This improves local surface detail and vegetation density; it is not a claim
that the entire level matches the reference pack's photogrammetry quality.
