# View controllers, `.umghaml` templates & `.umgss` themes

The Brain's UI authoring layer was previously written as inline `HAML.freeze`
heredocs inside Ruby modules. That folded three concerns into one place:
structure (what widgets exist), data (what values they carry) and styling
(what tone/size they use). This document describes the cleaner split.

## Three layers, three grammars

```
views/<name>/<name>.umghaml   →  structure + semantics   (UMGHAML, HAML-subset)
views/<name>/<name>.umgss     →  presentation tokens     (UMGSS, SASS-like)
views/<name>/<name>.rb        →  the view controller      (Ruby DSL)
```

| Concern | Where | Language | Example |
|---|---|---|---|
| Structure & intent | `.umghaml` | UMGHAML | `%Stat{ label: "HÖHE", value: #{altitude} }` |
| Presentation tokens | `.umgss` | UMGSS | `Banner#brand` → `size: xxl` |
| Data, defaults, row generation | `.rb` controller | Ruby | `def cockpit_locals(locals) …` |
| Token → pixels/colors | C++ | `Born2FlapUiTheme.h` | `size: xxl` → `40.f` font |

The key rule: **`.umghaml` never mentions styling** (no colors, fonts, sizes),
and **`.umgss` never mentions structure**. The C++ host is the only place a
semantic token becomes a concrete Slate value. The Brain stays behaviour +
intent; styling is declarative and host-resolved.

## View controllers

A `ViewController` owns one directory and can expose several named views
("endpoints") from it:

```ruby
class FlightHud < ViewController
  view :cockpit, template: "cockpit.umghaml", theme: "cockpit.umgss"
  view :minimal, template: "minimal.umghaml"

  def cockpit_locals(locals = {})
    { altitude: locals[:altitude] || 0 }
  end
end

FlightHud.render(:cockpit, altitude: 120)   # → neutral Node tree
```

`render` does three things, in order:

1. **Resolve locals** — calls `#{name}_locals(locals)` when defined (Ruby sugar
   for defaults/derived values).
2. **Interpolate + parse** — evaluates `#{...}` expressions in the template via
   `Template.interpolate`, then parses the result with the UMGHAML grammar.
3. **Apply the theme** — merges matched `.umgss` rules as *defaults* (an
   explicit prop on the node always wins).

`Template.interpolate` exposes each local as a method on the evaluation context,
so templates read like plain Ruby (`#{altitude}`, `#{speed || 0}`). Injected
values — multi-line fragments, arrays — are inserted intact.

## Data-driven rows

The tuning editor's 12 knobs are data, not markup. The controller expands them
into UMGHAML row fragments and injects them into the template:

```ruby
def slider_rows(fields, names)
  fields.select { |r| names.any? { |n| r.action.end_with?(n) } }.map do |row|
    %(  %Field{ label: "#{row.label}" }\n    %Slider{ value: #{row.value}, … })
  end.join("\n")
end
```

The `.umghaml` skeleton stays static; the dynamic parts arrive via `#{servo_rows}`.

## `.umgss` — a SASS-like theme language

```sass
$panel-space: 2     // variable (token alias)

Banner#brand        // id selector
  size: xxl

Panel               // component default
  spacing: $panel-space

Banner[tone=good]   // attribute-qualified rule
  size: l

.primary             // semantic class (matched via `class` in umghaml)
  tone: accent
```

Supported selectors: bare `Type`, `Type#id`, `Type.class`, `Type[tone=good]`,
and standalone `#id` / `.class`. `//` and `/* */` comments are ignored. Values
are **semantic tokens** (tone/size/spacing/align/valign/weight/wrap) — never
pixels — resolved by `Born2FlapUiTheme.h` and the renderer's styling pass.

## Backward compatibility

The old facades still work:

- `FlightHud.tree` / `FlightHud.build(telemetry)` → delegate to `Views::FlightHud`.
- `TuningEditor.tree(values:, bird_model:, profile:)` → delegates to `Views::TuningEditor`.

New code should use `Views::FlightHud.render(:cockpit, telemetry)` etc. The
`bin/view_demo` script exercises the full controller-driven pipeline.

## Files

- `Brain/lib/born2flap/ui/view_controller.rb` — the `ViewController` base + DSL.
- `Brain/lib/born2flap/ui/template.rb` — `#{...}` interpolation sugar.
- `Brain/lib/born2flap/ui/umgss.rb` — the `.umgss` parser + theme resolver.
- `Brain/lib/born2flap/ui/views/*/` — one directory per view controller.
- `Unreal/Born2Flap/Source/Born2Flap/UI/Born2FlapUiTheme.h` — token → Slate resolvers.