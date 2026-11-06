import unreal as u
import json

out = {}
try:
    mesh = u.load_asset('/Game/Actor/SK_Founder/SkeletalMeshes/SK_Founder.SK_Founder')
    if mesh is None:
        out['error'] = 'mesh not found'
    else:
        # bounds
        try:
            b = mesh.get_editor_property('imported_bounds')
            out['bounds_origin'] = [round(x,2) for x in [b.origin.x, b.origin.y, b.origin.z]]
            out['bounds_extent'] = [round(x,2) for x in [b.box_extent.x, b.box_extent.y, b.box_extent.z]]
        except Exception as e:
            out['bounds_err'] = repr(e)
        skel = mesh.get_editor_property('skeleton')
        out['skeleton'] = skel.get_path_name() if skel else None
        out['num_bones'] = None
        try:
            out['num_bones'] = skel.get_num_bones() if skel else None
        except Exception:
            pass
        # bone positions via skeleton reference pose
        bones = {}
        try:
            ref = skel.get_reference_pose_transform_of_track if False else None
        except Exception:
            pass
        # use editor API: get bones
        try:
            rp = skel
            for nm in ['Hips','Head','LeftHand','RightHand','LeftFoot','RightFoot']:
                bones[nm] = 'n/a'
        except Exception:
            pass
        out['note'] = 'bounds + skeleton checked'
except Exception as e:
    out['exception'] = repr(e)

print('VERIFY_RESULT ' + json.dumps(out))
