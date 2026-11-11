"""Refresh sand materials without regenerating terrain or vegetation."""
import ast
from pathlib import Path
import sys
import unreal as u
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
from project_guard import require_project
require_project()
A=u.AssetToolsHelpers.get_asset_tools()
E=u.EditorAssetLibrary
M=u.MaterialEditingLibrary
ROOT='/Game/Yunsha'
# Reuse material graph helpers without running the world generator.
source=ast.parse((HERE/'create_yunsha_map.py').read_text())
helpers={'expr','link','out','scalar','color','custom','material_usage_flags','material'}
exec(compile(ast.Module(body=[n for n in source.body if isinstance(n,ast.FunctionDef) and n.name in helpers],type_ignores=[]),str(HERE/'create_yunsha_map.py'),'exec'))
from yunsha_sand import normal_code, normal_inputs
from yunsha_oasis import ground_material
sand=material('WindRippleSand',(1.0,.77,.48))
out(custom(sand,normal_code(),normal_inputs(sand,expr)),'NORMAL')
sand.set_editor_property('tangent_space_normal',False)
ground_material(sand,expr,custom,out)
M.recompile_material(sand)
assert E.save_loaded_asset(sand)
# Rebuild the excavation material graph without running its geometry builder.
namespace=dict(globals())
code=(HERE/'yunsha_excavation.py').read_text().split('    # Replace coarse terrain')[0]
exec(compile(code,str(HERE/'yunsha_excavation.py'),'exec'),namespace)
namespace['build'](None,None,material,None,None,expr,custom,out)
u.log('YUNSHA_SAND_SAVED')
