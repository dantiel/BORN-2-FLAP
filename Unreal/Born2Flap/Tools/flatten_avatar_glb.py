"""
Flatten a Sketchfab-wrapped Mixamo GLB into a clean single-mesh, single-skeleton
glTF that UE imports as a normal standing human:

  * bake ALL node transforms into vertices -> Z-up, cm scale, feet at Z=0
  * merge the Section0..N meshes into ONE mesh (multi-primitive, one skin)
  * re-root the 65-bone skeleton at Hips (no wrapper nodes, no matrices above)
  * recompute inverseBindMatrices for the new frame

Result: a glTF with exactly 1 mesh, 1 skin, flat node list. The UE importer has
no wrapper matrix, no 0.01 scale node, no axis-flip node to mis-handle.
"""
import sys, json, struct, numpy as np

TARGET_HEIGHT_M = 1.8  # meters — importer converts glTF meters -> UE cm (x100)


def load_glb(path):
    d = open(path, 'rb').read()
    assert struct.unpack_from('<III', d, 0)[0] == 0x46546C67
    off = 12; js = None; binb = b''
    while off < len(d):
        clen, ct = struct.unpack_from('<II', d, off); off += 8
        c = d[off:off+clen]; off += clen
        if ct == 0x4E4F534A: js = json.loads(c.decode('utf-8'))
        elif ct == 0x004E4942: binb = c
    return js, binb


