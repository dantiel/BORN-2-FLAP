# Murex Spire Aerodrome - the sideways shell

Canonical world and architectural design, revised 2026-11-01.

**Murex is an airport made from one enormous shell lying on its side.** The
shell opening shelters the entrance and frames its own recessed glass facade.
The thick body whorl contains the main airport hall. Along the elongated,
horizontal axis, long spines become progressively smaller gate terminals. One
powerful upward spine beside the main hall becomes the control tower. A planted
entrance park sits on the public side; aircraft wait at the airside spine tips.

This supersedes the earlier concept of a conventional rectangular terminal with
an isolated shell-inspired tower. The shell is the organizing building, not an
ornament applied to a normal airport.

## Identity and architectural hierarchy

- Menu world: **Murex**.
- Full name: **Murex Spire Aerodrome**; mood: **the Living Hub**.
- Level: `/Game/Murex/Maps/MUREX`; deterministic seed: `290926`.
- Function: an airport with recognizable entrance, hall, gates, apron and runway.
- Form: a resting, elongated murex, with the broad aperture at the main body and
  a tapering sequence of shell growth and spines along its horizontal axis.

The first view must communicate the entire resting shell. The largest mass is
low and broad. Only one spine rises strongly above it. Gate spines stretch close
to ground level, and their widths, heights and lengths diminish toward the tip.
Unused spines are rounded stumps that retain the shell's character without
turning its silhouette into a forest of competing needles.

The building has four levels of visual importance:

1. The broad aperture, its pearl-colored rolled lip and the main hall behind it.
2. The single upward tower spine, physically rooted in the hall's shell shoulder.
3. The long, habitable gate spines along the coiled axial body.
4. Subdued growth ribs and small rounded nonfunctional spines.

## Plan and orientation

The horizontal shell axis runs approximately west-east, along world +X. The
aperture faces the public garden to the south (-Y). Gate spines extend north
(+Y) toward the apron and the parallel runway. These are architectural choices;
the design does not claim that every natural murex has exact golden proportions.

```text
                              NORTH / AIRSIDE
              runway and parallel taxiway, running west-east

                   aircraft    aircraft   aircraft   ...
                      |           |          |
                   long gate    smaller    smaller
                      \           |          |
             ATC       \__________|__________|___ tapering tip
              ^        coiled horizontal shell body
       [thick main hall]
       [arched glass front]
       (sheltering shell lip)

             planted entrance park and promenade
               curved public drop-off at its edge
                              SOUTH / LANDSIDE
```

The apron remains useful airport infrastructure. It must not become the dominant
architectural shape. Keep aircraft stands and taxi clearance on the airside;
keep the public park and approach to the doors clear of gate traffic.

## Aperture: entrance, canopy and window front

The opening belongs to the shell itself. Its asymmetric arch has a substantial,
rolled rim, a warm pearl underside and a broad sheltered threshold. The facade
follows that opening, rather than appearing as a rectangular glass box attached
to it. A darker bronze mullion grid provides human scale beneath the pale shell.

Recess the glass approximately 27 m behind the lip. This creates a readable
canopy and shaded forecourt before the entrance. Provide actual openings at
pedestrian level, with the promenade aligned to them. The facade must not be an
opaque wall pretending to be glass, nor may a collision hull seal the entrance.

The thick body behind the aperture is the main airport building: arrivals,
departures and passenger distribution belong here. Visible mezzanine edges and
warm interior bands establish an occupied building behind the glass. The first
implementation is an architectural interior shell, not a fully furnished or
simulated passenger terminal.

The broad main roof rises roughly 60 m, over a front opening about 300 m wide.
Its rear contracts and merges with the axial shell body. Avoid a repeated barrel
roof, detached entrance pavilion or generic rectangular terminal mass.

## Longitudinal shell and gate spines

The main whorl flows into a coiled, tapering horizontal body. Growth seams wind
around that body; the shell remains visibly continuous between gate roots. A
long spine is a habitable extension of this body, with a tapered roof, floor,
clerestory glazing and a glazed end. It must not read as a freestanding corridor
placed beside a separate sculpture.

Build six gate spines in the initial layout. They start broad and long near the
main hall and become smaller toward the end. The broadest is approximately
162 m long; the smallest is approximately 78 m. Clearances impose a practical
minimum width and root spacing, so tapering does not make outer gates unusable.

At every gate, provide:

- A continuous route from the shell body through the spine to its gate end.
- A short enclosed boarding bridge visibly connected to the aircraft-door area.
- A marked stand with room for the aircraft's wings, tail and nose.
- A correctly proportioned parked aircraft, with fuselage, swept wings,
  tailplane, fin, engines, cockpit, cabin windows and landing gear.

The initial aircraft are fictional static scale references, not particular real
airlines or certified aircraft models. Lengths decrease from 62, 52, 44, 36 and
29 m to 24 m; corresponding spans are 60, 46, 38, 31, 25 and 22 m. Their geometry
must stay full scale relative to gates, trees, cars and benches. Their positions
must leave the taxiway clear. Static parked aircraft do not imply that arrival,
taxi or departure AI is implemented.

