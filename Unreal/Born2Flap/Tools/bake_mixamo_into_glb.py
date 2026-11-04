"""Bake Mixamo FBX locomotion clips directly into the flattened Founder GLB as
glTF animations, so mesh + skeleton + animation all travel through the SAME
glTF importer (no FBX retarget / axis-conversion mismatch).

Why: the Mixamo FBX animation, when retargeted by UE's FBX importer onto the
glTF-imported skeleton, had Y-inverted bone local translations -> "crumpled"
pose.  Baking the animation into the GLB keeps everything in one consistent
frame (glTF Y-up, left=+X).

Method (verified numerically):
  rotation(t) = rest_rotation @ euler_xyz_to_quat(LclRotation(t))
  translation = skipped (root motion handled by pawn movement; in-place anim)

Run (Python, no UE needed):
  python Tools/bake_mixamo_into_glb.py
"""
import struct, json
import numpy as np

GLB = 'Unreal/Born2Flap/SourceAssets/Avatars/SK_Founder.glb'
OUT = 'Unreal/Born2Flap/SourceAssets/Avatars/SK_Founder.glb'
MIXAMO_DIR = 'Unreal/Born2Flap/SourceAssets/Mixamo'
FBX_TIME = 4618615800.0  # FBX ticks per second

CLIPS = [
    ('Idle.fbx',         'Idle'),
    ('Standing.fbx',     'Standing'),
    ('Walk.fbx',         'Walk'),
    ('Run.fbx',          'Run'),
    ('RunMedium.fbx',    'RunMedium'),
    ('WalkBackward.fbx', 'WalkBackward'),
    ('StrafeRight.fbx',  'StrafeRight'),
    ('StartWalk.fbx',    'StartWalk'),
    ('StopWalk.fbx',     'StopWalk'),
    ('Throw.fbx',        'Throw'),
    ('ThrowIn.fbx',      'ThrowIn'),
    ('Catch.fbx',        'Catch'),
    ('Lift.fbx',         'Lift'),
]


# ---------- binary FBX reader ----------
def read_node(buf, off, depth):
    if off + 25 > len(buf):
        raise RuntimeError('bad off')
    end = struct.unpack_from('<Q', buf, off)[0]
    numprops = struct.unpack_from('<Q', buf, off+8)[0]
    namelen = buf[off+24]
    if end > len(buf):
        end = len(buf)
    name = buf[off+25:off+25+namelen]
    cur = off + 25 + namelen
    props = []
    for _ in range(min(numprops, 4000)):
        if cur >= len(buf):
            break
        t = buf[cur:cur+1]; cur += 1
        if t == b'Y':
            props.append(('int16', struct.unpack_from('<h', buf, cur)[0])); cur += 2
        elif t == b'C':
            props.append(('bool', buf[cur] != 0)); cur += 1
        elif t == b'I':
            props.append(('int32', struct.unpack_from('<i', buf, cur)[0])); cur += 4
        elif t == b'F':
            props.append(('float', struct.unpack_from('<f', buf, cur)[0])); cur += 4
        elif t == b'D':
            props.append(('double', struct.unpack_from('<d', buf, cur)[0])); cur += 8
        elif t == b'L':
            props.append(('int64', struct.unpack_from('<q', buf, cur)[0])); cur += 8
        elif t == b'S':
            ln = struct.unpack_from('<I', buf, cur)[0]; cur += 4
            props.append(('string', buf[cur:cur+ln])); cur += ln
        elif t == b'R':
            ln = struct.unpack_from('<I', buf, cur)[0]; cur += 4
            props.append(('raw', buf[cur:cur+ln])); cur += ln
        elif t in (b'd', b'f', b'i', b'l', b'b'):
            esz = {'d':8,'f':4,'i':4,'l':8,'b':1}[t.decode()]
            arrlen = struct.unpack_from('<I', buf, cur)[0]; cur += 4
            cur += 8
            if arrlen > len(buf) - cur:
                arrlen = len(buf) - cur
            cnt = arrlen // esz if (esz and arrlen > 0) else 0
            if cnt > 20000000:
                cnt = 0
            fmt = {'d':'d','f':'f','i':'i','l':'q','b':'B'}[t.decode()]
            try:
                arr = struct.unpack_from('<%d%s' % (cnt, fmt), buf, cur)
            except Exception:
                arr = ()
            cur += arrlen
            props.append(('arr:'+t.decode(), arr))
        else:
            props.append(('UNKNOWN', t))
    children = []
    while cur < end and cur < len(buf) and depth < 200:
        child, cur = read_node(buf, cur, depth+1)
        children.append(child)
    return {'name': name, 'props': props, 'children': children}, end


