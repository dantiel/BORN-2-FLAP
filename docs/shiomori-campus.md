# Shiomori Ornithopter University

> **Design intent & narrative:** see [world-design.md](world-design.md) — what
> this world *is*, why it exists, and what could be developed next.

The campus is an adapted 1988 seaside hotel on the west (negative-X) side of the bay. From the sea, it appears on the right. Its drafting center is **(-59500, -13700) cm**, rotated -18 degrees, translated to world space by `SITE_OFFSET` (currently (-6000, 21000), i.e. hotel at world **(-65500, 7300) cm**): on the raised west-headland peninsula (the "big wasteland" west of the bay), clearly apart from the beach, promenade and coastal road. The coastal garden, torii and shrine face the volcanic island from the west across the sheltered water; the island and lagoon are untouched.

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

Reused assets: existing Nature fir meshes, level lighting, water, volcanic island and established material-generation/mesh-building helpers. New assets: campus architecture, materials, furnishings, torii/shrine and sculptural/display geometry. No assets purchased or downloaded.

The available static bird asset was a 100 cm-long kestrel fuselage; a complete large experimental aircraft was unavailable. The hangar therefore uses **labelled 6 m-span experimental study models** with structural spars and membrane silhouettes. These are dimension proxies/display models, not new flyable aircraft. The existing flight planform also documents a 1.44 m-span default bird, substantially smaller than the hangar clearance.

## Rebuild

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

It captures seven views (bay, front, aerial, rear hangar, torii/island, forecourt and balcony close-up) as `Saved/Screenshots/WindowsEditor/CAMPUS_Shiomori_sunny_0.png` through `_6.png`. It traces the central hangar opening, roof, forecourt, launch field and garden path, alongside existing surf/audio checks. Run the shoreline regression separately with `test-shiomori.ps1 -NoRender`.

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