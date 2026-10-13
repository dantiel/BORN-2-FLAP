# Shiomori Ornithopter University

> **Design intent & narrative:** see [world-design.md](world-design.md) — what
> this world *is*, why it exists, and what could be developed next.

The campus is an adapted 1988 seaside hotel on the west (negative-X) side of the bay. From the sea, it appears on the right. Its drafting center is **(-59500, -13700) cm**, rotated -18 degrees, translated by `SITE_OFFSET=(-12000,24000)` to world **(-71500,10300) cm**. This places it 60 m farther west and 30 m farther toward the peninsula than the previous park layout, apart from the beach and town. The inland hangar remains on low ground at **(-73500,-3900) cm**, connected by a graded service route. The coastal garden, torii and shrine face the volcanic island from the west across the sheltered water; the island and lagoon are untouched.

## Construction

- Twelve 3.3 m floors; a 120 m base slab with upper floors progressively shortened, creating a descending stepped silhouette. The east end is bevelled. Continuous floor bands and railings follow the bevel.
- Real 2.8 m balcony recesses, thick party walls, dark sliding-door recesses, faded sea-green parapets, beige room blocks, dirty off-white structural concrete and sparse drainage marks. The service elevation is intentionally plain.
- Heavy hotel entrance canopy, restrained university sign, retained 1988 hotel sign/frame, visible ground-floor testing hall and upper panoramic flight laboratory, sparse balcony research frames, rooftop net enclosure.
- Separate forecourt with a former fountain basin and provisional bronze founder sculpture holding an ornithopter aloft and a transmitter with strap. **No likeness was supplied; this is a stylized sculptural study of the founder, Obi-Wan Da Vinci Anakin Chronister Rodriguez, not an accurate portrait.**
- Inland workshop hangar: 26 x 22 m, pitched roof with two glazed daylight strips, steel framing, cladding ribs, parked sliding doors and a clear approximately 16 x 6 m opening. Two aircraft positions, rear workbenches/storage and an approximately 8 m central handling aisle. Apron and handling route connect to a separate grass launch field.
- Mainland pedestrian garden/path toward the island, existing fir assets, worktables and benches at ordinary human scale, low stone boundaries, bicycle rack and modest torii. The torii's opening faces the existing island center at (-45200,18200) cm. A small pitched-roof shrine sits off that sightline. There is no causeway or bridge across the lagoon.

The previous hotel's curtain-wall block, saucer tower, rooftop pool, solid hangar box and previous shrine additions are replaced. Public promenade furniture is preserved; the shared `Bench leg` tag is filtered by the old campus garden's bounds rather than deleted globally.

## Assets and repeatability

`Unreal/Born2Flap/Tools/shiomori_campus.py` is the parameterized construction module used by both the full map generator and the targeted rebuild. `HOTEL_CENTER`, `HOTEL_YAW`, `FLOORS`, `FLOOR_HEIGHT`, `BAY_WIDTH`, `BALCONY_DEPTH`, `HANGAR_CLEAR_DOOR` and `EXPERIMENT_SPAN` expose the key design dimensions. When changing the hangar opening, update the front-panel placements as well as the declared clearance.

The module saves `SM_Campus_*` meshes and `M_Campus*` materials under the existing Shiomori asset folders. Repeated architecture is baked into material/zone mesh batches to avoid thousands of per-detail actors. Meshes have complex collision where appropriate; netting and suspended display models do not obstruct flight. All generated actors carry `ShiomoriCampusV2`. Rerunning the targeted script replaces those actors without duplication and asserts that all volcanic actors survive unchanged.

