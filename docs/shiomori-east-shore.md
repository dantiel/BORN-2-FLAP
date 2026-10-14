# Shiomori eastern spit

The seaward end of the eastern peninsula (positive world X) is a low shingle
and rock shore. It deliberately stays distinct from the high western cliffs.
The existing central bathing beach, houses and west campus remain outside the
new terrain/material mask.

## Terrain and vegetation

- Continuous terrain rises gently from a submerged shelf to a pebble strand,
  gravel/grass patches and the inland woodland edge. Surface height, collision,
  native flight ground queries and water masking use the same height function.
- Eastern tiles use 2 m mesh spacing. The material fades into the existing
  headland shader at its boundaries; it does not create rectangular texture seams.
- Two scanned coastal formations, embedded stones and small loose pebbles give
  the shore varied shapes. These are ordinary coastal rocks, not lava assets.
- Sparse narrow-leaved shrubs and stiff grass occupy the upper shore. Ferns and
  shorter trees form tapering groups towards the existing woodland. Planting
  excludes the lower splash zone and uses deterministic patch distributions.
- Small vegetation is instanced with distance culling; scanned formations have
  four conventional LODs and complex collision. This is runtime map decoration.

Implementation: `Born2FlapEastShore.cpp`, `Born2FlapBayTerrain.cpp`,
`Tools/fetch_east_shore_assets.py`, `Tools/create_east_shore_assets.py`.
Downloaded sources/checksums are retained in `Saved/EastShoreSource/manifest.json`;
portable Unreal assets are under `/Game/Shiomori/EastShore`.

## Sources (CC0)

- https://polyhaven.com/a/coast_sand_rocks_02
- https://polyhaven.com/a/dry_river_pebbles
- https://polyhaven.com/a/gravelly_sand
- https://polyhaven.com/a/coast_rocks_02
- https://polyhaven.com/a/coast_land_rocks_02
- https://polyhaven.com/a/shrub_02

The surface sets use 4K maps; formation and shrub atlases use 2K maps. Existing
CC0 moss rocks, grass, ferns and trees are reused. The shrub scan supplies the
requested sparse, narrow-leaved silhouette, not a botanical claim of Japanese
native-species authenticity.

## Verification and renderer limitation

2026-10-13: Development Editor build passed. The dedicated eastern collision
test passed all twelve probes (maximum height discrepancy 7.5 cm). The normal
weather-render command passed twice after the shadow workaround, including the
final rock-skirt placement correction. Sea approach, waterline, aerial transition
and woodland views were inspected. Runtime decoration contains 7,197 instances,
including 65 scanned coastal formations and 874 shrubs. No Shipping package was
created for this change.

Commands:

```
Tools/test-shiomori.ps1 -NoRender -EastShore
Tools/test-weather.ps1 -EastShore -Weather sunny -DayTime noon
```

The eastern collision test exercises twelve transect points from the upper
shore through the waterline to the seabed. The separate original full-coast
test remains intact. It exposed an unrelated sea-wall wind assertion:
`WindNear=0` while the old test expects `>0.3`. The eastern height mask is zero
at that probe and along its downwind sample line; that wind behavior was not
changed here. See `.setup/east-shore/full-coast-test.log`.

Normal D3D12 rendering repeatedly faulted on frame 2 in the Nanite
`NodeAndClusterCull` virtual-shadow pass. Both disabling Nanite and, separately,
disabling virtual shadow maps allowed the same map to render. The project now
uses conventional shadow maps (`r.Shadow.Virtual.Enable=0`), retaining Nanite
geometry globally. This is a workaround, not an engine/driver root-cause fix.
Restore virtual shadows only after verifying this scene with an updated renderer
or driver. No user graphics preferences were rewritten.

### Texture repetition refinement

The eastern terrain material now uses deterministic, smoothly blended texture
patches with random rotations and offsets for pebbles, sand, exposed stone,
grass and forest litter. Texture scale is preserved. Pebble colour, normal and
roughness share the same transforms; grass colour and normals also match.
Normal directions are rotated back into the surface frame, and explicit UV
gradients keep mip selection stable across patch boundaries.

Implementation: `Tools/shore_antitiling.py`, used by
`Tools/create_east_shore_assets.py`. Four texture samples replace each plain
sample, increasing local material cost; no performance benchmark was run.
The sunny/noon eastern render test passed after rebuilding the material, with
no material compiler errors. Ground and aerial captures were inspected against
the previous shoreline view. Broader terrain outside the eastern material mask
is unchanged. Proof: `.setup/east-shore/antitiling-pebble-coast.png`.

### Older beach / eastern vegetation strand

The older `M_Sand` coarse patch previously ended at the mesh boundary while
`M_Headland` reverted to fine sand. Both now use `east_strand_material.py` for
matching world-space coarse sand colour, normal detail and roughness. The
original coarse surface and its scale are retained, with the same stochastic
rotation/offset sampling used by the outer eastern shore. The west beach keeps
its existing blend. The eastern headland has a broader, irregular elevation
fade from coarse sand into vegetation (roughly 0.8 to 5.2 metres).

`rebuild_east_strand.py` rebuilds these two materials without regenerating the
map, and refreshes `M_EastShore`, which embeds the headland graph. The main map
generator also includes the shared blend, so regeneration preserves the fix.

Verification: editor build and sunny/noon `test-weather.ps1 -EastStrand`
passed, with four inspected views covering both ends of the coarse patch and
the woodland edge. Captures are `.setup/east-shore/strand-ground.png` and
`.setup/east-shore/strand-transition.png`. No terrain geometry or collision was
changed. These additional material samples have not been performance-profiled.
The outer EastShore render check also passed after the shared material rebuild; shoreline and woodland captures were inspected.

### Close sand relief (2026-10-14)

`sand_surface.py` now combines the original bright 300 cm authored sand texture
with warped, differently rotated height-noise scales, gradient normals and a
small bounded view-dependent texture offset. There is no stretched directional
noise or periodic ridge train. Fine normal detail is added at 65 cm texture
scale and fades by pixel footprint. Colour, normals and roughness share the
same offset, and BC5 XY normal data is decoded once with reconstructed Z.
The beach, wet sand and headland sand use this common shading.

This is material relief, not displaced collision geometry or a moving sand
simulation. Parallax is deliberately restrained to avoid stretched patches at
grazing angles. The other agent's input was preserved before editing in
`.setup/east-shore/sand-surface-other-agent.py`. The original pre-change close
capture is `.setup/east-shore/sand-before.png`.

Use `test-weather.ps1 -SandDetail -Weather sunny -DayTime noon` (or `sunset`)
for low, standing, elevated and shoreline inspection cameras. Material rebuild:
`rebuild_east_strand.py`; it also refreshes the headland-derived EastShore graph.
Final validation: editor build succeeded; sunny/noon and sunny/sunset SandDetail render checks passed without material compiler errors. Close and standing views were inspected under both lighting conditions. Final captures: sand-final-noon.png and sand-final-sunset.png in .setup/east-shore. No performance benchmark or Shipping package was produced.
