# Repairing invisible YUNSHA terrain

## Which regression this fixes

On 2026-11-13 the playable `/Game/Yunsha/Maps/YUNSHA` had been overwritten by `create_yunsha_mp_map.py`, a separate experimental MeshPartition/World Partition generator. The supported manifest still selected `create_yunsha_map.py`, the static-mesh terrain route previously confirmed working by the user. Both scripts targeted the same map package.

The saved MeshPartition probe from that morning reported zero loaded compiled sections, zero collision components and five missed terrain traces. This establishes that its probe did not see terrain; it does not prove the external packages were absent, nor identify which streaming/activation step failed. MeshPartition runtime registration/material/data-layer workarounds are not proof of a visible world.

The repair restores the static-mesh route. It does not claim to repair the experimental MeshPartition pipeline. The playable world still uses mesh terrain, with 36 dune tiles, the Bone Archive mesh and the horizon apron.

## Exact recovery procedure (PowerShell, repository root)

1. Close Unreal Editor and the game. Verify there are no asset writers:

```powershell
Get-Process UnrealEditor,UnrealEditor-Cmd,Born2Flap -ErrorAction SilentlyContinue
```

2. Preserve the current map before overwriting it. This is a map-file snapshot; a World Partition restoration also depends on its external actor/object packages. Do not delete those packages.

```powershell
New-Item -ItemType Directory -Force .setup/yunsha-before-repair
Copy-Item -LiteralPath Unreal/Born2Flap/Content/Yunsha/Maps/YUNSHA.umap -Destination .setup/yunsha-before-repair/YUNSHA.umap
```

3. Normally rebuild native code and regenerate through the manifest:

```powershell
& 'V:\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe' Tools/generate_maps.py --maps yunsha --force
```

For an asset/script-only recovery, when the installed editor binary already contains `Born2FlapTerrainBuilder.smooth_yunsha_terrain` and the correct native `GroundHeight`, reuse it explicitly:

```powershell
& 'V:\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe' Tools/generate_maps.py --maps yunsha --force --skip-build
```

Do not use `--skip-build` to install C++ changes. On November 13 the native build check failed with duplicate declarations from `V:\UE_5.8\Engine` and `V:\UE_5.8\UE_5.8\Engine`; the asset repair used the existing binary and made no native-code changes. That separate build problem remains to be resolved before compiling new native changes.

4. Require process exit 0, `yunsha: saved and checked`, and a fresh `Saved/MapGeneration/yunsha.result.json` with `success: true` and `reloaded: true`. The expected dune tile count is 36. Read `Saved/MapGeneration/yunsha.log` for generation failures.

5. Validate actual rendering and collision:

```powershell
& 'V:\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe' Tools/test_yunsha.py --time noon --timeout 900
```

Inspect `Saved/Logs/yunsha-noon-dry.log`: require the terrain collision checks and `YunshaTest PASS`. Inspect fresh screenshots under `Saved/Screenshots/WindowsEditor`: view 0 shows Lunya and surrounding ground; 18?21 show desert; 22/23 are close-ups. A successful generator or collision trace alone does not establish terrain visibility. The wrapper also fails on any material compile error: report unrelated foliage errors separately, never call that a fully clean test.

## Mesh-generation invariants

- `surf(x,y)` must return native `ground(x,y)` directly. Do not add sand relief a second time: that previously separated rendered terrain from gameplay heights.
- Every `Dunes_*`, `BoneArchiveTerrain`, and `DistantSandSea` mesh must pass through `smooth_yunsha_terrain` after `build_from_static_mesh_descriptions` and before saving.
- The native helper writes normals and wind UV1, commits the mesh description, performs `Mesh->Build(false)`, and marks the package dirty. Do not replace this full asset build with a material refresh.
- Dune sampling offset is `(0,0)`; Bone Archive offset is `(100000,0)`; the horizon uses `bHorizon=true`.
- Preserve full-precision UVs and disable automatic lightmap UV generation so the solver data in UV1 is not overwritten.
- Save the meshes AND the regenerated map, then reload and render.

## Preventing recurrence

`Tools/generated-content.json` must select `create_yunsha_map.py` for `yunsha`. The experimental `create_yunsha_mp_map.py` now aborts before changing assets unless `B2F_ALLOW_YUNSHA_MP_OVERWRITE=1` is deliberately set. Do not set this flag as part of the repair. It exists solely for intentional experimental replacement after a backup and separate validation.

The supported generator no longer deletes every mesh it did not create: the directory is shared with MeshPartition source assets and external actors. Retiring those assets is a separate operation.

`repair_yunsha_sand.py` repairs materials only. `refresh_yunsha_aeolian.py` refreshes existing tile assets and their UVs. Neither recreates missing tile actors in a MeshPartition map. For this regression, run the full supported map generation command above.

## Evidence for the November 13 repair

The pre-repair map snapshot is `.setup/yunsha-repair-20261113/YUNSHA.before-repair.umap`. The original MeshPartition probe is `Unreal/Born2Flap/Saved/YunshaMetrics/meshpartition_probe.json`. Use the fresh generation result and rendered-test log for the outcome; do not infer it from this document alone.


### Verified outcome, 2026-11-13

- Supported generation exited 0: `yunsha: saved and checked in 194.1s`.
- Result: `success=true`, `reloaded=true`, 36 dune tiles, 538 actors.
- Fresh rendered views 0 and 18 were inspected: Lunya's surrounding ground and the open desert/Bone Archive approach are visible. All 24 views were freshly captured.
- Runtime log emitted `YunshaTest PASS` at 16:05:30 UTC; ground and excavation collision checks passed.
- The Python test wrapper exited 1 because `/Game/Shiomori/Materials/M_CoastalBark` failed shader compilation. No clean full-suite pass is claimed.
- The separate native build failure described above remains unresolved; the asset repair ran on existing editor binaries.
