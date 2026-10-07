"""Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>.
Grayed-out, slightly translucent material for replay ghosts (non-champion).
The champion keeps M_Gold; regular ghosts use M_Spirit instead of the old dark M_Ink.
"""
import unreal as u

path = '/Game/Training/M_Spirit'
m = u.load_asset(path) or u.AssetToolsHelpers.get_asset_tools().create_asset('M_Spirit', '/Game/Training', u.Material, u.MaterialFactoryNew())
lib = u.MaterialEditingLibrary
lib.delete_all_material_expressions(m)
m.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property('two_sided', True)

base = lib.create_material_expression(m, u.MaterialExpressionConstant3Vector)
base.set_editor_property('constant', u.LinearColor(0.46, 0.47, 0.50))
lib.connect_material_property(base, '', u.MaterialProperty.MP_BASE_COLOR)

opacity = lib.create_material_expression(m, u.MaterialExpressionConstant)
opacity.set_editor_property('r', 0.55)
lib.connect_material_property(opacity, '', u.MaterialProperty.MP_OPACITY)

roughness = lib.create_material_expression(m, u.MaterialExpressionConstant)
roughness.set_editor_property('r', 0.8)
lib.connect_material_property(roughness, '', u.MaterialProperty.MP_ROUGHNESS)

lib.recompile_material(m)
u.EditorAssetLibrary.save_asset(path)
u.log('B2F_SPIRIT_MATERIAL_READY')
