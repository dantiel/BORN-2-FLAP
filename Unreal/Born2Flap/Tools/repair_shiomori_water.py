"""Rebuild the two ocean shaders only, preserving all map geometry."""
import ast
import sys
from pathlib import Path
import unreal as u
source=Path(__file__).with_name('create_shiomori_map.py')
sys.path.insert(0,str(source.parent))
tree=ast.parse(source.read_text(encoding='utf-8'))
defs=[n for n in tree.body if isinstance(n,(ast.Import,ast.ImportFrom,ast.FunctionDef))]
scope=dict(__file__=str(source),assets=u.AssetToolsHelpers.get_asset_tools(),lib=u.MaterialEditingLibrary,ela=u.EditorAssetLibrary)
exec(compile(ast.Module(body=defs,type_ignores=[]),str(source),'exec'),scope)
scope['water_material']('Water',(0.07,0.42,0.48),rough=0.02,specular=1.0,shore_color=(0.12,0.52,0.48))
scope['water_material']('Shallows',(.05,.36,.38),shore_color=(.12,.52,.48))
u.log('SHIOMORI_WATER_REPAIR_READY')