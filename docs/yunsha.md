# Yúnshā — "the Cloud-Sand Gorge"

> **Design intent & narrative:** see [world-design.md](world-design.md) — what
> this world *is*, why it exists, and what could be developed next. This
> document is the *canonical concept*: it locks the name, the register, the
> lesson, the visual/audio palette, and the three signature monuments before any
> geometry is generated.

---

## Name lock

- **World label (menu):** `Yúnshā`
- **Lore subtitle / mood:** *the Cloud-Sand Gorge* — a vast desert corridor
  where a crescent spring feeds a temple garden in the sand's heart, and red
  sandstone pillars rise into a cloud-forest that only winged things can reach.
- **Internal level id:** `Yunsha` (`ABorn2FlapGameMode::GetLevelId()` will
  return `"Yunsha"`; a new `bDesertLevel` flag).
- **Saved map:** `/Game/Yunsha/Maps/YUNSHA`.

**In-world etymology (fiction, not history).** `Yúnshā` is the *modern*
name — a word of the later canyon-folk, given long after the fall. In their
tongue `Yún` means *cloud* and `Shā` means *sand*. **Yúnshā =
"cloud-sand"** — the two halves of one horizon: the sand-sea of the desert
floor below, and the cloud-sea of the pillar forest above. It is an invented
name for a real thing, exactly like *Shiomori Bay* — **and it is not the
Atlantean name.** The original Atlantean name for this colony was lost with
the empire itself; what survived is only the place, and the later word the
canyon-folk found for it.

Alternatives considered (trivially renamable — one string in `GetLevelId()` +
the menu catalog): *Xiáyún* ("sunglow cloud" — echoes the red rock), *Shāquán*
("sand spring" — honours the oasis), *Yúnyá* ("cloud cliff"). `Yúnshā` was
chosen because it holds the whole world in two syllables — the sand below and
the cloud above.

---

## Later echoes (a parallel world, not ours)

Atlantis fell roughly **20,000 years ago** — long before any later high civilization of
the world rose: before the first dynasty of the eastern sand-corridor, the
first city of the western sea, the first written word anywhere. This is a
parallel world: every place, people and dynasty in it is invented, and no name
here is a real one. So the sites below are not Yúnshā's *sources*; they are its
**descendants** — what an Atlantean–Pteronaut sky-temple *devolved into* after
eighteen millennia of forgetting, surviving as faint echoes in the cultures
that grew up long after the empires returned to dust, the same way Phaidros's flight
survives as the later legend of the wax-winged flier. The arrow of influence
runs one way: **the place is the original; the later east inherited it.** The
name `Yúnshā` is not the original — it is the modern word the descendants
gave the gorge, the original Atlantean name being long forgotten.

The later names below are the modern words of the corridor's peoples, in their
own invented tongues. `Lúnyā` (like `Yúnshā`, `Yǔ-ka`, `Hūnyā`) *reads* as
Chinese because the canyon-folk's tongue is this story's analogue of Chinese —
but each is a made-up word, not a real name, and not the Atlantean original.
Among all these echoes, the eastern sand-corridor keeps the **oldest known**:
the earliest surviving record of the Atlantean sanctuaries, preserved by the
first dynasty of that corridor when every other culture had already forgotten
the source.

| Later echo (fictional site) | The Atlantean original it dimly remembers |
| --- | --- |
| **The Qarāsh Caves** — a cliff of painted cave-temples in the deep sand-corridor | the Grotto Cliff (the Wind-Harp): a mile of carved, mural-painted cave-mouths, each a pipe the desert wind plays |
| **Lúnyā, the Spring-of-the-Moon** — singing dunes around a crescent oasis | the Moon-Spring Garden: the only water, ringed by dunes |
| **The Śul ancestor-halls** — monumental rock-carved shrines | the carved scale and the winged-figure reliefs |
| **Yǔ-ka, the Mist-Pillars** — misty pillar mountains with cloud forest | the Cloud-Forest Pillars: sheer sandstone summits wrapped in cloud |
| **Hūnyā, the Air-Perch** — a shrine cantilevered into a cliff, reached by no path | the "flight-only" principle, pushed to its limit: no ground path at all |

