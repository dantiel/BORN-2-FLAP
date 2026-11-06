"""Correct flatten of the Founder (Mixamo rig) into a single-mesh single-skeleton
glTF whose reference pose is EXACTLY Mixamo's canonical PreRotation, in Mixamo's
native orientation (left=+X, right=-X, Y-up).

Fixes the two root causes of the "crumpled / twisted / head-backwards" avatar:

  1. flatten_founder.py's `reorient` applied R_y(180 deg) which flipped the rig
     left<->right and front<->back.  The founder's clean GLB and the Mixamo FBX
     are ALREADY in the SAME orientation (both left=+X), so that rotation must
     NOT be applied.  We use identity orientation here.

  2. The founder's local bone rotations are Sketchfab's re-derivation, which
     differ from Mixamo's PreRotation.  We overwrite each bone's LOCAL rotation
     with Mixamo's PreRotation (NO mirror, because orientations already match),
     recompute each LOCAL translation so every GLOBAL position is unchanged,
     then recompute inverseBindMatrices = inv(global).

Result: glTF with 1 mesh, 1 skin, 65 joints, standing human (left=+X, Y-up,
~1.8 m), whose skeleton frame == Mixamo => retargeted Mixamo clips play 1:1.
"""
import sys, json, struct, re, numpy as np

SRC = 'Unreal/Born2Flap/SourceAssets/Founder/Founder_rigged.glb'
MIX = 'Unreal/Born2Flap/Tools/mixamo_bones.json'
DST = 'Unreal/Born2Flap/SourceAssets/Avatars/SK_Founder.glb'
TARGET_HEIGHT_M = 1.8


def load_glb(path):
    d = open(path, 'rb').read()
    off = 12; js = None; binb = b''
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


