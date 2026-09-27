"""Reproducible satin-black vertex-colour material for the folded polygon bird."""
import unreal
path = '/Game/Birds/M_RavenShard'
unreal.EditorAssetLibrary.make_directory('/Game/Birds')
mat = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_RavenShard','/Game/Birds',unreal.Material,unreal.MaterialFactoryNew())
unreal.MaterialEditingLibrary.delete_all_material_expressions(mat)
vertex = unreal.MaterialEditingLibrary.create_material_expression(mat,unreal.MaterialExpressionVertexColor)
unreal.MaterialEditingLibrary.connect_material_property(vertex,'RGB',unreal.MaterialProperty.MP_BASE_COLOR)
for value, prop in ((.43,unreal.MaterialProperty.MP_ROUGHNESS),(.24,unreal.MaterialProperty.MP_METALLIC)):
    node = unreal.MaterialEditingLibrary.create_material_expression(mat,unreal.MaterialExpressionConstant)
    node.set_editor_property('r',value)
    unreal.MaterialEditingLibrary.connect_material_property(node,'',prop)
unreal.MaterialEditingLibrary.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(path)
unreal.log('RAVENCROW_MATERIAL_READY')
