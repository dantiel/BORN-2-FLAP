# Neon Abyss — „the Aether Nursery"

> **Design intent & narrative:** see [world-design.md](world-design.md) — what
> this world *is*, why it exists, and what could be developed next. This
> document is the *canonical concept*: it locks the name, the register, the
> lesson, the visual/audio palette, and the capture-mode rules before any
> geometry is generated.

---

## Name lock

- **World label (menu):** `Neon Abyss`
- **Lore subtitle / mood:** *the Aether Nursery* — a primordial, eternal
  sanctuary where a baby turtle flies through a neon cosmos.
- **Internal level id:** `NeonAbyss` (`ABorn2FlapGameMode::GetLevelId()` returns
  `"NeonAbyss"`; new `bVoidLevel` flag).
- **Saved map:** `/Game/NeonAbyss/Maps/NEONABYSS`.

Alternatives considered (trivially renamable — one string in `GetLevelId()` +
the menu catalog): *Aether Nursery*, *The Dreaming Deep*, *Nebula Nursery*.
`Neon Abyss` was chosen because it reads cleanly beside `Valley` and
`Shiomori Bay` on the level-select menu, and it carries both the cosmic depth
(*abyss*) and the aesthetic (*neon*) of the world.

---

## One-sky justification

A BORN-2-FLAP world must teach a skill no existing world teaches, or express a
mood no existing world expresses. `Neon Abyss` does **both**:

- **New lesson — weightless aether flight.** Every existing world is
  terrestrial: gravity down, a ground plane, a horizon. In the Abyss there is
  no ground and no preferred "up" — the pilot *swims* through a dense, dreamy
  medium: buoyancy, drag, drift, and omni-directional control. Flap to pulse
  forward, glide to coast, and steer in any direction (including vertical and
  inverted). This is the game's first free-3D flight lesson.
- **New mood — a flying festival in a primordial dream.** A sensitive,
  adventurous baby turtle glows through a void of strange nebulae and planets,
  under a ring of ancient bronzes, chasing neon jellyfish that flutter like
  insects. It is weightless, luminous, and gently ecstatic — nothing else in
  the game shares this register.

| World | Register | Lesson |
| --- | --- | --- |
| Ravenstonefield | quiet autumn nature | free flight — flap, glide, turn, land |
| Shiomori Bay | civilization meets the sacred | coastal wind, updraft, precision around architecture |
| Training | the neutral gymnasium | gates, racing, precision, repeatability |
| **Neon Abyss** | **dreamlike neon cosmic festival — a primordial, eternal world** | **weightless aether flight — swim, drift, capture in motion** |

---

## Palette

**Void background.** Deep indigo/violet, near-black at the far edge; the
"water" is not water but *aether* — a translucent, faintly refractive medium
with volumetric fog.

**Nebulae & planets.** Soft teal→magenta nebula clouds (large billboard
sprites, additive), a handful of distant ringed planets, and sparse drifting
space-dust/plankton motes. Light behaves like it does underwater: gentle
caustic-like rays and long falloff.

**The turtle (sensitivity).** Light blue + turquoise body, with facial markings
that read as *sensitive* and *adventurous*. Shell and fins carry colorful neon
patterns (emissive) — the "flying festival" livery. Its face is a small
Sanxingdui mask: a gold-foil brow-and-cheek marking with the faintest
protruding-eye silhouette, so the baby turtle carries the primordial lineage
while staying soft and cute.

**The jellyfish.** Neon pinks, cyans and magentas, gently pulsing emissive
bells with trailing tentacle light.

**The bronzes.** A floating ring of Sanxingdui bronzes — monumental masks with
protruding pillar-eyes, a sacred tree, and sun wheels — green verdigris patina
with seams of emissive gold that brighten as the pilot draws near.

**Audio.** Ethereal, spacious — low drone + water-like swells, bright glassy
sparkles on capture, the turtle's soft finbeats. See
[flight-audio-design.md](flight-audio-design.md) for the aero-audio model.

