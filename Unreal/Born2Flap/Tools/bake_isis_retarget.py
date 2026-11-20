"""Re-bake the 8 Mixamo clips into SK_Isis.glb with CORRECT retargeting.

Root cause of Isis's "knees backward" / broken animation: the original
bake_isis_into_glb.py used the SAME formula as bake_mixamo_into_glb.py:

    q(t) = rest_gltf @ euler(LclRotation(t))

That formula is only valid when the GLB skeleton's rest pose equals the
Mixamo T-pose rest (true for John/SK_Founder, which IS a Mixamo-rigged
character). Isis (the Desert Queen) is a 24-bone NON-Mixamo rig whose rest
rotations live in a DIFFERENT local-axis frame — e.g. LeftUpLeg rest is
~175 deg around X (roll) vs the Founder's ~180 deg around Z (yaw). Baking
`rest_isis @ euler` therefore applies Mixamo's hip-flexion X-rotation in
Isis's frame where X means something else -> the knees bend BACKWARD.

Correct retarget preserves the world-space DELTA from bind:

    delta(t)  = rest_s @ R(t) @ rest_s^-1     (source world-space motion)
    q(t)      = delta(t) @ rest_t             (apply to target bind)

where rest_s = Mixamo rest rotation (taken from SK_Founder.glb, the known-
good Mixamo-rigged reference), rest_t = Isis's rest rotation, R(t) =
euler(LclRotation(t)). At t=0 this reduces to rest_t (Isis bind pose), and
the motion is the same world rotation as the Founder's -> knees bend forward.

Run (pure Python, no UE):
    python Tools/bake_isis_retarget.py
Then re-import headless (see repair_avatars_commandlet.py `_fix_isis`).
"""
import importlib.util
from pathlib import Path

import numpy as np

_HERE = Path(__file__).resolve().parent
_PROJECT = _HERE.parent          # Unreal/Born2Flap

GLB      = _PROJECT / 'SourceAssets/Mixamo/Characters/SK_Isis.glb'
REF_GLB  = _PROJECT / 'SourceAssets/Avatars/SK_Founder.glb'
CLIP_DIR = _PROJECT / 'SourceAssets/Mixamo/Clips_Colon'

CLIPS = [
    ('Idle.fbx',         'Idle'),
    ('Walk.fbx',         'Walk'),
    ('Run.fbx',          'Run'),
    ('Throw.fbx',        'Throw'),
    ('Catch.fbx',        'Catch'),
    ('Standing.fbx',     'Standing'),
    ('StrafeRight.fbx',  'StrafeRight'),
    ('WalkBackward.fbx', 'WalkBackward'),
]

spec = importlib.util.spec_from_file_location('bake', _HERE / 'bake_mixamo_into_glb.py')
bake = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bake)


def _name_map(js):
    m = {}
    for i, nd in enumerate(js['nodes']):
        nm = nd.get('name', '')
        if nm:
            m[nm] = i
            m[nm.replace(':', '_')] = i   # FBX uses underscores
    return m


def _rest_dict(js):
    """bone name -> rest rotation quaternion (normalised)."""
    d = {}
    for nd in js['nodes']:
        nm = nd.get('name', '')
        if not nm:
            continue
        r = nd.get('rotation', [0.0, 0.0, 0.0, 1.0])
        q = np.array(r, dtype=np.float64)
        q = q / np.linalg.norm(q)
        d[nm] = q
        d[nm.replace(':', '_')] = q
    return d


def bake_retarget_one(js, buf, anim, name, rest_s, rest_t, name_to_idx):
    """Like bake.bake_one but with world-space-delta retargeting."""
    bufferViews = js['bufferViews']
    accessors = js['accessors']

    def add_bin(data):
        if len(buf) % 4:
            buf.extend(b'\x00' * (4 - len(buf) % 4))
        off = len(buf)
        buf.extend(data)
        bufferViews.append({'buffer': 0, 'byteOffset': off, 'byteLength': len(data)})
        return len(bufferViews) - 1

    def add_acc(componentType, count, type_, bv_idx, mn=None, mx=None):
        acc = {'bufferView': bv_idx, 'componentType': componentType,
               'count': count, 'type': type_}
        if mn is not None:
            acc['min'] = mn
            acc['max'] = mx
        accessors.append(acc)
        return len(accessors) - 1

    samplers = []
    channels = []
    for bname, d in anim.items():
        if 'rot' not in d:
            continue
        r = d['rot']
        n = len(r['t'])
        if n == 0:
            continue
        idx = name_to_idx.get(bname)
        if idx is None:
            continue
        rs = rest_s.get(bname)
        rt = rest_t.get(bname)
        if rs is None or rt is None:
            continue
        rs_inv = np.array([-rs[0], -rs[1], -rs[2], rs[3]])  # unit quat inverse
        qs = []
        for k in range(n):
            R = bake.euler_xyz_to_quat([r['x'][k], r['y'][k], r['z'][k]])
            # delta = rest_s @ R @ rest_s^-1   (world-space motion)
            delta = bake.quat_mul(bake.quat_mul(rs, R), rs_inv)
            # q = delta @ rest_t             (apply to target bind)
            q = bake.quat_mul(delta, rt)
            q = q / np.linalg.norm(q)
            qs.append(q)
        t_data = __import__('struct').pack('<%df' % n, *r['t'])
        t_bv = add_bin(t_data)
        t_acc = add_acc(5126, n, 'SCALAR', t_bv, [min(r['t'])], [max(r['t'])])
        flat = []
        for q in qs:
            flat.extend([float(q[0]), float(q[1]), float(q[2]), float(q[3])])
        v_data = __import__('struct').pack('<%df' % len(flat), *flat)
        v_bv = add_bin(v_data)
        v_acc = add_acc(5126, n, 'VEC4', v_bv)
        samplers.append({'input': t_acc, 'output': v_acc, 'interpolation': 'LINEAR'})
        channels.append({'sampler': len(samplers) - 1,
                         'target': {'node': idx, 'path': 'rotation'}})
    return {'name': name, 'samplers': samplers, 'channels': channels}


def main():
    js, binb = bake.load_glb(str(GLB))
    ref_js, _ = bake.load_glb(str(REF_GLB))

    rest_s = _rest_dict(ref_js)          # Mixamo rest (from Founder)
    rest_t = _rest_dict(js)              # Isis rest
    name_to_idx = _name_map(js)

    buf = bytearray(binb)
    js['animations'] = []
    for fbx_name, anim_name in CLIPS:
        src = CLIP_DIR / fbx_name
        if not src.exists():
            print('SKIP (missing)', src)
            continue
        anim = bake.extract_fbx_anim(str(src))
        obj = bake_retarget_one(js, buf, anim, anim_name, rest_s, rest_t, name_to_idx)
        js['animations'].append(obj)
        nkeys = 0
        if obj['samplers']:
            nkeys = js['accessors'][obj['samplers'][0]['input']]['count']
        print('baked %s: %d channels, %d keys' % (anim_name, len(obj['channels']), nkeys))

    bake.write_glb(js, bytes(buf), str(GLB))
    print('wrote %s (%d animations)' % (GLB, len(js['animations'])))


if __name__ == '__main__':
    main()
