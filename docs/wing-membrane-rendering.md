# Aeroelastic wing designs

The F8 flight desk now offers three silhouettes:

- **Ravencrow** retains its slotted wing with seven parallel pinions.
- **Prototype** retains the original overlapping elliptical feathers and teal/ivory palette.
- **Manta** uses the new continuous, tapered membrane wings.

All three use subdivided, two-sided surfaces. Rendering interpolates the Haskell
solver's 16 strip states: body-Z beam bending, geometric plus elastic twist,
and signed membrane camber. Left and right wings have independent state.
The existing shoulder pose supplies flap and commanded pitch. Each design keeps
its own undeformed shape, including feather slots and overlapping ellipses.

This is a visual reconstruction of the existing reduced-order beam/section solver,
not a cloth or finite-element simulation. Bending is converted from metres to
centimetres and from body space into the moving wing's local space. Stations map
by normalized visual span. Model selection does not retune the flight physics.

## Painting

Import a texture and assign **Wing Paint** on the flight pawn, or call Blueprint
**Set Wing Paint** at runtime. All three model choices receive the painting. Null
restores base colours. **Set Paint Texture** on an individual wing allows separate
paintings. UV0 is fixed in undeformed wing coordinates: U=(30-X)/95 and
V=abs(Y)/105, with dimensions in centimetres. This places a continuous painting
across separate pinions and feathers; the slots remain open. Both surfaces share
the same artwork, and UVs stay attached to vertices during deformation.

The material is `/Game/Birds/M_WingMembrane`, with `WingPaint` and `PaintStrength`
parameters. Recreate it using `Tools/create_wing_membrane_material.py` in Unreal.

## Verification and compatibility

`Native/tests/wing_shape.py <DLL>` checks buffer bounds, finite and asymmetric
bending/camber, and that reading telemetry does not change physics. The Unreal
desktop input test selects all three models and checks all six wing meshes for
received bending, valid normals and stable UVs. Use
`Tools/test-desktop-input.ps1 -Capture -BirdPreview` for model screenshots; add
`-WingPaintPreview` for a temporary checker texture.

The optional `b2f_math_get_wing_shape` export preserves ABI v4's existing layouts.
Old DLLs and the simplified C++ fallback have no elastic telemetry; surfaces keep
their rest shape. Build the Haskell DLL together with Unreal. Replay recordings
still store only flap angles, so replay spirits use the slotted raven at rest shape
with recorded flapping instead of reconstructed elastic loads.
