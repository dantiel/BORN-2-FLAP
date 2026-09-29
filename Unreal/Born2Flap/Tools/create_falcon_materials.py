"""Falcon texture-ready materials (one texture surface per falcon part).

  M_FalconBody  -> BodyPaint   (side-view body texture)
  M_FalconWing  -> WingPaint   (membrane wing texture, two-sided)
  M_FalconTail  -> TailPaint   (top-view tail texture)

Each is the vertex-colour look plus a UV0 paintable texture slot with a
PaintStrength scalar (default 0 -> unchanged base colour). Run from Unreal:

  UnrealEditor-Cmd.exe "Unreal/Born2Flap/Born2Flap.uproject" -run=pythonscript \
    -script="Unreal/Born2Flap/Tools/create_falcon_materials.py" -unattended -nosplash -nullrhi -nosound
"""
import unreal


def build_paintable(path: str, param_name: str, roughness: float,
                    metallic: float = 0.0, two_sided: bool = False) -> None:
    unreal.EditorAssetLibrary.make_directory('/Game/Birds')
    lib = unreal.MaterialEditingLibrary
    name = path.rsplit('/', 1)[-1]
    folder = path.rsplit('/', 1)[0]
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mat = unreal.load_asset(path)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    if two_sided:
        mat.set_editor_property('two_sided', True)
    lib.delete_all_material_expressions(mat)
    vertex = lib.create_material_expression(mat, unreal.MaterialExpressionVertexColor)
    paint = lib.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D)
    paint.set_editor_property('parameter_name', param_name)
    paint.set_editor_property('texture', unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    strength = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter)
    strength.set_editor_property('parameter_name', 'PaintStrength')
    strength.set_editor_property('default_value', 0.0)
    alpha = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply)
    lib.connect_material_expressions(paint, 'A', alpha, 'A')
    lib.connect_material_expressions(strength, '', alpha, 'B')
    mix = lib.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate)
    lib.connect_material_expressions(vertex, 'RGB', mix, 'A')
    lib.connect_material_expressions(paint, 'RGB', mix, 'B')
    lib.connect_material_expressions(alpha, '', mix, 'Alpha')
    lib.connect_material_property(mix, '', unreal.MaterialProperty.MP_BASE_COLOR)
    roughness_node = lib.create_material_expression(mat, unreal.MaterialExpressionConstant)
    roughness_node.set_editor_property('r', roughness)
    lib.connect_material_property(roughness_node, '', unreal.MaterialProperty.MP_ROUGHNESS)
    if metallic > 0.0:
        metallic_node = lib.create_material_expression(mat, unreal.MaterialExpressionConstant)
        metallic_node.set_editor_property('r', metallic)
        lib.connect_material_property(metallic_node, '', unreal.MaterialProperty.MP_METALLIC)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f'{name}_MATERIAL_READY')


build_paintable('/Game/Birds/M_FalconBody', 'BodyPaint', 0.43, 0.24)
build_paintable('/Game/Birds/M_FalconTail', 'TailPaint', 0.43, 0.24)
build_paintable('/Game/Birds/M_FalconWing', 'WingPaint', 0.65, 0.0, two_sided=True)
