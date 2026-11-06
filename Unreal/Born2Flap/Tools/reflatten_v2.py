"""Re-flatten the founder skeleton to Mixamo's canonical reference pose.

The founder GLB (flatten_founder.py output) has a CORRECT mesh (bind error 0)
but its skeleton frame convention is Sketchfab's re-derivation, which differs
from Mixamo's canonical PreRotation. Mixamo animation clips therefore apply
rotations in the wrong local frames ("sitting"/twisted arms).

This script fixes ONLY the frame convention, preserving every bone's global
POSITION so the mesh stays perfectly aligned:

  * for each bone, set local rotation = Mixamo PreRotation (X-mirrored into
    the founder's +X=right convention);
  * recompute each bone's local translation so its GLOBAL position is unchanged;
  * recompute inverseBindMatrices = inv(global) in column-major;
  * leave mesh vertices / weights / indices untouched.

Verification: bind-pose skinning error must be ~0 and every joint's world
position must equal its pre-rewrite value (they do, by construction).
"""
import json, struct, math, numpy as np

SRC = 'Unreal/Born2Flap/SourceAssets/Avatars/SK_Founder.glb'
MIX = 'Unreal/Born2Flap/Tools/mixamo_bones.json'
DST = 'Unreal/Born2Flap/SourceAssets/Avatars/SK_Founder.glb'

# The flatten reorient rotates the rig 180 deg about Y (R_y(180) = diag(-1,1,-1)),
# flipping BOTH X and Z (left->-X, forward->+Z). Mixamo FBX and the clean founder
# GLB share the same native convention (left=+X, forward=-Z), so the correct
# frame transform into the flattened space is the R_y(180) conjugation, NOT a
# lone X mirror (diag(-1,1,1) only flips X, leaving Z mismatched -> "inside out").
M_x = np.diag([-1.0, 1.0, -1.0, 1.0])


def load_glb(path):
    d = open(path, 'rb').read(); off = 12; js = None; binb = b''
    while off < len(d):
        clen, ct = struct.unpack_from('<II', d, off); off += 8
        c = d[off:off+clen]; off += clen
        if ct == 0x4E4F534A: js = json.loads(c.decode('utf-8'))
        elif ct == 0x004E4942: binb = c
    return js, binb


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
        S = np.sqrt(tr + 1.0) * 2; w = 0.25*S
        x = (R[2,1]-R[1,2])/S; y = (R[0,2]-R[2,0])/S; z = (R[1,0]-R[0,1])/S
    elif R[0,0] > R[1,1] and R[0,0] > R[2,2]:
        S = np.sqrt(1.0+R[0,0]-R[1,1]-R[2,2]) * 2
        w = (R[2,1]-R[1,2])/S; x = 0.25*S
        y = (R[0,1]+R[1,0])/S; z = (R[0,2]+R[2,0])/S
    elif R[1,1] > R[2,2]:
        S = np.sqrt(1.0+R[1,1]-R[0,0]-R[2,2]) * 2
        w = (R[0,2]-R[2,0])/S; x = (R[0,1]+R[1,0])/S
        y = 0.25*S; z = (R[1,2]+R[2,1])/S
    else:
        S = np.sqrt(1.0+R[2,2]-R[0,0]-R[1,1]) * 2
        w = (R[1,0]-R[0,1])/S; x = (R[0,2]+R[2,0])/S
        y = (R[1,2]+R[2,1])/S; z = 0.25*S
    q = np.array([x, y, z, w])
    return q / np.linalg.norm(q)


def local_matrix(node):
    m = node.get('matrix')
    if m is not None:
        return np.array(m, dtype=np.float64).reshape(4, 4).T
    T = np.eye(4)
    if node.get('translation'): T[:3, 3] = node['translation']
    R = np.eye(4)
    if node.get('rotation'): R[:3, :3] = quat_to_matrix(node['rotation'])
    S = np.eye(4)
    if node.get('scale'): S[0,0], S[1,1], S[2,2] = node['scale']
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


def nodename(n):
    return n.get('name', '').replace(':', '_')


def mirror_quat(q):
    # conjugate the rotation by M_x:  M_x @ R @ M_x
    R = quat_to_matrix(q)
    return matrix_to_quat(M_x[:3,:3] @ R @ M_x[:3,:3])


