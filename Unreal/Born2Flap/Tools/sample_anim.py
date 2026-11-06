"""Sample the imported WALK animation on the founder skeleton headlessly.

Jumps the single-node animation to several positions and reports key bone
world locations so we can see whether the retargeted Mixamo clip is mirrored
or twisted vs the (correct) reference pose.

Run:
  UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript=Tools/sample_anim.py -unattended -nop4 -nosplash -RenderOffscreen -stdout
"""
import json, traceback
from pathlib import Path
import unreal as u

OUT = Path('V:/BORN 2 FLAP/_anim_sample.json')
diag = {}

BONES = ['mixamorig_Hips', 'mixamorig_Head',
         'mixamorig_LeftHand', 'mixamorig_RightHand',
         'mixamorig_LeftFoot', 'mixamorig_RightFoot',
         'mixamorig_LeftUpLeg', 'mixamorig_RightUpLeg',
         'mixamorig_LeftLeg', 'mixamorig_RightLeg']


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
    try:
        comp.set_update_animation_in_editor(True)
    except Exception as e:
        diag['update_editor_err'] = str(e)[:120]

    diag['walk_len'] = walk.get_editor_property('sequence_length') if walk else -1

    def sample(tag):
        out = {}
        for b in BONES:
            try:
                loc = comp.get_socket_location(b)
                out[b] = [round(loc.x, 1), round(loc.y, 1), round(loc.z, 1)]
            except Exception as e:
                out[b] = 'ERR:' + str(e)[:40]
        diag[tag] = out

    sample('ref_pose')

    # try to jump to mid-walk and re-sample
    for pos in (0.25, 0.5, 0.75):
        try:
            comp.set_position(pos)
            try:
                comp.refresh_bone_transforms()
            except Exception:
                pass
            try:
                comp.update_joints_from_animation()
            except Exception:
                pass
            sample('walk_pos_%.2f' % pos)
        except Exception as e:
            diag['pos_%.2f_err' % pos] = str(e)[:160]

    OUT.write_text(json.dumps(diag, indent=2), encoding='utf-8')
    u.log('ANIM_SAMPLE_DONE')


if __name__ == '__main__':
    try:
        main()
    except Exception:
        diag['fatal'] = traceback.format_exc()
        OUT.write_text(json.dumps(diag, indent=2), encoding='utf-8')
        u.log('ANIM_SAMPLE_FATAL')
