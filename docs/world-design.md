# World Design — Narrative, Intent & Roadmap

> **Orientation for contributors and AI agents.** This is the canonical map of
> *what each world is, why it exists, what it teaches, and what could be built
> next.* The per-level documents (`ravenstonefield.md`, `shiomori-bay.md`,
> `shiomori-campus.md`, `shiomori-volcanic.md`, `natural-valley-and-rc.md`) cover
> the *how* — geometry, assets, build steps, provenance. This document covers
> the *why*.

---

## The one-sky principle

A BORN-2-FLAP world is not a scenery pack. Each world is **one sky, one lesson**:
a single emotional register paired with a single flight-mechanic focus. A new
world is *justified* when it either

- teaches a skill no existing world teaches, or
- expresses a mood no existing world expresses.

This is the filter to apply to every "new world" idea. If a proposal duplicates
the lesson *and* the mood of an existing world, it is a reskin, not a world.

| World | Register | Lesson |
| --- | --- | --- |
| Ravenstonefield | quiet autumn nature | free flight — flap, glide, turn, land |
| Shiomori Bay | civilization meets the sacred | coastal wind, updraft, precision around architecture |
| Training | the neutral gymnasium | gates, racing, precision, repeatability |
| Neon Abyss *(planned)* | dreamlike neon cosmic festival | weightless aether flight — swim, drift, capture in motion |
| Yúnshā *(planned)* | vast, ancient, sun-scorched sacred silence | thermal + ridge soaring — read invisible air, earn altitude, land on a cloud-summit |

> **Naming note:** the natural valley world is called **Ravenstonefield** in the
> current menu (`/Game/Ravenstonefield/Maps/RAVENSTONEFIELD`, preview label
> "Valley"). Earlier docs (`natural-valley-and-rc.md`) and the runtime class
> `Born2FlapValley` call it **Waldtal / the valley**. They are the *same world*
> at different stages of development.

---

## The stage-art principle

A world's *physics* and its *atmosphere* are governed differently:

- **Simulation** — wind, weather profile, model tuning — is flight input. The
  pilot may tune it, in detail, because it is what the bird *feels*.
- **Stage art** — sun position and spectrum, fog, insect abundance and species
  mix, leaf density, seasonal radio — is the theatre around the physics. It is
  **never** exposed as individual knobs. It lives behind a single **season**
  cue that hermetically derives every hidden number.

The rule: *if it belongs to the physics, expose it; if it belongs to the
scenery, hide it behind the season.* Abundance comes from the derivation, not
from the configuration surface. See `seasons.md` for the full model.

---

## Existing worlds

### 1. Ravenstonefield — "the first breath"

**What it is.** A worn autumn river basin: low hills, cold green water, copper
foliage, weathered timber and overbuilt concrete. The Long Meadow is the quiet
centre; the meandering river (opening into a broad lake) is the spine that all
landmarks are strung along.

**Why it exists.** The low-stakes open sandbox. This is where a new pilot learns
the wingbeat — hold `W` to flap, release to glide, bank to turn, let the ground
meet you gently. Nature is the teacher; there is no timer, no fail state, only
the meadow and the river.

**Narrative register.** Quiet, melancholic-but-gentle. A landscape that has
watched time pass: the collapsed boathouse at Crow's Acre, the rusted debris and
dead trunk at Last Scrap, the inhabited Raven Watch gatehouse with its lanterns.
The world *implies* a lived-in past without stating it.

**Current state.** Geography complete (≈1.3 km × 800 m lake, 9 landmarks/POIs),
field radio (TURBORAVEN), raven/crow selectable birds, water landings return to
launch. See `ravenstonefield.md`.

**What could be developed next.**

- **Environmental storytelling.** Give the landmarks a voice. Why did the
  boathouse collapse? What is the Raven Watch watching? A radio broadcast, POI
  descriptions, or a short valley-tour flight could carry the lore.
- **A valley tour.** A guided flight visiting each landmark in order — the
  natural "introduction to free flight".