def parse(path):
    buf = open(path, 'rb').read()
    off = 27
    roots = []
    while off < len(buf) - 1:
        node, off = read_node(buf, off, 0)
        roots.append(node)
    return roots


def find_all(node, name, out):
    if node['name'] == name:
        out.append(node)
    for c in node['children']:
        find_all(c, name, out)
    return out


def node_id(node):
    for p in node['props']:
        if p[0] == 'int64':
            return p[1]
    return None


def extract_fbx_anim(fbx_path):
    roots = parse(fbx_path)
    alln = {'name': b'', 'props': [], 'children': roots}
    models = find_all(alln, b'Model', [])
    curnodes = find_all(alln, b'AnimationCurveNode', [])
    curves = find_all(alln, b'AnimationCurve', [])
    crecs = find_all(alln, b'C', [])

    model_name = {}
    for m in models:
        mid = node_id(m)
        nm = None
        for p in m['props']:
            if p[0] == 'string':
                s = p[1].decode('utf-8', 'replace')
                if 'mixamorig' in s:
                    nm = s.split('::')[-1].split('\x00')[0].replace(':', '_')
                    break
        if nm:
            model_name[mid] = nm

    curve_by_id = {}
    for cv in curves:
        cid = node_id(cv)
        kt = None; kv = None
        for c in cv['children']:
            if c['name'] == b'KeyTime':
                for p in c['props']:
                    if p[0].startswith('arr'):
                        kt = list(p[1])
            if c['name'] == b'KeyValueFloat':
                for p in c['props']:
                    if p[0].startswith('arr'):
                        kv = list(p[1])
        curve_by_id[cid] = {'times': kt, 'values': kv}

    op = []
    for cr in crecs:
        vals = []
        for x in cr['props']:
            vals.append(x[1].decode('utf-8', 'replace') if x[0] == 'string' else x[1])
        if vals and vals[0] == 'OP':
            op.append((vals[1], vals[2], vals[3]))

    curnode_to_model = {}
    for (a, b, prop) in op:
        if prop.startswith('Lcl'):
            curnode_to_model[a] = (b, prop)

    curnode_axes = {}
    for (a, b, prop) in op:
        if prop.startswith('d|'):
            curnode_axes.setdefault(b, {})[prop] = a

    anim = {}
    for cnid, (mid, prop) in curnode_to_model.items():
        bname = model_name.get(mid)
        if not bname:
            continue
        axes = curnode_axes.get(cnid)
        if not axes:
            continue
        chans = {}
        times = None
        for axis, curve_id in axes.items():
            c = curve_by_id.get(curve_id)
            if not c or c['times'] is None:
                continue
            t = [x / FBX_TIME for x in c['times']]
            v = list(c['values'])
            chans[axis] = v
            times = t
        if times is None:
            continue
        key = 'rot' if prop == 'Lcl Rotation' else 'trs'
        anim.setdefault(bname, {})[key] = {
            't': times, 'x': chans.get('d|X', []),
            'y': chans.get('d|Y', []), 'z': chans.get('d|Z', [])}
    return anim


# ---------- math ----------
def euler_xyz_to_quat(e):
    rx, ry, rz = np.radians(e)
    cx, sx = np.cos(rx/2), np.sin(rx/2)
    cy, sy = np.cos(ry/2), np.sin(ry/2)
    cz, sz = np.cos(rz/2), np.sin(rz/2)
    w = cx*cy*cz + sx*sy*sz
    x = sx*cy*cz - cx*sy*sz
    y = cx*sy*cz + sx*cy*sz
    z = cx*cy*sz - sx*sy*cz
    return np.array([x, y, z, w])


