"""Replace campus actors only. Requires full editor -ExecutePythonScript."""
import ast
import json
import sys
from pathlib import Path
import unreal as u

here=Path(__file__).resolve().parent
sys.path.insert(0,str(here))
import shiomori_campus
source=here/'create_shiomori_map.py'
tree=ast.parse(source.read_text(encoding='utf-8-sig'))
definitions=[n for n in tree.body if isinstance(n,(ast.Import,ast.ImportFrom,ast.FunctionDef))]
scope=dict(__file__=str(source),assets=u.AssetToolsHelpers.get_asset_tools(),lib=u.MaterialEditingLibrary,
           ela=u.EditorAssetLibrary,count=0)
exec(compile(ast.Module(body=definitions,type_ignores=[]),str(source),'exec'),scope)
scope['foliage_meshes']={key:[m for m in (u.load_asset('/Game/Nature/SM_%s%d'%(key,i)) for i in range(n)) if m]
                         for key,n in [('Grass',3),('Tree',2),('Fir',3),('Rock',4),('Leaf',2),('Fern',1)]}
world=u.EditorLoadingAndSavingUtils.load_map('/Game/Shiomori/Maps/SHIOMORI')
assert world
sub=u.get_editor_subsystem(u.EditorActorSubsystem)
legacy_prefixes=('Hotel ','Hangar ','Founder ','Launch field ','Garden retaining wall','Garden worktable','Garden bench',
                 'Campus shore path','Campus pine','Worktable leg','Torii ','Shrine ')
removed=[]
preserved_before={a.get_path_name() for a in sub.get_all_level_actors() if 'ShiomoriVolcanic' in [str(t) for t in a.tags]}
for a in list(sub.get_all_level_actors()):
    tags=[str(t) for t in a.tags];p=a.get_actor_location()
    old=any(t.startswith(legacy_prefixes) for t in tags)
    # Bench leg is also used by public promenade furnishings. Remove only the
    # old campus garden's exact location band, never all matching labels.
    old |= 'Bench leg' in tags and -48200<p.x<-46000 and -3650<p.y<-3200
    if shiomori_campus.TAG in tags or old:
        removed.append(a.get_actor_label());assert sub.destroy_actor(a)
shiomori_campus.install(scope)
preserved_after={a.get_path_name() for a in sub.get_all_level_actors() if 'ShiomoriVolcanic' in [str(t) for t in a.tags]}
assert preserved_before==preserved_after,'Campus rebuild changed volcanic actors'
assert u.EditorLoadingAndSavingUtils.save_map(world,'/Game/Shiomori/Maps/SHIOMORI')
report=dict(removed=removed,preserved_volcanic_actors=len(preserved_after),campus_actors=sum(shiomori_campus.TAG in [str(t) for t in a.tags] for a in sub.get_all_level_actors()))
here.parent.joinpath('Saved/campus-rebuild-report.json').write_text(json.dumps(report,indent=2))
u.log('CAMPUS_MAP_SAVED '+json.dumps(report))
u.SystemLibrary.execute_console_command(None,'quit')