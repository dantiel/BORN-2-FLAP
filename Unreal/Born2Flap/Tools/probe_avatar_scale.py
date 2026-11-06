"""Read-only probe: avatar skeleton units, mesh bounds, animation bindings."""
import json
import unreal as u

out = {}


def skel_info(path, label):
    m = u.load_asset(path)
    if not m:
        out[label] = 'MISSING'
        return
    info = {}
    try:
        info['bones'] = m.get_bone_count()
        names = [str(m.get_bone_name(i)) for i in range(m.get_bone_count())]
        info['names'] = names
    except Exception as e:
        info['bones_error'] = str(e)
        out[label] = info
        return
    pose = None
    try:
        sk = m.get_skeleton()
        pose = sk.get_reference_pose() if hasattr(sk, 'get_reference_pose') else None
    except Exception as e:
        info['pose_error'] = str(e)
    key = {}
    if pose is not None:
        for i, name in enumerate(names):
            if i < len(pose):
                t = pose[i].translation
                if any(k in name for k in ('Hips', 'Spine2', 'Head', 'LeftFoot', 'RightFoot',
                                           'LeftHand', 'RightHand', 'rootJoint')):
                    key[name] = [round(v, 4) for v in (t.x, t.y, t.z)]
    info['key_pose'] = key
    out[label] = info


def mesh_info(path, label):
    m = u.load_asset(path)
    if not m:
        out[label] = 'MISSING'
        return
    b = m.get_bounds()
    out[label] = dict(
        origin=[round(v, 3) for v in (b.origin.x, b.origin.y, b.origin.z)],
        box=[round(v, 3) for v in (b.box_extent.x, b.box_extent.y, b.box_extent.z)],
        radius=round(b.sphere_radius, 3))


skel_info('/Game/Actor/Clean/Founder_rigged_clean/SkeletalMeshes/Object_72', 'CLEAN_skeleton')
skel_info('/Game/Actor/Founder_rigged/SkeletalMeshes/Object_72', 'OLD_skeleton')
for n in (72, 73, 75):
    mesh_info('/Game/Actor/Clean/Founder_rigged_clean/SkeletalMeshes/Object_%d' % n, 'CLEAN_mesh_%d' % n)
    mesh_info('/Game/Actor/Founder_rigged/SkeletalMeshes/Object_%d' % n, 'OLD_mesh_%d' % n)

for an in ('AM_Idle', 'AM_Walk', 'AM_Throw', 'AM_Standing'):
    a = u.load_asset('/Game/Actor/Clean/Animations/%s' % an)
    if not a:
        out['anim_%s' % an] = 'MISSING'
        continue
    d = {}
    try:
        d['frames'] = a.get_number_of_frames()
    except Exception as e:
        d['frames_error'] = str(e)
    try:
        d['length'] = round(a.get_play_length(), 3)
    except Exception as e:
        d['length_error'] = str(e)
    try:
        sk = a.get_skeleton()
        d['skeleton'] = str(sk.get_path_name()) if sk else None
    except Exception as e:
        d['skeleton_error'] = str(e)
    out['anim_%s' % an] = d

s = 'PROBE_AVATAR ' + json.dumps(out)
u.log(s)
print(s)
u.SystemLibrary.execute_console_command(None, 'quit')