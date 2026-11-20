"""Rename bone strings in FBX 7.2/7.4 binary (uint32 node-record) files.

Brings non-Mixamo humanoid rigs (CC4 `CC_Base_*`, Maya/Blender production
rigs) onto the shared `mixamorig:` skeleton so the Mixamo clip set retargets
1:1 by bone name (see import_avatar_fbx.py).

Node record (FBX 7.2/7.4 — NOT the Mixamo 7.7 uint64 layout):
    uint32 EndOffset          (absolute offset of node end incl. children)
    uint32 NumProperties
    uint32 PropertyListLen    (byte length of the property payload)
    uint8  NameLen
    char   Name[NameLen]
    byte   Properties[PropertyListLen]
    NodeRecord Children[]

A bone's name is the FIRST segment of the `Name\x00\x01{Class}` string property
every Model object carries (class = NodeAttribute / Model / Null / LimbNode).
Each bone appears twice — under `Objects` (class=NodeAttribute/…) and under
`Connections` (class=Model) — both renamed identically.

Property type tags: Y i16, C bool, I i32, F f32, D f64, L i64,
S/R string/raw (u32 len + bytes),
f/d/i/l/b arrays (u32 ArrayLength + u32 Encoding + u32 CompressedLength + bytes).

Usage:
    python Tools/fbx_rename_bones.py <in.fbx> <mapping.json> <out.fbx>

where mapping.json is { "old_bone_name": "new_bone_name", ... }.
"""
import json
import struct
import sys
from pathlib import Path

HEADER_LEN = 27
_TAG_FIXED = {b'Y': 2, b'C': 1, b'I': 4, b'F': 4, b'D': 8, b'L': 8}
_TAG_ARRAY = {b'f', b'd', b'i', b'l', b'b'}


def parse_node(buf, off):
    """Return (node_dict, end_offset) for the node starting at `off`.

    node_dict: {name: bytes, numprops: int, props: raw bytes, children: [...],
    null: bool}. Property bytes are preserved verbatim (arrays included,
    compressed). A node's children list is terminated by a 13-byte all-zero
    null record; `null` records whether one was present (nodes with children
    carry it, leaves do not).
    """
    end, numprops, proplen = struct.unpack_from('<III', buf, off)
    namelen = buf[off + 12]
    name = buf[off + 13:off + 13 + namelen]
    props_start = off + 13 + namelen
    props_end = props_start + proplen
    props = buf[props_start:props_end]
    children = []
    has_null = False
    cur = props_end
    while cur < end:
        child_end = struct.unpack_from('<I', buf, cur)[0]
        if child_end == 0:
            # null record (13 zero bytes) marks the end of the children list
            has_null = True
            break
        child, cur = parse_node(buf, cur)
        children.append(child)
    return {'name': name, 'numprops': numprops, 'props': props,
            'children': children, 'null': has_null}, end


def _split_segments(s):
    """Split a FBX string property into (head, b'\\x00\\x01', tail).

    Bone names are the head segment of `Name\\x00\\x01{Class}`; rename head
    and preserve the class tail verbatim.
    """
    sep = s.find(b'\x00\x01')
    if sep < 0:
        return s, b'', b''
    return s[:sep], b'\x00\x01', s[sep + 2:]


def rename_props(props, mapping):
    """Rewrite string/raw properties whose head segment is a bone name."""
    out = bytearray()
    cur = 0
    n = len(props)
    while cur < n:
        tag = props[cur:cur + 1]
        cur += 1
        out += tag
        if tag in _TAG_FIXED:
            sz = _TAG_FIXED[tag]
            out += props[cur:cur + sz]
            cur += sz
        elif tag == b'S' or tag == b'R':
            ln = struct.unpack_from('<I', props, cur)[0]
            cur += 4
            s = props[cur:cur + ln]
            cur += ln
            head, sep, tail = _split_segments(s)
            if head in mapping:
                s = mapping[head] + sep + tail
            out += struct.pack('<I', len(s)) + s
        elif tag in _TAG_ARRAY:
            # ArrayLength(u32) + Encoding(u32) + CompressedLength(u32) + data.
            arrlen, enc, complen = struct.unpack_from('<III', props, cur)
            if cur + 12 + complen > n + 1:
                raise ValueError('array overrun at %d' % cur)
            out += props[cur:cur + 12 + complen]
            cur += 12 + complen
        else:
            raise ValueError('unknown property tag %r at %d' % (tag, cur - 1))
    return bytes(out)


def serialize_node(node, base):
    """Serialize `node` starting at absolute offset `base`; return (bytes, end)."""
    name = node['name']
    props = node['props']
    namelen = len(name)
    proplen = len(props)
    children_buf = b''
    child_off = base + 13 + namelen + proplen
    for c in node['children']:
        cb, child_off = serialize_node(c, child_off)
        children_buf += cb
    if node['null']:
        children_buf += b'\x00' * 13
        child_off += 13
    end = child_off
    header = struct.pack('<III', end, node['numprops'], proplen) \
        + bytes([namelen]) + name
    return header + props + children_buf, end


def rename_fbx(data, mapping):
    """Return the re-serialised FBX bytes with renamed bone strings."""
    header = data[:HEADER_LEN]
    roots = []
    off = HEADER_LEN
    while off < len(data):
        # The top-level node list is terminated by a null record (end == 0);
        # after it sits the FBX 7.2 footer (GUIDs + version, no offsets), which
        # is preserved verbatim below.
        if struct.unpack_from('<I', data, off)[0] == 0:
            break
        node, off = parse_node(data, off)
        roots.append(node)
    tail = data[off:]  # null record + footer (position-independent)

    def walk(node):
        node = dict(node)
        node['props'] = rename_props(node['props'], mapping)
        node['children'] = [walk(c) for c in node['children']]
        return node

    roots = [walk(r) for r in roots]

    out = bytearray(header)
    base = HEADER_LEN
    for r in roots:
        buf, base = serialize_node(r, base)
        out += buf
    out += tail
    return bytes(out)


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(1)
    src, mapping_path, dst = sys.argv[1], sys.argv[2], sys.argv[3]
    mapping = json.loads(Path(mapping_path).read_text(encoding='utf-8'))
    mapping = {k.encode('utf-8'): v.encode('utf-8') for k, v in mapping.items()}
    data = Path(src).read_bytes()
    out = rename_fbx(data, mapping)
    Path(dst).write_bytes(out)
    print('renamed %d bones: %s -> %s (%d -> %d bytes)' % (
        len(mapping), src, dst, len(data), len(out)))


if __name__ == '__main__':
    main()