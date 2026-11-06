"""Decisively sample the WALK animation at mid-pose to detect mirror/twist.

Uses the AnimSingleNodeInstance set_position to jump mid-clip, then reads bone
world locations. Compares Left/Right against the (known-correct) reference pose.

Run:
  UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript=Tools/check_anim2.py -unattended -nop4 -nosplash -stdout
"""
import json, traceback
from pathlib import Path
import unreal as u

OUT = Path('V:/BORN 2 FLAP/_check_anim2.json')
diag = {}

BONES = ['mixamorig_Hips', 'mixamorig_Head',
         'mixamorig_LeftHand', 'mixamorig_RightHand',
         'mixamorig_LeftFoot', 'mixamorig_RightFoot',
         'mixamorig_LeftUpLeg', 'mixamorig_RightUpLeg',
         'mixamorig_LeftLeg', 'mixamorig_RightLeg']


def sample(comp):
    out = {}
    for b in BONES:
        try:
            loc = comp.get_socket_location(b)
            out[b] = [round(loc.x, 1), round(loc.y, 1), round(loc.z, 1)]
        except Exception as e:
            out[b] = 'ERR'
    return out


def main():
    world = u.EditorLoadingAndSavingUtils.load_map('/Game/Shiomori/Maps/SHIOMORI')
    sub = u.get_editor_subsystem(u.EditorActorSubsystem)
    cls = u.load_class(None, '/Script/Born2Flap.Born2FlapAvatar')
    actor = sub.spawn_actor_from_class(cls, u.Vector(0, 0, 0), u.Rotator(0, 0, 0))
    comp = actor.get_components_by_class(u.SkeletalMeshComponent)[0]
    mesh = u.load_asset('/Game/Actor/SK_Founder/SkeletalMeshes/SK_Founder.SK_Founder')
    walk = u.load_asset('/Game/Actor/SK_Founder/Animations/AM_Walk.AM_Walk')
    comp.set_skeletal_mesh_asset(mesh)
    comp.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE)
    comp.set_animation(walk)
    comp.set_update_animation_in_editor(True)

    diag['ref'] = sample(comp)

    inst = comp.get_anim_instance()
    diag['inst'] = str(inst)[:80]
    try:
        inst.set_position(0.5)
        diag['set_position_05'] = 'ok'
    except Exception as e:
        diag['set_position_05_err'] = str(e)[:160]

    try:
        comp.refresh_bone_transforms()
    except Exception as e:
        diag['refresh_err'] = str(e)[:120]
    try:
        comp.update_joints_from_animation()
    except Exception as e:
        diag['update_err'] = str(e)[:120]

    diag['mid_05'] = sample(comp)

    # also try position 0.5 via component set_position
    try:
        comp.set_position(0.5)
        comp.refresh_bone_transforms()
        diag['mid_05_comp'] = sample(comp)
    except Exception as e:
        diag['comp_set_pos_err'] = str(e)[:160]

    OUT.write_text(json.dumps(diag, indent=2), encoding='utf-8')
    u.log('CHECK_ANIM2_DONE')


if __name__ == '__main__':
    try:
        main()
    except Exception:
        diag['fatal'] = traceback.format_exc()
        OUT.write_text(json.dumps(diag, indent=2), encoding='utf-8')
        u.log('CHECK_ANIM2_FATAL')
