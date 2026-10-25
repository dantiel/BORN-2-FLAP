# Yúnshā (云沙) — "the Cloud-Sand Gorge"

> **Design intent & narrative:** see [world-design.md](world-design.md) — what
> this world *is*, why it exists, and what could be developed next. This
> document is the *canonical concept*: it locks the name, the register, the
> lesson, the visual/audio palette, and the three signature monuments before any
> geometry is generated.
>
> **Cosmology:** Yúnshā is the **Age of Iron (玄铁)** — the present-day
> threshold of the Ages cosmology (Hesiod's *Works and Days*, re-imagined as a
> map threaded by time and space). Its Sky-Temples are the gates to the five
> elder realms — the Jade Kingdom 玉京, Bronze 赤铜, Silver 素银, Gold 鎏金,
> and the Source 鸿蒙. See **The Ages**, below.

---

## Name lock

- **World label (menu):** `Yúnshā` (云沙)
- **Lore subtitle / mood:** *the Cloud-Sand Gorge* — a Gobi-like desert corridor
  where a crescent spring feeds a temple garden in the sand's heart, and red
  sandstone pillars rise into a cloud-forest that only winged things can reach.
- **Internal level id:** `Yunsha` (`ABorn2FlapGameMode::GetLevelId()` will
  return `"Yunsha"`; a new `bDesertLevel` flag).
- **Saved map:** `/Game/Yunsha/Maps/YUNSHA`.

**In-world etymology (fiction, not history).** `Yún` (云) = *cloud*; `Shā` (沙)
= *sand*. **Yúnshā = "cloud-sand"** — the two halves of one horizon: the
sand-sea of the desert floor below, and the cloud-sea of the pillar forest
above. The name is invented and references no real place, exactly like
*Shiomori Bay*.

Alternatives considered (trivially renamable — one string in `GetLevelId()` +
the menu catalog): *Xiáyún* (霞云, "sunglow cloud" — echoes the red danxia
rock), *Shāquán* (沙泉, "sand spring" — honours the oasis), *Yúnyá* (云崖,
`Yúnshā` was chosen because it holds the whole world in two
syllables — the sand below and the cloud above.

---

## The Ages — six realms threaded through one gorge

The frame is Hesiod's *Works and Days* (Gold → Silver → Bronze → Heroic →
Iron), re-imagined as a **map** instead of a history. Each age is one **sky**
(its locked time) + one **architecture** (its symbolic space) + one unassailable
theme. Yúnshā — the **Age of Iron (玄铁)** — is the present-day gorge that
*remembers* the five elder ages, each surviving as one place on the map, each
reachable **only by flight**.

The thread of time is a **climb**: altitude is age. The gorge floor is the
present (Iron); the higher you fly, the older the world becomes — up through the
Jade oasis, the Bronze cliff, the Silver mirrors, the Golden peaks, to the
Source beyond the cloud. *Going back to the source* is the world's deepest
invitation, and the Sky-Temples are its gates.

| Realm | 名 Name | Sky (time) | Space (architecture) | Home on the map |
| --- | --- | --- | --- | --- |
| the Source | 鸿蒙 Hóngméng | beyond time — no sun | unformed cloud | the standing cloud + sky above the pillars |
| Gold | 鎏金 Liújīn | eternal noon | golden peaks, sun-temples | the pillar summits — 鎏金顶 Liújīndǐng, "the Gilded Peaks" |
| Silver | 素银 Sùyín | eternal moonlight | mirror-lakes, mirror-halls | the monsoon wadis — 素银镜湖 Sùyín Jìnghú, "the Silver Mirror-Lakes" |
| Bronze | 赤铜 Chìtóng | storm-dawn | bronze cliffs, forge-cathedrals | the Grotto Cliff — 赤铜崖 Chìtóngyá, "the Bronze Cliff" |
| Jade | 玉京 Yùjīng | dreamlike lamp-light | jade palaces & shrines | the oasis — Lunya 轮亚 "the Disc of Atlantis" · 琼宫 Qiónggōng "the Jade Palace" |
| Iron | 玄铁 Xuántiě | mortal time — dawn/noon/dusk, weather | the gorge itself | all of Yúnshā (the present) |

