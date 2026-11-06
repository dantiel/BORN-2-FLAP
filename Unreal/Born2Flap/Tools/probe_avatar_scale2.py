"""Read-only probe 2: dump skeleton ref pose + python API surface."""
import json
import unreal as u

out = {}

sk = u.load_asset('/Game/Actor/Clean/Founder_rigged_clean/SkeletalMeshes/Object_72_Skeleton')
if sk:
    out['skel_type'] = str(type(sk))
    out['skel_attrs'] = sorted(a for a in dir(sk) if not a.startswith('_'))
    for meth in ('get_reference_pose',):
        if hasattr(sk, meth):
            try:
                pose = getattr(sk, meth)()
                out['ref_pose_len'] = len(pose)
                first = []
                for i in range(min(8, len(pose))):
                    t = pose[i].translation
                    scl = pose[i].scale3d
                    first.append(dict(t=[round(v, 4) for v in (t.x, t.y, t.z)],
                                      s=[round(v, 4) for v in (scl.x, scl.y, scl.z)]))
                out['ref_pose_first8'] = first
            except Exception as e:
                out['ref_pose_error'] = str(e)

m = u.load_asset('/Game/Actor/Clean/Founder_rigged_clean/SkeletalMeshes/Object_72')
if m:
    out['mesh_attrs'] = sorted(a for a in dir(m) if not a.startswith('_'))
    for meth in ('get_bone_map', 'get_bone_count', 'get_num_bones'):
        if hasattr(m, meth):
            try:
                v = getattr(m, meth)()
                out['mesh_' + meth] = list(v)[:10] if not isinstance(v, int) else v
            except Exception as e:
                out['mesh_' + meth + '_err'] = str(e)

a = u.load_asset('/Game/Actor/Clean/Animations/AM_Idle')
if a:
    out['anim_attrs'] = sorted(x for x in dir(a) if not x.startswith('_'))

s = 'PROBE_AVATAR2 ' + json.dumps(out)
u.log(s)
print(s)
u.SystemLibrary.execute_console_command(None, 'quit')
