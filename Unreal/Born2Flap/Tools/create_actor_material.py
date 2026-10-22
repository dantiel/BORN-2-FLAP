"""Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>.
Generates the parameterized humanoid-avatar material used for actor skins.

M_Actor exposes a 'Color' vector parameter + a 'Roughness' scalar so the
C++ avatar can tint cloth/skin/hair/accent via UMaterialInstanceDynamic
without shipping one baked material per skin.
"""
import unreal


def ensure(path, factory):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    folder = path.rsplit('/', 1)[0]
    name = path.rsplit('/', 1)[1]
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, factory)


mat = ensure('/Game/Actor/M_Actor', unreal.MaterialFactoryNew())
unreal.MaterialEditingLibrary.delete_all_material_expressions(mat)

color = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionVectorParameter)
color.set_editor_property('parameter_name', 'Color')
color.set_editor_property('default_value', unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
unreal.MaterialEditingLibrary.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)

rough = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionScalarParameter)
rough.set_editor_property('parameter_name', 'Roughness')
rough.set_editor_property('default_value', 0.7)
unreal.MaterialEditingLibrary.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)

unreal.MaterialEditingLibrary.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset('/Game/Actor/M_Actor')
unreal.log('B2F_ACTOR_MATERIAL_READY')
