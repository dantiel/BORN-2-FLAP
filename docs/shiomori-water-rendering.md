# Shiomori Water Rendering — Findings & Invariants

> Internal technical record of the Shiomori Bay water investigation
> (symptoms: **black water texture**, **too-bright / washed-out world**,
> **corrupted breaking-wave simulation**). This documents *current state* and
> *durable invariants* — not the edit history. Source of truth for all tuning
> values is `Unreal/Born2Flap/Tools/create_shiomori_map.py` `water_material()`
> plus its invocation at line 1144.

## Current architecture (the way it is now)

The visible Shiomori ocean is a **plain translucent surface**, not the engine's
Single Layer Water (SLW) system:

- `SM_OceanGrid` — flat mesh at `Z=-50`, two-sided, `MSM_DEFAULT_LIT`,
  `BLEND_TRANSLUCENT`, `MTP_BEFORE_DOF`, refraction `RM_NONE`.
- The surface carries a **Beer-Lambert depth grade + Fresnel sky mirror +
  animated shoreline foam + 16-term Gerstner displacement**.
- Depth = `SceneDepth - PixelDepth` (the water-column thickness above the opaque
  procedural `Seabed` mesh at `Z=-50..-300`). No distance banding; shallow sand
  shows through, deep water grades to teal.

The native UE5 Water path (`AWaterBodyOcean` + SLW) is **dormant**:

- `bUseNativeOcean = false` in `Born2FlapWaterDirector.h`.
- The spawned ocean rendered **invisible in the game build** (Shiomori has no
  Landscape, only procedural meshes) and its engine-foam band rendered black
  (foam emissive never set). The spawn code stays for a future heightfield
  re-enable.

## Findings (symptom → root cause → resolution)

### 1. Black / checkered water → grey Default Material

**Root cause:** the material failed to compile, so UE silently swapped in the
grey Default Material.

Two independent triggers, both guarded now:

- **Single Layer Water node gotcha** — a
  `MaterialExpressionSingleLayerWaterMaterialOutput` node survives
  `delete_all_material_expressions()` (it is bound to the shading model). If a
  stale bake still carries `shading_model=MSM_SINGLE_LAYER_WATER` *and*
  `water_material()` re-adds the SLW node, the material fails with
  *"The material can contain only one Single Layer Water Material node"*.
- **Missing usage flags** — `used_with_nanite` and
  `used_with_instanced_static_meshes` must be set on programmatically-built
  materials. Absent flags log *"missing usage flag …"* and fall back to the
  grey Default Material (the classic checkerboard).

Guard: `_assert_translucent_water()` asserts the SLW node is absent and blend
mode is `BLEND_TRANSLUCENT` after every bake. `material_usage_flags()` sets the
flags. SLW is rejected on principle because it **compiles opaque/masked only**
and cannot take `WorldPositionOffset` — which is exactly what killed the bay's
transparency and hid the sea.

### 2. Black foam → two real bugs (NOT a missing texture)

`T_WaterFlow_01_Foam_Tiled` is Water-plugin content mounted at
`/Water/Textures/Foam/` and **is** cooked into the pak — the reference is fine.

- **Bug A (lit-only surf):** the foam band lived only in the lit base colour, so
  wherever the sun could not reach it, it shaded back down to dark teal —
  *"the foam is black"*. Fixed by making the shoreline foam **self-luminous
  white** (the same white as lit foam, `~1.1`→`~2.0` HDR for a gentle pop).
- **Bug B (sparse mask):** `M_BreakingSurf` v5/v6 was a LIT material sampling a
  sparse foam mask (mostly dark pixels, white lace) → lace term ≈ 0 → faint
  grey. The surf term now rides the same noise as the emissive core.

The foam term is `smoothstep(26.0, 6.0, d) * Foam` and also **drives opacity**,
saturating the waterline into a solid white band (aerated surf is opaque).

### 3. "Black slab" from above → depth source + near-black deep colour

