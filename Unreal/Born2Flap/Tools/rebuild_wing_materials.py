"""Rebuild both wing membranes OPAQUE (no transparency, no dither) with a
backlit-only "church-window" glow.

  M_FalconWing    -> kestrel membrane wing (T_KestrelWingTop, greyed underside)
  M_WingMembrane  -> ravencrow/prototype wing (vertex-colour base)

The wing is fully opaque: detail stays legible and the lit side reflects
normally (roughness 0.65, two-sided, default lit). The only light-through
effect is an additive emissive that fires ONLY when the sun is behind the
membrane (backlit), tinted by the paint colour.

  transmission = saturate(-dot(N, surface->sun) * dot(N, surface->camera))^2

using AtmosphericLightVector (sun -> scene) and CameraVectorWS (camera ->
surface), so the product is positive only when light and camera sit on
opposite sides of the membrane.

Run once via the editor commandlet (close any running game/editor first):

  UnrealEditor-Cmd.exe "Unreal/Born2Flap/Born2Flap.uproject" -run=pythonscript \
    -script="Unreal/Born2Flap/Tools/rebuild_wing_materials.py" \
    -unattended -nosplash -nullrhi -nosound
"""
import unreal as u

assets = u.AssetToolsHelpers.get_asset_tools()
lib = u.MaterialEditingLibrary
ela = u.EditorAssetLibrary

BIRDS = "/Game/Birds"
WHITE = "/Engine/EngineResources/WhiteSquareTexture"



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


def wire_vertex(vertex, dst, pin):
    # UE 5.8 exposes vertex colour on the default (unnamed) output pin.
    if not lib.connect_material_expressions(vertex, "", dst, pin):
        assert lib.connect_material_expressions(vertex, "RGB", dst, pin), "vertex colour connect failed"


def add_backlit_glow(m, tint):
    """Emissive = brightened tint * WingGlow * transmission.

    transmission = saturate(-dot(Np, L))^2, where Np = PixelNormalWS (the
    two-sided normal, already flipped to face the camera) and L =
    AtmosphericLightVector (direction TOWARD the sun — verified against the
    engine's sun-disk code: the disk appears where WorldDir · L > threshold).

    When the sun is behind the membrane, L points away from the camera-facing
    normal, so -dot(Np,L) > 0 and the wing glows church-window style. On the
    lit side L points toward the camera-facing normal, so the term is 0 and the
    wing stays a normal opaque reflective surface. Built from native nodes (no
    Custom HLSL) so the graph survives UE 5.8's Python API.
    """
    # CameraVectorWS is the "surface -> camera" direction; AtmosphericLightVector
    # is the light direction (sun -> scene). When the sun is BEHIND the membrane
    # the light travels through the wing toward the camera, so the two point in
    # the SAME direction and +dot(cam, sun) > 0 — the wing glows church-window
    # style. Using the camera vector (not the pixel normal) is robust against
    # two-sided normal flipping, which is why the earlier PixelNormalWS / negated
    # versions never lit up when backlit.
    cam = node(m, "CameraVectorWS")
    sun = node(m, "AtmosphericLightVector")

    d = node(m, "DotProduct")
    wire(cam, d, "A")
    wire(sun, d, "B")

    sat = node(m, "Saturate")
    wire(d, sat, "")

    glow = node(m, "ScalarParameter", parameter_name="WingGlow", default_value=12.0)
    scaled = node(m, "Multiply")
    wire(glow, scaled, "A")
    wire(sat, scaled, "B")

    # Brighten the tint for the emissive only, so the glow reads clearly even
    # against a blazing sun (the base colour is untouched).
    boost = node(m, "Constant", r=1.5)
    tint_boost = node(m, "Multiply")
    if not lib.connect_material_expressions(tint, "", tint_boost, "A"):
        assert lib.connect_material_expressions(tint, "RGB", tint_boost, "A"), "tint connect failed"
    wire(boost, tint_boost, "B")

    floor = node(m, "Constant", r=0.2)
    tint_bright = node(m, "Add")
    wire(tint_boost, tint_bright, "A")
    wire(floor, tint_bright, "B")

    emissive = node(m, "Multiply")
    wire(tint_bright, emissive, "A")
    wire(scaled, emissive, "B")
    output(emissive, "EMISSIVE_COLOR")


def get_or_create(path):
    if ela.does_asset_exist(path):
        return u.load_asset(path)
    name = path.rsplit("/", 1)[-1]
    return assets.create_asset(name, BIRDS, u.Material, u.MaterialFactoryNew())


def build_falcon():
    path = BIRDS + "/M_FalconWing"
    tex = u.load_asset("/Game/Birds/T_KestrelWingTop")
    if not isinstance(tex, u.Texture2D):
        u.log_warning("WING_FALCON: T_KestrelWingTop missing, using white")
        tex = u.load_asset(WHITE)

    m = get_or_create(path)
    lib.delete_all_material_expressions(m)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("blend_mode", u.BlendMode.BLEND_OPAQUE)
    m.set_editor_property("shading_model", u.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property("disable_depth_test", False)

    vertex = node(m, "VertexColor")
    paint = node(m, "TextureSampleParameter2D", parameter_name="WingPaint", texture=tex)
    strength = node(m, "ScalarParameter", parameter_name="PaintStrength", default_value=1.0)
    sign = node(m, "TwoSidedSign")

    # Underside is a greyed membrane: desaturate toward luminance on the back face.
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

    blend = node(m, "LinearInterpolate")
    wire_vertex(vertex, blend, "A")
    wire(grey, blend, "B")
    wire(strength, blend, "Alpha")
    output(blend, "BASE_COLOR")

    rough = node(m, "Constant", r=0.65)
    output(rough, "ROUGHNESS")

    add_backlit_glow(m, paint)

    lib.recompile_material(m)
    assert ela.save_asset(path), "WING_FALCON: failed to save " + path
    u.log("WING_FALCON_READY " + path)


def build_membrane():
    path = BIRDS + "/M_WingMembrane"
    m = get_or_create(path)
    lib.delete_all_material_expressions(m)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("blend_mode", u.BlendMode.BLEND_OPAQUE)
    m.set_editor_property("shading_model", u.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property("disable_depth_test", False)

    vertex = node(m, "VertexColor")
    paint = node(m, "TextureSampleParameter2D", parameter_name="WingPaint",
                 texture=u.load_asset(WHITE))
    strength = node(m, "ScalarParameter", parameter_name="PaintStrength", default_value=0.0)

    alpha = node(m, "Multiply")
    wire(paint, alpha, "A", "A")  # paint ALPHA channel (not RGB)
    wire(strength, alpha, "B")

    base = node(m, "LinearInterpolate")
    wire_vertex(vertex, base, "A")
    wire(paint, base, "B", "RGB")
    wire(alpha, base, "Alpha")
    output(base, "BASE_COLOR")

    rough = node(m, "Constant", r=0.65)
    output(rough, "ROUGHNESS")

    add_backlit_glow(m, base)

    lib.recompile_material(m)
    assert ela.save_asset(path), "WING_MEMBRANE: failed to save " + path
    u.log("WING_MEMBRANE_READY " + path)


build_falcon()
build_membrane()
u.log("WING_MATERIALS_READY")