def main():
    js, binb = load_glb(SRC)
    nodes = js['nodes']
    mix = json.load(open(MIX))

    skin = js['skins'][0]
    joints = skin['joints']
    joint_set = set(joints)
    print('joints=%d mixamo_bones=%d' % (len(joints), len(mix)))

    # parent map over the FULL node tree
    parent = {}
    for i, nd in enumerate(nodes):
        for ch in nd.get('children', []):
            parent[ch] = i

    g_old = global_transforms(nodes)

    # topological order of joints (parents before children)
    order = []
    visited = set()
    def visit(j):
        if j in visited:
            return
        visited.add(j)
        p = parent.get(j)
        if p is not None and p in joint_set:
            visit(p)
        order.append(j)
    for j in joints:
        visit(j)

    # store old positions for verification
    old_pos = {j: g_old[j][:3,3].copy() for j in joints}

    # new global rotations (by joint index)
    new_rot = {}
    new_pos = {}
    count = 0
    for j in order:
        nm = nodename(nodes[j])
        p = parent.get(j)

        if p is not None and p in joint_set:
            parent_rot = new_rot[p]
            parent_pos = new_pos[p]
        else:
            # non-joint parent (scene wrapper): keep its existing transform
            parent_rot = g_old[p][:3,:3] if p is not None else np.eye(3)
            parent_pos = g_old[p][:3,3] if p is not None else np.zeros(3)

        data = mix.get(nm)
        pre = data.get('Pre') if data else None
        if pre is not None and len(pre) >= 3:
            q = euler_xyz_to_quat(pre)
            q = mirror_quat(q)
        else:
            q = np.array([0.0, 0.0, 0.0, 1.0])  # identity frame

        R_local = quat_to_matrix(q)
        new_rot[j] = parent_rot @ R_local
        new_pos[j] = old_pos[j]  # preserve global position

        # recompute local translation so global position == old_pos[j]
        t_local = np.linalg.inv(parent_rot) @ (old_pos[j] - parent_pos)

        nodes[j]['translation'] = [float(x) for x in t_local]
        nodes[j]['rotation'] = [float(x) for x in q]
        nodes[j].pop('matrix', None)
        count += 1
    print('rewrote %d joints' % count)

    # recompute global transforms after rewrite and verify positions unchanged
    g_new = global_transforms(nodes)
    max_dp = 0.0
    for j in joints:
        dp = np.linalg.norm(g_new[j][:3,3] - old_pos[j])
        max_dp = max(max_dp, dp)
    print('max joint position drift = %.6f m' % max_dp)

    # recompute inverseBindMatrices (column-major)
    ibm = np.zeros((len(joints), 16), dtype=np.float32)
    for k, j in enumerate(joints):
        ibm[k] = np.linalg.inv(g_new[j]).T.reshape(16)
    acc = js['accessors'][skin['inverseBindMatrices']]
    bv = js['bufferViews'][acc['bufferView']]
    base = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
    data = ibm.tobytes()
    binb = binb[:base] + data + binb[base+len(data):]

    # verify bind-pose skinning: re-skin each mesh vertex using new IBM and
    # new rest-pose joint matrices; must reproduce the stored POSITION.
    def accessor_array(gltf, idx):
        a = gltf['accessors'][idx]
        bv = gltf['bufferViews'][a['bufferView']]
        comp = {'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
        dt = {5120:'<i1',5121:'<u1',5122:'<i2',5123:'<u2',5125:'<u4',5126:'<f4'}[a['componentType']]
        itemsize = np.dtype(dt).itemsize
        base = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        stride = bv.get('byteStride', comp * itemsize)
        arr = np.empty((a['count'], comp), dtype=np.float64)
        for i in range(a['count']):
            arr[i] = np.frombuffer(binb, dtype=dt, count=comp, offset=base + i*stride)
        return arr

    joints4 = np.array([g_new[j] for j in joints])       # (J,4,4)
    ibm4 = ibm.reshape(-1, 4, 4)                          # (J,4,4)
    skin_mat = joints4 @ ibm4                             # (J,4,4) == identity

    max_err = 0.0
    for prim in js['meshes'][0]['primitives']:
        pos_acc = accessor_array(js, prim['attributes']['POSITION'])
        for att, acc_idx in prim['attributes'].items():
            if att.startswith('JOINTS_'):
                jidx = accessor_array(js, acc_idx).astype(int)
                wname = 'WEIGHTS_' + att.split('_')[1]
                w = accessor_array(js, prim['attributes'][wname])
        for vi in range(pos_acc.shape[0]):
            p = np.zeros(3)
            for k in range(4):
                j = jidx[vi, k]; ww = w[vi, k]
                if ww == 0: continue
                p += ww * (skin_mat[j][:3,:3] @ pos_acc[vi] + skin_mat[j][:3,3])
            max_err = max(max_err, np.linalg.norm(p - pos_acc[vi]))
    print('bind-pose skinning error = %.6f m' % max_err)

    # quick T-pose sanity on key bones
    for nm in ['mixamorig_LeftHand', 'mixamorig_RightHand', 'mixamorig_LeftFoot',
               'mixamorig_RightFoot', 'mixamorig_Head']:
        for j in joints:
            if nodename(nodes[j]) == nm:
                print('  %-24s world=%s' % (nm, ','.join('%.3f'%x for x in g_new[j][:3,3])))

    # write GLB
    jsb = json.dumps(js, separators=(',',':')).encode('utf-8')
    jsb += b' ' * ((4 - len(jsb) % 4) % 4)
    bpad = b'\x00' * ((4 - len(binb) % 4) % 4)
    total = 12 + 8 + len(jsb) + 8 + len(binb) + len(bpad)
    out = bytearray()
    out += struct.pack('<III', 0x46546C67, 2, total)
    out += struct.pack('<II', len(jsb), 0x4E4F534A) + jsb
    out += struct.pack('<II', len(binb)+len(bpad), 0x004E4942) + binb + bpad
    open(DST, 'wb').write(bytes(out))
    print('DONE wrote %s' % DST)


if __name__ == '__main__':
    main()