"""Replace only volcanic actors in the current saved map; preserve other edits."""
import ast
import sys
from pathlib import Path
import unreal as u

here=Path(__file__).resolve().parent
sys.path.insert(0,str(here))
import shiomori_volcanic
source=here/'create_shiomori_map.py'
tree=ast.parse(source.read_text(encoding='utf-8'))
definitions=[n for n in tree.body if isinstance(n,(ast.Import,ast.ImportFrom,ast.FunctionDef))]
scope=dict(__file__=str(source),assets=u.AssetToolsHelpers.get_asset_tools(),lib=u.MaterialEditingLibrary,
    ela=u.EditorAssetLibrary,count=0)
exec(compile(ast.Module(body=definitions,type_ignores=[]),str(source),'exec'),scope)
scope['foliage_meshes']={key:[u.load_asset('/Game/Nature/SM_%s%d'%(key,i)) for i in range(n)] for key,n in [('Grass',3),('Tree',2),('Fir',3)]}
world=u.EditorLoadingAndSavingUtils.load_map('/Game/Shiomori/Maps/SHIOMORI')
assert world
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
removed=0
for actor in actors.get_all_level_actors():
    tags={str(t) for t in actor.tags}
    if tags.intersection({'ShiomoriVolcanic','Basalt island foundation','Volcanic outcrop','Island shore rock'}):
        assert actors.destroy_actor(actor)
        removed+=1
u.log('VOLCANIC_REMOVED '+str(removed))
shiomori_volcanic.install(scope)
assert u.EditorLoadingAndSavingUtils.save_map(world,'/Game/Shiomori/Maps/SHIOMORI')
u.log('VOLCANIC_MAP_SAVED')
u.SystemLibrary.execute_console_command(None,'quit')
