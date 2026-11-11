"""Rebuild only the Yunsha sky material; do not regenerate or overwrite the level.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>.
Uses the generator's definitions without executing its world-building code.

Fix carried: UE5.8's MIR gives VectorParameter output 0 the type float3 (RGB);
the float4 lives on the 'RGBA' pin. SkyWind carries the .w time channel, so it
must be wired from the RGBA pin or the Custom node's SkyWind.w swizzle fails
("vector swizzle 'w' is out of bounds" -> Default Material -> dead sky).
"""
import ast
import sys
from pathlib import Path
import unreal as u

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

source = Path(__file__).with_name('create_yunsha_mp_map.py')
tree = ast.parse(source.read_text(encoding='utf-8'))
needed = {'expr', 'link', 'out', 'scalar', 'material_usage_flags', 'yunsha_sky_material'}
definitions = []
for item in tree.body:
    if isinstance(item, (ast.Import, ast.ImportFrom)):
        definitions.append(item)
    elif isinstance(item, ast.FunctionDef) and item.name in needed:
        definitions.append(item)
scope = dict(__file__=str(source))
exec(compile(ast.Module(body=definitions, type_ignores=[]), str(source), 'exec'), scope)
scope['A'] = u.AssetToolsHelpers.get_asset_tools()
scope['E'] = u.EditorAssetLibrary
scope['M'] = u.MaterialEditingLibrary
scope['ROOT'] = '/Game/Yunsha'
scope['yunsha_sky_material']()
u.log('SKY_REPAIR_PASS: yunsha sky material rebuilt; level unchanged')