---

## The Sanxingdui visual language

The art direction borrows from **Sanxingdui** (三星堆) — a Bronze-Age culture of
the Sichuan basin that appeared without precedent, left no written records, and
vanished as suddenly as it came. Its surviving bronzes feel *primordial and
eternal*: objects from before the world was named, floating outside human
history. That is exactly the register the Abyss wants — a place that has always
existed, suspended out of time.

Four visual anchors are lifted from Sanxingdui and re-lit as neon:

- **The mask.** The iconic bronze face — angular, geometric, with the
  exaggerated protruding "pillar" eyes (纵目). These masks are the level's
  signature shapes: monumental, watchful, unblinking.
- **The sacred tree.** The bronze tree of life, a vertical axis rising through
  the void, hung with birds and sun wheels — the world's *axis mundi*, a ladder
  between aether and cosmos.
- **The sun wheel.** The openwork sun disc with radiating spokes, recurring as
  a halo behind the masks and as a floating ornament in the cosmos.
- **The gold foil.** Thin beaten gold that once sheathed the bronze faces,
  re-expressed here as emissive gold leaf over turquoise-green patina.

The material grammar is **bronze + verdigris patina + gold**, not stone: green
weathered bronze with seams of glowing gold, so the ancient metal reads as both
buried (patina) and alive (neon). This replaces the earlier "weathered stone"
reading of the ring.

---

## Geometry

### The void

No ground plane. The world is a bounded, buoyant volume of aether. Instead of a
terrain height function, there is a gentle **current field** (the `Born2FlapWind`
analogue, but slow and three-dimensional) that the turtle, jellyfish and dust
all drift through. Fog and refractive depth cue the scale.

### The cosmos sky

A procedural backdrop: nebula sprites, planet spheres, star field. This is the
level's "sky" — but because there is no horizon, it surrounds the pilot on all
sides.

### The bronze ring (attraction / POI anchor)

A ring of tall Sanxingdui bronzes floating in the void — monumental masks with
protruding pillar-eyes, a sacred tree rising through the centre, and sun wheels
orbiting like halos. It is the level's orientation landmark and the natural
**launch/reset zone** (a POI). The gold seams brighten with proximity — a soft
"approach" signal. It anchors the eye in an otherwise unbounded space, giving
the player a "here" to return to — a primordial sanctuary that has always
floated here.

### The jellyfish field

Scattered throughout the void, the jellyfish are the level's population (see
below) and the capture targets.

---

## The ornithopter — the Neon Turtle

A fourth bird design (`BirdModel = 3`, `NeonTurtle`) beside RavenCrow /
Prototype / Kestrel, built through the same `Born2FlapRaven::Build(Design)`
path and flown by `ABorn2FlapFlightPawn`.

- **Body + shell.** A rounded, domed shell over a small body — "a very cute
  turtle baby". The shell's rim is banded like the lip of a Sanxingdui bronze
  vessel: a verdigris-green edge with a thin seam of emissive gold.
- **Fins instead of wings.** Two large front flippers (the flapping pair) and
  the hind fins as the tail (rudder/elevator). Same input: `W` = flipper-stroke.
- **Neon livery.** Emissive, colorful patterns on the shell and fins —
  procedural, matching the existing material architecture.
- **Face.** Light blue/turquoise hues with facial patterns that express
  sensitivity and adventurousness, over a subtle Sanxingdui mask structure — a
  gold-foil brow-and-cheek marking and a faint protruding-eye silhouette that
  reads as ancient, watchful, and gentle.

In the Abyss the turtle *swims*: the flight model gains buoyancy and drag and
loses the ground-normal, so the familiar flap/glide/turn becomes a full 3D
pulse-and-coast. (The turtle is expected to remain flyable in the terrestrial
worlds too — it is a bird model, not a level-locked pawn.)

---

## The neon jellyfish

A new swarm species in the void. Behaviourally it extends the existing
`Born2FlapInsectSwarm` model (territory, drift, ride the current) but is
visually distinct:

