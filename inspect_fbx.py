import struct, sys, os

def read_node(buf, off, depth=0):
    if off + 25 > len(buf):
        return None, len(buf)
    end = struct.unpack_from('<Q', buf, off)[0]
    numprops = struct.unpack_from('<Q', buf, off+8)[0]
    namelen = buf[off+24]
    if end > len(buf):
        end = len(buf)
    name = buf[off+25:off+25+namelen].decode('utf-8', 'replace')
    cur = off + 25 + namelen
    props = []
    for _ in range(min(numprops, 5000)):
        if cur >= len(buf) or cur >= end:
            break
        t = buf[cur:cur+1]; cur += 1
        if t == b'Y':
            props.append(('int16', struct.unpack_from('<h', buf, cur)[0])); cur += 2
        elif t == b'C':
            props.append(('bool', buf[cur] != 0)); cur += 1
        elif t == b'I':
            props.append(('int32', struct.unpack_from('<i', buf, cur)[0])); cur += 4
        elif t == b'F':
            props.append(('float', struct.unpack_from('<f', buf, cur)[0])); cur += 4
        elif t == b'D':
            props.append(('double', struct.unpack_from('<d', buf, cur)[0])); cur += 8
        elif t == b'L':
            props.append(('int64', struct.unpack_from('<q', buf, cur)[0])); cur += 8
        elif t == b'S':
            ln = struct.unpack_from('<I', buf, cur)[0]; cur += 4
            props.append(('string', buf[cur:cur+ln].decode('utf-8','replace'))); cur += ln
        elif t == b'R':
            ln = struct.unpack_from('<I', buf, cur)[0]; cur += 4
            props.append(('raw', buf[cur:cur+ln])); cur += ln
        elif t in (b'd', b'f', b'i', b'l', b'b'):
            esz = {'d':8,'f':4,'i':4,'l':8,'b':1}[t.decode()]
            arrlen = struct.unpack_from('<I', buf, cur)[0]; cur += 4
            cur += 8  # encoding + compressed length
            if arrlen > len(buf) - cur:
                arrlen = max(0, len(buf) - cur)
            cnt = arrlen // esz if esz else 0
            if cnt > 10000000:
                cnt = 0
            fmt = {'d':'d','f':'f','i':'i','l':'q','b':'B'}[t.decode()]
            try:
                arr = struct.unpack_from('<%d%s' % (cnt, fmt), buf, cur)
            except Exception:
                arr = ()
            cur += arrlen
            props.append(('arr:'+t.decode(), arr))
        else:
            props.append(('UNKNOWN', t))
    children = []
    while cur < end and cur < len(buf) and depth < 200:
        child, cur = read_node(buf, cur, depth+1)
        if child is None:
            break
        children.append(child)
    node = {'name': name, 'props': props, 'children': children}
    return node, cur

def find(node, key):
    for i,(k,v) in enumerate(node['props']):
        if k == 'string' and v == key:
            return i
    return -1

def props_dict(node):
    d = {}
    for k,v in node['props']:
        d.setdefault(k, []).append(v)
    return d

def walk(node, out):
    out.append(node)
    for c in node['children']:
        walk(c, out)

def main(path):
    buf = open(path, 'rb').read()
    off = 27  # skip Kaydara FBX Binary header
    roots = []
    while off < len(buf) - 1:
        node, off = read_node(buf, off, 0)
        if node is None:
            break
        roots.append(node)
    nodes = []
    for r in roots:
        walk(r, nodes)
    print('=== %s ===' % os.path.basename(path))
    models = [n for n in nodes if n['name'] == 'Model']
    geometries = [n for n in nodes if n['name'] == 'Geometry']
    print('Model nodes: %d, Geometry nodes: %d' % (len(models), len(geometries)))
    # bone names (Model with Type=Skeleton/LimbNode)
    bones = []
    for m in models:
        pd = props_dict(m)
        ty = '?'
        if 'string' in pd:
            # find 'Type' string
            strs = [v for k,v in m['props'] if k=='string']
            for j,s in enumerate(strs):
                if s in ('Skeleton','LimbNode','Limb','Null','Mesh'):
                    ty = s
        name = '?'
        strs = [v for k,v in m['props'] if k=='string']
        # Model name is usually the last string prop? Actually first string is name
        if strs:
            name = strs[0]
        if ty in ('Skeleton','LimbNode','Limb'):
            bones.append((name, ty))
    print('Skeleton bones found: %d' % len(bones))
    print('First 12 bones:', bones[:12])
    print('Last 6 bones:', bones[-6:])
    # vertex bounds from Geometry
    for g in geometries:
        pd = props_dict(g)
        name = '?'
        strs = [v for k,v in m['props'] if k=='string']
        for k,v in g['props']:
            if k=='string' and v.startswith('Geometry'):
                name = v
        verts = None
        for k,v in g['props']:
            if k == 'arr:d' and len(v) > 30:
                verts = v  # likely Vertices (x,y,z doubles)
                break
        if verts is not None:
            xs = verts[0::3]; ys = verts[1::3]; zs = verts[2::3]
            if xs:
                print('  Geometry %s verts=%d  X[%.1f..%.1f] Y[%.1f..%.1f] Z[%.1f..%.1f]' %
                      (name, len(xs), min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)))
    # find skin deformers / clusters
    clusters = [n for n in nodes if n['name'] == 'Cluster']
    print('Skin clusters: %d' % len(clusters))

if __name__ == '__main__':
    main(sys.argv[1])