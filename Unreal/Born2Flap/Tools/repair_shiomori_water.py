"""Rebuild the two ocean shaders only, preserving all map geometry."""
import ast
from pathlib import Path
import unreal as u
source=Path(__file__).with_name('create_shiomori_map.py')
tree=ast.parse(source.read_text(encoding='utf-8'))
defs=[n for n in tree.body if isinstance(n,(ast.Import,ast.ImportFrom,ast.FunctionDef))]
scope=dict(__file__=str(source),assets=u.AssetToolsHelpers.get_asset_tools(),lib=u.MaterialEditingLibrary,ela=u.EditorAssetLibrary)
exec(compile(ast.Module(body=defs,type_ignores=[]),str(source),'exec'),scope)
scope['water_material']('Water',(.015,.075,.11))
scope['water_material']('Shallows',(.025,.16,.18),shore_color=(.05,.36,.32))
u.log('SHIOMORI_WATER_REPAIR_READY')
