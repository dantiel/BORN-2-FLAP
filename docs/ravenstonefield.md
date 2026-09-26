# RAVENSTONEFIELD

A worn autumn river basin for BORN 2 FLAP. Low hills, cold green water,
copper foliage, weathered timber and overbuilt concrete. The long meadow is
the quiet centre of the level; landmarks reward flight along the river.

## Geography

The existing meandering river now opens into an approximately 1.3 km by
800 m lake. The main terrain uses a six-metre collision grid. The outer
landscape continues to 4.8 km from the origin; the surrounding hills remain
low rather than becoming an alpine range.

| Landmark | Position in metres (X, Y) | Purpose |
| --- | --- | --- |
| Long Meadow | 0, 0 | Original launch/reset point; open riverside flight corridor |
| The Old Barns | -160, -155 | Three timber barns; the largest has a fly-through opening |
| The Underpass | 185, river centre | Concrete road crossing, piers, service bunker, embankments |
| Village houses | 270, -155 | Three plaster-and-timber houses with shutters and tiled roofs |
| Crow's Acre | 520, 190 | Larger delta island with a collapsed boathouse |
| Last Scrap | 670, 95 | Smaller, poorer island with a dead trunk and rusted debris |
| The Raven Watch | 740, 725 | Small medieval keep, ruined courtyard and lake-facing steps |
| Raven Lake | 965, 245 | Broad reflective water with shallow shelves |

Deadwood, small weathered skeletons, rusted culvert hoops, a pier and field
fences provide optional routes around the open meadow. Tree trunks collide;
leaf canopies do not act as solid walls. Water landings retain the existing
return-to-launch behavior.

## Presentation and radio

- Unreal 5.8, DirectX 12 / Shader Model 6, Lumen lighting and reflections,
  virtual shadow maps, temporal super resolution, warm low sun and volumetric fog.
- Single Layer Water with scattering, absorption, animated ripple normals and
  shallow-water caustic patterns. Caustics are an artistic shader approximation,
  not photon-traced caustics or a new fluid simulation.
- Scanned CC0 building surfaces, broadleaf tree and dead trunk from Poly Haven.
  Autumn foliage uses instance variation; grass is sparse and dry near the field.
- A physical field radio stands on the workbench near launch. Its receiver is
  audible during flight. The RAVENSTONEFIELD playlist currently has one looping
  track: **TURBORAVEN**, imported from the supplied Downloads/TurboRaven.mp3.

| Control | Action |
| --- | --- |
| M | Pause/resume radio |
| [ / ] | Lower/raise radio volume |
| F6 | Cycle five scenic views, then return to flight camera |
| F7 | Hide/show flight HUD |
| F4 | Switch between Ravenstonefield and the training course |

Existing flight and RC controls are preserved. Scenic views change the camera;
they do not pause flight physics.

## Build and regenerate

1. Run `fetch_ravenstonefield.py` with Python to download the source assets.
2. Run `create_ravenstonefield_assets.py` using Unreal's Python commandlet.
3. Build `Born2FlapEditor Win64 Development` with the UE 5.8 `Build.bat`.
4. Run `create_ravenstonefield_map.py` using Unreal's Python commandlet.
5. Start `Unreal/Born2Flap/Tools/play.ps1 -Level Ravenstonefield`.

`play.ps1` first runs `build.ps1`, which incrementally builds the Haskell DLL,
copies it into Unreal's `Binaries/ThirdParty`, and rebuilds the editor module.
Both sides must use the same ABI after physics updates; an old DLL disables
flapping and hand launch. Close the game/editor before replacing a loaded DLL.
`test-flight.ps1` checks training flight at 30/60/144 FPS, a soak flight, and
hand launch plus wing travel in the saved RAVENSTONEFIELD map.

The map is `/Game/Ravenstonefield/Maps/RAVENSTONEFIELD`. Its world actor saves
generated components so the level can also be inspected in the editor. The
actor's **Build World** action regenerates those components deterministically.
`test-ravenstonefield.ps1` checks dry launch, island/water classification, terrain
collision and radio availability. `-Capture` saves five actual rendered views
to `Saved/Screenshots/WindowsEditor` for visual inspection.

## Asset provenance

The downloaded sources and checksums are recorded in
`Saved/RavenstonefieldSource/manifest.json`. Poly Haven assets are CC0:

- https://polyhaven.com/a/tree_small_02
- https://polyhaven.com/a/dead_tree_trunk
- https://polyhaven.com/a/castle_wall_slates
- https://polyhaven.com/a/clay_roof_tiles
- https://polyhaven.com/a/concrete_moss
- https://polyhaven.com/a/clay_plaster
- https://polyhaven.com/a/weathered_planks

Existing grass, fir, rock and terrain textures retain the provenance documented
by `fetch_nature_assets.py`. TURBORAVEN is user-supplied music, separate from the
CC0 assets and the source-code license. This task does not publish the track.
