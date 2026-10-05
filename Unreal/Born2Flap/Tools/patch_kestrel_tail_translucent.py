"""Rebuild M_KestrelTail as a masked+dither two-sided tail (no re-import).

Run once via the editor commandlet (close any running game/editor first):

  UnrealEditor-Cmd.exe "Unreal/Born2Flap/Born2Flap.uproject" -run=pythonscript \
    -script="Unreal/Born2Flap/Tools/patch_kestrel_tail_translucent.py" \
    -unattended -nosplash -nullrhi -nosound

Reuses the already-imported T_KestrelTail texture. The tail was BLEND_TRANSLUCENT
(TailOpacity 0.72), which neither writes depth nor casts a shadow — so the wing's
two symptoms (no shadow, shoreline foam drawing over it) also applied here. This
rebuilds it BLEND_MASKED with a screen-space dither on the opacity mask, matching
M_FalconWing: a masked surface writes depth (foam stays behind it) and casts a
dithered shadow, while the "slight transparency" reads as a faint stipple.
TailOpacity (scalar) 0..1 tunes the see-through; TailGlow drives the soft backlight.
"""
import unreal as u

assets = u.AssetToolsHelpers.get_asset_tools()
lib = u.MaterialEditingLibrary
ela = u.EditorAssetLibrary

BIRDS = "/Game/Birds"
PATH = BIRDS + "/M_KestrelTail"


def node(m, kind, **props):
    n = lib.create_material_expression(m, getattr(u, "MaterialExpression" + kind))
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def wire(a, b, pin, output=""):
    assert lib.connect_material_expressions(a, output, b, pin), \
        "Invalid connection: %s.%s -> %s.%s" % (type(a).__name__, output, type(b).__name__, pin)


def output(n, prop, pin=""):
    assert lib.connect_material_property(n, pin, getattr(u.MaterialProperty, "MP_" + prop)), \
        "Invalid output: " + prop


ela.make_directory(BIRDS)
tail_tex = u.load_asset(BIRDS + "/T_KestrelTail")
assert isinstance(tail_tex, u.Texture2D), "T_KestrelTail texture missing"

m = u.load_asset(PATH) if ela.does_asset_exist(PATH) else assets.create_asset(
    "M_KestrelTail", BIRDS, u.Material, u.MaterialFactoryNew())
lib.delete_all_material_expressions(m)
m.set_editor_property("two_sided", True)
# Masked (not translucent): writes depth + casts a shadow, both of which the
# translucent tail lost. TailOpacity < 1 becomes a screen-space dither on the
# opacity mask below, so slight transparency reads as a faint stipple rather
# than a hard alpha cut.
m.set_editor_property("blend_mode", u.BlendMode.BLEND_MASKED)
m.set_editor_property("shading_model", u.MaterialShadingModel.MSM_DEFAULT_LIT)
m.set_editor_property("disable_depth_test", False)

paint = node(m, "TextureSampleParameter2D", parameter_name="TailPaint", texture=tail_tex)
output(paint, "BASE_COLOR", "RGB")

rough = node(m, "Constant", r=0.65)
output(rough, "ROUGHNESS")

# Slight transparency via a dithered opacity mask (same stipple as M_FalconWing).
opacity = node(m, "ScalarParameter", parameter_name="TailOpacity", default_value=0.95)
dither = node(m, "Custom",
              code=("float2 px = floor(Parameters.SvPosition.xy);\n"
                    "float d = frac(sin(dot(px, float2(12.9898, 78.233))) * 43758.5453);\n"
                    "return step(d, Opacity);"),
              output_type=u.CustomMaterialOutputType.CMOT_FLOAT1)
pin = u.CustomInput()
pin.set_editor_property("input_name", "Opacity")
dither.set_editor_property("inputs", [pin])
wire(opacity, dither, "Opacity")
output(dither, "OPACITY_MASK")

# Soft-light substitute: UE has no native Soft Light blend mode. A faint
# emissive backlight on the paint reads as light passing through the feathers.
glow = node(m, "ScalarParameter", parameter_name="TailGlow", default_value=0.15)
emissive = node(m, "Multiply")
wire(paint, emissive, "A", "RGB")
wire(glow, emissive, "B")
output(emissive, "EMISSIVE_COLOR")

lib.recompile_material(m)
assert ela.save_asset(PATH), "KESTREL_TAIL_MASKED: failed to save material"
u.log("KESTREL_TAIL_MASKED_READY " + PATH)
