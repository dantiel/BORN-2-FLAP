"""Read-only probe 3: skeleton AnimPose names + translation magnitudes."""
import json
import unreal as u

out = {}

for label, path in (
    ('CLEAN', '/Game/Actor/Clean/Founder_rigged_clean/SkeletalMeshes/Object_72_Skeleton'),
    ('OLD', '/Game/Actor/Founder_rigged/SkeletalMeshes/Object_72_Skeleton'),
):
    sk = u.load_asset(path)
    if not sk:
        out[label] = 'MISSING'
        continue
    try:
        pose = sk.get_reference_pose()
        names = [str(n) for n in pose.names]
        xforms = pose.transforms
        out[label + '_bones'] = len(names)
        key = {}
        for i, name in enumerate(names):
            if any(k in name for k in ('Hips', 'Spine2', 'Head', 'LeftFoot', 'RightFoot',
                                       'LeftHand', 'RightHand', 'rootJoint', 'Armature',
                                       'Object_4', 'RootNode')):
                t = xforms[i].translation
                scl = xforms[i].scale3d
                key[name] = dict(t=[round(v, 4) for v in (t.x, t.y, t.z)],
                                 s=[round(v, 4) for v in (scl.x, scl.y, scl.z)])
        out[label + '_key'] = key
    except Exception as e:
        out[label + '_error'] = str(e)

s = 'PROBE_AVATAR3 ' + json.dumps(out)
u.log(s)
print(s)
u.SystemLibrary.execute_console_command(None, 'quit')
