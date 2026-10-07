"""Rebuild both wing membranes as two-sided masked wings with a backlit-only
glow (church-window look): the wing reads solid with legible paint detail, and
only emits light when the sun is behind the membrane — i.e. light shining
through from the other side.

  M_WingMembrane  -> RavenCrow + Prototype wing (WingPaint)
  M_FalconWing    -> Kestrel membrane wing   (WingPaint)

Physics of the glow:
  transmission = saturate(-dot(N, -SunDir) * dot(N, View))^2
where N is the vertex normal (world), SunDir is the sun's travel direction
(world, from the SkyAtmosphere "Atmosphere Sun Light Vector" node) and View is
the surface-to-camera vector. The product of the two signed dot terms is only
positive when the light and the camera sit on OPPOSITE sides of the membrane —
so the emissive fires only when the wing is backlit, never on the lit side.

Run once via the editor commandlet (close any running game/editor first):

  UnrealEditor-Cmd.exe "Unreal/Born2Flap/Born2Flap.uproject" -run=pythonscript \
    -script="Unreal/Born2Flap/Tools/patch_wing_translucent.py" \
    -unattended -nosplash -nullrhi -nosound
"""
import unreal as u

assets = u.AssetToolsHelpers.get_asset_tools()
lib = u.MaterialEditingLibrary
ela = u.EditorAssetLibrary

BIRDS = "/Game/Birds"

TRANSMISSION_HLSL = """\
float3 N = normalize(NormalWS);
float3 L = normalize(-SunDirection);   /* surface -> sun */
float3 V = normalize(-CameraVectorWS); /* surface -> camera */
float NoL = dot(N, L);
float NoV = dot(N, V);
float t = saturate(-NoL * NoV);        /* >0 only when light is behind the wing */
return t * t;
"""


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


def dither(m, name, default):
    # Screen-space dither on the opacity mask: a near-opaque mask keeps the wing
    # solid (detail legible) with only a faint stipple so backlight can pass.
    p = node(m, "ScalarParameter", parameter_name=name, default_value=default)
    d = node(m, "Custom",
             code=("float2 px = floor(Parameters.SvPosition.xy);\n"
                   "float d = frac(sin(dot(px, float2(12.9898, 78.233))) * 43758.5453);\n"
                   "return step(d, Opacity);"),
             output_type=u.CustomMaterialOutputType.CMOT_FLOAT1)
    pin = u.CustomInput()
    pin.set_editor_property("input_name", "Opacity")
    d.set_editor_property("inputs", [pin])
    wire(p, d, "Opacity")
    return d


def transmission(m, normal, sun, cam):
    t = node(m, "Custom", code=TRANSMISSION_HLSL,
             description="Backlit transmission (sun through membrane)",
             output_type=u.CustomMaterialOutputType.CMOT_FLOAT1)
    pins = []
    for name in ("NormalWS", "SunDirection", "CameraVectorWS"):
        pin = u.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    t.set_editor_property("inputs", pins)
    wire(normal, t, "NormalWS")
    wire(sun, t, "SunDirection")
    wire(cam, t, "CameraVectorWS")
    return t


def build(path, paint_param):
    ela.make_directory(BIRDS)
    m = u.load_asset(path) if ela.does_asset_exist(path) else assets.create_asset(
        path.rsplit('/', 1)[-1], BIRDS, u.Material, u.MaterialFactoryNew())
    lib.delete_all_material_expressions(m)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("blend_mode", u.BlendMode.BLEND_MASKED)
    m.set_editor_property("shading_model", u.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property("disable_depth_test", False)

    vertex = node(m, "VertexColor")
    paint = node(m, "TextureSampleParameter2D", parameter_name=paint_param,
                 texture=u.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    strength = node(m, "ScalarParameter", parameter_name="PaintStrength", default_value=0.0)
    alpha = node(m, "Multiply")
    wire(paint, alpha, "A", "RGB")
    wire(strength, alpha, "B")
    base = node(m, "LinearInterpolate")
    wire(vertex, base, "A", "")
    wire(paint, base, "B", "RGB")
    wire(alpha, base, "Alpha")
    output(base, "BASE_COLOR", "")

    rough = node(m, "Constant", r=0.65)
    output(rough, "ROUGHNESS")

    # Near-opaque: the wing is solid, not stippled-transparent. Only a faint
    # dither lets a little light through.
    output(dither(m, "WingOpacity", 0.97), "OPACITY_MASK")

    # Backlit-only glow: emissive = base * WingGlow * transmission(sun behind).
    normal = node(m, "VertexNormalWS")
    sun = node(m, "AtmosphericLightVector")
    cam = node(m, "CameraVectorWS")
    trans = transmission(m, normal, sun, cam)

    glow = node(m, "ScalarParameter", parameter_name="WingGlow", default_value=0.45)
    glowscaled = node(m, "Multiply")
    wire(glow, glowscaled, "A", "")
    wire(trans, glowscaled, "B", "")

    emissive = node(m, "Multiply")
    wire(base, emissive, "A", "")
    wire(glowscaled, emissive, "B", "")
    output(emissive, "EMISSIVE_COLOR", "")

    lib.recompile_material(m)
    assert ela.save_asset(path), "WING_TRANSLUCENT: failed to save " + path
    u.log("WING_TRANSLUCENT_READY " + path)


build(BIRDS + "/M_WingMembrane", "WingPaint")
build(BIRDS + "/M_FalconWing", "WingPaint")
