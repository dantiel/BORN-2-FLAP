"""Regenerate the locomotion clips (Walk / Run / StrafeRight / WalkBackward)
inside SK_Founder.glb as DENSE, perfectly-looping procedural gaits.

Why: the Mixamo FBX clips baked earlier are heavily keyframe-reduced (2-4
keys) and, in the case of Run, truncated to 0.333 s, so the loops "cut off"
and snap.  Procedural sinusoid gaits are smooth, seamless (first frame ==
last frame by construction) and speed-matched via the existing play-rate
scaling in Born2FlapAvatar.cpp.

Method (same convention as bake_mixamo_into_glb.py, verified there):
    rotation(t) = rest_rotation @ euler_xyz_to_quat(offset(t))
where offset(t) is a local euler (X=pitch, Y=yaw, Z=roll) in Mixamo's Lcl
Rotation convention.  Translation is left at the rest pose (in-place).

Run (Python, no UE needed):
    python Tools/generate_procedural_gait.py
"""
import struct, json, math
import numpy as np

GLB = 'Unreal/Born2Flap/SourceAssets/Avatars/SK_Founder.glb'
OUT = GLB
SAMPLES = 64          # keyframes per cycle
DURATION = 1.0        # one full gait cycle (s); play-rate scaling matches speed


def load_glb(path):
    d = open(path, 'rb').read()
    off = 12; js = None; binb = b''
    while off < len(d):
        clen, ct = struct.unpack_from('<II', d, off); off += 8
        c = d[off:off+clen]; off += clen
        if ct == 0x4E4F534A: js = json.loads(c.decode('utf-8'))
        elif ct == 0x004E4942: binb = c
    return js, bytearray(binb)


def write_glb(js, binb, path):
    jsb = json.dumps(js, separators=(',', ':')).encode('utf-8')
    if len(jsb) % 4: jsb += b' ' * (4 - len(jsb) % 4)
    if len(binb) % 4: binb += b'\x00' * (4 - len(binb) % 4)
    total = 12 + 8 + len(jsb) + 8 + len(binb)
    out = struct.pack('<III', 0x46546C67, 2, total)
    out += struct.pack('<II', len(jsb), 0x4E4F534A) + jsb
    out += struct.pack('<II', len(binb), 0x004E4942) + bytes(binb)
    open(path, 'wb').write(out)


def euler_xyz_to_quat(rx, ry, rz):
    rx, ry, rz = np.radians([rx, ry, rz])
    cx, sx = math.cos(rx/2), math.sin(rx/2)
    cy, sy = math.cos(ry/2), math.sin(ry/2)
    cz, sz = math.cos(rz/2), math.sin(rz/2)
    return np.array([
        sx*cy*cz - cx*sy*sz,
        cx*sy*cz + sx*cy*sz,
        cx*cy*sz - sx*sy*cz,
        cx*cy*cz + sx*sy*sz])


def quat_mul(q1, q2):
    x1, y1, z1, w1 = q1; x2, y2, z2, w2 = q2
    return np.array([
        w1*x2 + x1*w2 + y1*z2 - z1*y2,
        w1*y2 - x1*z2 + y1*w2 + z1*x2,
        w1*z2 + x1*y2 - y1*x2 + z1*w2,
        w1*w2 - x1*x2 - y1*y2 - z1*z2])


# ---------------------------------------------------------------------------
# Gait models.  Each returns a dict bone_name -> (rx, ry, rz) degrees of Lcl
# Rotation offset at phase theta in [0, 2*pi).  theta=0 = right foot contact.
# ---------------------------------------------------------------------------
def _walk(theta, amp, knee_base, knee_swing, arm_amp, elbow, lean):
    s = math.sin(theta); c = math.cos(theta)
    swing = max(0.0, s)   # 0..1, peaks at left mid-swing
    rswing = max(0.0, -s)
    o = {
        'mixamorig:Hips':        (-2*c*math.sin(theta), 2*s, 0.5*s),
        'mixamorig:Spine':       (lean + 1.5*s, 1.5*s, 0),
        'mixamorig:Spine1':      (1.0*s, 0.5*s, 0),
        'mixamorig:Spine2':      (0.5*s, 0, 0),
        'mixamorig:Neck':        (-lean*0.3, -0.5*s, 0),
        'mixamorig:Head':        (-lean*0.3, -0.5*s, 0),
        'mixamorig:LeftShoulder':  (-4*s, 0, 0),
        'mixamorig:RightShoulder': (4*s, 0, 0),
        'mixamorig:LeftArm':     (arm_amp*s, 0, 0),
        'mixamorig:RightArm':    (-arm_amp*s, 0, 0),
        'mixamorig:LeftForeArm': (-elbow - 6*s, 0, 0),
        'mixamorig:RightForeArm':(-elbow + 6*s, 0, 0),
        'mixamorig:LeftUpLeg':   (amp*s, 3*c, 1.5*c),
        'mixamorig:RightUpLeg':  (-amp*s, -3*c, -1.5*c),
        'mixamorig:LeftLeg':     (-knee_base - knee_swing*swing, 0, 0),
        'mixamorig:RightLeg':    (-knee_base - knee_swing*rswing, 0, 0),
        'mixamorig:LeftFoot':    (-8*s + 3, 0, 0),
        'mixamorig:RightFoot':   (8*s + 3, 0, 0),
    }
    return o


