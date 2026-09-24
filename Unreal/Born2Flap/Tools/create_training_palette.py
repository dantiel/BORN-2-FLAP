"""Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>.
Generates the small, reproducible training palette; no external artwork required.
"""
import unreal
palette = {
    'Ivory': (0.88, 0.83, 0.66), 'Teal': (0.015, 0.27, 0.30),
    'Gold': (1.0, 0.49, 0.055), 'Ink': (0.012, 0.025, 0.04),
    'Grass': (0.18, 0.31, 0.18), 'Leaves': (0.045, 0.16, 0.13),
    'Stone': (0.31, 0.39, 0.39), 'Sand': (0.62, 0.53, 0.32),
}
assets = unreal.AssetToolsHelpers.get_asset_tools()
for name, rgb in palette.items():
    path = '/Game/Training/M_' + name
    material = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else assets.create_asset('M_' + name, '/Game/Training', unreal.Material, unreal.MaterialFactoryNew())
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    color = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant3Vector)
    color.set_editor_property('constant', unreal.LinearColor(*rgb))
    unreal.MaterialEditingLibrary.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    if name == 'Gold':
        unreal.MaterialEditingLibrary.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    roughness = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant)
    roughness.set_editor_property('r', 0.8)
    unreal.MaterialEditingLibrary.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(path)
unreal.log('B2F_PALETTE_READY')