- **Bell + trailing tentacles**, flapping like insects (the bell pulses, the
  tentacles trail).
- **Emissive neon**, gently pulsing; a soft light trail.
- **Swarm layer** (cheap instanced specks at distance) + **heroes** (close-up,
  fully articulated jellyfish), mirroring the existing swarm/hero split.

Implementation is either a new `EInsectType::Jellyfish` species in
`Born2FlapInsectSwarm` or a dedicated `Born2FlapNeonJellyfish` actor for the
void level — decided at build time. Assets come from a `create_neon_assets.py`
generator, matching `create_insect_assets.py`.

---

## Capture game mode — „flying festival hunt"

A new Ruby ruleset (see [game-modes.md](game-modes.md)) layered on the
void's free flight. The turtle captures neon jellyfish **in flight**.

- **Goal.** Capture `N` jellyfish before the festival timer ends. Best score /
  time is recorded per level (`NeonAbyss`) like the racing best-lap.
- **Capture.** Fly *through* a jellyfish to net it — a soft scoop, not a
  collision. Captured jellyfish flash and dissolve into sparkles.
- **Scoring.** Count + speed bonus + combo streak (captures close in time and
  distance chain for a multiplier).
- **Feedback.** Audio sparkle, HUD counter/combo, the captured jellyfish's
  light trails to the turtle for a beat.
- **Ruleset contract.** A `CaptureRules` in the Brain exposing the narrow
  Ruby contract — `setup(session)`, `tick(session, telemetry)`,
  `on_event(session, event)` (capture), `score(session)`, `finished?(session)`.
  C++ supplies jellyfish positions/state via telemetry and the capture event;
  Ruby owns the rules, scoring and view-model (per
  [ruby-as-redstone.md](ruby-as-redstone.md)).

Capture is a *collection* mode, distinct from the existing *racing* modes —
new lesson, new rule shape.

---

## Build steps / provenance

Generated locally; no downloaded environment assets required. Following the
existing pipeline (`create_shiomori_map.py`, `create_ravenstonefield_map.py`):

1. **`Tools/create_neon_abyss_map.py`** — cosmos backdrop (nebula, planets,
   stars), aether fog/refraction, the Sanxingdui bronze ring (masks, sacred
   tree, sun wheels), dust motes, jellyfish spawn points. Run through Unreal's
   Python commandlet.
2. **`Tools/create_neon_assets.py`** — emissive neon materials, Sanxingdui
   bronze-ring meshes (verdigris + gold foil), turtle shell/fins, jellyfish
   body/tentacles.
3. **C++ world/level wiring** — new `bVoidLevel` flag + `"NeonAbyss"` in
   `ABorn2FlapGameMode::GetLevelId()`; a `Born2FlapNeonVoid` world actor (or
   the map generator spawns it) for the current field, fog and the bronze ring.
4. **C++ bird model** — `Born2FlapRaven::Build(Design=3)` turtle shell + fins;
   register `NeonTurtle` in `ABorn2FlapFlightPawn` and the spirit/ghost
   serialization (`FB2FSpiritRecording::BirdModel`).
5. **C++ jellyfish** — new swarm species (or dedicated actor) + capture
   collision/event.
6. **Ruby capture ruleset** — `Brain/lib/born2flap/rulesets/capture.rb` (or
   equivalent) + telemetry/capture-event hooks in `HandleBrainCommand` /
   `BuildBrainTelemetry`.

---

## Roadmap (Salomo)

1. ✅ **Design doc + name lock** — this document.
2. **Neon Turtle ornithopter** — 4th bird design (shell, fins, emissive livery).
3. **Aether void level + bronze ring** — cosmos, fog, planets, floating
   Sanxingdui masks, sacred tree, sun wheels.
4. **Neon jellyfish** — swarm species with emissive pulse + tentacle trail.
5. **Capture game mode** — flying-festival hunt (rules, scoring, HUD, audio).