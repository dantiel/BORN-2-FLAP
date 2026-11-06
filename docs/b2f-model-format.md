# The `.b2f-model` format

A **Born2Flap model is data, not code.** This document defines the portable,
versioned package format for that data — the physics outline, the carbon-rod
inventory, the dry mass, and the visual references that together describe one
bird.

The solvers themselves stay hard-coded in the math core (Haskell) and the host
(Unreal); a model file *selects* solvers by name, it does not embed them.

---

## 1. The package

A model ships as a **folder** named `<name>.b2f-model/`. It may be zipped for
exchange (`.b2f-model.zip`); the unzipped folder is the canonical form.

```
kestrel.b2f-model/
├── manifest.json      ← the single source of truth (physics + visual refs)
└── art/               ← optional source art referenced by the manifest
    ├── wing.svg          wing outline (procedural source; optional)
    ├── T_WingTop.png     wing texture
    ├── T_Tail.png        tail texture
    └── fuselage.glb      baked/generated mesh source
```

The math core **owns and interprets only the physics sections** of the
manifest. The `visual` section is carried opaquely through the Haskell codec
and consumed by the Unreal host (materials, meshes, procedural generators).
`art/` contents are not parsed by the math core — they are referenced by path.

---

## 2. `manifest.json` schema

```json
{
  "format": "born2flap.model",
  "version": 1,
  "name": "kestrel",
  "mass_kg": 0.050,
  "wing": { … },
  "spars": { … },
  "visual": { … },
  "solvers": ["wing-strip", "tail-fixed-fan"]
}
```

| field | type | meaning |
|---|---|---|
| `format` | string | always `"born2flap.model"` (reject otherwise) |
| `version` | number | manifest schema version; currently `1` |
| `name` | string | catalog key; select by name, not id |
| `mass_kg` | number | dry mass of the whole bird [kg] |
| `wing` | object | `WingShape` outline (see §3) |
| `spars` | object | `SparInventory` rod list (see §4) |
| `visual` | object | opaque host-side asset references (see §5) |
| `solvers` | string[] | solver modules that apply (see §6) |

`name` is the **identity**: the model is registered in the runtime catalog by
`name`, and the visual asset bundle is keyed by the same `name`. The old
id-based dispatch (`modelById`) remains only for backward compatibility.

### 2.1 `wing` — the outline

```json
"wing": {
  "span_m": 0.55, "shoulder_m": 0.0127,
  "mount_angle_deg": 0.0, "arm_angle_deg": 0.0,
  "stations": [ … ]
}
```

Each station is a control point of the spanwise polyline (root → tip):

```json
{ "fraction": 0.0, "lead_x_m": -0.079, "chord_m": 0.260,
  "twist_deg": 0.0, "dihedral_deg": 0.0,
  "section":   { "rest_camber": 0.02, "reflex": 0.15,
                 "membrane_k": 0.015, "membrane_tau": 0.025 },
  "structure": { "bend_ei": 0.848, "twist_gj": 0.125,
                 "tau_bend": 0.05, "tau_twist": 0.05 } }
```

* Sweep is **never** a field — it is the slope of the leading-edge curve
  (`lead_x_m` over `fraction`), derived by the solver.
* `section` carries camber / reflex / membrane compliance (the aeroelastic
  airfoil material).
* `structure` carries the **baked** per-station `EI` / `GJ` / relaxation
  constants. These are derived values (from the rod list, via
  `armStructure`/`handStructure`), stored explicitly so the package is a
  *compiled* model — the loader reconstructs the solver-facing shape verbatim,
  with no derivation step.

### 2.2 `spars` — the carbon-rod inventory

```json
"spars": {
  "youngs_modulus_pa": 135000000000.0, "shear_modulus_pa": 5000000000.0,
  "bend_scale": 3.6, "twist_scale": 13.0,
  "membrane_gj": 0.01, "brace_scale": 0.4,
  "rods": [ … ]
}
```

`rods` is an **open-ended list**; each entry:

```json
{ "name": "inner main", "kind": "main", "diameter_m": 0.002,
  "length_m": 0.222, "lever_m": 0.0, "root_frac": 0.0, "tip_frac": 0.4036 }
```

`kind` is `"main"` (leading-edge spar) or `"brace"` (chordwise/diagonal).
The solver derives every stiffness (`EI ∝ d⁴`, brace `GJ`) from this list, so a
different bird is a different list, never a new code path.