Three of these are Pteronaut-built (Jade, Bronze, Gold); Silver is revealed by
the monsoon, the Source by altitude, and Iron is the present itself.

**Jade — the oasis.** The oasis and its temple are the **Jade Kingdom (玉京
Yùjīng)**, the blessed sanctuary — Hesiod's *Heroic* age, which he calls *more
just and better* than the Bronze that came before it. The crescent spring is
named **Lunya (轮亚) — "the Disc of Atlantis"** — the crescent recast as the
jade moon-wheel that sank with the golden source and resurfaced here (*Luna* +
亚 yà, the opening glyph of 亚特兰蒂斯 *Atlantis*; 轮 lún the moon-disc, the
玉轮 "jade wheel" of poetry, whose sliver *is* the crescent). The temple is
**琼宫 Qiónggōng —
"the Jade Palace"**. The old name "Moon-Spring Garden" was not wrong — the moon
is itself jade (玉盘 the jade disc, 玉兔 the jade hare, 玉轮 the jade wheel) —
and *Lunya* keeps the moon-crescent even as 亚 binds the spring to the sunken
source — the *return to the source* that Yúnshā threads: one material, one
colour, one dream.

**Gold — the summits.** The pillar summits, blazed by eternal noon, are the
**Age of Gold (鎏金 Liújīn)** — the **Gilded Peaks 鎏金顶 Liújīndǐng**, where
the Pteronauts raised **sun-temples**. Everything soars beneath their glow.

**Bronze — the cliff.** The red carved cliff — the **Grotto Cliff (the Wind-Harp)**
— is the **Age of Bronze (赤铜 Chìtóng)**, the **Bronze Cliff 赤铜崖
Chìtóngyá**: the forge-cathedrals of a hundred cave-mouths, red as copper in
the storm-dawn light.

**Silver — the mirror-wadis.** The dry monsoon channels are the **Age of Silver
(素银 Sùyín)**: only when the monsoon returns do they flash to **Silver
Mirror-Lakes 素银镜湖 Sùyín Jìnghú** — the one age you can *remember but never
hold*, because its architecture is a reflection that vanishes with the water.

**The Source — the cloud.** Above the peaks, the perpetual standing cloud is the
**Source (鸿蒙 Hóngméng)** — the unformed vastness before the first sunrise. No
sun, no ground, only becoming. To go back to the source is to fly into the
cloud and beyond.

**Iron — the gorge.** Yúnshā itself is the **Age of Iron (玄铁 Xuántiě)** — the
present, mortal time: dawn and noon and dusk, season and weather, the Canyon
Mouth and the Narrows. It keeps the memory of all five elder ages, the way the
Iron Age keeps the memory of Gold.

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
own invented tongues. `Lunya` (like `Yúnshā`, `Yǔ-ka`, `Hūnyā`) *reads* as
Chinese because the canyon-folk's tongue is this story's analogue of Chinese —
but each is a made-up word, not a real name, and not the Atlantean original.
Among all these echoes, the eastern sand-corridor keeps the **oldest known**:
the earliest surviving record of the Atlantean sanctuaries, preserved by the
first dynasty of that corridor when every other culture had already forgotten
the source.

| Later echo (fictional site) | The Atlantean original it dimly remembers |
| --- | --- |
| **The Qarāsh Caves** — a cliff of painted cave-temples in the deep sand-corridor | the Grotto Cliff (the Wind-Harp): a mile of carved, mural-painted cave-mouths, each a pipe the desert wind plays |
| **Lunya 轮亚, the Disc of Atlantis** — singing dunes around a crescent oasis | the Moon-Spring Garden: the only water, ringed by dunes |
| **The Śul ancestor-halls** — monumental rock-carved shrines | the carved scale and the winged-figure reliefs |
| **Yǔ-ka, the Mist-Pillars** — misty pillar mountains with cloud forest | the Cloud-Forest Pillars: sheer sandstone summits wrapped in cloud |
| **Hūnyā, the Air-Perch** — a shrine cantilevered into a cliff, reached by no path | the "flight-only" principle, pushed to its limit: no ground path at all |

---

## Real-world inspiration (sites as flavour, never as names)

The world borrows its *aesthetic grammar* from real Chinese excavation and
landscape sites, but every in-game name is invented — the same rule that keeps
`Shiomori Bay` Japanese-flavoured yet fictional. The sources of flavour:

