"""Merge all skinned mesh-node primitives of a GLB into ONE mesh node.

UE's Interchange glTF importer creates one SkeletalMesh per mesh NODE.
Sophie/Kate/Brian/Adam GLBs have 5-7 separate single-primitive mesh nodes
sharing one skin; merge them into a single multi-primitive mesh node so the
import yields ONE SkeletalMesh with one section per material (what
ABorn2FlapAvatar::LoadPerson expects).
"""
import json
import struct
import sys
from pathlib import Path


def load_glb(path: Path):
    b = path.read_bytes()
    assert b[:4] == b'glTF'
    version = struct.unpack_from('<I', b, 4)[0]
    total = struct.unpack_from('<I', b, 8)[0]
    assert version == 2
    off = 12
    chunks = []
    while off + 8 <= total:
        clen, ctype = struct.unpack_from('<II', b, off)
        chunks.append((ctype, b[off + 8:off + 8 + clen]))
        off += 8 + clen
    return chunks


def save_glb(path: Path, json_bytes: bytes, bin_chunk: bytes):
    json_bytes = json_bytes + b' ' * ((4 - len(json_bytes) % 4) % 4)
    header = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(json_bytes) + 8 + len(bin_chunk))
    json_hdr = struct.pack('<II', len(json_bytes), 0x4E4F534A)  # 'JSON'
    bin_hdr = struct.pack('<II', len(bin_chunk), 0x004E4942)   # 'BIN\0'
    path.write_bytes(header + json_hdr + json_bytes + bin_hdr + bin_chunk)


def merge(path: Path):
    chunks = load_glb(path)
    json_bytes = None
    bin_data = None
    for ctype, data in chunks:
        if ctype == 0x4E4F534A:
            json_bytes = data
        elif ctype == 0x004E4942:
            bin_data = data
    if json_bytes is None:
        raise RuntimeError('no JSON chunk')
    j = json.loads(json_bytes)

    mesh_nodes = [(i, n) for i, n in enumerate(j['nodes']) if 'mesh' in n]
    if len(mesh_nodes) <= 1:
        print('%s: single mesh node, nothing to merge' % path.name)
        return False
    skins = {n.get('skin') for _, n in mesh_nodes}
    if len(skins) != 1:
        raise RuntimeError('mesh nodes on different skins: %s' % skins)

    prims = []
    for _, n in mesh_nodes:
        for p in j['meshes'][n['mesh']]['primitives']:
            prims.append(p)
    merged_mesh = {'name': 'Body', 'primitives': prims}
    j['meshes'].append(merged_mesh)
    new_idx = len(j['meshes']) - 1

    first = mesh_nodes[0]
    j['nodes'][first[0]]['mesh'] = new_idx
    for i, n in mesh_nodes[1:]:
        del j['nodes'][i]['mesh']

    # also merge node extras used by the original mesh nodes (weights etc. are
    # per-primitive so nothing else to move)
    save_glb(path, json.dumps(j, separators=(',', ':')).encode('utf-8'), bin_data)
    print('%s: merged %d primitives into node %d (%s)' % (path.name, len(prims), first[0], first[1].get('name')))
    return True


if __name__ == '__main__':
    for p in sys.argv[1:]:
        merge(Path(p))