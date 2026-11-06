"""Headless verify: sample the WALK animation on the reflattened founder and
confirm the feet actually move (walk cycle) and the body stays human-shaped.

Run:
  UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript=Tools/verify_anim.py -unattended -nop4 -nosplash -stdout
"""
import json
import traceback
from pathlib import Path
import unreal as u

OUT = Path('V:/BORN 2 FLAP/_anim_verify.json')

diag = {}

try:
    mesh = u.load_asset('/Game/Actor/SK_Founder/SkeletalMeshes/SK_Founder.SK_Founder')
    walk = u.load_asset('/Game/Actor/SK_Founder/Animations/AM_Walk.AM_Walk')
    idle = u.load_asset('/Game/Actor/SK_Founder/Animations/AM_Idle.AM_Idle')
    run  = u.load_asset('/Game/Actor/SK_Founder/Animations/AM_Run.AM_Run')

    diag['mesh'] = str(mesh)
    diag['walk'] = str(walk)
    diag['walk_len'] = walk.get_play_length() if walk else None

    # spawn the avatar and grab its Body skeletal-mesh component
    sub = u.get_editor_subsystem(u.EditorActorSubsystem)
    cls = u.load_class(None, '/Script/Born2Flap.Born2FlapAvatar')
    actor = sub.spawn_actor_from_class(cls, u.Vector(0, 0, 0), u.Rotator(0, 0, 0))
    comps = actor.get_components_by_class(u.SkeletalMeshComponent)
    comp = comps[0]
    comp.set_skeletal_mesh_asset(mesh)
    comp.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE)
    comp.set_animation(walk)

    bone_of_interest = ['mixamorig_LeftFoot', 'mixamorig_RightFoot',
                        'mixamorig_LeftHand', 'mixamorig_RightHand',
                        'mixamorig_Hips', 'mixamorig_Head']

    def sample():
        out = {}
        for b in bone_of_interest:
            try:
                loc = comp.get_socket_location(b)
                out[b] = [round(loc.x, 2), round(loc.y, 2), round(loc.z, 2)]
            except Exception as e:
                out[b] = 'ERR:' + str(e)[:60]
        return out

    length = walk.get_play_length() if walk else 1.0
    frames = {}
    for i, frac in enumerate([0.0, 0.25, 0.5, 0.75]):
        t = frac * length
        try:
            comp.set_position(t)
            comp.update_joints_from_animation()
        except Exception as e:
            diag['set_pos_err'] = str(e)[:200]
        frames['t%.2f' % frac] = sample()

    diag['frames'] = frames

    # heuristic: feet should change vertical position across the cycle
    try:
        lf = [frames['t%.2f' % f]['mixamorig_LeftFoot'][2] for f in (0.0, 0.25, 0.5, 0.75)]
        rf = [frames['t%.2f' % f]['mixamorig_RightFoot'][2] for f in (0.0, 0.25, 0.5, 0.75)]
        diag['left_foot_z_range'] = round(max(lf) - min(lf), 3)
        diag['right_foot_z_range'] = round(max(rf) - min(rf), 3)
        diag['feet_move'] = (diag['left_foot_z_range'] > 0.5 or diag['right_foot_z_range'] > 0.5)
    except Exception as e:
        diag['heuristic_err'] = str(e)[:200]

except Exception:
    diag['fatal'] = traceback.format_exc()

Path(OUT).write_text(json.dumps(diag, indent=2), encoding='utf-8')
print(json.dumps(diag, indent=2))