# Murex Spire Aerodrome — "the Living Hub"

> **Design intent & narrative:** see [world-design.md](world-design.md) — what
> this world *is*, why it exists, and what could be developed next. This
> document is the *canonical concept*: it locks the name, the register, the
> lesson, the visual/audio palette, the signature architecture (the murex-spine
> control tower), and the procedural layout before any geometry is generated.
>
> **Cosmology:** Murex Spire Aerodrome is a **working hub** — a traditional but
> modern international airport where jets, turboprops and cargo aircraft land
> all day. Where Yúnshā remembers the Age of Iron in stone and Neon Abyss
> dreams the aether, the aerodrome is the *machine age still breathing*: a busy
> field of concrete, glass and jet wash, whose single flourish is a control
> tower shaped like one thorned murex spine. But the field carries one living
> instrument no jet can replace — the **ornithopter**, flown here for a single
> job: keep the bird flocks off the runways and stop a bird strike before it
> happens.

---

## Name lock

- **World label (menu):** `Murex`
- **Full name:** *Murex Spire Aerodrome*
- **Lore subtitle / mood:** *the Living Hub* — a working international airport,
  traditional in layout and modern in design, where the ornithopter flies one
  job: herd the bird flocks off the runways and keep every landing and takeoff
  clear of strikes.
- **Internal level id:** `Murex` (`ABorn2FlapGameMode::GetLevelId()` will
  return `"Murex"`; a new `bAirportLevel` flag).
- **Saved map:** `/Game/Murex/Maps/MUREX`.

**In-world etymology (fiction, not history).** *Murex* is the sea-snail genus
whose shell grows long, thorned spines along its windings. The aerodrome's
architect gave the **control tower** the shape of a single such spine — so the
tower *is* the "murex spire": a landmark, not a whole shell. The name
references no real place, exactly like *Shiomori Bay* and *Yúnshā*.

Alternatives considered (trivially renamable — one string in `GetLevelId()` +
the menu catalog): *Vortex Spire* (echoes the runway-thermal lesson but loses
the shell reference), *Aero-Shell Haven* (the shell, but implies a fantastical
building), *Helix Aerodrome* (the spiral, but generic). `Murex` was chosen
because it holds the world in one word — the spine, the tower, the sea, and the
one architectural flourish that makes the field memorable.

---

## Real-world inspiration (sites as flavour, never as names)

The world borrows its *aesthetic grammar* from real airports and biology, but
every in-game name is invented — the same rule that keeps `Shiomori Bay`
Japanese-flavoured yet fictional. The sources of flavour:

| Inspiration | What Murex takes from it |
| --- | --- |
| **Murex / Muricidae sea-snail spines** — long, ribbed, thorned varices | the control tower: a slender ribbed shaft tapering to a glazed cab |
| **Modern ATC control towers** — glazed cab, cylindrical/conic shaft | the tower's *function*: it is a real control tower with a spiral-rib motif, not an alien horn |
| **Modern terminals** — long concourse, curved/ribbed roof, glass curtain wall | the terminal's *massing*: a normal building with a real window grid |
| **Fibonacci / phyllotaxis** (nautilus, sunflower) — golden-angle growth | the tower's spiral rib placement and the terminal's recursive bay subdivision |

Explicitly **excluded**: any real airport name, airline, or landmark. The
aerodrome is a pure invention; its *type* (a busy international hub) is the
only borrowed reality.

---

## One-sky justification

A BORN-2-FLAP world must teach a skill no existing world teaches, or express a
mood no existing world expresses. `Murex` does **both**:

- **New lesson — bird-strike prevention: herd the flock, clear the runway.**
  Every existing world is about flying *yourself*. Murex is about flying
  *against other flyers*: the player must intercept and steer bird flocks —
  cut off their line, bend a murmuration, route them into the deterrent grid —
  so that every approach and departure stays clear. The skill is **herding**:
  read a flock's heading, position to turn it, and hold a live runway corridor
  empty while the next jet lands.
- **New mood — the hum of a working hub.** Shiomori Bay is inhabited;
  Ravenstonefield is gentle; Yúnshā is holy. Murex is **alive and busy**: a
  field that never sleeps, where the ornithopter is one aircraft among many —
  not a monument, but a participant in an ordinary, ongoing sky.

