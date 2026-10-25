# Airfield Watch

Choose **Shiomori Bay → AIRFIELD WATCH** in F10's world menu. The mode starts
beside the coastal airstrip with the kestrel/falcon ornithopter. Launch to
start a three-minute patrol. Ordinary Shiomori free flight retains ambient
gulls and ravens without a scored round.

This is the inverse of Harvest Watch. Instead of defending three scattered
crop plots from feeding birds, you defend one protected volume — the runway
strip and its approach corridor — against birds trying to enter it. The tower
calls in one ornithopter: you. Real airports haze birds off runways with
falconry; here the pilot *is* the falcon.

A protected airspace volume spans the runway, its clearway and a shallow
glide-slope approach funnel. Flocks of gulls and ravens roost in the coastal
marsh and periodically take off and drift toward the field. Some perch on
runway edge markers or the windsock before crossing. A moving falcon
approaching within roughly 16–23 metres scares them into flight; individual
boldness, previous escapes and the falcon's projected approach affect their
reaction. It must be above 0.6 m ground clearance and moving faster than
3 m/s; hovering beside the threshold is not a deterrent. Flying high overhead
does not scare birds on the ground. This is nonviolent bird control, with no
contact bonus or bird damage.

Birds flee and circle for roughly 7–16 seconds before attempting another
approach. They remember recent patrol pressure, consider the falcon's flight
direction, and probe the airspace from less-guarded angles — the seaward side,
the marsh, the perimeter road. A few bolder birds make decoy passes while
companions slip in low. The field starts at 1,000 clearance points; each bird
inside the protected airspace consumes 1.5 points/second, increasing by 0.08
points/second for each second of the current incursion, capped at 3.5. Birds
outside the zone cost nothing. Preserve any clearance until the timer expires
to win; zero loses. The HUD shows remaining time, clearance points, birds
airborne and birds inside the zone, and warns when the approach corridor is
being probed. ENTER after a result restarts the round at the airstrip launch.
R/flight resets do not erase clearance loss. Menus/settings/POI overlays pause
the activity. F7 hides the activity HUD with other flight UI.

The birds reuse the **Crow by Alexei Ostapenko (alexanders823)** flock from
Harvest Watch (Sketchfab, CC BY 4.0); gulls are a planned visual variant. The
flock simulation still drives hopping, escape, patrol memory and decoy passes,
with the protected-volume scoring inverted into an exclusion zone.

The airstrip itself is a planned venue — mown grass runway, edge markers,
windsock and a small tower, generated alongside the existing Shiomori coastal
geometry. Until it lands, the mode can run on a staked-out runway area with
the protected airspace drawn in the activity HUD.

Implementation (planned): `Game/AirfieldWatchSimulation.h` and
`Game/Born2FlapAirfieldWatch.cpp`, mirroring Harvest Watch. Both native and
Ruby menu paths will offer the activity; travel uses `?Activity=AirfieldWatch`,
or launch with `-B2FLevel=Shiomori -B2FActivity=AirfieldWatch`.

Validation (planned):

```
Tools/test-airfield-watch.ps1
Tools/test-airfield-watch.ps1 -Capture
ruby -I Brain/lib Brain/test/airfield_watch_menu_test.rb
```

Website announcement: labelled **in development**, published alongside Harvest
Watch and Neon Abyss. Gameplay source is design-only in this checkout.
