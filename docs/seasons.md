# Seasons — the year as stage art

> **Hermetic principle.** A season is a single visible *cue*, not a set of
knobs. Choosing one season re-derives the whole living world — the sun's
path and spectrum, the air, the insect community, the foliage, the weather's
temperament and (soon) the radio — from a handful of hidden numbers. The
player never sees those numbers. They are **s****cenic s****tage art**: the lighting and
dressing of a theatre, not the physics of the stage.

---

## The split: simulation vs. stage art

BORN-2-FLAP draws a hard line through its configuration surface:

| Layer | What it is | Configured by | Examples |
| Simulation | The rules the bird actually feels | explicitly, in detail | wind (direction, strength, gusts, randomness, thermals), weather profile, model tuning |
| Stage art | The atmosphere that surrounds those rules | one knob that hermetically derives everything else | sun position & spectrum, fog, insect abundance & species mix, leaf density, seasonal radio |

The rule of thumb: if a parameter belongs to the physics, expose it. If it
belongs to the scenery, hide it behind a season. This is what makes the
world feel *alive* rather than *adjusted*. A pilot tunes the wind because wind
is a flight force. Nobody tunes the number of moths — the season simply
*brings* them, because that is what a season does.

---

## The season is a single knob

Each open world carries a **season** (Spring / Summer / Autumn / Winter).
It is a per-world choice, saved like weather and time of day, and it is the
*only* decorative control the player ever touches.

| World | Default season | Why |
| Ravenstonefield | Autumn | its register is "quiet autumn nature" — copper foliage, haze, a leaf-storm |
| Shiomori Bay | Summer | the coastal university under a high, bright sun |
| Training | Spring (fixed) | the neutral gymnasium — no season selector, no weather to speak of |

Training is hermetically *locked* to one season: it has no `SeasonOptions`
beyond the single fixed value. The gymnasium does not change its costume.

---

## What a season silently derives

Everything below is **hidden** — resolved in code from the season id, never
shown as a slider. The table is the full current truth (`FBorn2FlapSeason`,
`World/Born2FlapWeatherProfiles.cpp`).

| Facet | Spring | Summer | Autumn | Winter |
| Sun azimuth (yaw bias, deg) | −12 | +8 | +15 | −20 |
| Sun elevation (bias, deg) | +4 | +12 | −8 | −16 |
| Sun lux (multiplier) | 1.05 | 1.20 | 0.85 | 0.60 |
| Light tint (spectrum) | warm-green (1,.97,.88) | golden (1,.91,.76) | copper (1,.76,.52) | cool blue (.72,.83,1) |
| Sky intensity | 1.10 | 1.20 | 0.95 | 0.80 |
| Fog bias (density) | −.002 (clarity) | 0 | +.006 (haze) | +.004 |
| Exposure bias (stops) | +.15 | +.30 | −.10 | −.25 |
| Insect factor (overall) | 1.35 | 1.65 | 0.80 | 0.22 |
| Leaf factor (airborne density) | 0.85 | 1.00 | 1.85 | 0.18 |
| Leaf shed (canopy readiness) | 0.55 | 0.40 | 1.60 | 0.10 |
| Native weather (tendency) | mist | sunny | mist | snow |
| Snow bias | 0 | 0 | 0 | 1.00 |

### The sun path and its spectrum

The season does not merely move the sun — it *bends* the whole sky. Sun
azimuth and elevation shift the daily arc (a low, short winter sun; a high,
long summer sun), sun lux scales its strength, and a spectral tint is layered
over the base colour that time-of-day and weather already chose. The result is
composed, not summed: a **winter sunset** is the low-sun copper *times* the
cool winter tint *times* the sunset orange — a cold, long, pale dusk. A
**summer noon** is bright golden. The same time-of-day and the same weather
profile therefore *feel different* in every season, without a single extra
control.

### Weather temperament

Weather stays an explicit simulation choice (sunny, cloudy, rain, mist, snow,
parhelion) — the player still decides the *weather*, because weather is flight
input. But the season changes how that weather *expresses itself*:

- winter **snow** settles deeper and lingers (`SnowBias` drives surface coverage);
- autumn **mist** is hazier (positive fog bias);
- spring air is clearer (negative fog bias), summer light brighter.

Each season also names a `NativeWeather` — the weather the world 
toward
as the season's natural temperament.

### The living world

- **Insects** (`Born2FlapInsectSwarm::SeasonActivity`): spring explodes with

  butterflies and stirs the mantids; summer rules with dragonflies and bummer
  bugs; autumn belongs to the lunar moths; winter is a near-bare sky with only
  a few hardy moths. The per-species mix is multiplied by the season's overall
  `InsectFactor`.

- **Foliage** (`Born2FlapWindLeaves`): autumn fills the air with shed leaves

  even in a light breeze (`LeafFactor` 1.85), winter strips the canopy bare
  (`0.18`), spring is sparse and green. `LeafTint` is kept as the documented
  hermetic colour cue for when foliage tinting lands.

---

## The hidden hooks (future)

- **Seasonal radio** — each season carries a `RadioCue` (`spring` / `summer` /

  `autumn` / `winter`). When the radio gains per-season playlists, the cue
  selects the soundtrack; today it is dormant and purely descriptive.

- **Seasonal soundscape** — the ambient bird/insect field could follow the same

  cue, thinning winter wildlife and waking spring songbirds.

- **Auto-weather** — `NativeWeather` is the prepared seed for "weather follows

  the season" without exposing a second weather control.

None of these are wired yet. They exist as *hermetic hooks*: one hidden field
per season, ready to light up, invisible until they do.

---

## Where the numbers live

- `FBorn2FlapSeason` + the four-row table: `World/Born2FlapWeatherProfiles.cpp`

  (`Born2FlapWeather::Seasons()`).

- Sun/spectrum/fog/snow application: `World/Born2FlapWeather.cpp` (`Apply`).
- Insect community: `World/Born2FlapInsectSwarm.cpp` (`SeasonActivity`).
- Foliage density: `World/Born2FlapWindLeaves.cpp` (`Tick`, `LeafFactor`).
- Menu selector: `Brain/lib/born2flap/ui/views/menu/menu.umghaml` + `menu.rb`,

  fed by the `seasons` / `season` fields the game mode publishes.
