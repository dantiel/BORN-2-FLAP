"""One-shot v2 bake: M_BreakingSurf v7 (white foam) + M_Water/M_Shallows
(bright teal, sky mirror, opacity floor, turbulence) + foam texture facts."""
import ast
import sys
from pathlib import Path
import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))


def load_scope(source_name):
    source = HERE / source_name
    tree = ast.parse(source.read_text(encoding='utf-8'))
    defs = [n for n in tree.body if isinstance(n, (ast.Import, ast.ImportFrom, ast.FunctionDef))]
    scope = dict(__file__=str(source), assets=u.AssetToolsHelpers.get_asset_tools(),
                 lib=u.MaterialEditingLibrary, ela=u.EditorAssetLibrary)
    exec(compile(ast.Module(body=defs, type_ignores=[]), str(source), 'exec'), scope)
    return scope


# 1) Breaking-surf foam -> v7 white.
fin = load_scope('finish_weather_assets.py')
fin['repair_breaking_surf']()

# 2) M_Water + M_Shallows -> bright teal, reflective, turbulent.
cs = load_scope('create_shiomori_map.py')
cs['water_material']('Water', (0.08, 0.52, 0.66), rough=0.02, specular=1.0,
                     shore_color=(0.12, 0.54, 0.60))
cs['water_material']('Shallows', (0.08, 0.50, 0.60), shore_color=(0.12, 0.56, 0.58))

# 3) Foam texture facts (diagnostic).
tex = u.load_asset('/Water/Textures/Foam/T_WaterFlow_01_Foam_Tiled')
if tex:
    try:
        u.log('FOAMTEX name=%s size=%dx%d srgb=%s compr=%s' % (
            tex.get_name(), tex.blueprint_get_size_x(), tex.blueprint_get_size_y(),
            tex.get_editor_property('srgb'), tex.get_editor_property('compression_settings')))
    except Exception as ex:
        u.log('FOAMTEX info failed: %s' % ex)
else:
    u.log('FOAMTEX MISSING')
u.log('WATER_V2_BAKE_DONE')