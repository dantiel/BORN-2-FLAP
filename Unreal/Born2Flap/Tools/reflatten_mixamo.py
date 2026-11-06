"""Re-flatten founder skeleton to Mixamo reference pose, with a correct X-mirror
applied via matrix conjugation (M_x · L · M_x)."""
import json, struct, numpy as np

SRC = 'Unreal/Born2Flap/SourceAssets/Avatars/SK_Founder.glb'
MIX = 'scratch/mixamo_bones.json'
DST = 'Unreal/Born2Flap/SourceAssets/Avatars/SK_Founder.glb'

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

def quat_to_matrix(q):
    x, y, z, w = q
    return np.array([
        [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
        [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
        [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])

def matrix_to_quat(R):
    R = np.asarray(R, dtype=np.float64)
    tr = np.trace(R)
    if tr > 0:
        S = np.sqrt(tr + 1.0) * 2
        w = 0.25 * S
        x = (R[2,1] - R[1,2]) / S
        y = (R[0,2] - R[2,0]) / S
        z = (R[1,0] - R[0,1]) / S
    elif R[0,0] > R[1,1] and R[0,0] > R[2,2]:
        S = np.sqrt(1.0 + R[0,0] - R[1,1] - R[2,2]) * 2
        w = (R[2,1] - R[1,2]) / S
        x = 0.25 * S
        y = (R[0,1] + R[1,0]) / S
        z = (R[0,2] + R[2,0]) / S
    elif R[1,1] > R[2,2]:
        S = np.sqrt(1.0 + R[1,1] - R[0,0] - R[2,2]) * 2
        w = (R[0,2] - R[2,0]) / S
        x = (R[0,1] + R[1,0]) / S
        y = 0.25 * S
        z = (R[1,2] + R[2,1]) / S
    else:
        S = np.sqrt(1.0 + R[2,2] - R[0,0] - R[1,1]) * 2
        w = (R[1,0] - R[0,1]) / S
        x = (R[0,2] + R[2,0]) / S
        y = (R[1,2] + R[2,1]) / S
        z = 0.25 * S
    q = np.array([x, y, z, w])
    return q / np.linalg.norm(q)

def load_glb(path):
    d = open(path, 'rb').read()
    off = 12; js = None; binb = b''
    while off < len(d):
        clen, ct = struct.unpack_from('<II', d, off); off += 8
        c = d[off:off+clen]; off += clen
        if ct == 0x4E4F534A: js = json.loads(c.decode('utf-8'))
        elif ct == 0x004E4942: binb = c
    return js, binb

def local_matrix(node):
    m = node.get('matrix')
    if m is not None:
        return np.array(m, dtype=np.float64).reshape(4,4).T
    T = np.eye(4)
    if node.get('translation'): T[:3,3] = node['translation']
    R = np.eye(4)
    if node.get('rotation'):
        R[:3,:3] = quat_to_matrix(node['rotation'])
    S = np.eye(4)
    if node.get('scale'): S[0,0],S[1,1],S[2,2] = node['scale']
    return T @ R @ S

def global_transforms(nodes):
    n = len(nodes); g = [None]*n
    def c(i, p):
        g[i] = p @ local_matrix(nodes[i])
        for ch in nodes[i].get('children', []):
            c(ch, g[i])
    roots = set(range(n)) - {ch for nd in nodes for ch in nd.get('children', [])}
    for r in roots: c(r, np.eye(4))
    for i in range(n):
        if g[i] is None: c(i, np.eye(4))
    return g

M_x = np.diag([-1.0, 1.0, 1.0, 1.0])

mix = json.load(open(MIX))
js, binb = load_glb(SRC)
nodes = js['nodes']

def nodename(n):
    return n.get('name','').replace(':', '_')

hips_idx = None
for i, n in enumerate(nodes):
    if nodename(n) == 'mixamorig_Hips':
        hips_idx = i
        break

cur_g = global_transforms(nodes)
cur_miny = min(cur_g[i][1,3] for i in range(len(nodes)) if cur_g[i] is not None)
cur_maxy = max(cur_g[i][1,3] for i in range(len(nodes)) if cur_g[i] is not None)
cur_height = cur_maxy - cur_miny

count = 0
for i, n in enumerate(nodes):
    nm = nodename(n)
    if nm not in mix:
        continue
    data = mix[nm]
    T = data.get('T')
    Pre = data.get('Pre')
    if T is not None and len(T) >= 3:
        trans = np.array([T[0], T[1], T[2]]) / 100.0  # cm -> m
    else:
        trans = np.array(n.get('translation', [0,0,0]), dtype=np.float64)
    if Pre is not None and len(Pre) >= 3:
        rot = euler_xyz_to_quat(Pre)
    else:
        rot = np.array(n.get('rotation', [0,0,0,1]), dtype=np.float64)
    if i == hips_idx:
        rot = np.array([0,0,0,1])
    # build local matrix, apply mirror conjugation
    L = np.eye(4)
    L[:3,3] = trans
    L[:3,:3] = quat_to_matrix(rot)
    Lm = M_x @ L @ M_x
    # decompose back
    new_trans = Lm[:3,3].tolist()
    new_rot = matrix_to_quat(Lm[:3,:3]).tolist()
    n['translation'] = new_trans
    n['rotation'] = new_rot
    n.pop('matrix', None)
    count += 1
print('rewrote', count, 'bones; cur_height=%.3f' % cur_height)

g = global_transforms(nodes)
new_miny = min(g[i][1,3] for i in range(len(nodes)) if g[i] is not None)
new_maxy = max(g[i][1,3] for i in range(len(nodes)) if g[i] is not None)
new_height = new_maxy - new_miny
scale = 1.8 / new_height
M = np.diag([scale, scale, scale, 1.0])
M[1,3] = -scale * new_miny
for i in range(len(nodes)):
    if g[i] is None: continue
    g[i] = M @ g[i]
hips_world = g[hips_idx]
nodes[hips_idx].pop('translation', None)
nodes[hips_idx].pop('rotation', None)
nodes[hips_idx].pop('scale', None)
nodes[hips_idx]['matrix'] = [float(x) for x in hips_world.T.reshape(16)]
print('new_height=%.3f scale=%.4f' % (new_height, scale))

skin = js['skins'][0]
joints = skin['joints']
ibm = np.zeros((len(joints), 16), dtype=np.float32)
for k, j in enumerate(joints):
    ibm[k] = np.linalg.inv(g[j]).T.reshape(16)
acc = js['accessors'][skin['inverseBindMatrices']]
bv = js['bufferViews'][acc['bufferView']]
base = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
data = ibm.tobytes()
binb = binb[:base] + data + binb[base+len(data):]

def write_glb(js, binb, path):
    jsb = json.dumps(js, separators=(',',':')).encode('utf-8')
    jsb += b' ' * ((4 - len(jsb) % 4) % 4)
    bpad = b'\x00' * ((4 - len(binb) % 4) % 4)
    total = 12 + 8 + len(jsb) + 8 + len(binb) + len(bpad)
    out = bytearray()
    out += struct.pack('<III', 0x46546C67, 2, total)
    out += struct.pack('<II', len(jsb), 0x4E4F534A) + jsb
    out += struct.pack('<II', len(binb)+len(bpad), 0x004E4942) + binb + bpad
    open(path, 'wb').write(bytes(out))

write_glb(js, binb, DST)
print('DONE')
