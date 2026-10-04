"""Import the kestrel wing membrane and rebuild M_FalconWing with a greyed underside.

Run once via the editor commandlet (same style as create_falcon_materials.py):

  UnrealEditor-Cmd.exe "Unreal/Born2Flap/Born2Flap.uproject" -run=pythonscript \\
    -script="Unreal/Born2Flap/Tools/import_kestrel_wing.py" -unattended -nosplash -nullrhi -nosound

Imports  Tools/out/kestrel-wing-top.png  ->  /Game/Birds/T_KestrelWingTop
Rebuilds /Game/Birds/M_FalconWing: two-sided, front face = membrane (full colour),
back face = greyed membrane (desaturated via TwoSidedSign).
"""
import unreal as u
from pathlib import Path

assets = u.AssetToolsHelpers.get_asset_tools()
lib = u.MaterialEditingLibrary
ela = u.EditorAssetLibrary

SRC = Path(__file__).resolve().parent / "out" / "kestrel-wing-top.png"
TEX_PATH = "/Game/Birds/T_KestrelWingTop"
MAT_PATH = "/Game/Birds/M_FalconWing"


def import_texture():
    if not SRC.exists():
        u.log_warning("KESTREL_WING: source missing " + str(SRC))
        raise SystemExit(1)
    ela.make_directory("/Game/Birds")
    # Always re-import: the mesh now uses the raw membrane raster (not a warped
    # version), so the texture must be refreshed whenever the SVG changes.
    task = u.AssetImportTask()
    task.filename = str(SRC)
    task.destination_path = "/Game/Birds"
    task.destination_name = "T_KestrelWingTop"
    task.automated = True
    task.replace_existing = True
    task.save = True
    assets.import_asset_tasks([task])
    tex = u.load_asset(TEX_PATH)
    if not isinstance(tex, u.Texture2D):
        u.log_warning("KESTREL_WING: failed to load " + TEX_PATH)
        raise SystemExit(1)
    tex.set_editor_property("srgb", True)
    tex.set_editor_property("never_stream", True)
    assert ela.save_asset(TEX_PATH), "KESTREL_WING: failed to save texture"
    u.log("KESTREL_WING_TEX " + TEX_PATH)
    return tex


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


def wing_material(tex):
    m = u.load_asset(MAT_PATH) if ela.does_asset_exist(MAT_PATH) else assets.create_asset(
        "M_FalconWing", "/Game/Birds", u.Material, u.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    m.set_editor_property("two_sided", True)

    vertex = node(m, "VertexColor")
    paint = node(m, "TextureSampleParameter2D", parameter_name="WingPaint", texture=tex)
    strength = node(m, "ScalarParameter", parameter_name="PaintStrength", default_value=1.0)
    sign = node(m, "TwoSidedSign")

    # Back face (underside) is the greyed membrane: desaturate by blending the
    # paint colour toward its own luminance on the back face.
    grey = node(m, "Custom",
                code="float l = dot(Paint, float3(0.299, 0.587, 0.114)); return lerp(Paint, l.xxx, saturate(-Sign));",
                output_type=u.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for nm in ("Paint", "Sign"):
        pin = u.CustomInput()
        pin.set_editor_property("input_name", nm)
        pins.append(pin)
    grey.set_editor_property("inputs", pins)
    wire(paint, grey, "Paint", "RGB")
    wire(sign, grey, "Sign")

    # blend greyed/sided paint over the vertex-colour base by PaintStrength.
    blend = node(m, "LinearInterpolate")
    # UMaterialExpressionVertexColor (UE 5.8 external-code base) exposes its
    # colour on the default (unnamed) output pin, not "RGB".
    if not lib.connect_material_expressions(vertex, "", blend, "A"):
        assert lib.connect_material_expressions(vertex, "RGB", blend, "A"), "vertex colour connect failed"
    wire(grey, blend, "B")
    wire(strength, blend, "Alpha")
    output(blend, "BASE_COLOR")

    rough = node(m, "Constant", r=0.65)
    output(rough, "ROUGHNESS")

    lib.recompile_material(m)
    assert ela.save_asset(MAT_PATH), "KESTREL_WING: failed to save material"
    u.log("KESTREL_WING_MAT " + MAT_PATH)
    return m


tex = import_texture()
wing_material(tex)
u.log("KESTREL_WING_READY")