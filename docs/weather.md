# Level weather and time of day

Open **Fly** in the main menu (or press Escape during flight). Each level has
its own weather and time selectors. Choose conditions before pressing that
level's **Fly** button. Choices are saved separately per level.

| Level | Weather | Time of day |
| --- | --- | --- |
| Ravenstonefield | Sunny, cloudy, gentle rain, mist, light snow, parhelion | Dawn, morning, midday, sunset, moonlit night |
| Shiomori | Sunny, cloudy, gentle rain, mist, parhelion | Dawn, morning, midday, sunset, moonlit night |
| Training | Sunny, cloudy, gentle rain | Morning, midday, sunset |

Parhelion requires daylight; selecting it replaces a night choice with morning.
Shiomori defaults to the morning parhelion display, including the two sun dogs,
halo and opposite-sun rainbow. The halo follows the selected sun direction.

Each profile controls sun strength, clouds, fog, exposure, precipitation and
wind. Wind includes smooth spatial gusts and changes over time. The same field
drives flight, wind audio, leaves, precipitation drift and coastal water.
Training has gentler wind, Shiomori has onshore flow and seawall lift, and
Ravenstonefield has weather-dependent thermals. Night reduces wind and thermals.
Rain is moderate and snow falls lightly; neither adds ground accumulation.

## Development and verification

Profiles and allowed combinations live in `World/Born2FlapWeatherProfiles.cpp`.
The weather actor applies the selected atmosphere and maintains a bounded pool
of falling particles. Menu travel uses Unreal's `?` option separators.

For a direct launch, use `-B2FLevel=Shiomori -B2FWeather=rain -B2FDayTime=noon`.
Invalid combinations fall back to that level's defaults. Plain boot opens the
main menu; an explicit level argument starts flight directly.

Build `Born2FlapEditor`, then run from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File Unreal/Born2Flap/Tools/test-weather.ps1 -All
powershell -ExecutionPolicy Bypass -File Unreal/Born2Flap/Tools/test-weather.ps1 -Menu
```

The rendered checks cover all six weather profiles plus night and Training,
validate allowed combinations and finite deterministic wind, and check menu
selection and actual level travel. Captures are saved as `WEATHER_*.png` under
`Unreal/Born2Flap/Saved/Screenshots/WindowsEditor`.

To regenerate precipitation materials and the parameterized halo, run
`Tools/create_weather_materials.py` through Unreal's Python commandlet with
the game/editor closed. It does not regenerate maps.
