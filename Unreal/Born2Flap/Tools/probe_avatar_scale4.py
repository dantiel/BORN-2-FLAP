"""Read-only probe 4: AnimPose attribute surface + transforms."""
import json
import unreal as u

out = {}
sk = u.load_asset('/Game/Actor/Clean/Founder_rigged_clean/SkeletalMeshes/Object_72_Skeleton')
try:
    pose = sk.get_reference_pose()
    out['pose_attrs'] = sorted(a for a in dir(pose) if not a.startswith('_'))
    if hasattr(pose, 'transforms'):
        xforms = pose.transforms
        out['pose_transforms_len'] = len(xforms)
        first = []
        for i in range(min(10, len(xforms))):
            t = xforms[i].translation
            scl = xforms[i].scale3d
            first.append(dict(t=[round(v, 4) for v in (t.x, t.y, t.z)],
                              s=[round(v, 4) for v in (scl.x, scl.y, scl.z)]))
        out['pose_first10'] = first
        # magnitudes
        mags = sorted(((round(xforms[i].translation.size(), 3), i) for i in range(len(xforms))), reverse=True)
        out['pose_largest_mag'] = mags[:10]
except Exception as e:
    out['pose_error'] = str(e)

a = u.load_asset('/Game/Actor/Clean/Animations/AM_Idle')
if a:
    props = {}
    for p in ('sequence_length', 'raw_data', 'bone_compression_settings',
              'compressed_track_offsets', 'number_of_frames', 'rate_scale'):
        try:
            v = a.get_editor_property(p)
            props[p] = str(v)[:200]
        except Exception:
            pass
    out['anim_props'] = props

s = 'PROBE_AVATAR4 ' + json.dumps(out)
u.log(s)
print(s)
u.SystemLibrary.execute_console_command(None, 'quit')
