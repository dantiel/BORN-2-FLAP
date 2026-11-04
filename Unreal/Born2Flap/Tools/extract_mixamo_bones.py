"""Extract Mixamo FBX reference pose (PreRotation + Lcl Translation) for ALL 65 bones,
converting to glTF Y-up meters, and write to JSON for the re-flatten step."""
import struct, json, numpy as np

def read_node(buf, off, depth):
    if off + 25 > len(buf):
        raise RuntimeError('bad off')
    end = struct.unpack_from('<Q', buf, off)[0]
    numprops = struct.unpack_from('<Q', buf, off+8)[0]
    namelen = buf[off+24]
    if end > len(buf):
        end = len(buf)
    name = buf[off+25:off+25+namelen]
    cur = off + 25 + namelen
    props = []
    for _ in range(min(numprops, 2000)):
        if cur >= len(buf):
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
            props.append(('string', buf[cur:cur+ln])); cur += ln
        elif t == b'R':
            ln = struct.unpack_from('<I', buf, cur)[0]; cur += 4
            props.append(('raw', buf[cur:cur+ln])); cur += ln
        elif t in (b'd', b'f', b'i', b'l', b'b'):
            esz = {'d':8,'f':4,'i':4,'l':8,'b':1}[t.decode()]
            arrlen = struct.unpack_from('<I', buf, cur)[0]; cur += 4
            cur += 8
            if arrlen > len(buf) - cur:
                arrlen = len(buf) - cur
            cnt = arrlen // esz if (esz and arrlen > 0) else 0
            if cnt > 5000000:
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
    while cur < end and cur < len(buf) and depth < 150:
        child, cur = read_node(buf, cur, depth+1)
        children.append(child)
    return {'name': name, 'props': props, 'children': children}, end

def parse(path):
    buf = open(path, 'rb').read()
    off = 27
    roots = []
    while off < len(buf) - 1:
        node, off = read_node(buf, off, 0)
        roots.append(node)
    return roots

def sval(props):
    for p in props:
        if p[0] == 'string':
            return p[1].decode('utf-8', 'replace')
    return None

def p70(node):
    out = {}
    for c in node['children']:
        if c['name'] != b'Properties70':
            continue
        for p in c['children']:
            if p['name'] != b'P':
                continue
            pname = sval(p['props'])
            if not pname:
                continue
            nums = [x[1] for x in p['props'] if x[0] in ('double','float','int32','int16','int64')]
            out[pname] = nums
    return out

def find_all(node, name, out):
    for c in node['children']:
        if c['name'] == name:
            out.append(c)
        find_all(c, name, out)
    return out

roots = parse('Unreal/Born2Flap/SourceAssets/Mixamo/Idle.fbx')
models = []
for r in roots:
    find_all(r, b'Model', models)

# hierarchy: extract "C" (connections) to get parent-child for models
result = {}
for m in models:
    nm = sval(m['props'])
    if not nm or 'mixamorig' not in nm:
        continue
    # the model name is like "Model::mixamorig:Hips" or the 2nd string prop
    # find the prop containing 'mixamorig'
    name = None
    for p in m['props']:
        if p[0] == 'string':
            s = p[1].decode('utf-8','replace')
            if 'mixamorig' in s:
                name = s.split('::')[-1]
                break
    if not name:
        continue
    name = name.split('\x00')[0].replace(':', '_')
    vals = p70(m)
    result[name] = {
        'T': vals.get('Lcl Translation'),
        'Pre': vals.get('PreRotation'),
        'R': vals.get('Lcl Rotation'),
    }

open('scratch/mixamo_bones.json', 'w').write(json.dumps(result, indent=1))
print('WROTE', len(result), 'bones')
for k in list(result.keys())[:8]:
    print(k, result[k])