---

## 3. The `visual` section (host-owned, opaque to the math core)

The math core parses and round-trips `visual` verbatim. The Unreal host
interprets it. Three geometry node kinds are recognised:

| `source` | meaning |
|---|---|
| `"stations"` | the wing mesh is **derived from `wing.stations`** — no mesh file; one outline serves both physics and visual |
| `"baked"` | reference a pre-baked `.uasset` mesh |
| `"generated"` | a named procedural generator + parameters (run in-editor or at runtime) |

```json
"visual": {
  "wing": { "source": "stations", "material": "wing-feather" },
  "parts": [
    { "name": "fuselage", "source": "baked",
      "mesh": { "asset": "/Game/Birds/KestrelFuselage" },
      "material": "fuselage-pbr" },
    { "name": "crest", "source": "generated",
      "generator": "svg-loft",
      "params": { "profile": "art/crest.svg", "depth_m": 0.08 },
      "material": "crest-skin" }
  ],
  "materials": [
    { "name": "wing-feather", "template": "feather-two-sided",
      "textures": { "base": "art/T_WingTop.png" },
      "params": { "roughness": 0.65, "backlit": true } }
  ]
}
```

This is the seam where the **procedural** wing (already the project's reality —
`Born2FlapWingMesh.cpp` builds the membrane from an SVG + parameters) and the
**baked** fuselage/tail coexist. A material is a *recipe* (template + textures +
params), matching the existing material templates (feather-two-sided, tail,
fuselage-pbr), never a flat texture path.

---

## 4. The `solvers` list

`solvers` names which hard-coded solver modules apply to this bird. Presence =
active; absence = default.

| name | role | notes |
|---|---|---|
| `wing-strip` | core strip aerodynamics | always present |
| `tail-fixed-fan` | fixed ruddervator tail | kestrel / kestrel-light today (FirmwareVehicle) |
| `tail-cruropatagium` | passive leg-membrane tail | future pterosaur |
| `head-actuator` | articulated head + servo | future pterosaur |

A model that omits `tail-*` gets the default tail behaviour. This is the
extensibility slot: adding an articulated head is *"add a section + write one
solver module"*, not *"edit the model registry"*.

---

## 5. ABI (additive v20)

The model-format entry points, declared in `Native/include/born2flap_math.h`
and implemented in `MathCore/src/Born2Flap/Math/FFI.hs`:

```c
int32_t b2f_math_load_model(const char* manifest_json);
int32_t b2f_math_set_wing_model_by_name(B2F_MathContext*, const char* name);
uint32_t b2f_math_get_model_count(void);
int32_t b2f_math_get_model_name(uint32_t index, char* buffer, uint32_t buffer_size);
```

* `load_model` parses a manifest JSON string and registers the bird by `name`.
* `set_wing_model_by_name` selects a registered model on a context.
* `get_model_count` / `get_model_name` enumerate the catalog (built-ins +
  loaded), alphabetically.

The legacy `b2f_math_set_wing_model(context, id)` and
`b2f_math_get_model_mass_kg(id)` remain unchanged. The ABI version is synced in
three places (header `B2F_MATH_ABI_VERSION`, `bridge.c` `b2f_math_abi_version`,
`FFI.hs` `b2f_math_abi_version`); the manifest's own `version` field is a fourth
sync point owned by the Haskell parser.

---

## 6. Module layout (implementation)

| module | role | deps |
|---|---|---|
| `Born2Flap.Math.Json` | JSON AST + hand-rolled parser/printer | base only |
| `Born2Flap.Math.B2fModel` | manifest type + `WingModel` codec | base only |
| `Born2Flap.Math.B2fPackage` | folder IO (`save`/`load` manifest) | directory, filepath |
| `Born2Flap.Math.ModelCatalog` | name → model runtime registry | base only |

`Json`, `B2fModel`, `ModelCatalog` are **base-only** so the foreign library
stays under the Windows PE export-table ceiling under `--export-all-symbols`
(Parsec + containers + directory/filepath previously blew past 65535 exported
symbols). `B2fPackage` is excluded from the DLL and used only by tools.

The exporter (`cabal run born2flap-export-models -- <outDir>`) writes a
`.b2f-model` package per built-in and round-trip-verifies each one.
