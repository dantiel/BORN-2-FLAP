"""Normalize a Mixamo-rigged GLB to the clean `mixamorig:*` 65-bone skeleton.

Handles the common Sketchfab/Objaverse Mixamo export variants:
  - `mixamorig:Hips_01`  (founder style)     -> `mixamorig:Hips`
  - `Hips_01`            (prefix stripped)   -> `mixamorig:Hips`
  - `mott_var01:Hips_01` (renamed prefix)    -> `mixamorig:Hips`
  - `mixamorig1:Hips_147`(renamed prefix)    -> `mixamorig:Hips`
  - `_rootJoint` / `*_rootJoint` root        -> removed, Hips reparented
Only BONE nodes (skin.joints) are renamed; mesh nodes are relabelled Section0..N
for predictable Unreal import paths. inverseBindMatrices is re-packed after the
root-joint removal.
"""
import json, struct, re, sys, copy
import numpy as np


def _base_name(name):
    name = re.sub(r'_\d+$', '', name)
    if ':' in name:
        name = name.split(':')[-1]
    return name.strip(' _')


def _is_root_joint(name):
    b = _base_name(name)
    return b.lower().endswith('rootjoint') or b.lower() == 'root'


def normalize(glb_bytes):
    assert struct.unpack_from('<III', glb_bytes, 0)[0] == 0x46546C67
    clen, ctype = struct.unpack_from('<II', glb_bytes, 12)
    assert ctype == 0x4E4F534A
    gltf = json.loads(glb_bytes[20:20 + clen].decode('utf-8'))
    bin_off = 20 + clen
    blen, btype = struct.unpack_from('<II', glb_bytes, bin_off)
    assert btype == 0x004E4942
    bin_data = bytearray(glb_bytes[bin_off + 8:bin_off + 8 + blen])

    nodes = gltf['nodes']

    # bone nodes = union of all skin.joints
    bone_nodes = set()
    for sk in gltf.get('skins', []):
        bone_nodes.update(sk['joints'])

    # nodes to remove (root joints)
    remove = set()
    for i, n in enumerate(nodes):
        if _is_root_joint(n.get('name', '')):
            remove.add(i)
    bone_nodes -= remove

    orig_skins = [list(sk['joints']) for sk in gltf.get('skins', [])]

    # rename bone nodes -> mixamorig:*
    for i in bone_nodes:
        nm = nodes[i].get('name', '')
        if nm:
            nodes[i]['name'] = 'mixamorig:' + _base_name(nm)

    # relabel mesh nodes + meshes -> Section0..N (predictable import paths)
    mesh_section = {}
    counter = 0
    for i, n in enumerate(nodes):
        if 'mesh' in n and i not in remove:
            label = 'Section%d' % counter
            counter += 1
            n['name'] = label
            mesh_section[n['mesh']] = label
    for mi, label in mesh_section.items():
        gltf['meshes'][mi]['name'] = label

    old_parent = {}
    for i, n in enumerate(nodes):
        for c in n.get('children', []):
            old_parent[c] = i

    new_index = {}
    new_nodes = []
    for i, n in enumerate(nodes):
        if i in remove:
            continue
        new_index[i] = len(new_nodes)
        new_nodes.append(copy.deepcopy(n))

    def fix_children(lst):
        return [new_index[c] for c in lst if c not in remove]

    for n in new_nodes:
        if 'children' in n:
            n['children'] = fix_children(n['children'])

    for s in gltf.get('scenes', []):
        s['nodes'] = [new_index[c] for c in s.get('nodes', []) if c not in remove]

    # adopt children of removed root joints
    for i, n in enumerate(nodes):
        if i not in remove:
            continue
        p = old_parent.get(i)
        for gc in n.get('children', []):
            if gc in remove:
                continue
            ngc = new_index[gc]
            if p is not None and p not in remove:
                new_nodes[new_index[p]].setdefault('children', [])
                if ngc not in new_nodes[new_index[p]]['children']:
                    new_nodes[new_index[p]]['children'].append(ngc)
            else:
                scene = gltf['scenes'][gltf.get('scene', 0)]
                scene.setdefault('nodes', [])
                if ngc not in scene['nodes']:
                    scene['nodes'].append(ngc)

    for sk, orig_joints in zip(gltf.get('skins', []), orig_skins):
        new_joints = [new_index[j] for j in orig_joints if j not in remove]
        sk['joints'] = new_joints
        if 'skeleton' in sk and sk['skeleton'] is not None and sk['skeleton'] not in remove:
            sk['skeleton'] = new_index[sk['skeleton']]
        elif 'skeleton' in sk:
            sk['skeleton'] = new_joints[0] if new_joints else None

    gltf['nodes'] = new_nodes

    for sk, orig_joints in zip(gltf.get('skins', []), orig_skins):
        ibm_idx = sk.get('inverseBindMatrices')
        if ibm_idx is None:
            continue
        acc = gltf['accessors'][ibm_idx]
        bv = gltf['bufferViews'][acc['bufferView']]
        n_old = acc['count']
        dt = np.dtype('<f4')
        start = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
        raw = np.frombuffer(bin_data, dtype=dt, count=n_old * 16, offset=start).copy()
        removed_count = n_old - len(sk['joints'])
        if removed_count > 0:
            raw = raw[removed_count * 16:]
        raw = raw.reshape(-1, 16).astype('<f4').tobytes()

        pad = (-len(bin_data)) % 4
        bin_data.extend(b'\x00' * pad)
        offset = len(bin_data)
        bin_data.extend(raw)

        new_bv_idx = len(gltf.get('bufferViews', []))
        gltf.setdefault('bufferViews', []).append({
            'buffer': 0,
            'byteOffset': offset,
            'byteLength': len(raw),
        })
        acc['bufferView'] = new_bv_idx
        acc['byteOffset'] = 0
        acc['count'] = len(sk['joints'])
        acc.pop('min', None)
        acc.pop('max', None)

    gltf['buffers'] = [{'byteLength': len(bin_data)}]

    json_bytes = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
    pad = (-len(json_bytes)) % 4
    json_bytes += b' ' * pad
    out = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(json_bytes) + 8 + len(bin_data))
    out += struct.pack('<II', len(json_bytes), 0x4E4F534A) + json_bytes
    out += struct.pack('<II', len(bin_data), 0x004E4942) + bytes(bin_data)
    return out, len(nodes), len(new_nodes)


if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, 'rb').read()
    out, n_old, n_new = normalize(data)
    open(dst, 'wb').write(out)
    print('normalized %s -> %s (%d bytes, nodes %d -> %d)' % (src, dst, len(out), n_old, n_new))
    clen, _ = struct.unpack_from('<II', out, 12)
    g = json.loads(out[20:20 + clen].decode('utf-8'))
    for sk in g.get('skins', []):
        print('  joints (%d):' % len(sk['joints']))
        for j in sk['joints'][:8]:
            print('    ', g['nodes'][j]['name'])
    for i, n in enumerate(g['nodes']):
        if 'mesh' in n:
            print('  mesh node:', n['name'], '-> mesh', n['mesh'])
