"""Check whether the retargeted AM_Walk animation is on the founder skeleton and
read its raw bone track for LeftUpLeg/RightUpLeg to detect mirroring.

Run:
  UnrealEditor-Cmd Born2Flap.uproject -ExecutePythonScript=Tools/check_anim.py -unattended -nop4 -nosplash -stdout
"""
import json, traceback
from pathlib import Path
import unreal as u

OUT = Path('V:/BORN 2 FLAP/_check_anim.json')
diag = {}


def main():
    walk = u.load_asset('/Game/Actor/SK_Founder/Animations/AM_Walk.AM_Walk')
    skel = u.load_asset('/Game/Actor/SK_Founder/SkeletalMeshes/SK_Founder_Skeleton.SK_Founder_Skeleton')
    if not skel:
        # try to find skeleton
        found = []
        for p in u.EditorAssetLibrary.list_assets('/Game/Actor/SK_Founder', recursive=True, include_folder=False):
            a = u.load_asset(p)
            if a and isinstance(a, u.Skeleton):
                found.append(p)
        diag['skeleton_candidates'] = found
    else:
        diag['skeleton_loaded'] = str(skel)

    if walk:
        try:
            wskel = walk.get_editor_property('skeleton')
            diag['walk_skeleton'] = str(wskel)
            diag['walk_skeleton_name'] = wskel.get_name() if wskel else None
        except Exception as e:
            diag['walk_skeleton_err'] = str(e)[:200]

        diag['walk_len'] = walk.get_editor_property('sequence_length')
        diag['num_frames'] = walk.get_editor_property('num_frames')
        try:
            diag['num_tracks'] = walk.get_editor_property('animation_track_names')  # may fail
        except Exception:
            pass
        # try to read raw animation data
        for method in ['get_bone_animation_track', 'get_raw_animation_track', 'get_animation_track_names']:
            if hasattr(walk, method):
                diag['has_%s' % method] = True

    # component evaluation attempt with AnimSingleNodeInstance
    try:
        world = u.EditorLoadingAndSavingUtils.load_map('/Game/Shiomori/Maps/SHIOMORI')
        sub = u.get_editor_subsystem(u.EditorActorSubsystem)
        cls = u.load_class(None, '/Script/Born2Flap.Born2FlapAvatar')
        actor = sub.spawn_actor_from_class(cls, u.Vector(0, 0, 0), u.Rotator(0, 0, 0))
        comp = actor.get_components_by_class(u.SkeletalMeshComponent)[0]
        mesh = u.load_asset('/Game/Actor/SK_Founder/SkeletalMeshes/SK_Founder.SK_Founder')
        comp.set_skeletal_mesh_asset(mesh)
        comp.set_animation_mode(u.AnimationMode.ANIMATION_SINGLE_NODE)
        comp.set_animation(walk)
        comp.set_update_animation_in_editor(True)
        inst = comp.get_anim_instance()
        diag['anim_instance'] = str(inst)
        if inst:
            for m in ['set_position', 'set_play_rate', 'set_position_in_editor', 'montage_set_position']:
                diag['inst_has_%s' % m] = hasattr(inst, m)
            # try set_position on instance
            try:
                inst.set_position(0.5)
                diag['inst_set_position'] = 'ok'
            except Exception as e:
                diag['inst_set_position_err'] = str(e)[:160]
            try:
                comp.refresh_bone_transforms()
            except Exception:
                pass
            # sample bones now
            for b in ['mixamorig_LeftUpLeg', 'mixamorig_RightUpLeg', 'mixamorig_LeftFoot', 'mixamorig_RightFoot', 'mixamorig_LeftHand', 'mixamorig_RightHand']:
                try:
                    loc = comp.get_socket_location(b)
                    diag['pos_' + b] = [round(loc.x, 1), round(loc.y, 1), round(loc.z, 1)]
                except Exception as e:
                    diag['pos_' + b] = 'ERR:' + str(e)[:40]
    except Exception as e:
        diag['component_err'] = str(e)[:200]

    OUT.write_text(json.dumps(diag, indent=2), encoding='utf-8')
    u.log('CHECK_ANIM_DONE')


if __name__ == '__main__':
    try:
        main()
    except Exception:
        diag['fatal'] = traceback.format_exc()
        OUT.write_text(json.dumps(diag, indent=2), encoding='utf-8')
        u.log('CHECK_ANIM_FATAL')