| World | Register | Lesson |
| --- | --- | --- |
| Ravenstonefield | quiet autumn nature | free flight — flap, glide, turn, land |
| Shiomori Bay | civilization meets the sacred | coastal wind, updraft, precision around architecture |
| Training | the neutral gymnasium | gates, racing, precision, repeatability |
| Neon Abyss *(planned)* | dreamlike neon cosmic festival | weightless aether flight — swim, drift, capture in motion |
| Yúnshā *(planned)* | vast, ancient, sun-scorched sacred silence | thermal + ridge soaring — read invisible air, earn altitude, land on a cloud-summit |
| **Murex** *(planned)* | **the hum of a working hub** | **bird-strike prevention — herd the flock, clear the runway, ride machined thermals, land on the tower** |

---

## A working aerodrome — the ornithopter's one job

Murex is a **normal, functioning airport**: jets, turboprops and cargo aircraft
land and take off all day. There is no post-jet premise, no forbidden airspace,
no fantasy. The ornithopter has exactly **one job** on the field: it is the
airport's **bird-scarer** — the living instrument of the bird-strike grid.

**The danger.** A single bird into a turbine during takeoff can down an
aircraft; a flock drifting through an approach lane is a disaster. Real airports
fight this with falconry, sound cannons, lasers and drones. Murex uses all of
these — and one more: an **ornithopter**, a machine that flies *like* a bird and
can out-fly and herd any flock off the runway, with no engine wake, no fuel, and
no risk of the bird turning on a hovering drone. It is the only bird-scarer that
is itself a bird.

**The loop.** A flock drifts toward the active runway → the pilot intercepts it,
cuts off its line, and turns it — steering the murmuration back toward the
wetlands and the deterrent grid — then returns to the tower as the next jet
clears. It is not a fight; it is **herding**, a skill no other world teaches:
read a flock's heading, position to turn it, hold the runway corridor empty.

The job rests on three ordinary, physical supports:

**1. The machined thermal.** Heat rising off the runway and apron — the same
real phenomenon that makes asphalt shimmer on a hot day — produces strong,
predictable updrafts. The ornithopter *rides* them to patrol altitude without a
single engine and without spending fuel, so it can loiter over the field all
day. The runway is the level's natural lift source.

**2. The bird-machine's authority.** A flock that ignores a laser will break for
a large, fast, wing-flapping silhouette — a predator. The ornithopter's shape is
its deterrent: it does not displace the birds by force, it *turns* them the way
a falcon turns a flock, by being what they instinctively avoid.

**3. The tower perch.** No conventional aircraft can set down on a control
tower, and no ground crew can see the whole field at once. The ornithopter can —
on the spiral gallery or the glazed cab — the observation post from which the
pilot spots a flock before it reaches the approach lane.

---

## The Murex Spire — control tower (architectural focus)

### Design concept

A **control tower shaped like a single murex spine**: a slender, ribbed,
slightly twisted shaft tapering to a glazed control cab. It is the one
architectural flourish of the field — but it stays *grounded*: a real ATC tower
with a spiral-rib motif, not a fantastical horn. The ribs are thin vertical
fins wrapped around the shaft at regular spiral intervals (murex varices), the
cab is a normal glass-and-steel room, and the whole reads as "a tower with
character", visible from every corner of the map.

### Mathematical blueprint (direct C++ / Geometry Script)

The tower is a **logarithmic-taper shaft** wrapped with golden-angle ribs:

```
r(h) = r₀ · e^(-k·h)          — shaft radius tapers from base r₀ to the cab
θ(h) = Tower_Twist · h        — the spiral rib pitch along the shaft height
rib(i) at angle φᵢ = i · 137.5°   — golden-angle phyllotaxis along the winding
```

