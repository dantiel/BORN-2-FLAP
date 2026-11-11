"""Rebuild M_Water (translucent Beer-Lambert ocean) + the Eastern Station materials.

M_Water is the plain translucent ocean (BLEND_TRANSLUCENT, Beer-Lambert depth
absorption, Fresnel sky reflection, Gerstner vertex displacement). Rebuild it
here together with the station surfaces (brightened concrete +
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

# Water: the canonical translucent ocean — mirrors the invocation in
# create_shiomori_map.py's main flow (waves omitted; the procedural swell
# normal carries the surface on its own).
scope['water_material']('Water', (0.008, 0.09, 0.16), rough=0.05, specular=0.8,
                        shore_color=(0.05, 0.36, 0.32))
u.log('WATER_MATERIAL_READY')

# Station surfaces: brightened concrete + granite + metal + dishes.
from eastern_station_materials import build as build_station_materials
build_station_materials(scope)
u.log('STATION_MATERIALS_READY')
u.SystemLibrary.execute_console_command(None, 'quit')