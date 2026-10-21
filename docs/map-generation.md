# Map generation

The playable levels (`SHIOMORI`, `NEONABYSS`, `RAVENSTONEFIELD`) and their
procedural meshes are **not committed** to the repository. They are deterministic
outputs regenerated from versioned inputs (generator scripts, materials,
textures, audio, imported art). A fresh checkout has everything needed to
regenerate them on Windows or macOS.

## What is generated vs. committed

`Tools/generated-content.json` is the single source of truth. It lists each
level's generator script, output package, and the `outputs` globs (relative to
`Unreal/Born2Flap/Content`) that are intentionally untracked:

- `{Ravenstonefield,NeonAbyss,Shiomori}/Maps/*.umap`
- Shiomori procedural meshes: `SM_Campus_*.uasset`, `SM_ContinuousBeach`,
  `SM_Seabed`, `SM_OceanGrid`, `SM_Mountains*`, `SM_SweetPotato*`

Materials, textures, audio, the founder statue, and imported nature meshes
remain committed as inputs.

## Regenerate

Requirements: Unreal Engine 5.8, the `Born2FlapEditor` target built, and Git LFS
assets pulled (`git lfs pull`).

Windows:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/generate-maps.ps1
```

macOS:

```bash
UE_ROOT=/Users/Shared/Epic\ Games/UE_5.8 ./Tools/generate-maps.sh
```

Both wrappers run `Tools/generate_maps.py` (stdlib-only Python). It locates the
engine (via `UE_ROOT` or `--engine-root`), builds the editor unless
`--skip-build` is given, computes an input fingerprint, and only regenerates
maps whose outputs are missing or stale. Cached stamps live in
`Unreal/Born2Flap/Saved/MapGeneration/state.json`; `--force` bypasses the cache,
`--maps <name...>` selects specific levels, and `--plan` prints the commands
without running anything.

Release packaging (`Tools/package-win.ps1`, `Tools/package-mac.sh`) invokes
generation automatically before cooking, so CI checkouts need no committed maps.