| Inspiration | What Yúnshā takes from it |
| --- | --- |
| **Mogao Grottoes / Dunhuang (莫高窟)** — cliff of painted cave-temples in the Gobi | the Grotto Cliff: a mile of carved, mural-painted cave-mouths |
| **Mingsha Shan + Crescent Spring / Yuèyáquán (鸣沙山·月牙泉)** — singing dunes around a crescent oasis | Lunya 轮亚, the Disc of Atlantis spring (瑶池 "the Jade Pool"): the only water, ringed by dunes |
| **Yungang / Longmen / Dazu grottos (云冈·龙门·大足) ** — monumental rock-carved shrines | the carved scale and the winged-figure reliefs |
| **Zhangjiajie sandstone pillars / Wulingyuan (张家界)** — misty pillar mountains with cloud forest | the Cloud-Forest Pillars: sheer sandstone summits wrapped in cloud |
| **Xuankong Si, the Hanging Temple (悬空寺)** — shrine cantilevered into a cliff | the "flight-only" principle, pushed to its limit: no ground path at all |

Explicitly **excluded**: rice terraces (Longji/Yuanyang) — the user has ruled
them out; the world's agriculture is nothing, its water is one spring.

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

## The Pteronauts — winged colonists of lost Atlantis

The world's lore is fictional from the root, but it is anchored to one name the
project already knows: **Atlantis (亚特兰蒂斯)**. Twenty thousand years ago —
long before any written history — the **Pteronauts** were the winged citizens
of a seafaring island-civilisation that sank beneath the sea. Before the fall
they rode the world's winds outward, and wherever they found a place no
foot-traveller could reach, they built a **sky-temple**: a marker addressed to
the sky, a refuge, a shrine.

They left no cities — only sanctuaries, and only in the places flight could
enter. **Yúnshā** is one of them: a desert gorge at the far edge of a vast
sand-corridor, where the Pteronauts left three marks on the stone:

1. **Lunya 轮亚, the Disc of Atlantis (the Oasis) — the Jade Kingdom 玉京.** A
   crescent spring in the desert's heart — the only water for a hundred miles —
   ringed by the Jade Palace 琼宫 (Qiónggōng), a classical Chinese temple, and a
   walled
   garden of borrowed scenery. The garden is composed so that every view from
   within it frames a pillar of the cloud-forest. It is the world's serene
   centre — the place to which all flight returns.
2. **The Bronze Cliff 赤铜崖 — the Grotto Cliff (the Wind-Harp).** A mile-long
   cliff hollowed into hundreds of carved cave-temples, painted inside with
   murals of wing and wind. They were not tombs — they were *song-holes*: the
   mouths were shaped so the wind would play them.
3. **The Gilded Peaks 鎏金顶 — the Cloud-Forest Pillars (the Sky-Temples).**
   Red sandstone pillars whose summits carry a forest wrapped in perpetual cloud
   — the "mist mountains". Their walls are sheer; no stair, no trail, no ramp
   reaches the top. The Pteronauts built their sun-temples on the summits
   precisely so that **only flight could enter** — and above the peaks, the
   standing cloud is the Source 鸿蒙. Reaching one unassisted was the rite of
   passage for every young Pteronaut; in the game it is the world's central
   challenge and its crown POI.

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

**The oasis (the Jade Kingdom 玉京).** The single green note: the crescent
spring — Lunya 轮亚, the Disc of Atlantis — a band of jade and cinnabar roof-tiles,
the Jade Palace 琼宫, pale garden walls, a few desert willows. At night the
temple's
lanterns are the only warm light for miles.

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
  pilot flies past and sees only the honeycomb).
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