- **Depth source:** the SLW renderer computes water depth from **SceneDepth**
  (opaque geometry under the surface). `r.Water.OceanFallbackDepth` only feeds
  CPU-side `WaterInfo` queries (buoyancy / water-mesh); it appears in **no**
  engine shader. The historic black slab came from the engine
  `Water_Material_Ocean`'s dark deep-ocean colour, not a missing depth.
- **Deep colour:** a near-black "deep" colour paints the thick-water crest faces
  black, so wave ribs read as hard dark bands instead of water.

### 4. Too-bright / washed-out world → emissive water vs auto-exposure

A sunny Shiomori scene runs ~EV-14 auto-exposure. A lit-only translucent water
gets tonemapped down to near-black against sunlit land. Resolution: the water
**emits** a self-luminous HDR body glow (`water*2.4`) so wave slopes and troughs
never shade to black, with the sky mirror riding on top. The old approach (a
16%-of-lit-colour floor) produced ~0.01–0.07 and the black wave faces survived
every colour tweak.

## Durable tuning invariants (do not violate)

- **Deep colour MUST stay in the teal/wave family** — never near-black.
  Current bake (`create_shiomori_map.py:1144`): deep `(0.08, 0.52, 0.66)`,
  shore `(0.12, 0.54, 0.60)` (turquoise-blue body; foam/surf stay white).
- **Soft Beer-Lambert**: `water = deep + (shallow - deep) * exp(-d/650)`.
  Do not tighten below ~`/450` — a `/250`-style absorption drops the ~3 m lagoon
  into a dark abyss.
- **Opacity floor 0.45**: `max(0.45 + 0.55*(1-exp(-d/110)), surf*0.95)`. The old
  depth-only `1-exp(-d/110)` went ~0.05 at the thin shoreline, letting the dark
  seabed read through the "transparent" water and blacking out the bay.
- **Fresnel floor 0.25** (not 0.05): `refl = saturate(0.25 + (1.6 + 0.9*Sun)*Fresnel)`.
  The old floor let the water read as a black slab from directly above.
- **World-space normals** (`tangent_space_normal = false`): the Gerstner normals
  are world-space; the default tangent-space interpretation re-rotates them and
  flips `NdotL` on lee faces, shading every swell black.
- **Sun-coupled mirror**: the `SunMirror` MPC parameter follows the weather
  sunlight every frame, so the ocean tracks sky brightness (dim sheen under
  cloud, hard mirror in full sun).

## Engine gotchas (reference)

- **UE 5.8 `delete_all_material_expressions`** iterates a live
  `TConstArrayView` while `RemoveExpression` mutates it → deletes only ~half the
  nodes per call. Loop until the expression list is actually empty.
- **Mobile (Android Vulkan / GLSL ES3.1):** SingleLayerWater materials fail to
  compile → grey Default Material on mobile. Keep Shiomori water a plain
  translucent `MSM_DEFAULT_LIT` material.
- **Runtime `AWaterBodyOcean`** has no default `WaterMaterial` (nullptr) — must
  call `SetWaterMaterial()` explicitly.

## Key files

- `Unreal/Born2Flap/Tools/create_shiomori_map.py` — `water_material()` +
  invocation (line 1144), `_assert_translucent_water()`, `material_usage_flags()`.
- `Unreal/Born2Flap/Tools/repair_shiomori_water.py` — rebuilds `M_Water` /
  `M_Shallows` only, preserving geometry.
- `Unreal/Born2Flap/Tools/repair_all_water.py` / `rebuild_water_and_station.py`.
- `Unreal/Born2Flap/Source/Born2Flap/Water/Born2FlapWaterDirector.{h,cpp}` —
  owns the ocean MPC + wave parameters; `bUseNativeOcean=false`.
- `Unreal/Born2Flap/Source/Born2Flap/Water/Born2FlapSurf.cpp` — shoreline foam.