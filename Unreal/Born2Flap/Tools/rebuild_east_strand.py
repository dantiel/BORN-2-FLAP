"""Rebuild only the beach/headland materials, then refresh their east derivative."""
import ast
import sys
from pathlib import Path
import unreal as u

here = Path(__file__).resolve().parent
sys.path.insert(0, str(here))
from project_guard import require_project
require_project()
source = here/'create_shiomori_map.py'
tree = ast.parse(source.read_text(encoding='utf-8-sig'))
scope = dict(__file__=str(source), assets=u.AssetToolsHelpers.get_asset_tools(),
             lib=u.MaterialEditingLibrary, ela=u.EditorAssetLibrary)
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n, (ast.Import, ast.ImportFrom, ast.FunctionDef))],
                       type_ignores=[]), str(source), 'exec'), scope)

def textures(prefix, channels):
    result = tuple(u.load_asset(prefix+c) for c in channels)
    assert all(result), prefix
    return result

sand = textures('/Game/Shiomori/Textures/T_SandBeach_', ('Diffuse','Normal','Rough'))
rock = textures('/Game/Shiomori/Textures/T_Basalt_', ('Diffuse','Normal','Rough'))
grass = textures('/Game/Nature/Textures/T_aerial_grass_rock_', ('Diffuse','nor_dx','Rough'))
scope['sand_material']('Sand', sand[0], sand[1], rough_tex=sand[2], tiling_cm=300.,
                      normal_strength=.65, pebble=(*rock, (.55,.55,.56), 36000.,44800.))
scope['sand_material']('WetSand', sand[0], sand[1], rough_tex=sand[2], tiling_cm=300.,
                      normal_strength=.65)
scope['coastal_terrain_material']('Headland',sand,grass,rock,scale_cm=240.)
u.log('EAST_STRAND_MATERIALS_READY')
# M_EastShore embeds the Headland graph, so refresh it after its source.
exec(compile((here/'create_east_shore_assets.py').read_text(),str(here/'create_east_shore_assets.py'),'exec'),
     {'__file__':str(here/'create_east_shore_assets.py')})