- **Build order** (in `create_murex_map.py` or a `Born2FlapMurexTower` C++
  builder using `UGeometryScriptLibrary_MeshPrimitiveFunctions`):
  1. Extrude a tapered cylinder (or cone) for the shaft over `Tower_Height`.
  2. Instance `Tower_Spines` thin rib fins, each following the spiral at
     golden-angle intervals — a murex-varix armature, but scaled to read as
     architecture (ribs ≈ 0.15× shaft radius, not organic spines).
  3. Cap the shaft with a glazed **control cab** (a small octagonal
     glass-and-steel room) and a window band, so the top reads as a real tower.
- **Parameters** (deterministic, from the generator's seed): `Tower_Height`
  (default ~70 m), `Tower_Spines` (rib count, default 8), `Tower_Twist`
  (spiral pitch), `Cab_Glazing` (window density on the cab).

### The terminal — a real building, procedurally generated

The terminal is a normal, modern concourse — **not** fantastical, but still
fully procedural:

- **Massing via recursive subdivision.** A base concourse rectangle is split
  into `Terminal_Modules` self-similar bays; each bay's roof is a curved
  barrel/sawtooth profile with smaller ribs at depth `Fractal_Depth`. The
  "fractal" lives in the *roof form*, not in an alien structure.
- **A real facade.** A full **glass curtain wall** is instanced over each
  facade panel — a rectangular window grid at `Window_Density` — so the
  building unmistakably reads as an airport terminal with windows, not a shell
  or a solid mass.

### Gameplay integration

- **The tower's spiral ribs are a subtle obstacle and landing parcours.** Each
  rib carries a collision body; the player threads between ribs (precision) and
  may set down on the **spiral gallery** or the **control cab** (a perch no jet
  can reach — F8 POI marker).
- **The apron/runway thermals are the lift source.** The terminal roof and the
  runway's heat shimmer give the machined updrafts the lesson is built on.
- **The facade is realism, not gameplay** — instanced window panels, cheap at
  distance, no per-window gameplay.

### Material strategy (low-poly Nanite)

The buildings are low-poly by design but read as rich through **Nanite** and
**cheap tiling/instancing**:

- **Glass curtain wall** — a single instanced window-panel mesh + a thin-film
  glass material (subtle Fresnel tint, low roughness); one material, thousands
  of windows, no per-window cost.
- **Concrete / asphalt / steel** — coarse, tiled materials for the apron,
  runway, and tower shaft; no scans needed.
- **Tower ribs** — a restrained metallic accent (anodised steel) so the spine
  reads as architecture, *not* full chitin fantasy.
- All geometry is Nanite-ready; detail comes from tiling + instancing, keeping
  the solo-dev build lean.

---

## Palette

- **Light.** A warm, hazy sun; heat-shimmer over the apron; long shadows from
  the tower ribs. The sky is *busy* — contrails, a distant arrival, the glint
  of the control cab.
- **Colour.** Concrete grey and runway asphalt against the glass curtain wall's
  cool blue-green; white runway markings; the anodised steel of the tower ribs;
  the dull red/amber of the bird-strike grid's lenses.
- **Sound.** Tower radio chatter and distant jet wash; the *hiss* of a sound
  cannon and the murmur of a flock; the hollow wind through the tower's spiral
  ribs as you climb it.

---

## The flight lesson, in motion

The world's natural circuit: launch at the runway head → ride the **runway
thermal** (machined lift) up to patrol altitude → **spot a flock drifting toward
the approach lane** → **intercept and herd it** (cut off its line, turn the
murmuration, route it into the deterrent grid) → land on the **control cab**
(the bird-scarer's observation perch) → glide down and **clear the next runway
corridor** before the next jet lands. Every step teaches a distinct part of
*bird-strike prevention*: climb, spot, intercept, turn a flock, hold the
corridor empty, land.

---

## Season & weather facets (hermetic)

Weather derives from ordinary airport seasons, not from exposed knobs:

- **Clear season** — strong, steady apron thermals; sparse flock traffic;
  the lesson is pure *air and architecture*.
- **Migration season** — dense flock traffic through the flyway; the runway
  corridors fill with murmurations and the pilot's herding is the difference
  between a clear approach and a strike. The lesson is *herding under load*.
- **Storm season** — gusty crosswinds and low haze; the runway thermal becomes
  ragged and the tower ribs are harder to thread; flying by feel.

The player never tunes these individually; they select one **season** cue that
hermetically derives every hidden number, exactly per `seasons.md`.

---

## Radio (future, seasonal)

One looping track, as in Ravenstonefield. Mood: **working-hub ambience** — a
steady, echoing, processed-signal piece that recalls the hum of an airport
between departures: tower radio, jet wash, the murmur of a living field.
Seasonal variation follows the weather facets above.

---

## Procedural airport layout & ecosystem

### Runway & infrastructure (rule-based, from a central vector baseline)

All hard infrastructure derives from one **central vector baseline**:

- A single **baseline vector** (origin + yaw, default aligned to world +X)
  anchors the runway. The **runway** is a strip extruded along the baseline,
  length `Runway_Length` (default 3200 m) and fixed width.
- **Taxiways** are placed perpendicular to the runway at regular intervals,
  connected to a parallel taxi spine.
- **Safety fences** and **blast pads** are offset by fixed rules (a standoff
  distance from the runway edge), never hand-placed.
- The **control tower** (the murex spine) sits beside the runway head; the
  **terminal**, **hangars**, and **fuel farm** place radially from the baseline
  by deterministic rules.

### Avian hotspots (deterministic spawn points)

Three habitat types are scattered by the `Hotspot_Density` parameter and act
as **deterministic spawn points for bird flocks**:

- **Wetlands** — drainage basins and retention ponds at the field edges;
  waterfowl flocks.
- **Grasslands** — the mown-but-returning verges between taxiways; ground
  feeders and insectivorous flocks.
- **Radar masts** — tall lattice towers whose high perches host roosting
  raptor flocks and mark the flyway's altitude.

`Hotspot_Density` (default 1.0) scales hotspot count and flock size from a
quiet field up to a full migration pulse.

### Bird-management grid (landmarks + gameplay helpers)

Like any real airport, Murex manages its bird problem with standard deterrents
— placed by rule, serving as visual landmarks and **gameplay helpers**. The
ornithopter is the **living core** of this grid: the deterrents *shape* the
flock, the pilot *steers* it.

- **Laser towers** — anti-strike emplacements that sweep the flock lanes;
  flying through their beam is safe, but they **clear a flock corridor**
  briefly when the player routes a flock through them (a tool to open a line).
- **Sound cannons** — pulse emitters that steer murmurations; they bend a
  flock's course, letting the player redirect traffic around an obstacle.
- **Drone hangars** — launch bays and the visual anchor of the grid; they
  double as **landing pads** and F8 POI markers.

The grid is **never** a weapon against the player: it is the player's toolkit.
The whole system — lasers, cannons, drones, and the ornithopter at its centre —
exists to keep the runways clear, and the player is the one hand flying it.

---

## POI plan (named launch/reset points, `F8`)

- **The Runway Head** — the default launch; where the apron thermal begins.
- **The Tower Cab** — the control tower's glazed cab; the perch-landing summit.
- **The Spiral Gallery** — the tower's ribbed walkway; where the precision
  thread-and-land lesson lives.
- **The Approach Lane** — a radar-mast marker along the arrival corridor; the
  clear-the-corridor lesson.
- **The Wetland Basin** — the open-field flock sanctuary at the map edge; where
  herded flocks are routed to rest.
- **Hangar Row** — the bird-grid anchor and a flat landing pad.

---

## Build plan (scripted, deterministic — `Tools/create_murex_map.py`)

Follows the established generator pattern (`create_shiomori_map.py`,
`create_yunsha_map.py`):

1. **Terrain + horizon illusion.** A 4×4 km colliding heightfield (concrete
   apron, bleached grassland, wetland basins) inside a **10 km visual world**
   built from a collisionless horizon ring — see "Technical level
   specifications" below.
2. **The runway & infrastructure.** Rule-based extrusion from the central
   vector baseline (`Runway_Length`, taxiways, fences, blast pads).
3. **The control tower (murex spine).** The tapered shaft with golden-angle
   spiral ribs, the glazed cab, and the spiral-gallery perch.
4. **The terminal.** Recursive-bay massing with a barrel/sawtooth roof and an
   instanced glass curtain-wall facade (real windows).
5. **Avian hotspots + bird-management grid.** Wetlands, grasslands, radar masts
   (flock spawn points, scaled by `Hotspot_Density`); laser towers, sound
   cannons, drone hangars.
6. **World actor + GameMode wiring.** New `bAirportLevel` flag,
   `GetLevelId()` → `"Murex"`, `?Level=Murex` detection, weather/season
   defaults, and a menu card in `MenuWorlds[]`.
7. **Menu catalog + artwork.** Menu card, `Airport` artwork key, and a POI
   card teaching *"ride the runway, land on the tower."*

Record assets and provenance in this doc's appendix when geometry lands, per
the contribution checklist in `world-design.md`.

---

## Technical level specifications (code-only pipeline)

### World dimensions

The playable, colliding area is a fixed **4 × 4 km** square (16 km²),
origin-centred, matching the largest existing generator (Yúnshā ≈ 2.4 ×
2.8 km) and sized so a 3.2 km runway plus its clearways, taxiways and fence
standoffs fit with room for the wetland basins at the edges.

### Visual horizon illusion (10 km world, lean build)

The aerodrome *reads* as a 10 km world without simulating 10 km:

- **Playable core (4×4 km):** the colliding terrain, runway, tower, terminal,
  and gameplay emplacements — everything with physics and AI.
- **Horizon ring (5–10 km, no collision):** a collisionless `ISM`/`HISM`
  layer of far-field set dressing — distant terminal spires, a water tower,
  low hills, wind-turbine silhouettes, and a smudged city skyline — placed on
  a ring around the playable core.
- **Procedural skybox + atmosphere:** a procedural sky dome and height fog +
  atmospheric scattering blend the horizon ring into the sky, so the eye
  believes the world extends to the horizon.
- **Why this is lean:** the far field has **no collision, no AI, no gameplay**
  — it is a few hundred LOD'd instances and one sky material, giving the
  *illusion* of 10 km at the cost of a 4 km sim. The tower and terminal use
  Nanite + instancing for near detail; the far field uses the cheapest possible
  instancing.

### Deterministic generation (parameters)

All generation is code-only and seed-driven, structured at the top of
`create_murex_map.py`:

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `Seed` | int | `290926` | master `random.Random(Seed)` — every layout, hotspot and rib is reproducible |
| `Runway_Length` | float | `3200.0` | runway strip length (m) along the baseline |
| `Hotspot_Density` | float | `1.0` | scales wetland/grassland/radar-mast count and flock size |
| `Baseline_Yaw` | float | `0.0` | runway baseline orientation (deg) |
| `Tower_Height` | float | `70.0` | control-tower shaft height (m) |
| `Tower_Spines` | int | `8` | spiral rib fins (murex varices) along the shaft |
| `Tower_Twist` | float | `1.5` | spiral rib pitch (turns over the full height) |
| `Terminal_Modules` | int | `12` | recursive bays subdividing the concourse |
| `Fractal_Depth` | int | `2` | roof-rib recursion depth per bay |
| `Window_Density` | float | `1.0` | facade window-grid density |

Changing any parameter regenerates a *stable, deterministic* map; the seed is
the single source of truth for the whole layout.

---

## Procedural implementation

### Generate and check

`create_murex_map.py` builds `/Game/Murex/Maps/MUREX` from seed **290926** in
Unreal's Python commandlet, exactly like the other generators
(`UnrealEditor-Cmd -run=pythonscript -script=...create_murex_map.py`). A
`test-murex.ps1` checks: runway/apron collision, tower-rib collision and the
cab perch, apron-thermal upwash, flock spawn determinism (same seed → same
layout), and captures rendered views of the tower, the terminal facade, and the
horizon ring. All content is generated locally; no downloaded environment
assets are required.

### Assets and provenance

To be recorded when geometry lands, per the contribution checklist:
- **Glass curtain wall + concrete/asphalt/steel** — procedural materials (no
  scans), instanced window panels authored in-engine.
- **Far-field horizon ring** — low-poly primitive silhouettes, no scans.
- Any CC0/scan assets (e.g. wetland vegetation, fence/steel props) recorded
  with source here.