def local_matrix(node):
    m = node.get('matrix')
    if m is not None:
        return np.array(m, dtype=np.float64).reshape(4, 4).T
    T = np.eye(4)
    if node.get('translation'): T[:3, 3] = node['translation']
    R = np.eye(4)
    if node.get('rotation'):
        x, y, z, w = node['rotation']
        R[:3, :3] = np.array([
            [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
            [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
            [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
    S = np.eye(4)
    if node.get('scale'): S[0,0], S[1,1], S[2,2] = node['scale']
    return T @ R @ S


def global_transforms(nodes):
    n = len(nodes); g = [None]*n
    def c(i, p):
        g[i] = p @ local_matrix(nodes[i])
        for ch in nodes[i].get('children', []): c(ch, g[i])
    roots = set(range(n)) - {ch for nd in nodes for ch in nd.get('children', [])}
    for r in roots: c(r, np.eye(4))
    for i in range(n):
        if g[i] is None: c(i, np.eye(4))
    return g


def accessor_array(gltf, binb, idx):
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


def bone_pos(gltf, g, joints, name):
    for j in joints:
        if gltf['nodes'][j].get('name') == name:
            return g[j][:3, 3].copy()
    raise RuntimeError('bone not found: ' + name)


def main():
    gltf, binb = load_glb(SRC)
    nodes = gltf['nodes']
    mix = json.load(open(MIX))

    root_joint_node = None
    for i, n in enumerate(nodes):
        nm = n.get('name')
        if nm == '_rootJoint':
            root_joint_node = i
        elif nm:
            n['name'] = re.sub(r'_\d+$', '', nm)

    skin = gltf['skins'][0]
    joints = [j for j in skin['joints'] if j != root_joint_node]
    assert len(joints) == 65, 'expected 65 joints, got %d' % len(joints)
    joint_set = set(joints)

    g = global_transforms(nodes)
    mesh_node_indices = [i for i, n in enumerate(nodes) if 'mesh' in n]

    # full parent map over node tree
    full_parent = {}
    for i, nd in enumerate(nodes):
        for ch in nd.get('children', []):
            full_parent[ch] = i

    # original skin matrices (world[joint] @ IBM) place vertices in world space
    orig_joint_list = skin['joints']
    ibm_raw = accessor_array(gltf, binb, skin['inverseBindMatrices']).reshape(-1,4,4)
    ibm_orig = ibm_raw.transpose(0,2,1)
    skin_matrices = np.stack([g[jnode] @ ibm_orig[oi] for oi, jnode in enumerate(orig_joint_list)])

    def skinned(prim):
        v = accessor_array(gltf, binb, prim['attributes']['POSITION'])
        j = accessor_array(gltf, binb, prim['attributes']['JOINTS_0']).astype(np.int64)
        w = accessor_array(gltf, binb, prim['attributes']['WEIGHTS_0'])
        w = w / np.maximum(w.sum(1)[:,None], 1e-9)
        mats = skin_matrices[j]
        transforms = (mats * w[:,:,None,None]).sum(1)
        xyz = np.einsum('nij,nj->ni', transforms, np.c_[v, np.ones(len(v))])[:,:3]
        return xyz, transforms, v

    # The ORIGINAL founder GLB skeleton is rotated 90 deg about Y: its LeftHand
    # sits at +Z (not +X).  Mixamo FBX (and the clean founder GLB) have Left=+X.
    # R_y(+90) maps +Z -> +X and +X -> -Z, so left/right AND facing both line up
    # with Mixamo.  (flatten_founder.py used R_y(-90), which is the 180-degree
    # error that flipped left<->right and front<->back.)
    R_y90 = np.array([[0.0, 0.0, 1.0],
                      [0.0, 1.0, 0.0],
                      [-1.0, 0.0, 0.0]])

    # world-space bounds, rotated into Mixamo convention (Left=+X)
    all_min = np.array([np.inf]*3); all_max = np.array([-np.inf]*3)
    for ni in mesh_node_indices:
        for prim in gltf['meshes'][nodes[ni]['mesh']]['primitives']:
            xyz, _, _ = skinned(prim)
            xyz_rot = (R_y90 @ xyz.T).T
            all_min = np.minimum(all_min, xyz_rot.min(axis=0))
            all_max = np.maximum(all_max, xyz_rot.max(axis=0))
    height = all_max[1] - all_min[1]
    scale = TARGET_HEIGHT_M / height
    shift = np.array([(all_min[0] + all_max[0]) / 2.0,
                      all_min[1],
                      (all_min[2] + all_max[2]) / 2.0])
    print('height=%.3f scale=%.4f shift=%s' % (height, scale, np.round(shift,3)))

    # target GLOBAL positions for every joint (rotate + scale + center feet)
    def world_pos(j):
        p = R_y90 @ g[j][:3,3]
        return scale * (p - shift)

    # ---- rewrite local frames to Mixamo PreRotation (NO mirror), preserving positions ----
    def nodename(i):
        return (nodes[i].get('name') or '').replace(':', '_')

    # topological order of joints (parents before children), ignoring wrappers
    order = []
    visited = set()
    def visit(j):
        if j in visited: return
        visited.add(j)
        p = full_parent.get(j)
        while p is not None and p not in joint_set:
            p = full_parent.get(p)
        if p is not None:
            visit(p)
        order.append(j)
    for j in joints:
        visit(j)

    new_world = {}   # 4x4 world transform per joint (new frames)
    local_T = {}     # local translation (3,) per joint
    local_Q = {}     # local rotation quat per joint
    for j in order:
        nm = nodename(j)
        data = mix.get(nm)
        pre = data.get('Pre') if data else None
        if pre is not None and len(pre) >= 3:
            q = euler_xyz_to_quat(pre)
        else:
            q = np.array([0.0, 0.0, 0.0, 1.0])  # identity frame (Mixamo null Pre)
        R_local = quat_to_matrix(q)

        # find joint parent
        p = full_parent.get(j)
        while p is not None and p not in joint_set:
            p = full_parent.get(p)
        if p is None:
            # root (Hips): world = T @ R, position == world_pos(j)
            T = world_pos(j)
            W = np.eye(4); W[:3,3] = T; W[:3,:3] = R_local
        else:
            pw = new_world[p]
            T = np.linalg.inv(pw[:3,:3]) @ (world_pos(j) - pw[:3,3])
            W = pw @ (np.eye(4).at[:3,:3].set(R_local)) if False else None
            L = np.eye(4); L[:3,:3] = R_local; L[:3,3] = T
            W = pw @ L
        new_world[j] = W
        local_T[j] = T
        local_Q[j] = q

    # verify positions unchanged
    max_dp = 0.0
    for j in joints:
        dp = np.linalg.norm(new_world[j][:3,3] - world_pos(j))
        max_dp = max(max_dp, dp)
    print('max joint position drift = %.6f m' % max_dp)

    # ---- bake mesh vertices with M = scale @ (x - shift) ----
    new_prim_data = []
    for ni in mesh_node_indices:
        for prim in gltf['meshes'][nodes[ni]['mesh']]['primitives']:
            nrm_idx = prim['attributes'].get('NORMAL')
            jidx_idx = prim['attributes'].get('JOINTS_0')
            xyz, transforms, _ = skinned(prim)
            xyz_rot = (R_y90 @ xyz.T).T
            vw = scale * (xyz_rot - shift)
            nrm = None
            if nrm_idx is not None:
                n = accessor_array(gltf, binb, nrm_idx)
                nw = np.einsum('nij,nj->ni', np.linalg.inv(transforms[:,:3,:3]).transpose(0,2,1), n)
                nrm = (R_y90 @ nw.T).T
                ln = np.linalg.norm(nrm, axis=1, keepdims=True); ln[ln==0] = 1
                nrm = nrm / ln
            jidx_ctype = gltf['accessors'][jidx_idx]['componentType']
            jidx = accessor_array(gltf, binb, jidx_idx).astype(np.int64)
            jidx = np.clip(jidx - 1, 0, len(joints)-1)
            new_prim_data.append((vw, nrm, jidx, jidx_ctype, dict(prim)))

    # ---- build new nodes: Hips root (TRS), children local TRS ----
    new_nodes = [{'name': 'AvatarMesh', 'mesh': 0, 'skin': 0}]
    old_to_new = {}
    for k, j in enumerate(joints):
        old_to_new[j] = len(new_nodes)
        n = {'name': nodes[j].get('name')}
        if j == joints[0]:  # Hips root: translation = world_pos, rotation = local_Q
            n['translation'] = [float(x) for x in world_pos(j)]
            n['rotation'] = [float(x) for x in local_Q[j]]
        else:
            n['translation'] = [float(x) for x in local_T[j]]
            n['rotation'] = [float(x) for x in local_Q[j]]
        new_nodes.append(n)
    for n in new_nodes:
        if 'children' in n:
            n['children'] = [old_to_new[c] for c in n['children'] if c in old_to_new]
    # rebuild children from original hierarchy
    for k, j in enumerate(joints):
        ch = [c for c in nodes[j].get('children', []) if c in joint_set]
        new_nodes[old_to_new[j]]['children'] = [old_to_new[c] for c in ch]
    scene_roots = [0, old_to_new[joints[0]]]

    # ---- rebuild IBM = inv(new_world), column-major ----
    ibm = np.zeros((len(joints), 16), dtype=np.float32)
    for k, j in enumerate(joints):
        ibm[k] = np.linalg.inv(new_world[j]).T.reshape(16)

    # ---- buffer writing (reuse flatten_founder logic) ----
    bin_parts = [bytearray(binb)]
    new_buffer_views = list(gltf.get('bufferViews', []))
    new_accessors = list(gltf.get('accessors', []))
    def total_len(): return sum(len(p) for p in bin_parts)
    def add_bytes(b): bin_parts.append(bytearray(b))
    def make_accessor(arr_f64, comp):
        arr = np.asarray(arr_f64, dtype=np.float32)
        raw = arr.astype('<f4').tobytes()
        pad = (-len(raw)) % 4
        if pad: raw += b'\x00'*pad
        bv_idx = len(new_buffer_views)
        new_buffer_views.append({'buffer':0,'byteOffset':total_len(),'byteLength':len(raw)})
        new_accessors.append({'bufferView':bv_idx,'componentType':5126,
                              'count':len(arr),'type':'VEC3' if comp==3 else 'SCALAR'})
        add_bytes(raw)
        return len(new_accessors) - 1
    def make_accessor_int(arr, ctype):
        dt = {5121:'<u1', 5123:'<u2', 5125:'<u4'}[ctype]
        raw = np.asarray(arr, dtype=np.int64).astype(dt).tobytes()
        pad = (-len(raw)) % 4
        if pad: raw += b'\x00'*pad
        bv_idx = len(new_buffer_views)
        new_buffer_views.append({'buffer':0,'byteOffset':total_len(),'byteLength':len(raw)})
        new_accessors.append({'bufferView':bv_idx,'componentType':ctype,
                              'count':len(arr),'type':'VEC4'})
        add_bytes(raw)
        return len(new_accessors) - 1
    def make_index_accessor(arr, ctype):
        dt = {5121:'<u1', 5123:'<u2', 5125:'<u4'}[ctype]
        raw = np.asarray(arr, dtype=np.int64).astype(dt).tobytes()
        pad = (-len(raw)) % 4
        if pad: raw += b'\x00'*pad
        bv_idx = len(new_buffer_views)
        new_buffer_views.append({'buffer':0,'byteOffset':total_len(),'byteLength':len(raw)})
        new_accessors.append({'bufferView':bv_idx,'componentType':ctype,
                              'count':len(arr),'type':'SCALAR'})
        add_bytes(raw)
        return len(new_accessors) - 1

    merged_prims = []
    for vw, nrm, jidx, jctype, prim in new_prim_data:
        new_attrs = {}
        for k, idx in prim['attributes'].items():
            if k in ('POSITION','NORMAL','JOINTS_0'): continue
            new_attrs[k] = idx
        new_attrs['POSITION'] = make_accessor(vw, 3)
        if nrm is not None: new_attrs['NORMAL'] = make_accessor(nrm, 3)
        new_attrs['JOINTS_0'] = make_accessor_int(jidx, jctype)
        prim_out = {'attributes':new_attrs,'material':prim.get('material',0),
                    'mode':prim.get('mode',4)}
        if 'indices' in prim:
            idx_acc = prim['indices']
            ict = gltf['accessors'][idx_acc]['componentType']
            idx_arr = accessor_array(gltf, binb, idx_acc).astype(np.int64)
            prim_out['indices'] = make_index_accessor(idx_arr, ict)
        merged_prims.append(prim_out)

    ibm_bytes = ibm.astype('<f4').tobytes()
    pad = (-len(ibm_bytes)) % 4
    if pad: ibm_bytes += b'\x00'*pad
    ibm_bv = len(new_buffer_views)
    new_buffer_views.append({'buffer':0,'byteOffset':total_len(),'byteLength':len(ibm_bytes)})
    ibm_acc = len(new_accessors)
    new_accessors.append({'bufferView':ibm_bv,'componentType':5126,'count':len(joints),'type':'MAT4'})
    add_bytes(ibm_bytes)

    out_gltf = {
        'asset': {'version':'2.0','generator':'flatten_founder_v3'},
        'scene': 0,
        'scenes': [{'nodes': scene_roots}],
        'nodes': new_nodes,
        'meshes': [{'name':'Avatar','primitives':merged_prims}],
        'skins': [{'joints':[old_to_new[j] for j in joints],
                   'inverseBindMatrices':ibm_acc,'skeleton':old_to_new[joints[0]]}],
        'accessors': new_accessors,
        'bufferViews': new_buffer_views,
        'buffers': [{'byteLength': total_len()}],
        'materials': gltf.get('materials', []),
    }
    for key in ('textures','images','samplers'):
        if key in gltf: out_gltf[key] = gltf[key]

    bin_out = b''.join(bytes(p) for p in bin_parts)
    json_bytes = json.dumps(out_gltf, separators=(',',':')).encode('utf-8')
    json_bytes += b' ' * ((-len(json_bytes)) % 4)
    header = struct.pack('<III', 0x46546C67, 2, 12+8+len(json_bytes)+8+len(bin_out))
    out = header + struct.pack('<II', len(json_bytes), 0x4E4F534A) + json_bytes
    out += struct.pack('<II', len(bin_out), 0x004E4942) + bin_out
    open(DST, 'wb').write(out)
    print('wrote %s (%d bytes, %d nodes, %d prims, %d joints)' %
          (DST, len(out), len(new_nodes), len(merged_prims), len(joints)))

    # ---- verify ----
    gltf2, binb2 = load_glb(DST)
    nodes2 = gltf2['nodes']
    G2 = global_transforms(nodes2)
    joints2 = gltf2['skins'][0]['joints']
    ibm2_raw = accessor_array(gltf2, binb2, gltf2['skins'][0]['inverseBindMatrices']).reshape(-1,4,4)
    ibm2 = ibm2_raw.transpose(0,2,1)
    J2 = np.array([G2[j] for j in joints2])
    skin_mat2 = J2 @ ibm2
    print('skin_mat deviation from identity = %.6f' % float(np.abs(skin_mat2 - np.eye(4)).max()))
    def wpos(name):
        for j in joints2:
            if (nodes2[j].get('name') or '').replace(':','_') == name:
                return G2[j][:3,3]
        return None
    for nm in ['mixamorig_Head','mixamorig_LeftHand','mixamorig_RightHand','mixamorig_LeftFoot','mixamorig_RightFoot']:
        print('  %-22s %s' % (nm, np.round(wpos(nm),3)))

main()