- **The lake as a challenge.** Broad reflective water with shallow shelves is
  currently just scenery; make it a landing/refuel zone or a low-level flyover
  course.
- **The distant panel-housing district.** Four apartment slabs exist behind the
  forest; they are a backdrop, not a place. Populate them or connect them.
- **Seasonal/time-of-day arc.** *Done.* Each world now carries a four-season
  cycle (autumn by default here); see `seasons.md`. Remaining mood work is the
  seasonal radio soundtrack and soundscape.

### 2. Shiomori Bay — "civilization meets the sacred"

**What it is.** A repurposed late-1980s seaside hotel turned ornithopter
university, on the west side of a bay; a volcanic island and sheltered bathing
lagoon; a coastal garden with a torii gate and a small shrine. The founder's
bronze statue stands in the former fountain forecourt.

**Why it exists.** The world *with a purpose*. It teaches coastal wind and
updraft (a 3.2 m/s onshore breeze produces a localized updraft above the stair
wall) and precision around architecture — flying through the hangar door, around
the campus, toward the island.

**Narrative register.** Pragmatic reuse coexisting with something older. The
adapted hotel is bulky and plainly weathered on its service side; the torii and
shrine are deliberately restrained, *accommodated by* rather than absorbed into
the campus. The founder's statue — a stylized study of "Obi-Wan Da Vinci Anakin
Chronister Rodriguez" — is an intentional, playful placeholder, **not** a
portrait. The university's research purpose is the narrative spine: this is
where ornithopters are *made and studied*.

**Current state.** Campus built (12 floors, hangar, forecourt, garden, torii,
shrine), volcanic island with tidal reefs, parhelion sky. See `shiomori-bay.md`,
`shiomori-campus.md`, `shiomori-volcanic.md`.

**What could be developed next.**

- **The founder's story / research program.** Why ornithopters? What does the
  university study? This is the clearest open narrative thread and would give
  the world a reason beyond scenery.
- **Interiors.** Most former guest rooms are closed blocks; open a few as
  walkable or fly-through rooms (the testing hall and panoramic flight lab are
  already visible).
- **The island as a destination.** The volcanic island is a visual goal but has
  no POIs or gameplay; make it a named, selectable destination.
- **The hangar as the editor's home.** The inland workshop hangar is the natural
  narrative home of the ornithopter editor — "bring your design to the flight
  field".
- **Weather.** A thermal off the volcanic rock; crosswind from the bay.

### 3. Training — "the school"

**What it is.** The flat practice ground with the gate-race course.

**Why it exists.** The mechanics gymnasium — gates that turn green when passed,
a timed race clock, ghost replay and best-lap records. This is where *precision*
and *repeatability* are drilled without the distractions of a living landscape.

**Narrative register.** Functional and neutral. The "gymnasium"; no story, by
design.

**Current state.** Gate-race timer, ghost replay, best-lap, POIs (Training
gates), weather excludes snow. See `natural-valley-and-rc.md` (RC controls) and
`game-modes.md`.

**What could be developed next.**

- **Flight-school lessons.** The 9-step curriculum in `game-design.md`
  (transmitter/neutral, flap frequency, straight flight, coordinated turns,
  launch/landing, wind/gusts, stall/recovery, race lines, manual tuning).
- **Generated racing tracks.** New gate layouts and race seasons on the same
  neutral ground — authored tracks, not an endless procedural run.
- **Precision-landing pads** and a **freestyle/trick arena** as separate zones
  of the same neutral ground.

---

## New worlds (proposals)

Each proposal names the mechanic it teaches, so a new world is a deliberate
addition — not "another map".

1. **High alpine ridge** — *thermal + ridge-soaring, altitude management.*
   "The high country." Lesson: reading rising air, energy from windward slopes.
2. **Desert canyon** — *thermal columns, endurance, precision.* "The oven."
   Lesson: finding lift, managing battery/energy, tight canyon lines.
   **→ Developed into `Yúnshā` — "the Cloud-Sand Gorge"** (see
   [yunsha.md](yunsha.md)): a wind-carved canyon hollowed into the singing
   Wind-Harp grottoes and crowned by the Sky-Temples, a cloud-summit perch
   reachable only by flight. Fictional throughout — no real place, no real
   name survives in the design.
