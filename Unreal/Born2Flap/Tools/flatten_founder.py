"""
Definitive flatten of the Founder character (Sketchfab-wrapped Mixamo rig)
into a clean STANDING single-mesh single-skeleton glTF (Y-up, meters) that UE
imports as a normal human.

Works from the ORIGINAL self-consistent Founder_rigged.glb (mesh + skeleton
both under the same wrapper chain), and performs the cleanup itself:

  1. strip the `_NN` suffix from mixamorig bone names (Mixamo-FBX compatible)
  2. remove `_rootJoint` from the skin; Hips becomes the skeleton root
  3. compute full node world transforms (0.01 + 100 + axis-flip wrappers)
  4. reorient by the rig (Hips->Head = +Y) and uniform-scale to TARGET_HEIGHT_M
  5. bake that into vertices + normals AND the skeleton joints
  6. merge all primitives into ONE mesh, re-root at Hips
  7. recompute inverseBindMatrices in COLUMN-MAJOR (glTF/UE MAT4 convention)
  8. verify bind-pose skinning (re-skin == baked POSITION)

Result: glTF with 1 mesh, 1 skin, flat node list, standing human, correct skin.
"""
import sys, json, struct, re, numpy as np

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


def main(src, dst):
    gltf, binb = load_glb(src)
    nodes = gltf['nodes']

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

    g = global_transforms(nodes)
    mesh_node_indices = [i for i, n in enumerate(nodes) if 'mesh' in n]

    # original skin matrices: world[joint] @ inverseBindMatrix (row-major).
    # bake_founder.py computes skinned vertices this way -- NOT via the mesh
    # node transform (the mesh node sits under a permuting wrapper, but the IBM
    # already places vertices in world space).
    orig_joint_list = skin['joints']
    ibm_raw = accessor_array(gltf, binb, skin['inverseBindMatrices']).reshape(-1,4,4)
    ibm_orig = ibm_raw.transpose(0,2,1)   # column-major -> row-major
    skin_matrices = np.stack([g[jnode] @ ibm_orig[oi] for oi, jnode in enumerate(orig_joint_list)])

    hips = bone_pos(gltf, g, joints, 'mixamorig:Hips')
    head = bone_pos(gltf, g, joints, 'mixamorig:Head')
    lsh  = bone_pos(gltf, g, joints, 'mixamorig:LeftShoulder')
    rsh  = bone_pos(gltf, g, joints, 'mixamorig:RightShoulder')
    up = head - hips; up /= np.linalg.norm(up)
    right = rsh - lsh; right -= up * np.dot(up, right); right /= np.linalg.norm(right)
    forward = np.cross(right, up)
    basis = np.stack([right, up, forward])
    R = np.eye(4); R[:3, :3] = basis

    def skinned(prim):
        v = accessor_array(gltf, binb, prim['attributes']['POSITION'])
        j = accessor_array(gltf, binb, prim['attributes']['JOINTS_0']).astype(np.int64)
        w = accessor_array(gltf, binb, prim['attributes']['WEIGHTS_0'])
        w = w / np.maximum(w.sum(1)[:,None], 1e-9)
        mats = skin_matrices[j]                       # (N,4,4,4)
        transforms = (mats * w[:,:,None,None]).sum(1) # (N,4,4)
        xyz = np.einsum('nij,nj->ni', transforms, np.c_[v, np.ones(len(v))])[:,:3]
        return xyz, transforms, v

    all_min = np.array([np.inf]*3); all_max = np.array([-np.inf]*3)
    for ni in mesh_node_indices:
        for prim in gltf['meshes'][nodes[ni]['mesh']]['primitives']:
            xyz, _, _ = skinned(prim)
            vw = (R[:3,:3] @ xyz.T).T
            all_min = np.minimum(all_min, vw.min(axis=0))
            all_max = np.maximum(all_max, vw.max(axis=0))
    height = all_max[1] - all_min[1]
    scale = TARGET_HEIGHT_M / height
    M = np.diag([scale, scale, scale, 1.0]) @ R
    # Zero the feet and center X/Z so the avatar stands ON the origin (head at
    # +Y, feet at Y=0), instead of hanging ~1.68 m below the pivot.
    shift = np.array([(all_min[0] + all_max[0]) / 2.0,
                      all_min[1],
                      (all_min[2] + all_max[2]) / 2.0])
    M[:3, 3] = -scale * shift
    print('height=%.3f scale=%.4f' % (height, scale))

    new_prim_data = []
    for ni in mesh_node_indices:
        for prim in gltf['meshes'][nodes[ni]['mesh']]['primitives']:
            nrm_idx = prim['attributes'].get('NORMAL')
            jidx_idx = prim['attributes'].get('JOINTS_0')
            xyz, transforms, _ = skinned(prim)
            vw = (M[:3,:3] @ xyz.T).T + M[:3,3]
            nrm = None
            if nrm_idx is not None:
                n = accessor_array(gltf, binb, nrm_idx)
                nw = np.einsum('nij,nj->ni', np.linalg.inv(transforms[:,:3,:3]).transpose(0,2,1), n)
                nrm = (R[:3,:3] @ nw.T).T
                ln = np.linalg.norm(nrm, axis=1, keepdims=True); ln[ln==0] = 1
                nrm = nrm / ln
            # remap JOINTS_0: _rootJoint was skin index 0 -> drop it (old-1)
            jidx_ctype = gltf['accessors'][jidx_idx]['componentType']
            jidx = accessor_array(gltf, binb, jidx_idx).astype(np.int64)
            jidx = np.clip(jidx - 1, 0, len(joints)-1)
            new_prim_data.append((vw, nrm, jidx, jidx_ctype, dict(prim)))

    jw = {j: (M @ g[j]) for j in joints}
    ibm = np.zeros((len(joints), 16), dtype=np.float32)
    for k, j in enumerate(joints):
        ibm[k] = np.linalg.inv(jw[j]).T.reshape(16)   # COLUMN-MAJOR (glTF MAT4)

    new_nodes = [{'name': 'AvatarMesh', 'mesh': 0, 'skin': 0}]
    old_to_new = {}
    for k, j in enumerate(joints):
        old_to_new[j] = len(new_nodes)
        n = dict(nodes[j])
        n.pop('mesh', None); n.pop('skin', None); n.pop('matrix', None)
        if j == joints[0]:  # Hips root: replace TRS with full world matrix
            n.pop('translation', None); n.pop('rotation', None); n.pop('scale', None)
            n['matrix'] = [float(x) for x in jw[j].T.reshape(16)]  # column-major
        # child joints keep their local translation/rotation/scale
        new_nodes.append(n)
    for n in new_nodes:
        if 'children' in n:
            n['children'] = [old_to_new[c] for c in n['children'] if c in old_to_new]
    scene_roots = [0, old_to_new[joints[0]]]

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
        # CRITICAL: preserve triangle index buffer, else the mesh renders as
        # non-indexed triangle soup (torn/inverted polygons).
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

    ibm_raw = ibm.astype('<f4').tobytes()
    pad = (-len(ibm_raw)) % 4
    if pad: ibm_raw += b'\x00'*pad
    ibm_bv = len(new_buffer_views)
    new_buffer_views.append({'buffer':0,'byteOffset':total_len(),'byteLength':len(ibm_raw)})
    ibm_acc = len(new_accessors)
    new_accessors.append({'bufferView':ibm_bv,'componentType':5126,'count':len(joints),'type':'MAT4'})
    add_bytes(ibm_raw)

    out_gltf = {
        'asset': {'version':'2.0','generator':'flatten_founder'},
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
    open(dst, 'wb').write(out)
    print('wrote %s (%d bytes, %d nodes, %d prims, %d joints)' %
          (dst, len(out), len(new_nodes), len(merged_prims), len(joints)))


def verify(path):
    gltf, binb = load_glb(path)
    nodes = gltf['nodes']
    G = global_transforms(nodes)
    skin = gltf['skins'][0]
    joints = skin['joints']
    ibm_raw = accessor_array(gltf, binb, skin['inverseBindMatrices']).reshape(-1,4,4)
    ibm = ibm_raw.transpose(0,2,1)   # glTF column-major -> numpy row-major
    J = np.array([G[j] for j in joints])
    maxerr = 0.0
    for mi, mesh in enumerate(gltf['meshes']):
        mn = [i for i, nd in enumerate(nodes) if nd.get('mesh') == mi]
        if not mn: continue
        Gmesh = G[mn[0]]
        for prim in mesh['primitives']:
            pos = accessor_array(gltf, binb, prim['attributes']['POSITION'])
            jidx = accessor_array(gltf, binb, prim['attributes']['JOINTS_0']).astype(int)
            w = accessor_array(gltf, binb, prim['attributes']['WEIGHTS_0'])
            skinned = np.zeros_like(pos)
            for v in range(len(pos)):
                p = np.zeros(3)
                for k in range(4):
                    jj = jidx[v,k]; ww = w[v,k]
                    if ww == 0: continue
                    Msk = J[jj] @ ibm[jj] @ Gmesh
                    p += ww * (Msk[:3,:3] @ pos[v] + Msk[:3,3])
                skinned[v] = p
            target = (Gmesh[:3,:3] @ pos.T).T + Gmesh[:3,3]
            err = float(np.abs(skinned - target).max())
            maxerr = max(maxerr, err)
            print('mesh %d prim: verts=%d bind_skinning_err=%.5f' % (mi, len(pos), err))
    print('OVERALL bind-pose skinning error = %.5f' % maxerr)
    return maxerr


if __name__ == '__main__':
    src = r'Unreal/Born2Flap/SourceAssets/Founder/Founder_rigged.glb'
    dst = r'scratch/SK_Founder_flat.glb'
    main(src, dst)
    print('--- verifying bind-pose skinning ---')
    err = verify(dst)
    sys.exit(0 if err < 0.5 else 1)
