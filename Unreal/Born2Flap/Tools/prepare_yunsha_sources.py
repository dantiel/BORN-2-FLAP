"""One-time art preparation (Python + numpy); map generation uses retained imports.

Sources and license records live in Saved/YunshaSource. The resulting shared
Unreal imports are versioned through LFS; neither generation nor cooking needs
network access, numpy, Blender or a Windows-only converter.
"""
import copy
import json
import struct
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1] / 'Saved/YunshaSource'


def prepare(name):
    raw = (ROOT / (name + '.glb')).read_bytes()
    length = struct.unpack_from('<I', raw, 12)[0]
    doc = json.loads(raw[20:20 + length])
    binary = raw[28 + length:]
    output = bytearray()
    accessors, views, primitives, positions = [], [], [], []

    def block(data):
        output.extend(b'\0' * (-len(output) % 4))
        views.append(dict(buffer=0, byteOffset=len(output), byteLength=len(data)))
        output.extend(data)
        return len(views) - 1

    def read(index):
        a = doc['accessors'][index]; v = doc['bufferViews'][a['bufferView']]
        dtype = {5126: '<f4', 5125: '<u4', 5123: '<u2', 5121: 'u1'}[a['componentType']]
        dim = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
        item = np.dtype(dtype).itemsize
        return np.ndarray((a['count'], dim), dtype=dtype, buffer=binary,
                          offset=v.get('byteOffset', 0)+a.get('byteOffset', 0),
                          strides=(v.get('byteStride', item*dim), item)).copy()

    def write(values, kind, component=5126):
        a = dict(bufferView=block(values.astype('<f4' if component == 5126 else '<u4').tobytes()),
                 componentType=component, count=len(values), type=kind)
        if kind == 'VEC3':
            a.update(min=values.min(axis=0).tolist(), max=values.max(axis=0).tolist())
        accessors.append(a)
        return len(accessors)-1

    def visit(index, parent):
        n = doc['nodes'][index]
        if 'matrix' in n:
            local = np.array(n['matrix']).reshape(4, 4).T
        else:
            x,y,z,w = n.get('rotation', [0,0,0,1])
            local = np.eye(4)
            local[:3,:3] = np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
                [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
                [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]) @ np.diag(n.get('scale',[1,1,1]))
            local[:3,3] = n.get('translation',[0,0,0])
        matrix = parent @ local
        if 'mesh' in n:
            mesh = doc['meshes'][n['mesh']]
            keep = not (name == 'Pagoda' and 'Ground_0' in mesh.get('name',''))
            keep &= not (name == 'TempleHalls' and n['mesh'] != 1)
            if keep:
                for original in mesh['primitives']:
                    p = copy.deepcopy(original)
                    pos = read(p['attributes']['POSITION'])
                    pos = (np.c_[pos, np.ones(len(pos))] @ matrix.T)[:,:3]
                    indices = read(p['indices']).reshape(-1,3)
                    # Remove the photographed road/cars around the hall. Keep
                    # the building, and bed its trimmed basement into terrain.
                    if name == 'TempleHalls': indices = indices[np.min(pos[indices,1],axis=1)>8]
                    # Paifang ships on a huge display plane. Keep the actual
                    # gate and its footings; no ground slab across the lagoon.
                    if name == 'Paifang':
                        tri=pos[indices]
                        flat=(np.ptp(tri[:,:,1],axis=1)<.01)
                        broad=np.max(np.ptp(tri,axis=1),axis=1)>8000
                        indices=indices[~(flat&broad)]
                    if not len(indices):continue
                    if np.linalg.det(matrix[:3,:3])<0: indices=indices[:,[0,2,1]]
                    used, remap = np.unique(indices,return_inverse=True)
                    attrs = {}
                    for key, source in p['attributes'].items():
                        if key == 'TANGENT': continue
                        data = pos[used] if key == 'POSITION' else read(source)[used]
                        if key == 'NORMAL':
                            data = data @ np.linalg.inv(matrix[:3,:3])
                            data /= np.maximum(np.linalg.norm(data,axis=1)[:,None],1e-10)
                        attrs[key] = write(data, doc['accessors'][source]['type'])
                        if key == 'POSITION': positions.append((attrs[key],data))
                    p['attributes']=attrs
                    p['indices']=write(remap.reshape(-1,1),'SCALAR',5125)
                    primitives.append(p)
        for child in n.get('children',[]): visit(child,matrix)

    for root in doc['scenes'][doc.get('scene',0)]['nodes']: visit(root,np.eye(4))
    combined=np.concatenate([p for _,p in positions]);low=combined.min(axis=0);high=combined.max(axis=0)
    centre=(low+high)/2;centre[1]=low[1]
    for index,pos in positions:
        values=pos-centre;a=accessors[index];v=views[a['bufferView']]
        output[v['byteOffset']:v['byteOffset']+v['byteLength']]=values.astype('<f4').tobytes()
        a.update(min=values.min(axis=0).tolist(),max=values.max(axis=0).tolist())
    for im in doc.get('images',[]):
        v=doc['bufferViews'][im['bufferView']]
        im['bufferView']=block(binary[v.get('byteOffset',0):v.get('byteOffset',0)+v['byteLength']])
    doc.update(accessors=accessors,bufferViews=views,buffers=[dict(byteLength=len(output))],
               meshes=[dict(name=name,primitives=primitives)],nodes=[dict(name=name,mesh=0)],scenes=[dict(nodes=[0])],scene=0)
    doc.pop('animations',None);doc.pop('skins',None)
    encoded=json.dumps(doc,separators=(',',':')).encode();encoded+=b' '*(-len(encoded)%4)
    output+=b'\0'*(-len(output)%4)
    (ROOT/(name+'_prepared.glb')).write_bytes(struct.pack('<III',0x46546c67,2,28+len(encoded)+len(output))+
        struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',len(output),0x004e4942)+output)
    print(name,'bounds',high-low,'bytes',len(output),flush=True)
    if name=='GoldenHall':
        from carve_yunsha_hall import carve
        carve(ROOT/(name+'_prepared.glb'))


if __name__ == '__main__':
    import sys
    for name in (sys.argv[1:] or ('TempleHalls','Pagoda','Gate','GoldenHall','Paifang','Bonsai')): prepare(name)
