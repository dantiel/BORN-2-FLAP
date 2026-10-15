"""Neon Abyss — emissive livery + Sanxingdui bronze + jellyfish materials.

Builds the material set the void level and the Neon Turtle reference:

  /Game/NeonAbyss/M_TurtleShell   vertex-colour carapace/head/hindfin livery
  /Game/NeonAbyss/M_TurtleFlipper vertex-colour front flipper (two-sided)
  /Game/NeonAbyss/M_JellyBell     translucent emissive jellyfish bell
  /Game/NeonAbyss/M_Bronze        verdigris bronze + emissive gold seams
  /Game/NeonAbyss/M_Nebula        additive soft billboard (nebula clouds)
  /Game/NeonAbyss/M_Star          unlit additive point (stars / dust)

Every material lifts vertex colour into emissive glow (the neon read). Run:

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


def _const3(mat, colour):
    n = lib.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
    n.set_editor_property('constant', unreal.LinearColor(colour[0], colour[1], colour[2], 1.0))
    return n


def _connect_prop(node, prop):
    lib.connect_material_property(node, '', prop)


def vertex_colour_emissive(path, roughness=0.55, metallic=0.1,
                           glow=1.6, two_sided=False):
    """Vertex RGB -> BaseColor, plus Vertex RGB * Glow -> Emissive."""
    mat = _mat(path, path.rsplit('/', 1)[-1])
    if two_sided:
        mat.set_editor_property('two_sided', True)
    lib.delete_all_material_expressions(mat)
    vertex = lib.create_material_expression(mat, unreal.MaterialExpressionVertexColor)
    lib.connect_material_property(vertex, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
    glow_p = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter)
    glow_p.set_editor_property('parameter_name', 'Glow')
    glow_p.set_editor_property('default_value', glow)
    emissive = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply)
    lib.connect_material_expressions(vertex, 'RGB', emissive, 'A')
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
    lib.connect_material_property(vertex, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
    glow_p = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter)
    glow_p.set_editor_property('parameter_name', 'Glow')
    glow_p.set_editor_property('default_value', glow)
    emissive = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply)
    lib.connect_material_expressions(vertex, 'RGB', emissive, 'A')
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

# --- Cosmos (additive billboards) ---------------------------------------------
unlit_additive(FOLDER + '/M_Nebula', (0.30, 0.20, 0.55), opacity=0.5)
unlit_additive(FOLDER + '/M_Star', (0.85, 0.90, 1.00), opacity=1.0)
unlit_opaque(FOLDER + '/M_Aether', (0.02, 0.03, 0.10))
unlit_opaque(FOLDER + '/M_Planet', (0.35, 0.16, 0.42))

unreal.log('NEON_ASSETS_READY')