def _idle(theta):
    s = math.sin(theta)
    return {
        'mixamorig:Spine':  (3 + 1.5*s, 0.6*s, 0),
        'mixamorig:Spine1': (0.5*s, 0.3*s, 0),
        'mixamorig:Neck':   (-0.8, 0, 0),
        'mixamorig:Head':   (-0.8, 0, 0),
        'mixamorig:LeftArm':  (4*s, 0, 0),
        'mixamorig:RightArm': (-4*s, 0, 0),
        'mixamorig:LeftForeArm':  (-15 - 2*s, 0, 0),
        'mixamorig:RightForeArm': (-15 + 2*s, 0, 0),
        'mixamorig:LeftUpLeg':  (2*s, 0, 0),
        'mixamorig:RightUpLeg': (-2*s, 0, 0),
        'mixamorig:LeftLeg':  (-6 - 2*s, 0, 0),
        'mixamorig:RightLeg': (-6 + 2*s, 0, 0),
    }


CLIPS = [
    ('Walk',           lambda t: _walk(t, amp=24, knee_base=10, knee_swing=50, arm_amp=26, elbow=18, lean=6)),
    ('Run',            lambda t: _walk(t, amp=40, knee_base=60, knee_swing=40, arm_amp=55, elbow=95, lean=12)),
    ('WalkBackward',   lambda t: _walk(-t, amp=22, knee_base=10, knee_swing=48, arm_amp=22, elbow=18, lean=-2)),
    ('StrafeRight',    lambda t: _strafe(t)),
    ('Idle',           _idle),
]


def _strafe(theta):
    s = math.sin(theta); c = math.cos(theta)
    swing = max(0.0, s); rswing = max(0.0, -s)
    return {
        'mixamorig:Hips':        (0, 3*s, 0),
        'mixamorig:Spine':       (4 + 1*s, 0, 0),
        'mixamorig:LeftArm':     (15 + 8*s, 0, 0),
        'mixamorig:RightArm':    (15 - 8*s, 0, 0),
        'mixamorig:LeftForeArm': (-25 - 4*s, 0, 0),
        'mixamorig:RightForeArm':(-25 + 4*s, 0, 0),
        'mixamorig:LeftUpLeg':   (8 + 8*c, 14*s, 0),
        'mixamorig:RightUpLeg':  (8 - 8*c, -14*s, 0),
        'mixamorig:LeftLeg':     (-20 - 25*swing, 0, 0),
        'mixamorig:RightLeg':    (-20 - 25*rswing, 0, 0),
        'mixamorig:LeftFoot':    (2, 0, 0),
        'mixamorig:RightFoot':   (2, 0, 0),
    }


def build_animation(js, buf, name, model_fn):
    nodes = js['nodes']
    name_to_idx = {}
    for i, nd in enumerate(nodes):
        nm = nd.get('name', '')
        if nm:
            name_to_idx[nm] = i
            name_to_idx[nm.replace(':', '_')] = i
    bufferViews = js['bufferViews']; accessors = js['accessors']

    def add_bin(data):
        if len(buf) % 4: buf.extend(b'\x00' * (4 - len(buf) % 4))
        off = len(buf); buf.extend(data)
        bv = {'buffer': 0, 'byteOffset': off, 'byteLength': len(data)}
        bufferViews.append(bv); return len(bufferViews) - 1

    def add_acc(componentType, count, type_, bv_idx, mn=None, mx=None):
        acc = {'bufferView': bv_idx, 'componentType': componentType, 'count': count, 'type': type_}
        if mn is not None: acc['min'] = mn; acc['max'] = mx
        accessors.append(acc); return len(accessors) - 1

    times = [DURATION * k / (SAMPLES - 1) for k in range(SAMPLES)]
    samplers = []; channels = []
    for bname, off in model_fn(0.0).items():
        idx = name_to_idx.get(bname)
        if idx is None:
            continue
        rest = nodes[idx].get('rotation')
        rest = np.array(rest, dtype=np.float64) if rest is not None else np.array([0., 0., 0., 1.])
        qs = []
        for k in range(SAMPLES):
            theta = 2.0 * math.pi * k / (SAMPLES - 1)
            rx, ry, rz = model_fn(theta)[bname]
            q = quat_mul(rest, euler_xyz_to_quat(rx, ry, rz))
            qs.append(q / np.linalg.norm(q))
        t_data = struct.pack('<%df' % SAMPLES, *times)
        t_bv = add_bin(t_data)
        t_acc = add_acc(5126, SAMPLES, 'SCALAR', t_bv, [times[0]], [times[-1]])
        flat = [float(x) for q in qs for x in q]
        v_data = struct.pack('<%df' % len(flat), *flat)
        v_bv = add_bin(v_data)
        v_acc = add_acc(5126, SAMPLES, 'VEC4', v_bv)
        samplers.append({'input': t_acc, 'output': v_acc, 'interpolation': 'LINEAR'})
        channels.append({'sampler': len(samplers)-1, 'target': {'node': idx, 'path': 'rotation'}})
    return {'name': name, 'samplers': samplers, 'channels': channels}


def main():
    js, buf = load_glb(GLB)
    keep = [a for a in js.get('animations', []) if a.get('name') not in {c[0] for c in CLIPS}]
    js['animations'] = keep
    for name, fn in CLIPS:
        anim = build_animation(js, buf, name, fn)
        js['animations'].append(anim)
        print('generated %s: %d channels' % (name, len(anim['channels'])))
    write_glb(js, buf, OUT)
    print('wrote', OUT, 'animations=', len(js['animations']))


if __name__ == '__main__':
    main()
