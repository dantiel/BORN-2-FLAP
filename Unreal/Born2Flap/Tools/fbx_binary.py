"""Minimal FBX binary (7.x, uint32 node records) reader/writer.

Only what the avatar importer needs: walk the node tree, read properties, find
Model objects (bones) by name, and re-serialise the file with renamed bone
strings.  Handles the standard FBX SDK layout:

    Node record:
        uint32 EndOffset        (absolute offset of node end incl. children)
        uint32 NumProperties
        uint32 PropertyListLen  (byte length of the property payload)
        uint8  NameLen
        char   Name[NameLen]
        byte   Properties[PropertyListLen]
        NodeRecord Children[]

Properties are type-tagged: Y i16, C bool, I i32, F f32, D f64, L i64,
S string (u32 len + bytes), R raw (u32 len + bytes),
f/d/i/l/b arrays (u32 byte-len + u32 encoding + u32 compressed-len + bytes).

The Mixamo Clips_Colon FBX files are a DIFFERENT (uint64 EndOffset) layout and
are NOT handled here — bake_mixamo_into_glb.py reads those.
"""
import struct
from pathlib import Path


def node_header(buf, off):
    """Return (name, numprops, proplen, props_start, children_start, end)."""
    end = struct.unpack_from('<I', buf, off)[0]
    numprops = struct.unpack_from('<I', buf, off + 4)[0]
    proplen = struct.unpack_from('<I', buf, off + 8)[0]
    namelen = buf[off + 12]
    name = buf[off + 13:off + 13 + namelen]
    props_start = off + 13 + namelen
    children_start = props_start + proplen
    return name, numprops, proplen, props_start, children_start, end


_ARRAY_CODES = {b'f', b'd', b'i', b'l', b'b'}


def read_props(buf, props_start, numprops):
    """Read `numprops` typed properties starting at props_start."""
    props = []
    cur = props_start
    for _ in range(numprops):
        t = buf[cur:cur + 1]
        cur += 1
        if t == b'S':
            ln = struct.unpack_from('<I', buf, cur)[0]
            cur += 4
            props.append(('S', bytes(buf[cur:cur + ln])))
            cur += ln
        elif t == b'R':
            ln = struct.unpack_from('<I', buf, cur)[0]
            cur += 4
            props.append(('R', bytes(buf[cur:cur + ln])))
            cur += ln
        elif t == b'Y':
            props.append(('Y', struct.unpack_from('<h', buf, cur)[0])); cur += 2
        elif t == b'C':
            props.append(('C', buf[cur])); cur += 1
        elif t == b'I':
            props.append(('I', struct.unpack_from('<i', buf, cur)[0])); cur += 4
        elif t == b'F':
            props.append(('F', struct.unpack_from('<f', buf, cur)[0])); cur += 4
        elif t == b'D':
            props.append(('D', struct.unpack_from('<d', buf, cur)[0])); cur += 8
        elif t == b'L':
            props.append(('L', struct.unpack_from('<q', buf, cur)[0])); cur += 8
        elif t in _ARRAY_CODES:
            arrlen = struct.unpack_from('<I', buf, cur)[0]; cur += 4
            # encoding u32 + compressed-length u32 follow, then arrlen bytes
            cur += 8 + arrlen
            props.append((t.decode(), arrlen))
        else:
            # unknown — cannot reliably advance; bail
            props.append(('?', t))
            break
    return props


def iter_children(buf, start, end):
    """Yield (name, props, node_off, node_end) for direct children in [start,end)."""
    off = start
    while off + 13 <= end and off < len(buf) - 13:
        name, numprops, proplen, props_start, children_start, e = node_header(buf, off)
        props = read_props(buf, props_start, numprops)
        yield name, props, off, e
        if e <= off or e > len(buf):
            break
        off = e


def find_child(buf, start, end, want):
    for name, props, off, e in iter_children(buf, start, end):
        if name == want:
            return name, props, off, e
    return None, None, None, None


def find_model_names(buf):
    """Return [(bone_name_bytes, node_off, end)] for every Model object.

    Walks only the top level to find `Objects`, then its direct children
    (Model / Geometry / Deformer / ...).  A Model's bone name is its first
    string property (FBX 7.x stores `Model::Name`; newer exporters store the
    bare name without the `Model::` prefix).
    """
    top_end = len(buf)
    result = []
    for name, props, off, e in iter_children(buf, 27, top_end):
        if name != b'Objects':
            continue
        _, _, _, _, children_start, obj_end = node_header(buf, off)
        for mname, mprops, moff, mend in iter_children(buf, children_start, obj_end):
            if mname != b'Model':
                continue
            for p in mprops:
                if p[0] == 'S':
                    result.append((p[1], moff, mend))
                    break
    return result
