# Brain (Ruby)

A Ruby framework that turns a declarative tree (UMGHAML) into semantic UI actions. It owns the menu/views/RC panels and their text; Unreal renders the semantic nodes.

## Important files

### `born2flap/brain.rb` - entry
Boots the framework and the view tree.

### `born2flap/router.rb` - action routing
Routes semantic actions (e.g. `menu.shiomori`, `rc.calibrate.start`) to handlers. New UI actions are wired here.

### `born2flap/event_bus.rb` - events
Pub/sub between the Brain and the game bridge.

### `born2flap/plugin_registry.rb` - plugins
Registration of views/panels so they can be discovered and mounted.

### `born2flap/i18n.rb` - localisation
Locale loading and fallback; drives the generated `Born2FlapI18n.h`.

### `born2flap/audio.rb` + `audio/aero_audio.rb` - audio model
The Brain-side audio state that pairs with the Unreal synth.

### `born2flap/ui.rb` - UI entry
The UI dispatcher and view tree assembly.

### `ui/views/menu/menu.umghaml` + `menu.rb` - main menu
The declarative menu tree and its builder. Text lives in locale JSON, the tree in `.umghaml`, the logic in `.rb`.

### `ui/views/rc/rc.umghaml` + `rc.rb` - RC panel
The CONTROL SETTINGS panel (device, calibration, learn, live axes, channel mapping).

### `ui/views/tuning_editor/tuning_editor.umghaml` + `tuning_editor.rb` - tuning panel
The flight-desk tuning tree.

## The UMGHAML pattern

A view is two files: `.umghaml` (the semantic tree) and `.rb` (the builder that emits actions). The tree uses nodes like `Panel`, `Tabs`, `Segment`, `Slider`, `Dropdown`, `Button`, `Text`. The Unreal side (`Born2FlapUIRenderer`) consumes the same node vocabulary. Adding a panel = add the two files + register the route.
