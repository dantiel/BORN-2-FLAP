# Progressive Scroll Blur

The UI's scrollable lists dissolve into a **progressive content blur** at their
top/bottom edges — the actual scroll content stays sharp in the safe zone, then
progressively blurs (a real gaussian, refracted like hammered glass / water) and
finally dissolves organically into the void.

## What it is

A `ScrollBlur` is a semantic composite: a `UScrollBox` wrapped in a
`URetainerBox`. The retainer renders the scroll content into a render target and
post-processes it through the effect material `/Game/UI/M_ScrollRetainerBlur`
(a `MD_UserInterface` material, `AlphaComposite`). That material samples the
retained content via a `TextureSampleParameter2D` named `"Texture"` (the name
the retainer binds its render target to) and applies a 3×3 gaussian whose tap
radius is zero in the safe zone and grows toward the edge — so the safe zone is
reproduced pixel-exact and the edges blur into the void.

```
┌─────────────────────────────────┐
│  ScrollBlur composite (Border)  │
│  ┌─────────────────────────────┐ │
│  │ URetainerBox                │ │
│  │  ┌───────────────────────┐  │ │
│  │  │ UScrollBox (content)  │  │ │   ← declared children scroll here
│  │  └───────────────────────┘  │ │   ← retained + post-processed
│  └─────────────────────────────┘ │
└─────────────────────────────────┘
```

This supersedes the old overlay `UImage` veil (`M_ScrollProgressiveBlur`), which
could only tint on top of the content — a true blur needs the retainer's render
target, so the blur now affects the content itself.

## Parameters (props on `ScrollBlur`)

| prop          | type   | default    | meaning                                                        |
|---------------|--------|------------|----------------------------------------------------------------|
| `edge`        | number | `"auto"`   | safe zone as a normalized half-band `0..0.5`                   |
| `edge: "auto"`| string | —          | derive the band from the platform safe-area inset (notch)      |
| `blur`        | number | `0.02`     | max blur radius in UV units                                    |
| `amount`      | number | `0.65`     | progressive-blur falloff width `0..1`                          |
| `refract`     | number | `0.35`     | refraction warp strength (hammered glass / water) `0..1`       |
| `refractScale`| number | `6.0`      | organic noise frequency                                        |
| `void`        | number | `0.9`      | how fully the edge dissolves into the void `0..1`              |

`edge` is the answer to "definable amount **or** automatic safe-area-bar size":
pass a number for a fixed band, or `"auto"` (the default) to size it from the
display's title-safe inset so the blur never hides the notch / home-indicator
surface. On desktop there is no notch, so `"auto"` falls back to `0.14`.

## Usage

**Native C++** (how the menu/settings build their trees):

```cpp
Nd("ScrollBlur", P({ {"edge", S("auto")}, {"blur", N(0.02)} }),
{
    Nd("VerticalBox", P({ {"spacing", N(12)} }), std::move(Rows))
})
```

**UMGHAML** (declarative, via `Brain/lib/born2flap/ui/emitter.rb`):

```haml
%scroll{ edge: "auto", blur: 0.02, refract: 0.4 }
  %vbox{ spacing: 12 }
    %button{ label: "…" }
```

## Files

- `Unreal/Born2Flap/Tools/create_scroll_retainer_blur.py` — bakes
  `M_ScrollRetainerBlur` (the retainer effect material).
- `Unreal/Born2Flap/Tools/create_scroll_blur_ui.py` — the superseded
  `M_ScrollProgressiveBlur` veil material (kept for reference).
- `Unreal/Born2Flap/Source/Born2Flap/UI/Born2FlapUIRenderer.cpp` — the
  `ScrollBlur` composite (`BuildComponent` builds `URetainerBox` + `UScrollBox`),
  prop application (`ApplyCompositeProps`), `AutoSafeAreaEdge()`, and the
  linear-box `Fill` pinning.
- `Brain/lib/born2flap/ui/emitter.rb` — `scroll` → `ScrollBlur` mapping.

## Baking the material

```powershell
V:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe `
  "V:\BORN 2 FLAP\Unreal\Born2Flap\Born2Flap.uproject" `
  -run=pythonscript `
  -script="V:\BORN 2 FLAP\Unreal\Born2Flap\Tools\create_scroll_retainer_blur.py" `
  -unattended -nosplash -nullrhi -nosound
```

If the asset is missing, the renderer degrades gracefully — the retainer passes
the content through unblurred and the ScrollBox behaves like a plain scroll box.