Explicitly **excluded**: the terraced paddy-mountains of the later east — the
user has ruled them out; the world's agriculture is nothing, its water is one
spring.

---

## One-sky justification

A BORN-2-FLAP world must teach a skill no existing world teaches, or express a
mood no existing world expresses. `Yúnshā` does **both**:

- **New lesson — invisible air, read and ridden.** Every existing world treats
  air as a *condition* (a wind vector you trim against). In Yúnshā the air is
  the *terrain*. The pilot must learn to read lift the way a hiker reads a
  slope: **thermals** (columns of rising heat off sun-baked sand), **ridge
  lift** (wind forced up a windward canyon wall), and **venturi wind** (air
  squeezed through the narrows). Altitude is *earned*, never given — the climb
  from the oasis to the cloud-forest summits and back is the world's natural
  circuit.
- **New mood — awe, solitude, and the sacred in a place older than flight.**
  Every existing world is either inhabited (Shiomori Bay) or gentle
  (Ravenstonefield, Neon Abyss). Yúnshā is **empty and immense**: bone-white
  light on the sand, cloud on the summits, dust the only weather, the wind the
  only living voice. It is the first world that feels *holy* — a pilgrimage,
  not a playground.

| World | Register | Lesson |
| --- | --- | --- |
| Ravenstonefield | quiet autumn nature | free flight — flap, glide, turn, land |
| Shiomori Bay | civilization meets the sacred | coastal wind, updraft, precision around architecture |
| Training | the neutral gymnasium | gates, racing, precision, repeatability |
| Neon Abyss *(planned)* | dreamlike neon cosmic festival | weightless aether flight — swim, drift, capture in motion |
| **Yúnshā** *(planned)* | **vast, ancient, sun-scorched sacred silence** | **thermal + ridge soaring — read invisible air, earn altitude, land on a cloud-summit** |

---

## The Pteronauts — winged colonists of a world empire

The world's lore is fictional from the root, but it is anchored to one name the
project already knows: **Atlantis**. In this story Atlantis was
already a **world empire twenty thousand years ago** — fifteen millennia before
the first dynasty of the eastern sand-corridor, the first city of the western
sea, the first written word. Its
reach was planetary: not one island, but a civilisation whose ships and wings
had touched every coast and every wind. When the continent sank, what the world
kept was not its cities (those are gone) but its **sanctuaries** — the
sky-temples its winged citizens, the **Pteronauts**, had raised in every place
no foot-traveller could reach. Each is a marker addressed to the sky, a refuge,
a shrine.