## One dominant tower spine

The largest upward spine grows from the thick shoulder beside the main hall.
It shares the shell material, taper and growth ribs with the rest of the airport.
Its root overlaps the structural shell; there is no detached tower plinth across
the runway and no independent cylindrical tower merely decorated with spiral fins.

The spine curves gently upward into a compact glazed control cab, with a
landable roof around 117-118 m above the apron. A small gallery around 84 m gives
a lower perch. Keep these functional elements subordinate to the spine silhouette.
The cab's framing and glass make the airport function readable at close range.

Only this upward spine is tall and strong. Other upward/sideward remnants are
short and softly rounded. Long ground-level spines are reserved for usable
terminal routes. No additional tower-height decorative spikes are permitted.

## Fibonacci and golden-angle construction

Use exact derived constants, not a rounded angle hidden in several functions:

```text
phi = (1 + sqrt(5)) / 2
alpha = 2*pi / phi^2 = approximately 137.507764 degrees
```

The rule is an architectural growth system, applied consistently at useful scales:

| Element | Construction rule |
| --- | --- |
| Axial shell radius | Geometric decay with phi along normalized axial distance |
| Coiled surface | Continuous winding with restrained secondary corrugation |
| Main-hall growth ribs | Fibonacci index rhythm: 0, 3, 5, 8, 13, 21, 34 |
| Gate sequence | Golden decay of length, height and width, with functional minimums |
| Gate spacing | Decreasing golden intervals plus a minimum clearance allowance |
| Small unused spines | Golden-angle distribution; short, rounded terminations |
| Park trees | Golden-angle distribution filtered against paths and vehicle access |

Current geometric seed formulas, in metres, are:

```text
axial radius(t) = 73 * phi^(-5.25*t), 0 <= t <= 1
axial position(t) = (-1240 + 690*t, -390, varying shell height)
gate length(i) = 62 + 100 * phi^(-0.75*i), i = 0..5
gate width parameter(i) = 7 + 19 * phi^(-0.8*i)
next root spacing(i) = 60 + 85 * phi^(-i)
```

These formulas establish rhythm; they do not override spatial requirements.
Ground planes, doors, aircraft clearances and accessible approach paths remain
explicit constraints. Do not scatter long spines blindly at every golden angle.
The direction and role of each functional spine are deliberate.

## Park-like entrance

The public arrival area should feel sheltered and planted. Use a low, irregular
oval green space, curved pale-stone walks, grouped trees, seating, warm pedestrian
lights and a restrained drop-off loop. The main promenade leads directly under
the shell lip toward the door openings.

Keep tree canopies away from the facade and the central approach, so the aperture
remains legible from the park. Use mixed green tones and varied tree heights
around 8-11 m. Planting should form groves rather than a rigid grid. Benches and a
few ordinary-sized cars establish the scale of the much larger shell architecture.

Place the vehicle loop at the park perimeter. Avoid a vast hard forecourt,
symmetrical palace garden, raised rectangular platform or parking lot directly
in front of the entrance. The park should meet the airport terrain close to grade.

## Material, light and atmosphere

- **Shell exterior:** warm ivory mineral surface, subtle fine variation and
  restrained sandy growth seams. Keep ribs close in value to the shell.
- **Lip and soffit:** lighter pearl/cream, with enough thickness to cast a real
  sheltered shadow and distinguish the aperture from a painted arch.
- **Glass:** muted blue-green with visible bronze framing; daylight reflection
  and partial transparency reveal the hall behind it.
- **Ground:** pale warm paving in the public area, darker working apron and
  runway on the airside.
- **Park:** green groves and low planting against the broader dry airport field.
- **Aircraft:** mostly light bodies, dark glazing and modest fictional bronze/gold
  tail accents. Aircraft provide scale without becoming the visual centerpiece.
- **Light:** clear, warm daylight. Preserve shadow detail under the opening;
  atmospheric haze must not erase the terminal sequence.

The environment may retain its wider airport horizon, hangars, wetlands, radar
masts and perimeter details. Those elements remain background infrastructure.

## World function and future gameplay

The world remains *the Living Hub*: an inhabited airport where the ornithopter
can eventually patrol and guide bird flocks away from active approach corridors.
The shell's gallery and tower roof provide observation/perch destinations, while
its canopy and gate sequence provide low-level precision routes.

Bird-herding, airport traffic scheduling, runway thermals, operational deterrents
and seasonal migration are future gameplay systems. Their existence must be
verified separately before documentation or UI describes them as functioning.
Current architectural props, laser-looking emitters or parked aircraft do not
establish a working simulation of those systems.

## Procedural implementation and regeneration

The recipe has two layers:

- [murex_shell_geometry.py](../Unreal/Born2Flap/Tools/murex_shell_geometry.py):
  engine-independent surface geometry, functional spine layout, aircraft,
  park placement and landmark coordinates; all dimensions are metres.
- [create_murex_map.py](../Unreal/Born2Flap/Tools/create_murex_map.py): Unreal
  materials, collision, instancing, terrain/infrastructure, camera views and map
  saving; conversion to centimetres occurs at this boundary.

