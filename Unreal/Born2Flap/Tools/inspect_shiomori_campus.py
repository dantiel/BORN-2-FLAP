import json
from pathlib import Path
import unreal as u

world = u.EditorLoadingAndSavingUtils.load_map('/Game/Shiomori/Maps/SHIOMORI')
actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
rows = []
for a in actors:
    p = a.get_actor_location()
    if p.x < -39000 and -31000 < p.y < 24000:
        origin, extent = a.get_actor_bounds(False)
        rows.append(dict(label=a.get_actor_label(), tags=[str(t) for t in a.tags],
                         position=[p.x,p.y,p.z], bounds=[origin.x,origin.y,origin.z,extent.x,extent.y,extent.z]))
assets = u.EditorAssetLibrary.list_assets('/Game/Birds', recursive=True)
birds=[]
for path in assets:
    obj=u.load_asset(path)
    if isinstance(obj,u.StaticMesh):
        b=obj.get_bounds()
        birds.append(dict(path=path,size=[2*b.box_extent.x,2*b.box_extent.y,2*b.box_extent.z]))
result=dict(actors=rows,birds=birds,engine=u.SystemLibrary.get_engine_version())
Path(__file__).resolve().parents[1].joinpath('Saved/campus-inspection.json').write_text(json.dumps(result,indent=2))
u.log('CAMPUS_INSPECTION_READY actors=%d birds=%d'%(len(rows),len(birds)))
u.SystemLibrary.execute_console_command(None,'quit')