3. **Tropical island chain / open ocean** — *long-distance, crosswind, storm.*
   "The open water." Lesson: navigation, energy budget, gust recovery.
4. **Urban rooftops** — *delivery/rescue, obstacles, tight precision.* "The
   grid." Lesson: controlled descents, precision landing, confined airspace.
5. **Volcanic highlands / winter** — *ice, low visibility, wind.* "The freeze."
   Lesson: flying by feel, gust recovery, orientation without a clear horizon.
6. **Night city / bioluminescent coast** — *FPV lights, night flight.* "The
   glow." Lesson: orientation without horizon cues, instrument discipline.
7. **Endless procedural sky** — *cross-country, energy management, flow.*
   "The horizon." Lesson: reading a biome gradient and riding whatever lift the
   terrain offers — thermals over baked rock, ridge lift on windward slopes —
   with no fixed goal and no map edge.

**Proposal template** (use when pitching a new world):

- **Name & one-line mood** — the emotional register.
- **Mechanic taught** — the "one lesson"; must not duplicate an existing world.
- **Why now** — what it unlocks for the player or the platform.
- **Geometry plan** — scripted (`Tools/create_*.py`) or assembled.
- **POI plan** — named launch/reset points (`F8`).
- **Weather + radio profile** — wind, light, and a music/radio track.

---

## Procedural richness (reference: slowroads.io)

`slowroads.io` — an endless browser driving game — feels "rich and diverse"
not from more authored assets but from a handful of procedural rules. They
transfer directly to flight:

1. **Elevation-driven biome blending (an authored tool, not an automatic
   gradient).** slowroads reads as many worlds because biome is a *continuous
   function* of elevation, moisture and temperature — meadow → forest → rock →
   snow → coast, with no hard seams. BORN-2-FLAP deliberately does **not** adopt
   an automated elevation gradient: biome and seasonal differences are
   **manually tuned per level**, so each world's register stays singular and
   curated rather than emergent.
2. **Dense instancing for perceived richness.** Thousands of cheap instances
   (trees, grass, rock) read as "lush" far more than a few detailed props.
   Unreal's ISM/HISM and Nanite foliage are the native tool; the lesson is
   *coverage over individual detail*.
3. **Natural flight lines laid along terrain.** slowroads' road follows the
   heightfield, so motion always has a natural path. The flight equivalent is
   placing thermals and ridge lift *where the terrain says they belong* —
   sun-baked south slopes and windward ridges — so a pilot who reads the land
   reads the air.
4. **Atmospheric coherence.** One strong mood (fog + color grade + sun angle)
   unifies every asset. BORN-2-FLAP already exceeds this (parhelion, surf,
   snow accumulation, per-world soundscapes); the discipline is to keep each
   world's register *singular* rather than everything-everywhere.

**Completion tie-in.** These rules are the concrete "what could be developed
next" for the existing worlds: denser instanced undergrowth and manually tuned
biome/seasonal dressing for Ravenstonefield; cliff-ridge lift and a manually
tuned biome transition (garden → forest → volcanic rock) for Shiomori Bay; and
generated racing tracks in the existing levels for Training (new gate layouts
on the same neutral ground — no endless run, no map edge). New-world proposals
above remain the *deliberate* way to add a genuinely new lesson.

---

## How to contribute a world

1. **Write the narrative first** (this document), then the technical doc (the
   level's own `.md`). Intent precedes implementation.
2. **Keep "one sky, one lesson."** If a world teaches nothing new, fold it into
   an existing world instead.
3. **Script geometry** with `Tools/create_*.py` so it is deterministic and
   reproducible; record assets and provenance (Poly Haven CC0, licensed scans,
   etc.) in the level doc.
4. **Add POIs** (`F8`), a **weather profile**, and a **radio/music profile** —
   a world is not complete until a pilot can *orient, launch, and listen*.