Aircraft parts are merged by aircraft/material; repeated mullions, trees and
park fixtures are instanced. Keep the geometry lightweight enough for flight.
The map must contain the generated surfaces, not merely a changed source recipe.

Generate with the full project Editor and its Python/editor scripting plugins:

```powershell
& 'V:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'V:\BORN 2 FLAP\Unreal\Born2Flap\Born2Flap.uproject' `
  '-ExecutePythonScript=V:\BORN 2 FLAP\Unreal\Born2Flap\Tools\create_murex_map.py' `
  -unattended -nosplash -nosound -nullrhi
```

Close the editor/game before regeneration to release asset locks. Require
`MUREX_MAP_READY revision=sideways-shell-v2` and `MUREX_MAP_SAVED`; then reload the
saved map in a separate process. Unreferenced legacy assets may remain available
for rollback, but the saved level must not contain the former detached tower or
repeated rectangular terminal bays.

[Capture and validation script](../Unreal/Born2Flap/Tools/capture_murex_views.py)
loads the saved map, checks its architectural components and collision, and
renders six camera views with `-RenderOffscreen`. Use the full Editor with
`-ExecutePythonScript`, without `-nullrhi`, for those captures. Reports and images
are written to `.setup/murex-redesign/`.

Named F8 destinations include the runway head, aperture park, shell entrance,
tower cab, gallery and outer gate spines. Their coordinates must follow the
rebuilt geometry rather than the previous detached tower location.

Run the separate runtime/flight check with the saved Kestrel profile used for
the current physics validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  Unreal/Born2Flap/Tools/test-murex.ps1 -NullRhi -BirdModel 2 `
  -Preferences Unreal/Born2Flap/Saved/Config/FlightPreferences.ini -PhysicsHz 240
```

`-NullRhi` checks runtime behavior without rendering. Omit it to request the two
flight-camera screenshots. The six architectural views remain a separate check.
The flight reset assertion compares with the selected launch point, not world
origin: Murex's runway head is 1.55 km west of the origin.

## Acceptance views and checks

A rebuild is accepted only after the saved map is reloaded and these views are
inspected. Passing a geometry count or saving successfully does not establish the
intended appearance.

| View/check | Required visible result |
| --- | --- |
| Three-quarter overview | One shell lying along its horizontal axis; broad main whorl, tapered body, gate sequence and one upward tower |
| Park approach | A planted public entrance and a deep canopy formed by the shell opening |
| Aperture close view | Recessed arched glazing, substantial rolled lip, readable window grid and actual entrance gaps |
| Airside view | Six attached, diminishing terminal spines, six scale aircraft, boarding connections and clear taxiway |
| Tower view | A strong spine integrated into the shell shoulder, glazed cab and usable roof/gallery |
| Outer gates | Smaller terminal forms and aircraft; unused spines remain rounded stumps |
| Collision | Walkable approach, landable roof/gallery and gate roofs; canopy/door space not filled by an enclosing collision hull |
| Runtime | Murex loads with the rebuilt map, updated destination positions and no material compile or fatal errors |

Record geometry, saved-map, collision and rendered results independently. If a
view still reads as a conventional airport plus a decorative shell tower, the
architecture has failed this design even if all scripts and builds succeed.

## Built revision and verification, 2026-11-01

The `sideways-shell-v2` map has been generated, saved and reloaded in a separate
Editor process. It contains the main aperture hall, tapering axial shell, six
gate spines, six static aircraft, one dominant tower and an entrance park with
48 trees. Architectural geometry uses 232 meshes, 102,288 triangles and 914
instances; the complete saved level has 380 actors including infrastructure.

All six Unreal camera images were rendered and inspected. The aperture is
recessed beneath the shell lip, the terminal sequence decreases toward the tip,
and the tower grows from the shell shoulder. These establish the implemented
layout; visual approval against the intended shell character remains the user's.
The interior and planting are procedural architectural representations, and the
aircraft are static props.

| Verification | Result |
| --- | --- |
| Editor Development build | Succeeded |
| Saved map reload and component counts | Pass |
| Tower roof / gallery collision | 117.75 m / 84.65 m |
| First gate roof collision | 11.93 m at the midpoint probe |
| Entrance walk / runway collision | 0.10 m / 0.00 m |
| Entrance clearance | Unblocked horizontal trace from promenade through door opening at 1.8 m height |
| Aircraft clearance | All stands clear the taxiway; nearest aircraft envelope has 14.67 m clearance |
| Murex runtime with saved 50 g Kestrel profile, 240 Hz solver, 60 FPS test | 110-second FlightTest PASS, zero math failures and safety resets |

Evidence is under `.setup/murex-redesign/`: `generated-scene.json`,
`saved-map-validation.json`, six numbered PNGs, `editor-build.log` and
`runtime-saved-profile.log`. An initial unconfigured flight-test run failed with
a math rejection; `runtime-default-preset.log` preserves that result. The passing
runtime result applies to the explicitly selected saved Kestrel configuration,
not every aircraft or test preset.
