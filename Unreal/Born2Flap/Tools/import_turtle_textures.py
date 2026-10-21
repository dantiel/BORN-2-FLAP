"""Import the Neon Turtle textures and rebuild M_TurtleShell / M_TurtleHead / M_TurtleFlipper.

Run once via the editor commandlet (the editor must be closed):

  UnrealEditor-Cmd.exe "Unreal/Born2Flap/Born2Flap.uproject" \
    -ExecutePythonScript="Unreal/Born2Flap/Tools/import_turtle_textures.py" \
    -unattended -nosplash -nullrhi -nosound

Imports (Tools/out/*.png -> /Game/NeonAbyss/):
  turtle-carapace.png -> T_TurtleCarapace
  turtle-flipper.png  -> T_TurtleFlipper
  turtle-head.png     -> T_TurtleHead

Rebuilds (self-luminous / neon: Lit shading + texture*Glow -> Emissive, so the
pattern reads in light AND glows in shadow):
  M_TurtleShell   carapace texture -> BaseColor + texture*Glow -> Emissive
  M_TurtleHead    head texture      -> BaseColor + texture*Glow -> Emissive
  M_TurtleFlipper flipper texture   -> BaseColor + texture*Glow -> Emissive (two-sided)
  M_TurtleEye     constant amber emissive (the glowing eyes)
"""
import unreal as u
from pathlib import Path

assets = u.AssetToolsHelpers.get_asset_tools()
lib = u.MaterialEditingLibrary
ela = u.EditorAssetLibrary

FOLDER = "/Game/NeonAbyss"
OUT = Path(__file__).resolve().parent / "out"

TEXTS = [
    ("turtle-carapace.png", "T_TurtleCarapace"),
    ("turtle-flipper.png", "T_TurtleFlipper"),
    ("turtle-head.png", "T_TurtleHead"),
]


def import_texture(src_name, dest_name):
    src = OUT / src_name
    if not src.exists():
        raise SystemExit("TURTLE: missing source " + str(src))
    ela.make_directory(FOLDER)
    task = u.AssetImportTask()
    task.filename = str(src)
    task.destination_path = FOLDER
    task.destination_name = dest_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    assets.import_asset_tasks([task])
    path = FOLDER + "/" + dest_name
    tex = u.load_asset(path)
    if not isinstance(tex, u.Texture2D):
        raise SystemExit("TURTLE: failed to load " + path)
    tex.set_editor_property("srgb", True)
    tex.set_editor_property("never_stream", True)
    tex.set_editor_property("max_texture_size", 2048)
    tex.set_editor_property("compression_settings", u.TextureCompressionSettings.TC_BC7)
    assert ela.save_asset(path), "TURTLE: failed to save texture " + path
    u.log("TURTLE_TEX " + path)
    return tex


def node(m, kind, **props):
    n = lib.create_material_expression(m, getattr(u, "MaterialExpression" + kind))
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def wire(a, b, pin, output="RGB"):
    if lib.connect_material_expressions(a, output, b, pin):
        return
    # Some material expressions expose their colour on the unnamed '' pin instead
    # of 'RGB' (see UMaterialExpressionVertexColor). Fall back before failing.
    alt = '' if output else 'RGB'
    assert lib.connect_material_expressions(a, alt, b, pin), \
        "connect failed: %s.%s -> %s.%s" % (type(a).__name__, output, type(b).__name__, pin)


def out(n, prop, pin=""):
    assert lib.connect_material_property(n, pin, getattr(u.MaterialProperty, "MP_" + prop)), \
        "output failed: " + prop


def textured_emissive(path, tex, roughness, metallic, glow, two_sided):
    m = u.load_asset(path) if ela.does_asset_exist(path) else assets.create_asset(
        path.rsplit("/", 1)[-1], FOLDER, u.Material, u.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    m.set_editor_property("blend_mode", u.BlendMode.BLEND_OPAQUE)
    m.set_editor_property("shading_model", u.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property("two_sided", two_sided)
    sample = node(m, "TextureSampleParameter2D", parameter_name="Albedo", texture=tex)
    out(sample, "BASE_COLOR", "RGB")
    glow_p = node(m, "ScalarParameter", parameter_name="Glow", default_value=glow)
    emissive = node(m, "Multiply")
    wire(sample, emissive, "A")
    wire(glow_p, emissive, "B", output="")
    out(emissive, "EMISSIVE_COLOR")
    out(node(m, "Constant", r=roughness), "ROUGHNESS")
    out(node(m, "Constant", r=metallic), "METALLIC")
    lib.recompile_material(m)
    assert ela.save_asset(path), "TURTLE: failed to save material " + path
    u.log("TURTLE_MAT " + path)
    return m


def eye_material(path):
    m = u.load_asset(path) if ela.does_asset_exist(path) else assets.create_asset(
        "M_TurtleEye", FOLDER, u.Material, u.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    m.set_editor_property("blend_mode", u.BlendMode.BLEND_OPAQUE)
    m.set_editor_property("shading_model", u.MaterialShadingModel.MSM_DEFAULT_LIT)
    amber = node(m, "Constant3Vector", constant=u.LinearColor(0.98, 0.72, 0.16, 1.0))
    out(amber, "BASE_COLOR")
    glow_p = node(m, "ScalarParameter", parameter_name="Glow", default_value=2.0)
    emissive = node(m, "Multiply")
    wire(amber, emissive, "A", output="")
    wire(glow_p, emissive, "B", output="")
    out(emissive, "EMISSIVE_COLOR")
    out(node(m, "Constant", r=0.2), "ROUGHNESS")
    lib.recompile_material(m)
    assert ela.save_asset(path), "TURTLE: failed to save material " + path
    u.log("TURTLE_MAT " + path)
    return m


texs = {dest: import_texture(src, dest) for src, dest in TEXTS}
textured_emissive(FOLDER + "/M_TurtleShell", texs["T_TurtleCarapace"], 0.55, 0.10, 2.0, False)
textured_emissive(FOLDER + "/M_TurtleHead", texs["T_TurtleHead"], 0.50, 0.05, 2.0, False)
textured_emissive(FOLDER + "/M_TurtleFlipper", texs["T_TurtleFlipper"], 0.45, 0.05, 2.0, True)
eye_material(FOLDER + "/M_TurtleEye")
u.log("TURTLE_TEXTURES_READY")