# Harvest Watch

Choose **Shiomori Bay → HARVEST WATCH** in F10's world menu. The mode starts
beside the sweet-potato fields with the kestrel/falcon ornithopter. Launch to
start a three-minute patrol. Ordinary Shiomori free flight retains ambient
ravens and scarecrows without a scored round.

Three plots (west, centre, east) have eighteen ravens. Three initially sit on
scarecrow crossbars; the rest peck along the existing raised crop rows. The
scarecrows do not repel birds. A moving falcon approaching within roughly 16-23 metres scares
them into flight; individual boldness, previous escapes and the falcon's
projected approach affect their reaction. It must be above 0.6 m ground clearance and moving faster
than 3 m/s; parking beside a crop is not a deterrent. Flying high overhead does
not scare birds on the ground. This is nonviolent crop protection, with no
contact bonus or bird damage.

Ravens flee and keep circling for roughly 7-16 seconds before attempting a
return. They remember recent patrol pressure, consider approaching flight
direction, and spread out towards less-guarded plots. Some return to a
scarecrow before feeding. Return travel takes additional time. The harvest
starts at 1,000 points; each feeding bird consumes 1.5 points/second, increasing
by 0.08 points/second for each second in the current feeding bout, capped at
3.5. Short hops preserve that appetite; a flight and landing starts a new bout.
Perched and airborne birds cause no damage. There are no farmable fly-by point
bonuses. Preserve any crop points until the timer expires to win; zero loses.
The HUD shows remaining time, crop points, birds scattered and feeding birds
per plot, and warns when a decoy is teasing the pilot. ENTER after a result restarts the round at the field launch. R/flight
resets do not erase crop damage. Menus/settings/POI overlays pause the activity.
F7 hides the activity HUD with other flight UI.

The first-pass raven visuals use instanced body/head/beak/tail/leg shapes and
articulated feather fans: folded wings while feeding/perching, wingbeats while
escaping/returning, banked turns, pecking, head-cocking and watching the falcon. These are lightweight procedural
stand-ins, not scanned or skeletal bird assets. The new materials are under
`/Game/Shiomori/HarvestWatch`, built by `Tools/create_harvest_watch_assets.py`.
The field crops themselves are the existing generated satsumaimo plants.

Implementation: `Game/HarvestWatchSimulation.h` and
`Game/Born2FlapHarvestWatch.cpp`. Both native and Ruby menu paths offer the
activity; travel uses `?Activity=HarvestWatch`, or launch with
`-B2FLevel=Shiomori -B2FActivity=HarvestWatch`. Free flight keeps its own launch.

Validation:

```
Tools/test-harvest-watch.ps1
Tools/test-harvest-watch.ps1 -Capture
ruby -I Brain/lib Brain/test/harvest_watch_menu_test.rb
```

The runtime check verifies launch location, falcon selection, field collision,
idle scoring, damage, stationary immunity, scare cooldown, return to feeding,
win/loss and restart. Render captures cover the field, perched bird/scarecrow,
plot layout and a flying bird (`Saved/Screenshots/WindowsEditor/HARVEST_WATCH_*`).
Balance and real pilot handling still need playtesting; no Shipping release
is implied by these checks. The separate broad Ruby UI contract test currently
reports an unrelated unsupported `stick` widget in this working checkout.

Website announcement: Harvest Watch and Neon Abyss are labelled **in
development**, published in website-only commit `50bbc72`; GitHub Pages run
`39166428767` succeeded. Gameplay source remains in this local working checkout.

## Mischievous flock update (2026-10-19)

The original simulation stopped all movement before launch and after results;
feeding had no repositioning transition, and escaped birds stopped at a fixed
airborne waypoint. Flock movement now runs independently of the scoring clock.
The score and timer remain frozen before launch and after results, while birds
keep hopping, feeding, perching and making short flights. Menus still pause.

Each bird has a seeded boldness and its own timing. Nearby alarmed birds warn
others, escape paths are replanned if the falcon follows, and return paths can
switch away from a newly guarded plot. Three bolder decoys occasionally make
bounded lateral teasing passes while their companions exploit other plots.
Perched birds turn their heads to watch; feeders move between plants every few
seconds. Escape waypoints become moving circuits, never stationary hover spots.
The cultivated strip bounds flight destinations away from the warehouse row.
This is lightweight rule-based flock behaviour, not trained AI or pathfinding.

The expanded regression test checks pre-launch and post-result movement,
hopping, stationary immunity, escape circling, successful returns, patrol
memory, decoy passes, lookout alarms, score boundaries and actual mesh-instance
movement. It passed in the editor runtime. The render test now takes the first
two screenshots from the same camera, four seconds apart, to expose stalled
flocks; subsequent views inspect individual ground and flying poses.

Consecutive rendered views confirmed that the flock changes position before
launch, with no crop score charged. Evidence: `.setup/harvest-mischief-0.png`
and `.setup/harvest-mischief-1.png`; behaviour log:
`.setup/harvest-mischief-test.log`.
