"""Neon Abyss — luminous neon cosmos + Sanxingdui bronze + jellyfish materials.

Builds the material set the void level and the Neon Turtle reference:

  /Game/NeonAbyss/M_TurtleShell    vertex-colour carapace/head/hindfin livery
  /Game/NeonAbyss/M_TurtleFlipper  vertex-colour front flipper (two-sided)
  /Game/NeonAbyss/M_JellyBell      translucent emissive jellyfish bell
  /Game/NeonAbyss/M_Bronze         verdigris bronze + emissive gold seams

  /Game/NeonAbyss/M_Aether         unlit vertical-gradient aether backdrop
  /Game/NeonAbyss/M_Planet0..3     unlit gradient planet + fresnel rim glow
  /Game/NeonAbyss/M_PlanetRing     additive glowing ring band
  /Game/NeonAbyss/M_Nebula_*       additive soft radial-glow nebula sprites
  /Game/NeonAbyss/M_NebulaRainbow  additive 3-stop radial rainbow nebula
  /Game/NeonAbyss/M_Star           flat additive bright point (runtime swarm)
  /Game/NeonAbyss/M_StarWhite/Cyan/Gold/Magenta — tight star billboards
  /Game/NeonAbyss/M_QuasarCore     white-hot additive core (blooms)
  /Game/NeonAbyss/M_QuasarAccretion soft accretion-disc glow sprite
  /Game/NeonAbyss/M_QuasarJet      additive relativistic beam

Every material lifts colour into emissive glow (the neon read). Run:

  UnrealEditor-Cmd.exe "Unreal/Born2Flap/Born2Flap.uproject" -run=pythonscript \
    -script="Unreal/Born2Flap/Tools/create_neon_assets.py" -unattended -nosplash -nullrhi -nosound
"""
import unreal

lib = unreal.MaterialEditingLibrary
FOLDER = '/Game/NeonAbyss'


def _mat(path, name):
    unreal.EditorAssetLibrary.make_directory(FOLDER)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())


def _const(mat, value):
    n = lib.create_material_expression(mat, unreal.MaterialExpressionConstant)
    n.set_editor_property('r', value)
    return n


def _const2(mat, a, b):
    # Constant2Vector exposes R/G floats (DisplayName X/Y), NOT a LinearColor
    # 'constant' — that property only exists on Constant3/4Vector. The old code
    # crashed the whole cosmos bake at the first nebula sprite.
    n = lib.create_material_expression(mat, unreal.MaterialExpressionConstant2Vector)
    n.set_editor_property('R', a)
    n.set_editor_property('G', b)
    return n


def _const3(mat, colour):
    n = lib.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
    n.set_editor_property('constant', unreal.LinearColor(colour[0], colour[1], colour[2], 1.0))
    return n


def _texcoord(mat):
    n = lib.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate)
    n.set_editor_property('coordinate_index', 0)
    return n


def _connect_prop(node, prop):
    lib.connect_material_property(node, '', prop)


def _connect_vertex(vertex, dst, pin):
    """UE 5.8 exposes vertex colour on the default (unnamed) output pin, not 'RGB'.
    Fall back to 'RGB' for older engine builds so the graph never silently drops."""
    if not lib.connect_material_expressions(vertex, '', dst, pin):
        lib.connect_material_expressions(vertex, 'RGB', dst, pin)


def _link(src, dst, pin):
    """Connect src output '' -> dst input pin (pin '' for single inputs)."""
    lib.connect_material_expressions(src, '', dst, pin)


def _additive_unlit(mat, two_sided=True):
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', two_sided)
    lib.delete_all_material_expressions(mat)
    return mat