Reused assets: existing textured Nature trees, firs, ferns, grass and rocks, level lighting, water, volcanic island and mesh-building helpers. New assets include campus architecture and furnishings, plus the CC0 [Periwinkle Plant from Poly Haven](https://polyhaven.com/a/periwinkle_plant), imported as 2K textured meshes with opacity masks, normal maps and four LODs. Powered by Poly Haven. `fetch_campus_plants.py` downloads the source and verifies supplied hashes; `campus_plant_assets.py` imports it. Run the fetch script before rebuilding on a fresh checkout without `Saved/NatureSource`.

`shiomori_park.py` replaces the former colored cube flowers, spherical ornamental trees and floating ground boxes with irregular ground-following beds, low flowering plants, ferns, partly buried rocks and asymmetric tree groups. Tall planting is excluded within 13 m of the torii and from its island-view corridor. Footpaths and shrine access have separate planting clearances. Existing fir assets serve as coastal evergreens; this is not a claim of botanical Japanese pine accuracy.

The available static bird asset was a 100 cm-long kestrel fuselage; a complete large experimental aircraft was unavailable. The hangar therefore uses **labelled 6 m-span experimental study models** with structural spars and membrane silhouettes. These are dimension proxies/display models, not new flyable aircraft. The existing flight planform also documents a 1.44 m-span default bird, substantially smaller than the hangar clearance.

## Rebuild

For the imported shrine, install the source files once with
`python Unreal/Born2Flap/Tools/fetch_shrine_assets.py`, then run
`python Unreal/Born2Flap/Tools/flatten_shrine_glb.py`. The flattening step bakes
node transforms into a single mesh while retaining material slots, UVs and
textures; it does not replace the artist's surfaces with generic colors.
`fetch_campus_plants.py` supplies the flowering plants and photographed grass,
pine-needle litter and mossy rock textures. Imported `.uasset` files are portable
and do not require these download steps to play the level.

The torii is **Japanese Torii gate Game Asset by Bazylonator** and the small
hokora is **Small Japanese Shrine by SamRi**, both CC BY 4.0, obtained from
Ai2's public Objaverse mirror. Source links, license links and adaptation notes
are in `Content/ThirdParty/Shiomori-assets.txt`; that directory is configured
for loose-file staging in packaged builds. The larger GreenG shrine was
reviewed but rejected because its long raised corridor does not fit this site.

The former primitive gate and shrine are removed during the tagged rebuild.
The island-facing approach is now a ribbon of irregular, bevelled mossy slabs
set in pine-needle litter, with photographic litter patches beneath evergreen
groups. The final approach aligns with the gate opening. Ground sampling
matches the native terrain's 10 m triangle interpolation so small flowers and
slabs sit on the rendered surface rather than its underlying analytical curve.

Close the running game/editor first to prevent map locks. Back up the current map, then run from the repository root:

```powershell
& 'V:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'V:/BORN 2 FLAP/Unreal/Born2Flap/Born2Flap.uproject' '-ExecutePythonScript=V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/rebuild_shiomori_campus.py' -unattended -nosplash -nullrhi '-abslog=V:/BORN 2 FLAP/.setup/campus-rebuild.log'
```

Use the full editor Python entry point, not a bare commandlet. Check `CAMPUS_MAP_SAVED` in the log; successful mesh generation alone does not confirm map saving. The original replacement attempt encountered a running-game file lock and was retried after closing that game.

`Saved/campus-rebuild-report.json` records removed actors and preserved volcanic actor count. The pre-rebuild map and original campus source were backed up in `.setup/campus-20261010/`; do not restore that whole map over subsequent unrelated edits.

## Verification

The campus camera/collision test is integrated with the existing weather experience test:

```powershell
./Unreal/Born2Flap/Tools/test-weather.ps1 -Campus -Weather sunny -DayTime noon
```

It captures seven views (bay, hotel front, aerial, hangar, torii/island, garden and shrine) as `Saved/Screenshots/WindowsEditor/CAMPUS_Shiomori_sunny_0.png` through `_6.png`. Generated target-point anchors keep cameras and collision probes attached to the current campus layout. The test traces the central hangar opening, roof, forecourt, launch field, garden path, shrine and terrace, alongside existing surf/audio checks. Run the shoreline regression separately with `test-shiomori.ps1 -NoRender`.

Inspect screenshots as well as logs: collision traces cannot establish architectural quality, correct framing or attractive proportions. Interiors are limited to important visible rooms. Most former guest rooms are closed room blocks, not fully furnished or walkable rooms.

### Results recorded on 10 October 2026

- Native Unreal 5.8.2 Development Editor build passed.
- The targeted rebuild saved 57 tagged campus actors and 37 persistent mesh batches (27,128 triangles), preserving all 143 existing volcanic actors. Repeated rebuilds replaced the previous tagged generation rather than duplicating it.
- Rendered campus/weather tests passed with screenshots inspected. Geometry inspection caught and corrected inverted path faces, the shrine roof winding, and bevel/rear-wall closure. HUD is hidden for these captures.
- Campus collision probes passed for the open handling aisle, roof, forecourt, launch field and garden path. A swept 6 m-wide, 3.6 m-long, 2.4 m-high clearance envelope passed through the doorway and central aisle. This is a geometric clearance test, not a piloted flight certification.
- `ShiomoriTest PASS`: sand, stairs, promenade collision, bay/island water mask, stair lift and existing shelters/posts. No water, surf or music implementation was replaced.
- The first rendered startup suffered a Nanite `NodeAndClusterCull` GPU page fault. Subsequent renders passed without disabling Nanite or changing project rendering settings; the initial failure is retained in `.setup/campus-20261010/first-render-gpu-crash.log`. Its root cause was not established.

Remaining deliberate placeholders: the founder's likeness, the experimental aircraft study models, and closed/unfurnished guest-room interiors. Imported firs are reused as the coastal evergreens; no new botanical pine asset was acquired.

The final seven captures, passing logs and rebuild manifest are preserved locally in `.setup/campus-20261010/verified/` so future tests do not overwrite this evidence. Review the [hotel front](../.setup/campus-20261010/verified/CAMPUS_Shiomori_sunny_1.png), [aerial layout](../.setup/campus-20261010/verified/CAMPUS_Shiomori_sunny_2.png), [daylit hangar](../.setup/campus-20261010/verified/CAMPUS_Shiomori_sunny_3.png) and [torii framing the island](../.setup/campus-20261010/verified/CAMPUS_Shiomori_sunny_4.png).

### Park and imported shrine verification, 11 October 2026

- The saved campus now has 1,667 tagged actors, including 1,522 planted assets;
  all 143 volcanic actors remain unchanged. Hotel center: (-71500,10300) cm.
- One imported torii, 480 cm high and 524.45 cm wide, and one imported hokora,
  360 cm high and 205.94 cm wide. Original primitive shrine batches are absent.
  The shrine faces its approach; the torii retains the clear island view.
- Seven rendered views inspected. `CampusTest PASS`, `WeatherExperienceTest PASS`,
  soundscape waveform check PASS and `ShiomoriTest PASS`. The 6 m handling
  envelope, hotel terrace, path, launch field and shrine collision probes pass.
- Repeatable D3D12 `NodeAndClusterCull` startup page faults were avoided by
  `r.Nanite.AsyncRasterization=0` in DefaultEngine.ini. Nanite remains enabled;
  this changes scheduling, not geometric quality. It is a tested workaround on
  this machine, not a proven driver/engine root-cause fix or a cross-GPU benchmark.
- The render check also exposed a disconnected Sine input in the existing
  insect-wing material. Its connection and generator were repaired; the final
  run has no material compilation errors. Existing insect Nanite usage warnings
  remain outside the campus asset work.
- Evidence: `.setup/shiomori-park-20261010/verified/`, including seven PNGs,
  both passing logs, the rebuild report and numeric asset audit. The model
  flattening utility uses NumPy. `-B2FCampusPlacementOnly` may be supplied to
  the rebuild command when only geometry/placement changed and all campus
  materials already exist; omit it after editing material definitions.
