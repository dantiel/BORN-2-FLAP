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
| The Raven Watch | 290, 205 | Nearby inhabited gatehouse: fly-through arch, timber lookout, tiled roof, keeper's cottage and lanterns |
| Panel housing | -460 to -295, -370 to -422 | Four distant apartment slabs behind the forest, away from the flight meadow |
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
| F6 | Switch between the following bird camera and the last-landing ground camera |
| V | Choose chase or body-mounted FPV for the airborne camera |
| F7 | Hide/show flight HUD |
| F8 | Pause flight and open bird selection / signed mouse response sliders |
| F4 | Switch between Ravenstonefield and the training course |

The chase camera follows behind the bird with a level horizon. The low ground
camera stays beside the latest landing spot, turns to follow the bird and adds
subtle handheld drift and motion blur. It moves to the next landing location
only when the bird settles there; Space launches from that new location.
FPV is mounted ahead of the bird's head and inherits its bank/pitch, with a small
wingbeat vibration. V chooses chase or FPV while F6 returns to the same ground
observer. Camera switches do not affect flight physics. F8 also offers the
airborne-camera choice and remembers it. Fixed scenic views are no longer part
of the gameplay camera cycle; the old offline landscape capture command remains.

Mouse steering is always available alongside keyboard steering: mouse Y pitches;
mouse X adds yaw and roll. F8 provides independent roll, pitch and yaw gains from
−2 to +2: negative reverses direction, zero disables that mouse axis, and magnitude
controls sensitivity. The new default pitch gain is −1; roll/yaw default to +1.
At gain 1, 1800 pixels moves a stick from neutral to full travel, with fine-control
expo near the centre. Keyboard direction is unaffected by these mouse settings.
Mouse displacement sets persistent stick positions: pitch, yaw and roll hold
when movement stops. Click either mouse button to reset all three mouse axes to
neutral. Opposing keyboard input cancels the mouse contribution while held.
Hold left mouse to mute mouse yaw; hold right mouse to mute mouse roll.
The buttons leave keyboard steering active; holding both leaves mouse pitch active.

W commands 72% throttle, Ctrl/Strg+W 32%, Shift+W 100% (Ctrl wins if both are held).
The wheel latches throttle in 2% increments, including fractional wheel events.
Pressing W takes ownership; releasing W then returns to idle. The wheel remembers
its own setting throughout: wheel 50%, W, release, wheel up becomes 52%.
While W is held it wins, even if the wheel moves. R resets all desktop controls.
F2 toggles a compact live THR/PIT/YAW/ROLL display and the stored wheel value;
F7 hides/shows the entire HUD in either level. A configured USB RC transmitter
still has priority while enabled; F3 opens its setup panel.

### Selectable birds

RAVENCROW is the default: a folded, ray-like black fuselage, angular crow head,
rectangular wing panels and seven long parallel shard pinions on each wing.
Every visible surface is a flat-shaded triangle. Layered charcoal/navy facets
catch the daylight; the continuous triangular tail forms a shallow inverted V.
The original teal prototype is also selectable in F8. Selection changes the
visual airframe; both currently use the same provisional aerodynamic model.

F8 pauses the simulation while editing. Save & Return, Escape or F8 resumes
flight; changing mouse gains centres the stored mouse sticks. Model selection
and gains persist across restarts and level changes in
`Saved/Config/FlightPreferences.ini`. Flight reset does not erase preferences.
The procedural model is in `Born2FlapRavenCrow.cpp`; its material is reproduced
by `Tools/create_ravencrow_materials.py`.

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
`test-desktop-input.ps1` sends simulated key and axis events through Unreal's
PlayerInput and checks the resulting pawn channels, including throttle takeover
and wheel memory. `-Capture` also renders the channel HUD. The standalone CMake
`desktop_input_tests` target checks desktop input at 30/60/144 FPS.

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
