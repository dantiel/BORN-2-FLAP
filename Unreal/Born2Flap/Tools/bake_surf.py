"""One-shot: bake ONLY M_BreakingSurf (v4 bulletproof unlit). Water untouched."""
import ast
import sys
from pathlib import Path
import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
source = HERE / 'finish_weather_assets.py'
tree = ast.parse(source.read_text(encoding='utf-8'))
defs = [n for n in tree.body if isinstance(n, (ast.Import, ast.ImportFrom, ast.FunctionDef))]
scope = dict(__file__=str(source), assets=u.AssetToolsHelpers.get_asset_tools(),
             lib=u.MaterialEditingLibrary, ela=u.EditorAssetLibrary)
exec(compile(ast.Module(body=defs, type_ignores=[]), str(source), 'exec'), scope)
scope['repair_breaking_surf']()
u.log('BREAKING_SURF_V4_READY')