def quat_mul(q1, q2):
    x1,y1,z1,w1 = q1; x2,y2,z2,w2 = q2
    return np.array([
        w1*x2 + x1*w2 + y1*z2 - z1*y2,
        w1*y2 - x1*z2 + y1*w2 + z1*x2,
        w1*z2 + x1*y2 - y1*x2 + z1*w2,
        w1*w2 - x1*x2 - y1*y2 - z1*z2])


def load_glb(path):
    d = open(path, 'rb').read()
    off = 12; js = None; binb = b''
    while off < len(d):
        clen, ct = struct.unpack_from('<II', d, off); off += 8
        c = d[off:off+clen]; off += clen
        if ct == 0x4E4F534A: js = json.loads(c.decode('utf-8'))
        elif ct == 0x004E4942: binb = c
    return js, binb


def write_glb(js, binb, path):
    jsb = json.dumps(js, separators=(',', ':')).encode('utf-8')
    if len(jsb) % 4: jsb += b' ' * (4 - len(jsb) % 4)
    if len(binb) % 4: binb += b'\x00' * (4 - len(binb) % 4)
    total = 12 + 8 + len(jsb) + 8 + len(binb)
    out = struct.pack('<III', 0x46546C67, 2, total)
    out += struct.pack('<II', len(jsb), 0x4E4F534A) + jsb
    out += struct.pack('<II', len(binb), 0x004E4942) + binb
    open(path, 'wb').write(out)


def bake_one(js, buf, anim, name):
    nodes = js['nodes']
    name_to_idx = {}
    for i, nd in enumerate(nodes):
        nm = nd.get('name', '')
        if nm:
            name_to_idx[nm] = i
            name_to_idx[nm.replace(':', '_')] = i

    bufferViews = js['bufferViews']
    accessors = js['accessors']

    def add_bin(data):
        if len(buf) % 4:
            buf.extend(b'\x00' * (4 - len(buf) % 4))
        off = len(buf)
        buf.extend(data)
        bv = {'buffer': 0, 'byteOffset': off, 'byteLength': len(data)}
        bufferViews.append(bv)
        return len(bufferViews) - 1

    def add_acc(componentType, count, type_, bv_idx, mn=None, mx=None):
        acc = {'bufferView': bv_idx, 'componentType': componentType,
               'count': count, 'type': type_}
        if mn is not None:
            acc['min'] = mn; acc['max'] = mx
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
        rest = nodes[idx].get('rotation')
        rest = np.array(rest, dtype=np.float64) if rest is not None else np.array([0., 0., 0., 1.])
        qs = []
        for k in range(n):
            q = quat_mul(rest, euler_xyz_to_quat([r['x'][k], r['y'][k], r['z'][k]]))
            q = q / np.linalg.norm(q)
            qs.append(q)
        t_data = struct.pack('<%df' % n, *r['t'])
        t_bv = add_bin(t_data)
        t_acc = add_acc(5126, n, 'SCALAR', t_bv, [min(r['t'])], [max(r['t'])])
        flat = []
        for q in qs:
            flat.extend([float(q[0]), float(q[1]), float(q[2]), float(q[3])])
        v_data = struct.pack('<%df' % len(flat), *flat)
        v_bv = add_bin(v_data)
        v_acc = add_acc(5126, n, 'VEC4', v_bv)
        samplers.append({'input': t_acc, 'output': v_acc, 'interpolation': 'LINEAR'})
        channels.append({'sampler': len(samplers)-1, 'target': {'node': idx, 'path': 'rotation'}})

    return {'name': name, 'samplers': samplers, 'channels': channels}


def main():
    js, binb = load_glb(GLB)
    buf = bytearray(binb)
    js['animations'] = []
    for fbx_name, anim_name in CLIPS:
        fbx = f'{MIXAMO_DIR}/{fbx_name}'
        import os
        if not os.path.exists(fbx):
            print('SKIP (missing)', fbx)
            continue
        anim = extract_fbx_anim(fbx)
        obj = bake_one(js, buf, anim, anim_name)
        js['animations'].append(obj)
        print(f'baked {anim_name}: {len(obj["channels"])} channels')
    write_glb(js, bytes(buf), OUT)
    print('wrote', OUT, 'with', len(js['animations']), 'animations')


if __name__ == '__main__':
    main()
