"""Rebuild M_Water (single-SLW-node fix) + the Eastern Station materials.

M_Water was failing to compile ("only one Single Layer Water Material node")
because the saved asset carried a stale shading_model alongside the
SingleLayerWaterMaterialOutput node, so it fell back to the grey Default
Material -- the checkered squares seen under the bay. Rebuild it here with the
shading_model reset, and refresh the station surfaces (brightened concrete +
InstancedStaticMeshes usage flag) in the same pass.
"""
import ast
import sys
from pathlib import Path
import unreal as u

here = Path(__file__).resolve().parent
sys.path.insert(0, str(here))
from project_guard import require_project
require_project()

source = here / 'create_shiomori_map.py'
tree = ast.parse(source.read_text(encoding='utf-8-sig'))
scope = dict(__file__=str(source), assets=u.AssetToolsHelpers.get_asset_tools(),
             lib=u.MaterialEditingLibrary, ela=u.EditorAssetLibrary)
exec(compile(ast.Module(body=[n for n in tree.body
                              if isinstance(n, (ast.Import, ast.ImportFrom, ast.FunctionDef))],
                       type_ignores=[]), str(source), 'exec'), scope)

# Water: the actual "checkered squares under the bay" fix.
scope['water_material']('Water', (0.008, 0.09, 0.16), rough=0.05, specular=0.8,
                        shore_color=(0.05, 0.36, 0.32))
u.log('WATER_MATERIAL_READY')

# Station surfaces: brightened concrete + granite + metal + dishes.
from eastern_station_materials import build as build_station_materials
build_station_materials(scope)
u.log('STATION_MATERIALS_READY')
u.SystemLibrary.execute_console_command(None, 'quit')