def vertex_colour_emissive(path, roughness=0.55, metallic=0.1,
                           glow=1.6, two_sided=False):
    """Vertex RGB -> BaseColor, plus Vertex RGB * Glow -> Emissive."""
    mat = _mat(path, path.rsplit('/', 1)[-1])
    if two_sided:
        mat.set_editor_property('two_sided', True)
    lib.delete_all_material_expressions(mat)
    vertex = lib.create_material_expression(mat, unreal.MaterialExpressionVertexColor)
    lib.connect_material_property(vertex, '', unreal.MaterialProperty.MP_BASE_COLOR)
    glow_p = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter)
    glow_p.set_editor_property('parameter_name', 'Glow')
    glow_p.set_editor_property('default_value', glow)
    emissive = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply)
    _connect_vertex(vertex, emissive, 'A')
    lib.connect_material_expressions(glow_p, '', emissive, 'B')
    lib.connect_material_property(emissive, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _connect_prop(_const(mat, roughness), unreal.MaterialProperty.MP_ROUGHNESS)
    _connect_prop(_const(mat, metallic), unreal.MaterialProperty.MP_METALLIC)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(path.rsplit('/', 1)[-1] + '_MATERIAL_READY')


def translucent_emissive(path, colour, glow=2.2, two_sided=False):
    """Flat emissive translucent body (jellyfish bell / glow shells)."""
    mat = _mat(path, path.rsplit('/', 1)[-1])
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    if two_sided:
        mat.set_editor_property('two_sided', True)
    lib.delete_all_material_expressions(mat)
    base = _const3(mat, colour)
    lib.connect_material_property(base, '', unreal.MaterialProperty.MP_BASE_COLOR)
    emit = _const3(mat, (colour[0] * glow, colour[1] * glow, colour[2] * glow))
    lib.connect_material_property(emit, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _connect_prop(_const(mat, 0.2), unreal.MaterialProperty.MP_ROUGHNESS)
    _connect_prop(_const(mat, 0.35), unreal.MaterialProperty.MP_OPACITY)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(path.rsplit('/', 1)[-1] + '_MATERIAL_READY')


def vertex_emissive_translucent(path, glow=2.4, two_sided=True):
    """Vertex RGB -> BaseColor + Emissive, translucent (jellyfish bell)."""
    mat = _mat(path, path.rsplit('/', 1)[-1])
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    if two_sided:
        mat.set_editor_property('two_sided', True)
    lib.delete_all_material_expressions(mat)
    vertex = lib.create_material_expression(mat, unreal.MaterialExpressionVertexColor)
    lib.connect_material_property(vertex, '', unreal.MaterialProperty.MP_BASE_COLOR)
    glow_p = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter)
    glow_p.set_editor_property('parameter_name', 'Glow')
    glow_p.set_editor_property('default_value', glow)
    emissive = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply)
    _connect_vertex(vertex, emissive, 'A')
    lib.connect_material_expressions(glow_p, '', emissive, 'B')
    lib.connect_material_property(emissive, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _connect_prop(_const(mat, 0.2), unreal.MaterialProperty.MP_ROUGHNESS)
    _connect_prop(_const(mat, 0.75), unreal.MaterialProperty.MP_OPACITY)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(path.rsplit('/', 1)[-1] + '_MATERIAL_READY')


def unlit_opaque(path, colour, two_sided=True):
    """Unlit opaque colour (the deep aether backdrop, planet bodies)."""
    mat = _mat(path, path.rsplit('/', 1)[-1])
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    if two_sided:
        mat.set_editor_property('two_sided', True)
    lib.delete_all_material_expressions(mat)
    base = _const3(mat, colour)
    lib.connect_material_property(base, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(path.rsplit('/', 1)[-1] + '_MATERIAL_READY')


def unlit_additive(path, colour, opacity=1.0):
    """Unlit additive colour (nebula sprites, stars, dust motes)."""
    mat = _mat(path, path.rsplit('/', 1)[-1])
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    lib.delete_all_material_expressions(mat)
    base = _const3(mat, colour)
    lib.connect_material_property(base, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _connect_prop(_const(mat, opacity), unreal.MaterialProperty.MP_OPACITY)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(path.rsplit('/', 1)[-1] + '_MATERIAL_READY')


def gradient_unlit(path, top, bottom, two_sided=True):
    """Unlit vertical (UV.V) gradient — aether backdrop / planet bodies."""
    mat = _mat(path, path.rsplit('/', 1)[-1])
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', two_sided)
    lib.delete_all_material_expressions(mat)
    uv = _texcoord(mat)
    mask = lib.create_material_expression(mat, unreal.MaterialExpressionComponentMask)
    mask.set_editor_property('r', False)
    mask.set_editor_property('g', True)
    mask.set_editor_property('b', False)
    mask.set_editor_property('a', False)
    _link(uv, mask, '')
    lerp = lib.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate)
    _link(_const3(mat, top), lerp, 'A')
    _link(_const3(mat, bottom), lerp, 'B')
    _link(mask, lerp, 'Alpha')
    _connect_prop(lerp, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(path.rsplit('/', 1)[-1] + '_MATERIAL_READY')


def planet_gradient(path, top, bottom, rim, rim_intensity=2.5, fresnel_exp=2.5):
    """Unlit gradient planet body + fresnel rim glow (reads as an atmosphere)."""
    mat = _mat(path, path.rsplit('/', 1)[-1])
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', True)
    lib.delete_all_material_expressions(mat)
    uv = _texcoord(mat)
    mask = lib.create_material_expression(mat, unreal.MaterialExpressionComponentMask)
    mask.set_editor_property('r', False)
    mask.set_editor_property('g', True)
    mask.set_editor_property('b', False)
    mask.set_editor_property('a', False)
    _link(uv, mask, '')
    lerp = lib.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate)
    _link(_const3(mat, top), lerp, 'A')
    _link(_const3(mat, bottom), lerp, 'B')
    _link(mask, lerp, 'Alpha')

    fres = lib.create_material_expression(mat, unreal.MaterialExpressionFresnel)
    fres.set_editor_property('exponent', fresnel_exp)
    rimc = _const3(mat, (rim[0] * rim_intensity, rim[1] * rim_intensity, rim[2] * rim_intensity))
    rimg = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply)
    _link(fres, rimg, 'A')
    _link(rimc, rimg, 'B')

    total = lib.create_material_expression(mat, unreal.MaterialExpressionAdd)
    _link(lerp, total, 'A')
    _link(rimg, total, 'B')
    _connect_prop(total, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(path.rsplit('/', 1)[-1] + '_MATERIAL_READY')


def sprite_glow(path, colour, intensity=3.0, softness=2.4):
    """Additive soft radial-falloff glow billboard (nebulae / stars / glows)."""
    mat = _additive_unlit(_mat(path, path.rsplit('/', 1)[-1]))
    uv = _texcoord(mat)
    d = lib.create_material_expression(mat, unreal.MaterialExpressionDistance)
    _link(uv, d, 'A')
    _link(_const2(mat, 0.5, 0.5), d, 'B')
    scaled = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply)
    _link(d, scaled, 'A')
    _link(_const(mat, softness), scaled, 'B')
    falloff = lib.create_material_expression(mat, unreal.MaterialExpressionOneMinus)
    _link(scaled, falloff, '')
    clamped = lib.create_material_expression(mat, unreal.MaterialExpressionClamp)
    clamped.set_editor_property('min_default', 0.0)
    clamped.set_editor_property('max_default', 1.0)
    _link(falloff, clamped, '')
    col = _const3(mat, (colour[0] * intensity, colour[1] * intensity, colour[2] * intensity))
    emit = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply)
    _link(col, emit, 'A')
    _link(clamped, emit, 'B')
    _connect_prop(emit, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(path.rsplit('/', 1)[-1] + '_MATERIAL_READY')


def sprite_rainbow(path, inner, mid, outer, intensity=3.0, softness=2.2):
    """Additive 3-stop radial rainbow nebula: inner -> mid -> outer hue ramp."""
    mat = _additive_unlit(_mat(path, path.rsplit('/', 1)[-1]))
    uv = _texcoord(mat)
    d = lib.create_material_expression(mat, unreal.MaterialExpressionDistance)
    _link(uv, d, 'A')
    _link(_const2(mat, 0.5, 0.5), d, 'B')
    t = lib.create_material_expression(mat, unreal.MaterialExpressionClamp)
    t.set_editor_property('min_default', 0.0)
    t.set_editor_property('max_default', 1.0)
    scaled = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply)
    _link(d, scaled, 'A')
    _link(_const(mat, softness), scaled, 'B')
    _link(scaled, t, '')

    two_t = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply)
    _link(t, two_t, 'A')
    _link(_const(mat, 2.0), two_t, 'B')
    a1 = lib.create_material_expression(mat, unreal.MaterialExpressionClamp)
    a1.set_editor_property('min_default', 0.0)
    a1.set_editor_property('max_default', 1.0)
    _link(two_t, a1, '')
    minus1 = lib.create_material_expression(mat, unreal.MaterialExpressionSubtract)
    _link(two_t, minus1, 'A')
    _link(_const(mat, 1.0), minus1, 'B')
    a2 = lib.create_material_expression(mat, unreal.MaterialExpressionClamp)
    a2.set_editor_property('min_default', 0.0)
    a2.set_editor_property('max_default', 1.0)
    _link(minus1, a2, '')

    stage1 = lib.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate)
    _link(_const3(mat, (inner[0] * intensity, inner[1] * intensity, inner[2] * intensity)), stage1, 'A')
    _link(_const3(mat, (mid[0] * intensity, mid[1] * intensity, mid[2] * intensity)), stage1, 'B')
    _link(a1, stage1, 'Alpha')
    stage2 = lib.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate)
    _link(stage1, stage2, 'A')
    _link(_const3(mat, (outer[0] * intensity, outer[1] * intensity, outer[2] * intensity)), stage2, 'B')
    _link(a2, stage2, 'Alpha')

    falloff = lib.create_material_expression(mat, unreal.MaterialExpressionOneMinus)
    _link(t, falloff, '')
    emit = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply)
    _link(stage2, emit, 'A')
    _link(falloff, emit, 'B')
    _connect_prop(emit, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(path.rsplit('/', 1)[-1] + '_MATERIAL_READY')


# --- Turtle + flipper (the vertex-colour neon livery) -------------------------
vertex_colour_emissive(FOLDER + '/M_TurtleShell', roughness=0.55, metallic=0.1, glow=1.6)
vertex_colour_emissive(FOLDER + '/M_TurtleFlipper', roughness=0.45, metallic=0.05, glow=1.8, two_sided=True)

# --- Jellyfish bell (vertex-colour translucent emissive, painted per-jelly) ----
vertex_emissive_translucent(FOLDER + '/M_JellyBell', glow=2.4, two_sided=True)

# --- Bronze ring (verdigris + gold seams) -------------------------------------
mat = _mat(FOLDER + '/M_Bronze', 'M_Bronze')
lib.delete_all_material_expressions(mat)
# Verdigris base + metallic so the patina reads as weathered bronze, not stone.
_connect_prop(_const(mat, 0.14), unreal.MaterialProperty.MP_ROUGHNESS)
_connect_prop(_const(mat, 0.72), unreal.MaterialProperty.MP_METALLIC)
patina = _const3(mat, (0.16, 0.42, 0.34))
lib.connect_material_property(patina, '', unreal.MaterialProperty.MP_BASE_COLOR)
# Emissive gold seam (the bronze that "opens" as the pilot draws near).
gold = _const3(mat, (0.99, 0.72, 0.16))
seam = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter)
seam.set_editor_property('parameter_name', 'GoldSeam')
seam.set_editor_property('default_value', 0.15)
seam_glow = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply)
lib.connect_material_expressions(gold, '', seam_glow, 'A')
lib.connect_material_expressions(seam, '', seam_glow, 'B')
lib.connect_material_property(seam_glow, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
lib.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(FOLDER + '/M_Bronze')
unreal.log('M_Bronze_MATERIAL_READY')

# --- Cosmos -------------------------------------------------------------------
# Backdrop: deep indigo floor -> luminous violet zenith (NOT near-black).
gradient_unlit(FOLDER + '/M_Aether', top=(0.26, 0.13, 0.50), bottom=(0.06, 0.04, 0.20))

# Soft radial-glow nebula sprites (a balanced six-hue neon palette).
sprite_glow(FOLDER + '/M_NebulaTeal', (0.00, 0.85, 0.80), intensity=2.8, softness=2.2)
sprite_glow(FOLDER + '/M_NebulaMagenta', (0.92, 0.18, 0.68), intensity=2.8, softness=2.2)
sprite_glow(FOLDER + '/M_NebulaGold', (1.00, 0.74, 0.24), intensity=2.6, softness=2.2)
sprite_glow(FOLDER + '/M_NebulaViolet', (0.55, 0.26, 0.95), intensity=3.0, softness=2.2)
sprite_glow(FOLDER + '/M_NebulaCyan', (0.20, 0.82, 1.00), intensity=2.8, softness=2.2)
sprite_glow(FOLDER + '/M_NebulaEmerald', (0.15, 0.95, 0.55), intensity=2.6, softness=2.2)
sprite_glow(FOLDER + '/M_NebulaRose', (0.95, 0.38, 0.72), intensity=2.8, softness=2.2)
# Rainbow nebula: teal heart -> magenta mid -> violet rim.
sprite_rainbow(FOLDER + '/M_NebulaRainbow', (0.00, 0.90, 0.85), (0.90, 0.25, 0.70), (0.45, 0.30, 1.00),
               intensity=3.0, softness=2.0)

# Stars: a flat additive bright point (runtime jellyfish swarm) + tight billboards.
unlit_additive(FOLDER + '/M_Star', (2.0, 2.2, 2.6))
sprite_glow(FOLDER + '/M_StarWhite', (0.95, 0.98, 1.00), intensity=4.5, softness=7.0)
sprite_glow(FOLDER + '/M_StarCyan', (0.55, 0.90, 1.00), intensity=4.0, softness=7.0)
sprite_glow(FOLDER + '/M_StarGold', (1.00, 0.82, 0.45), intensity=4.0, softness=7.0)
sprite_glow(FOLDER + '/M_StarMagenta', (1.00, 0.50, 0.90), intensity=4.0, softness=7.0)

# Quasars: white-hot core, soft accretion glow, and a bright relativistic beam.
sprite_glow(FOLDER + '/M_QuasarCore', (1.00, 1.00, 1.00), intensity=9.0, softness=10.0)
sprite_glow(FOLDER + '/M_QuasarAccretion', (0.60, 0.82, 1.00), intensity=2.4, softness=1.8)
unlit_additive(FOLDER + '/M_QuasarJet', (1.6, 2.2, 3.2))

# Planets: gradient bodies with a glowing atmosphere rim + a luminous ring band.
planet_gradient(FOLDER + '/M_Planet0', top=(0.50, 0.20, 0.60), bottom=(0.15, 0.05, 0.30),
                rim=(0.90, 0.40, 1.00), rim_intensity=2.5)
planet_gradient(FOLDER + '/M_Planet1', top=(0.20, 0.70, 0.70), bottom=(0.05, 0.20, 0.30),
                rim=(0.40, 0.95, 0.90), rim_intensity=2.5)
planet_gradient(FOLDER + '/M_Planet2', top=(0.90, 0.70, 0.30), bottom=(0.40, 0.15, 0.05),
                rim=(1.00, 0.85, 0.40), rim_intensity=2.5)
planet_gradient(FOLDER + '/M_Planet3', top=(0.30, 0.50, 0.90), bottom=(0.05, 0.10, 0.35),
                rim=(0.50, 0.80, 1.00), rim_intensity=2.5)
unlit_additive(FOLDER + '/M_PlanetRing', (1.2, 1.0, 0.6), opacity=0.65)