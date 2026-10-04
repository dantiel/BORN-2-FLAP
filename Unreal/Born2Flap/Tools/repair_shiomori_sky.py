"""Rebuild only the sky material; do not regenerate or overwrite the level.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>.
Uses the generator's definitions without executing its world-building code.
"""
import ast
from pathlib import Path
import unreal as u

source = Path(__file__).with_name('create_shiomori_map.py')
tree = ast.parse(source.read_text(encoding='utf-8'))
names = {'PARHELION_WEATHER', 'PARHELION_BY_NAME', 'PARHELION_DEFAULT'}
definitions = []
for item in tree.body:
    if isinstance(item, (ast.Import, ast.ImportFrom, ast.FunctionDef)):
        definitions.append(item)
    elif isinstance(item, ast.Assign) and any(
            isinstance(t, ast.Name) and t.id in names for t in item.targets):
        definitions.append(item)
scope = dict(__file__=str(source),
             assets=u.AssetToolsHelpers.get_asset_tools(),
             lib=u.MaterialEditingLibrary, ela=u.EditorAssetLibrary)
exec(compile(ast.Module(body=definitions, type_ignores=[]), str(source), 'exec'), scope)
assert u.EditorLoadingAndSavingUtils.load_map('/Game/Shiomori/Maps/SHIOMORI')
suns = [a for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
        if isinstance(a, u.DirectionalLight)
        and a.light_component.get_editor_property('atmosphere_sun_light')]
assert len(suns) == 1, 'Expected one atmosphere sun in Shiomori'
forward = suns[0].get_actor_forward_vector()
scope['sun_parhelion_material']((-forward.x, -forward.y, -forward.z))
scope['fpv_fisheye_material']()
u.log('SKY_REPAIR_PASS: scene-preserving full display saved; level unchanged')
