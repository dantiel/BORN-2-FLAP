"""Transfer glTF animation clips from one Mixamo GLB into another (by bone NAME).

The Founder GLB carries the 13 baked Mixamo clips (Idle/Walk/Run/Throw/...).
Every avatar character shares the same `mixamorig:*` skeleton, so the clips
can be re-targeted by re-indexing the channel targets to the destination GLB's
nodes. Raw sampler data (times + quaternions) is copied into the destination
BIN chunk and new bufferViews/accessors are appended.

Run:
  py Tools/transfer_avatar_anims.py <src.glb> <dst.glb> <out.glb>
"""
import json, struct, sys


def read_glb(path):
    b = open(path, 'rb').read()
    assert struct.unpack_from('<III', b, 0)[0] == 0x46546C67
    off = 12
    chunks = {}
    while off < len(b):
        clen, ctype = struct.unpack_from('<II', b, off)
        chunks[ctype] = (off + 8, clen)
        off += 8 + clen
    joff, jlen = chunks[0x4E4F534A]
    gltf = json.loads(b[joff:joff + jlen].decode('utf-8'))
    binoff, binlen = chunks[0x4E4942]
    return gltf, b[joff + jlen + 8:], gltf  # json, raw bin bytes


def extract_raw(gltf, bin_data, acc):
    bvs = gltf['bufferViews']
    bv = bvs[acc['bufferView']]
    start = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
    fmt = {5126: 'f', 5123: 'H', 5125: 'I', 5121: 'B'}[acc['componentType']]
    comps = {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}[acc['type']]
    count = acc['count']
    length = count * comps * struct.calcsize(fmt)
    return bin_data[start:start + length]


def main():
    src_path, dst_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    s_gltf, s_bin, _ = read_glb(src_path)
    d_gltf, d_bin, _ = read_glb(dst_path)

    if 'animations' not in s_gltf:
        print('source has no animations')
        return 1

    s_name2node = {}
    for i, n in enumerate(s_gltf['nodes']):
        if n.get('name'):
            s_name2node[n['name']] = i
    d_name2node = {}
    for i, n in enumerate(d_gltf['nodes']):
        if n.get('name'):
            d_name2node[n['name']] = i

    d_bin = bytearray(d_bin)
    d_bvs = d_gltf.setdefault('bufferViews', [])
    d_accs = d_gltf.setdefault('accessors', [])

    def append_raw(raw):
        # 4-byte alignment for float accessors
        if len(d_bin) % 4:
            d_bin.extend(b'\x00' * (4 - len(d_bin) % 4))
        off = len(d_bin)
        d_bin.extend(raw)
        d_bvs.append({'buffer': 0, 'byteOffset': off, 'byteLength': len(raw)})
        return len(d_bvs) - 1

    new_anims = []
    skipped = 0
    for anim in s_gltf['animations']:
        out_samplers = []
        out_channels = []
        sampler_map = {}
        for ci, ch in enumerate(anim['channels']):
            src_node = ch['target']['node']
            nm = s_gltf['nodes'][src_node].get('name')
            if nm not in d_name2node:
                skipped += 1
                continue
            si = ch['sampler']
            if si in sampler_map:
                new_si = sampler_map[si]
            else:
                samp = anim['samplers'][si]
                in_acc = s_gltf['accessors'][samp['input']]
                out_acc = s_gltf['accessors'][samp['output']]
                in_raw = extract_raw(s_gltf, s_bin, in_acc)
                out_raw = extract_raw(s_gltf, s_bin, out_acc)
                new_in_bv = append_raw(in_raw)
                new_out_bv = append_raw(out_raw)
                def clone_acc(a, bv_idx, raw):
                    na = {k: v for k, v in a.items() if k not in ('byteOffset', 'bufferView', 'name')}
                    na['bufferView'] = bv_idx
                    na.pop('min', None)
                    na.pop('max', None)
                    return na
                d_accs.append(clone_acc(in_acc, new_in_bv, in_raw))
                new_in_acc = len(d_accs) - 1
                d_accs.append(clone_acc(out_acc, new_out_bv, out_raw))
                new_out_acc = len(d_accs) - 1
                out_samplers.append({'input': new_in_acc, 'output': new_out_acc,
                                     'interpolation': samp.get('interpolation', 'LINEAR')})
                new_si = len(out_samplers) - 1
                sampler_map[si] = new_si
            out_channels.append({
                'sampler': new_si,
                'target': {'node': d_name2node[nm], 'path': ch['target']['path']},
            })
        if out_channels:
            new_anims.append({'name': anim.get('name'), 'samplers': out_samplers, 'channels': out_channels})

    d_gltf['animations'] = new_anims
    d_gltf['buffers'] = [{'byteLength': len(d_bin)}]

    json_bytes = json.dumps(d_gltf, separators=(',', ':')).encode('utf-8')
    pad = (-len(json_bytes)) % 4
    json_bytes += b' ' * pad
    total = 12 + 8 + len(json_bytes) + 8 + len(d_bin)
    out = struct.pack('<III', 0x46546C67, 2, total)
    out += struct.pack('<II', len(json_bytes), 0x4E4F534A) + json_bytes
    out += struct.pack('<II', len(d_bin), 0x004E4942) + bytes(d_bin)
    open(out_path, 'wb').write(out)
    print('transferred %d animations (%d channels skipped) -> %s (%d bytes)'
          % (len(new_anims), skipped, out_path, total))
    return 0


if __name__ == '__main__':
    sys.exit(main())
