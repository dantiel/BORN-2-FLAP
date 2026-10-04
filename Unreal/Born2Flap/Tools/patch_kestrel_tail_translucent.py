"""Make the existing M_KestrelTail material translucent in place (no re-import).

Run once via the editor commandlet (close any running game/editor first):

  UnrealEditor-Cmd.exe "Unreal/Born2Flap/Born2Flap.uproject" -run=pythonscript \
    -script="Unreal/Born2Flap/Tools/patch_kestrel_tail_translucent.py" \
    -unattended -nosplash -nullrhi -nosound

Rebuilds /Game/Birds/M_KestrelTail exactly like import_kestrel_fuselage.py's
tail_material(), but reuses the already-imported T_KestrelTail texture so no
downloads or mesh re-import are needed. TailOpacity (scalar) tunes how much
light passes through the tail fan.
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
m.set_editor_property("blend_mode", u.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property("shading_model", u.MaterialShadingModel.MSM_DEFAULT_LIT)

paint = node(m, "TextureSampleParameter2D", parameter_name="TailPaint", texture=tail_tex)
output(paint, "BASE_COLOR", "RGB")

rough = node(m, "Constant", r=0.65)
output(rough, "ROUGHNESS")

opacity = node(m, "ScalarParameter", parameter_name="TailOpacity", default_value=0.72)
output(opacity, "OPACITY")

lib.recompile_material(m)
assert ela.save_asset(PATH), "KESTREL_TAIL_TRANSLUCENT: failed to save material"
u.log("KESTREL_TAIL_TRANSLUCENT_READY " + PATH)
