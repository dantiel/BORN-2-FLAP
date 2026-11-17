"""One-shot: re-bake M_Water + M_Shallows with the foam term lifted into the
emissive core (foam is self-luminous white and can never shade to black).
Same tuned colours as repair_shiomori_water.py (2026-11-15 bake)."""
import ast
import sys
from pathlib import Path
import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
source = HERE / 'create_shiomori_map.py'
tree = ast.parse(source.read_text(encoding='utf-8'))
defs = [n for n in tree.body if isinstance(n, (ast.Import, ast.ImportFrom, ast.FunctionDef))]
scope = dict(__file__=str(source), assets=u.AssetToolsHelpers.get_asset_tools(),
             lib=u.MaterialEditingLibrary, ela=u.EditorAssetLibrary)
exec(compile(ast.Module(body=defs, type_ignores=[]), str(source), 'exec'), scope)
# Teal-family colours (the tuning invariant: deep MUST stay bright teal, never
# near-black navy — a dark "deep" reads as a black slab under an overcast sky).
scope['water_material']('Water', (0.10, 0.52, 0.58), rough=0.02, specular=1.0,
                        shore_color=(0.16, 0.62, 0.55))
scope['water_material']('Shallows', (0.08, 0.46, 0.44), shore_color=(0.16, 0.62, 0.55))
u.log('WATER_EMISSIVE_FOAM_READY')