- **Lunya 轮亚, the Disc of Atlantis spring** — the default launch; the Jade Palace
  and garden of the Jade Kingdom 玉京, the serene centre.
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
2. **The Wind-Harp.** A procedurally scattered honeycomb of cave-mouths (ISM
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

## Procedural implementation - revised 24 October 2026

`create_yunsha_map.py` builds `/Game/Yunsha/Maps/YUNSHA` from seed **221022**.
The 2.4 x 2.8 km gorge contains Lunya's excavated crescent spring, the Jade
Palace precinct, an 860 m Wind-Harp mountain with 60 recessed mouths and wing
reliefs, the Narrows, five freestanding temple summits at 230-610 m, and a
cloud-forest canopy with local volumetric mist. Distant dunes extend the sand-sea
beyond the playable gorge. The summit launch shortcut is deliberately absent: the
Cloud-Forest launch starts below the first peak so reaching the perch remains
a flight challenge. There are no rice terraces or routes up the pillars.

The native `ABorn2FlapYunsha` height sampler supplies the generator's terrain
vertices and flight ground fallback. Saved mesh collision includes roofs,
grotto floors and summit perches. The runtime director supplies rising dust
streamers, five widening thermal cores with surrounding sink, direction-aware
ridge lift, a Narrows venturi, proximity/speed-dependent Wind-Harp sound and
summit bells. Dry, monsoon and cool seasons derive atmospheric differences;
mirror-wadis appear only during monsoon. Dawn/sunset reveal the sun-glass wings,
noon fades them and sunset/night add an afterglow. These are authored artistic
responses, not a spectral-material simulation.

The pterosaur is model 5: a distinct crested body, long bill and articulated
membrane silhouette. It reuses model 2's existing membrane-wing aerodynamics;
this is not a newly validated reconstruction of prehistoric flight. Its model
selection and native/Ruby menus are wired alongside the existing birds.

### Generate and check

Windows:

```powershell
$env:UE_ROOT = "V:/UE_5.8" # Adjust to your Unreal installation.
./Tools/generate-maps.ps1 -Maps yunsha
& "$env:UE_ROOT/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" Tools/test_yunsha.py --time sunset
```

macOS (native UE 5.8 and its supported Xcode toolchain required):

```bash
UE_ROOT="/Users/Shared/Epic Games/UE_5.8" bash Tools/generate-maps.sh --maps yunsha
UE_ROOT="/Users/Shared/Epic Games/UE_5.8" python3 Tools/test_yunsha.py --time sunset
```

The generated map, meshes, materials and WAV imports are ignored by Git and
listed in `Tools/generated-content.json`; release scripts generate them before
cooking. The generator uses only Unreal's Python plus the standard library.
No NumPy, Windows imaging tools, personal Downloads folder or remote asset
fetch is required. `--no-render` runs native collision and wind checks without
a GPU. Rendered checks capture eight views under `Saved/Screenshots`.

### Assets and provenance

Yunsha now uses retained, reusable architecture under
`Content/WorldAssets/Yunsha` (Git LFS): a scanned temple hall by 3Dystopia,
a detailed Chinese Buddhist pagoda by Aaron Huo, and a weathered gateway by
mervemacit, all CC BY 4.0. A Poly Haven rock-face scan supplies broken cliff
shoulders and exposed bedrock (CC0). Exact source URLs, attribution, license,
download hashes and modifications are in `Content/ThirdParty/Yunsha-assets.json`.
The hall's photographed road context and the pagoda's separate ground plane
were removed. Original UVs and materials remain intact. These are architectural
references adapted to a fictional landscape, not reconstructions of their real sites.

The imported packages are source inputs; generated world placements, terrain,
materials, reliefs and audio remain excluded from Git. Retained source art is
shared across instances rather than duplicated per building.
Normal Windows/macOS generation needs no downloads or extra Python packages.
For art maintenance only: `fetch_yunsha_sources.py` verifies pinned downloads,
`prepare_yunsha_sources.py` uses NumPy to bake transforms and trim scan context,
and `import_yunsha_assets.py` imports the resulting models in Unreal.

Terrain, wing reliefs and pterosaur geometry remain original procedural work.
Wind, harp and bell audio remain original deterministic PCM synthesis. Sand,
rock-face and concrete textures and Nature vegetation reuse retained CC0 inputs.
Keep the third-party credits with release bundles.

### Composition revision (24 October 2026)

The earlier arcade and cube-built pavilions were rejected visually. Their
technical checks below describe an obsolete art pass, not visual acceptance.

- Jade: a fourfold-area crescent lagoon beside the Moon Jade Palace: three
  progressively larger halls, open paifang gateways, and four winding scholar
  gardens with bonsai collections. The palace axis is on the eastern dry bank;
  the lake promenade follows the crescent rather than a square foundation.
- Iron: low dunes and a long open flight across the basin. Outer sand ridges
  rise roughly 15?35 m instead of the old 165 m wall, keeping the main sites lit.
- Bronze: sixty irregular excavated grottoes in five clusters within one
  860 m long mountain, with broad uncarved ribs and a deep western mass.
  The former 256-cell arcade, block plinths and repeated crown columns are gone.
- Silver: the riverbed opens again after the cliff; seasonal pools draw the
  eye toward the narrow passage and the ascent beyond.
- Gold: fewer intervening spires leave air between the five high sanctuaries.
  Full timber pagodas, forest margins and scanned rock shoulders replace the
  small two-roof placeholders. The summit approaches remain flight-only.
- Source: mist and the empty sky finish the ascent. Do not fill every gap with
  another monument; distances and changing elevation carry the narrative.

The concept's future vocal Grotto Choir station remains future work; the world
currently has its own non-radio environmental wind, harp and bell soundscape.
Mac command construction is portable, but a native Mac run is not available on
this Windows machine.

The generation and validation tools keep their legacy derived-data cache under
`Saved/DerivedDataCache` (unless `UE-LocalDataCachePath` is explicitly set),
while preserving an existing, valid Zen data path from project configuration. On
Windows, shader temporary files use `Saved/GenerationTemp`, avoiding reliance
on free space in the system drive's temporary directory.


### Earlier technical validation (22?23 October 2026; superseded art)

- Native Unreal 5.8 editor build: passed.
- Isolated checkout with no Yunsha content: generated, saved, reloaded and exited
  successfully (278.4 seconds, including cold asset compilation).
- Main checkout regeneration: saved and reloaded 444 actors, 256 wing reliefs
  and five flight-only summit perches; clean editor exit (313.7 seconds with
  the relocated cold cache). A subsequent input/output-hash check reused the
  cache without launching Unreal. After preserving the configured Zen store,
  the final full regeneration completed in 83.3 seconds.
- Dry sunset game run: passed terrain, grotto mouth/rear wall/floor, spring
  boundary, summit collision, thermal core/off, venturi, seasonal visibility,
  mural time response and pterosaur selection checks; six screenshots saved.
- Final noon/monsoon game run: all world checks passed, including the five
  seasonal mirrors and subdued noon pigments; six new captures saved.
- Final dry/morning run on 23 October: all world checks passed; six captures
  inspected, including the softened mist and distant sand-sea.
- Native macOS generation and packaged-release validation remain unexecuted.

### Moon Jade Palace expansion (24 October 2026)

The crescent is twice as wide and twice as long: outer radii 156/96 m,
with the inner cutout and its offset scaled identically about the old centre.
That makes the analytic water area exactly four times the previous area.
Terrain excavation, water mesh and runtime water queries share this mask.

The Hall of Arrival, Hall of Still Water and Moon Jade Great Hall have roof
heights of 45, 60 and 90 m. They use V?rldskulturmuseerna's detailed CC0
Wan fa Gui yi Hall model, adapted to this fictional palace. A continuous
axial opening is cut through actual door/wall triangles while preserving
UVs, floors, columns and ceilings. The resulting portals are approximately
7.1, 9.4 and 14.1 m wide. Three ornate paifang use Leo.Leung's CC BY model;
36 bonsai displays use zacharyfrs's CC BY bonsai, across four scholar gardens.
All additional credits and source hashes are in the same asset manifest.

Runtime verification sweeps a 4.4 m wide flight corridor through every hall
and paifang and probes each hall's real floor and ceiling. These checks prove
clear geometric passages; they are not an autonomous ornithopter piloting test.
Eight cameras include the great hall interior and the garden.

Expansion validation: the native Unreal build passed; the final map generation
saved and reloaded 405 actors in 71.2 seconds. Dry-morning and noon-monsoon
world runs passed the six passage checks (18 sphere sweeps), three actual
interior floors and vaulted ceilings, and the existing landscape/season tests.
The final interior exposure change was additionally verified in a fresh rendered
monsoon run: all three bounded exposure volumes passed, and the hall image
showed readable columns, floor and exit. Eight views were captured per run.
The source download hashes were verified. Native macOS and packaged release
execution remain untested on this Windows workstation.
