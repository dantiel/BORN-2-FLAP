import json, traceback
from pathlib import Path
import unreal as u

OUT = Path('V:/BORN 2 FLAP/_final_verify.json')
diag = {}
try:
    mesh = u.load_asset('/Game/Actor/SK_Founder/SkeletalMeshes/SK_Founder.SK_Founder')
    skel = mesh.get_editor_property('skeleton')
    diag['mesh'] = str(mesh)
    diag['skeleton'] = skel.get_path_name() if skel else None

    sub = u.get_editor_subsystem(u.EditorActorSubsystem)
    actor = sub.spawn_actor_from_class(u.SkeletalMeshActor, u.Vector(0,0,0), u.Rotator(0,0,0))
    comp = actor.get_component_by_class(u.SkeletalMeshComponent)
    comp.set_skeletal_mesh_asset(mesh)
    diag['num_bones'] = comp.get_num_bones()

    bones = ['mixamorig_LeftHand','mixamorig_RightHand','mixamorig_Head','mixamorig_LeftFoot','mixamorig_RightFoot']
    rest = {}
    for b in bones:
        try:
            loc = comp.get_socket_location(b)
            rest[b] = [round(loc.x,1), round(loc.y,1), round(loc.z,1)]
        except Exception as e:
            rest[b] = 'ERR:' + str(e)[:40]
    diag['rest_pose'] = rest
    sub.destroy_actor(actor)
except Exception:
    diag['fatal'] = traceback.format_exc()

OUT.write_text(json.dumps(diag, indent=2), encoding='utf-8')
print(json.dumps(diag, indent=2))