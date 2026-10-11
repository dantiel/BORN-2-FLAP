"""Bake licensed glTF node transforms into one mesh, retaining original UVs/materials."""
import copy
import json
import struct
import numpy as np
from fetch_nature_assets import ROOT

def flatten(name):
    path=ROOT/name/(name+'.glb');raw=path.read_bytes()
    size=struct.unpack_from('<I',raw,12)[0];doc=json.loads(raw[20:20+size])
    data=bytearray(raw[28+size:]);primitives=[]
    def values(index):
        a=doc['accessors'][index];v=doc['bufferViews'][a['bufferView']]
        dtype={5126:'<f4',5125:'<u4',5123:'<u2',5121:'u1'}[a['componentType']]
        dim={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
        offset=v.get('byteOffset',0)+a.get('byteOffset',0)
        return np.ndarray((a['count'],dim),dtype=dtype,buffer=data,offset=offset,
                          strides=(v.get('byteStride',np.dtype(dtype).itemsize*dim),np.dtype(dtype).itemsize)).copy()
    def append(array,kind,ctype=5126):
        while len(data)%4:data.append(0)
        offset=len(data);chunk=array.astype('<f4' if ctype==5126 else '<u4').tobytes();data.extend(chunk)
        doc['bufferViews'].append(dict(buffer=0,byteOffset=offset,byteLength=len(chunk)))
        a=dict(bufferView=len(doc['bufferViews'])-1,componentType=ctype,count=len(array),type=kind)
        if kind=='VEC3':a.update(min=array.min(axis=0).tolist(),max=array.max(axis=0).tolist())
        doc['accessors'].append(a);return len(doc['accessors'])-1
    def visit(index,parent):
        n=doc['nodes'][index]
        assert not any(k in n for k in ('translation','rotation','scale')),'Unexpected TRS; review source'
        matrix=parent@np.array(n.get('matrix',np.eye(4).T.flatten())).reshape(4,4).T
        if 'mesh' in n:
            for original in doc['meshes'][n['mesh']]['primitives']:
                p=copy.deepcopy(original);positions=values(p['attributes']['POSITION'])
                positions=(np.c_[positions,np.ones(len(positions))]@matrix.T)[:,:3]
                p['attributes']['POSITION']=append(positions,'VEC3')
                if 'NORMAL' in p['attributes']:
                    normals=values(p['attributes']['NORMAL'])@np.linalg.inv(matrix[:3,:3])
                    normals/=np.maximum(np.linalg.norm(normals,axis=1)[:,None],1e-10)
                    p['attributes']['NORMAL']=append(normals,'VEC3')
                p['attributes'].pop('TANGENT',None)
                if np.linalg.det(matrix[:3,:3])<0:
                    indices=values(p['indices']).reshape(-1,3)[:,[0,2,1]].reshape(-1,1)
                    p['indices']=append(indices,'SCALAR',5125)
                primitives.append(p)
        for child in n.get('children',[]):visit(child,matrix)
    for root in doc['scenes'][doc.get('scene',0)]['nodes']:visit(root,np.eye(4))
    doc['meshes']=[dict(name=name,primitives=primitives)];doc['nodes']=[dict(name=name,mesh=0)]
    doc['scenes']=[dict(nodes=[0])];doc['scene']=0;doc.pop('animations',None)
    doc['buffers']=[dict(byteLength=len(data))]
    encoded=json.dumps(doc,separators=(',',':')).encode();encoded+=b' '*((-len(encoded))%4)
    data+=b'\0'*((-len(data))%4)
    result=struct.pack('<III',0x46546c67,2,28+len(encoded)+len(data))+struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',len(data),0x004e4942)+data
    (ROOT/name/(name+'_baked.glb')).write_bytes(result)
    print(name,len(primitives),'material parts retained')

if __name__=='__main__':
    for name in ('CoastalTorii','CoastalShrine'):flatten(name)