The [Pteronaut Saga](http://darographix.de/2026/07/15/the-pteronaut-saga/)
records *one* departure — the Dodekapteroi flying **west** beyond the Pillars of
Heracles to a landing the later peoples called "the Place of the Thunderbirds."
That was the western branch of the diaspora. **Yúnshā is the eastern branch**:
a sky-temple colony at the far edge of a vast sand-corridor, raised in the same
era, by the same hands. Two continents, two echoes of one fall.

Because every written culture postdates Atlantis, the original names are lost;
the Archive names each colony by its descendant culture's name for it
(Thunderbirds in the west, **Yúnshā** in the east). Those later names are
themselves faint, distorted loanwords of the Atlantean originals.

They left no cities — only sanctuaries, and only in the places flight could
enter. In Yúnshā the Pteronauts left three marks on the stone:

1. **The Moon-Spring Garden (the Oasis).** A crescent spring in the desert's
   heart — the only water for a hundred miles — ringed by a temple whose
   bracket-set roofs and walled garden of borrowed scenery would, millennia
   later, be imitated by the first builders of the eastern sand-corridor. The
   garden is composed so
   that every view from within it frames a pillar of the cloud-forest. It is the
   world's serene centre — the place to which all flight returns.
2. **The Grotto Cliff (the Wind-Harp).** A mile-long cliff hollowed into
   hundreds of carved cave-temples, painted inside with murals of wing and wind.
   They were not tombs — they were *song-holes*: the mouths were shaped so the
   wind would play them, a harp of stone the desert breathes through. The
   Qarāsh Caves are what this cliff became after the memory faded.
3. **The Cloud-Forest Pillars (the Sky-Temples).** Red sandstone pillars whose
   summits carry a forest wrapped in perpetual cloud — the "mist mountains".
   Their walls are sheer; no stair, no trail, no ramp reaches the top. The
   Pteronauts built their sky-temples on the summits precisely so that **only
   flight could enter**. Reaching one unassisted was the rite of passage for
   every young Pteronaut; in the game it is the world's central challenge and
   its crown POI.

> **Design principle — "flight-only architecture".** The Sky-Temples are the
> purest expression of the world's lesson. Their geometry must make a ground
> approach *impossible* (sheer pillar walls, a gap between pillar and canyon
> rim, the cloud-forest canopy hiding the perch) and a flight approach *legible*
> (a thermal or ridge line that delivers you to the summit). No invisible walls;
> the *terrain* is the gate.

---

## Palette

**The desert.** Warm sandstone — ochre, terracotta, umber — cut by bone-white
sun. Long shadows at dawn and dusk; at noon the stone bleaches pale and the
heat-shimmer makes the air itself visible. The floor is sand and wind-rippled
dune; the narrows pinch into a slot canyon where the venturi sings.

**The oasis.** The single green note: the crescent spring, a band of jade and
cinnabar roof-tiles, pale garden walls, a few desert willows. At night the
temple's lanterns are the only warm light for miles.

**The cloud-forest.** Above the desert line, the pillars turn green. Their
summits are wrapped in a standing cloud — the one place in the world where the
sun is soft and the air is wet. The forest canopy is the sky-temples' roof.

**The sky.** A vast, dry, pale-blue-to-brass sky over the sand; a pale grey
ceiling over the summits. Heat-haze and dust motes are the only clouds in the
dry season; the sun is a hard, bleaching presence that doubles as the *light
source for the grotto murals*.

**The murals (the signature light-reactive pigment).** The Wind-Harp murals are
painted in a fictional pigment — **"sun-glass"** — that responds to the sun's
*spectrum*, not just its angle. Because the game already hermetically derives
sun colour by time-of-day, weather and season, the same mural has **three
lives**:

- **Golden hour (dawn/dusk):** the warm low spectrum reveals the **Wing Murals**
  — the Pteronauts in flight — in full colour.
- **High noon:** the hard white light bleaches them to near-bare stone (the
  pilot flies past and sees only the bare stone mouths).
- **Dusk, briefly:** after the sun drops, the pigment releases a faint stored
  glow — a pale ember in each cave-mouth, like a canyon full of banked coals.

This is *stage art, not simulation*: no "mural brightness" slider. The player
is simply told — by the radio, by a POI card — *"come back at dusk."* Abundance
from derivation, exactly per `seasons.md`.

**The bird (pterosaur).** The player flies a **pterosaur** — the living
Pteronaut form. Long membranous wings, a fine crest, a long counterweighted
head. Its livery is desert-faded: ochre and bone with a thin line of sun-glass
pigment along the wing membrane that flares faintly at dusk, tying the player to
the murals' palette. (New bird model + aero-audio; see below.)

**Audio.** Vast and hollow. A low desert drone; wind that *pitches* as you pass
the cave-mouths (the Wind-Harp "choir" rises with airspeed, an aero-audio
extension of `flight-audio-design.md`); the sudden hush of a thermal you are
centred in; the wet hush of the cloud-forest canopy as you break into it; a
single distant bell-tone when a Sky-Temple comes into view. Future: a dedicated
**Grotto Choir** radio station — see below.

---

## The flight lesson, in motion

The world is built to *teach invisible air*, in order:

1. **Read the shimmer.** Thermals are made legible by heat-shimmer (mirage),
   dust-devils, and rising sand-motes — the existing wind-mote / insect-swarm /
   wind-leaves systems repurposed as **lift streamers**. A pilot who learns to
   read the shimmer learns to find the column.
2. **Earn the ridge.** The windward canyon wall produces ridge lift; the
   leeward side drops you. The natural line is a figure-eight along the rim.
3. **Thread the narrows.** The slot canyon compresses the wind into a venturi —
   fast, and a *requirement* to hold energy for the next climb.
4. **Break the cloud line.** The pillars rise out of the thermals into the
   cloud-forest. Punching through the standing cloud is the moment the world
   changes register — sand to canopy, silence to wet wind.
5. **Land the Sky-Temple.** The summit perch is the precision test: a small,
   sheer, high landing zone reached only by a clean approach.

The tuning surface stays **simulation-only**: wind (strength/gusts/thermals) and
model tuning remain the exposed knobs. Everything above is derived, hidden,
stage art.

---

## Season & weather facets (hermetic)

Yúnshā gets its own hidden season table (same `FBorn2FlapSeason` shape as
`seasons.md`), so the *same* gorge reads as several different sacred places:

| Season | Desert facet | Thermal strength | Sun & air |
| --- | --- | --- | --- |
| **Dry** | dust haze, pale sky, dune drift | **strongest** | hard white noon, long shadows |
| **Monsoon** | rare green flush, flash-flood wadis (water!), thunderheads | variable, gusty | bruised indigo sky, humid air |
| **Cool** | clear cold dawns, still air | **weakest** (endurance lesson) | long low light, breath-fog at dawn |

Weather *behaviour* (wind, gust, thermal) remains simulation — tunable. The
*consequences* (haze, flood, green flush, sun spectrum, cloud-forest density)
are hermetically derived and never exposed.

---

## Radio (future, seasonal)

Per the seasonal-soundtrack direction, Yúnshā will carry a dedicated station:

- **The Grotto Choir** — the Wind-Harp's own voice: a slow wind-and-stone drone
  that subtly repitches with the live wind, plus the Pteronauts' "song" — a
  sparse, far-off vocalised phrase heard only near the Sky-Temples.
- Seasonal colour: dry = hollow and bright; monsoon = low, wet, rolling; cool =
  clear, cold, minimal.

---

## POI plan (named launch/reset points, `F8`)

- **Moon-Spring Garden** — the default launch; the oasis temple and garden, the
  serene centre.
- **Canyon Mouth** — where the first thermal teaches the lesson.
- **The Wind-Harp** — the grotto cliff; fly-past POIs at several cave-mouths.
- **The Narrows** — the slot canyon venturi.
- **The Cloud-Forest Pillars** — the mist mountains, one sky-temple per summit;
  the crown POI and precision-landing perch, reachable **only by flight**.
- **The Dune Sea** — the open sandbox edge for free thermal play.

---

## Build plan (scripted, deterministic — `Tools/create_yunsha_map.py`)

Follows the established generator pattern (`create_shiomori_map.py`,
`create_neon_abyss_map.py`):

1. **Terrain + materials.** A gorge heightfield (sand, red sandstone cliff,
   wind-rippled dune, slot-canyon walls) with a crescent-spring oasis and a
   band of green around it; world-space triplanar materials, sun-glass emissive
   pigment for the murals.
2. **The Wind-Harp.** A procedurally scattered field of carved cave-mouths (ISM
   instancing for coverage over detail) on the cliff face, with sun-glass mural
   quads inside each mouth.
3. **The Sky-Temples.** Freestanding sandstone pillars + a cloud-forest canopy
   on each summit, carved so no ground path exists; a small perch landing zone
   and the Wing Mural interior per temple.
4. **World actor + GameMode wiring.** New `bDesertLevel` flag, `GetLevelId()`
   → `"Yunsha"`, `?Level=Yunsha` detection, weather/season defaults,
   `bPterosaur` model flag.
5. **Menu catalog + artwork.** Menu card, `Desert` artwork key, pterosaur bird
   select, and a POI card teaching *"come back at dusk."*

Record assets and provenance in this doc's appendix when geometry lands (Poly
Haven CC0 sand/rock, etc.), per the contribution checklist in
`world-design.md`.