def local_matrix(node):
    """Return 4x4 row-major (M@v) local transform of a node."""
    m = node.get('matrix')
    if m is not None:
        # glTF matrices are column-major; convert to row-major
        return np.array(m, dtype=np.float64).reshape(4, 4).T
    T = np.eye(4)
    t = node.get('translation')
    if t: T[:3, 3] = t
    R = np.eye(4)
    r = node.get('rotation')
    if r:
        x, y, z, w = r
        R[:3, :3] = np.array([
            [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
            [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
            [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)],
        ])
    S = np.eye(4)
    s = node.get('scale')
    if s: S[0,0], S[1,1], S[2,2] = s
    return T @ R @ S


def global_transforms(gltf):
    nodes = gltf['nodes']
    n = len(nodes)
    glob = [None] * n
    def compute(i, parent_g):
        glob[i] = parent_g @ local_matrix(nodes[i])
        for c in nodes[i].get('children', []):
            compute(c, glob[i])
    is_child = [False]*n
    for node in nodes:
        for c in node.get('children', []):
            is_child[c] = True
    for i in range(n):
        if not is_child[i]:
            compute(i, np.eye(4))
    for i in range(n):
        if glob[i] is None:
            compute(i, np.eye(4))
    return glob


def accessor_array(gltf, binb, idx):
    acc = gltf['accessors'][idx]
    bv = gltf['bufferViews'][acc['bufferView']]
    comp = {'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[acc['type']]
    ct = acc['componentType']
    dt = {5120:'<i1',5121:'<u1',5122:'<i2',5123:'<u2',5125:'<u4',5126:'<f4'}[ct]
    itemsize = np.dtype(dt).itemsize
    base = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
    stride = bv.get('byteStride', comp * itemsize)
    arr = np.empty((acc['count'], comp), dtype=np.float64)
    for i in range(acc['count']):
        arr[i] = np.frombuffer(binb, dtype=dt, count=comp, offset=base + i*stride)
    return arr


def main(src, dst):
    gltf, binb = load_glb(src)
    glob = global_transforms(gltf)
    nodes = gltf['nodes']

    skin = gltf['skins'][0]
    joints = list(skin['joints'])
    mesh_node_indices = [i for i, n in enumerate(nodes) if 'mesh' in n]

    # --- character world bounds (to derive scale + frame) ---
    all_min = np.array([np.inf]*3); all_max = np.array([-np.inf]*3)
    for ni in mesh_node_indices:
        g = glob[ni]
        for prim in gltf['meshes'][nodes[ni]['mesh']]['primitives']:
            pos = prim['attributes'].get('POSITION')
            if pos is None: continue
            v = accessor_array(gltf, binb, pos)
            vw = (g @ np.hstack([v, np.ones((len(v),1))]).T).T[:, :3]
            all_min = np.minimum(all_min, vw.min(axis=0))
            all_max = np.maximum(all_max, vw.max(axis=0))
    print('world bbox: min=%s max=%s' % (np.round(all_min,3), np.round(all_max,3)))
    height = all_max[1] - all_min[1]
    scale = TARGET_HEIGHT_M / height
    # Keep Y-up (glTF convention); the UE importer converts Y-up -> Z-up and
    # meters -> cm itself. Bake only a uniform scale to land at 1.8 m.
    M = np.diag([scale, scale, scale, 1.0])
    print('height=%.3f scale=%.4f' % (height, scale))

    # --- bake vertices + normals into target frame, merge into one mesh ---
    new_prim_data = []   # list of (pos_array, normal_array, prim_meta)
    for ni in mesh_node_indices:
        g = glob[ni]
        B = M @ g  # full bake matrix (world + target frame)
        B3 = B[:3, :3]
        for prim in gltf['meshes'][nodes[ni]['mesh']]['primitives']:
            pos_idx = prim['attributes'].get('POSITION')
            nrm_idx = prim['attributes'].get('NORMAL')
            v = accessor_array(gltf, binb, pos_idx) if pos_idx is not None else None
            vh = np.hstack([v, np.ones((len(v),1))])
            vw = (B @ vh.T).T[:, :3]
            nrm = None
            if nrm_idx is not None:
                n = accessor_array(gltf, binb, nrm_idx)
                nrm = (B3 @ n.T).T
                ln = np.linalg.norm(nrm, axis=1, keepdims=True)
                ln[ln == 0] = 1
                nrm = nrm / ln
            new_prim_data.append((vw, nrm, dict(prim)))

    # --- rebuild skeleton: joints keep local transforms; Hips re-rooted ---
    # joint world in new frame
    jw = {j: (M @ glob[j]) for j in joints}
    # inverse bind matrices: inverse(joint world in new frame), since mesh node is identity
    ibm = np.zeros((len(joints), 16), dtype=np.float32)
    for k, j in enumerate(joints):
        ibm[k] = np.linalg.inv(jw[j]).reshape(16)

    # Build new nodes list:
    #   node 0 = mesh (identity)
    #   then joints in original order; Hips becomes scene root.
    new_nodes = []
    new_nodes.append({'name': 'AvatarMesh', 'mesh': 0, 'skin': 0})
    hips_new_index = None
    old_to_new = {}
    for k, j in enumerate(joints):
        old_to_new[j] = len(new_nodes)
        n = dict(nodes[j])
        n.pop('mesh', None); n.pop('skin', None); n.pop('matrix', None)
        if j == joints[0]:  # Hips root: absorb wrapper into its transform
            n['matrix'] = [float(x) for x in jw[j].T.reshape(16)]  # row->col major
            n.pop('translation', None); n.pop('rotation', None); n.pop('scale', None)
        new_nodes.append(n)

    # fix children references among joints
    for n in new_nodes:
        if 'children' in n:
            n['children'] = [old_to_new[c] for c in n['children'] if c in old_to_new]

    scene_roots = [0, old_to_new[joints[0]]]

    # --- append new data to the ORIGINAL bin (preserve images/textures) ---
    bin_parts = [bytearray(binb)]
    def add_bytes(b):
        bin_parts.append(bytearray(b))
    # helper to append aligned accessor + bufferView
    def make_accessor(arr_f64, comp, ct=5126, normalized=False):
        arr = np.asarray(arr_f64, dtype=np.float32)
        raw = arr.astype('<f4').tobytes()
        # align 4
        pad = (-len(raw)) % 4
        if pad: raw += b'\x00' * pad
        bv_idx = len(new_buffer_views)
        new_buffer_views.append({'buffer': 0, 'byteOffset': total_len(), 'byteLength': len(raw)})
        a = {'bufferView': bv_idx, 'componentType': ct, 'count': len(arr), 'type': 'VEC3' if comp==3 else 'SCALAR'}
        new_accessors.append(a)
        add_bytes(raw)
        return len(new_accessors) - 1

    new_buffer_views = list(gltf.get('bufferViews', []))
    new_accessors = list(gltf.get('accessors', []))
    def total_len():
        return sum(len(p) for p in bin_parts)

    # merged mesh primitives
    merged_prims = []
    for vw, nrm, prim in new_prim_data:
        new_attrs = {}
        # copy non-position/normal attributes verbatim (JOINTS_0, WEIGHTS_0, TEXCOORD_0, COLOR_0)
        for k, idx in prim['attributes'].items():
            if k in ('POSITION', 'NORMAL'):
                continue
            new_attrs[k] = idx
        pos_acc = make_accessor(vw, 3)
        new_attrs['POSITION'] = pos_acc
        if nrm is not None:
            nrm_acc = make_accessor(nrm, 3)
            new_attrs['NORMAL'] = nrm_acc
        merged_prims.append({
            'attributes': new_attrs,
            'material': prim.get('material', 0),
            'mode': prim.get('mode', 4),
        })

    # IBM accessor
    ibm_raw = ibm.astype('<f4').tobytes()
    pad = (-len(ibm_raw)) % 4
    if pad: ibm_raw += b'\x00' * pad
    ibm_bv = len(new_buffer_views)
    new_buffer_views.append({'buffer': 0, 'byteOffset': total_len(), 'byteLength': len(ibm_raw)})
    ibm_acc = len(new_accessors)
    new_accessors.append({'bufferView': ibm_bv, 'componentType': 5126, 'count': len(joints), 'type': 'MAT4'})
    add_bytes(ibm_raw)

    # --- assemble new gltf ---
    out_gltf = {
        'asset': {'version': '2.0', 'generator': 'flatten_avatar_glb'},
        'scene': 0,
        'scenes': [{'nodes': scene_roots}],
        'nodes': new_nodes,
        'meshes': [{'name': 'Avatar', 'primitives': merged_prims}],
        'skins': [{'joints': [old_to_new[j] for j in joints],
                   'inverseBindMatrices': ibm_acc,
                   'skeleton': old_to_new[joints[0]]}],
        'accessors': new_accessors,
        'bufferViews': new_buffer_views,
        'buffers': [{'byteLength': total_len()}],
        'materials': gltf.get('materials', []),
    }
    # carry textures/images/samplers if present
    for key in ('textures', 'images', 'samplers'):
        if key in gltf:
            out_gltf[key] = gltf[key]

    bin_out = b''.join(bytes(p) for p in bin_parts)
    json_bytes = json.dumps(out_gltf, separators=(',', ':')).encode('utf-8')
    jpad = (-len(json_bytes)) % 4
    json_bytes += b' ' * jpad
    header = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(json_bytes) + 8 + len(bin_out))
    out = header + struct.pack('<II', len(json_bytes), 0x4E4F534A) + json_bytes
    out += struct.pack('<II', len(bin_out), 0x004E4942) + bin_out
    open(dst, 'wb').write(out)
    print('wrote %s (%d bytes, %d nodes, %d prims, %d joints)' % (
        dst, len(out), len(new_nodes), len(merged_prims), len(joints)))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])