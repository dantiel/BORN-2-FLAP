"""Preserve the vertex-coloured Herbstosaurus research scan in portable GLB.

One-time art maintenance only; ordinary map generation uses retained imports.
The pinned CC BY research archive is recorded in ThirdParty/Yunsha-assets.json.
"""
import json
import re
import struct
import zipfile
from pathlib import Path
import numpy as np


def convert(root):
    with zipfile.ZipFile(root/'Herbstosaurus.zip') as archive:
        raw=archive.read('3D models/Herbstosaurus holotype 2400K faces for positive.ply')
    end=raw.index(b'end_header\n')+len(b'end_header\n');header=raw[:end].decode()
    assert 'format binary_little_endian 1.0' in header
    nv=int(re.search(r'element vertex (\d+)',header)[1]);nf=int(re.search(r'element face (\d+)',header)[1])
    dtype=np.dtype([('pos','<f4',(3,)),('rgb','u1',(3,))])
    vertices=np.frombuffer(raw,dtype=dtype,count=nv,offset=end)
    faces=np.frombuffer(raw,dtype=np.dtype([('count','u1'),('indices','<i4',(3,))]),count=nf,offset=end+nv*dtype.itemsize)
    assert np.all(faces['count']==3)
    pos=vertices['pos'].copy();indices=faces['indices'].copy();normal=np.zeros_like(pos)
    tri=pos[indices];cross=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0])
    for i in range(3):np.add.at(normal,indices[:,i],cross)
    normal/=np.maximum(np.linalg.norm(normal,axis=1)[:,None],1e-12)
    srgb=vertices['rgb'].astype(np.float32)/255
    rgb=np.where(srgb<=.04045,srgb/12.92,((srgb+.055)/1.055)**2.4)
    binary=bytearray();views=[];accessors=[]
    def add(values,kind,component=5126):
        binary.extend(b'\0'*(-len(binary)%4));offset=len(binary)
        binary.extend(values.astype('<u4' if component==5125 else '<f4').tobytes())
        views.append(dict(buffer=0,byteOffset=offset,byteLength=len(binary)-offset))
        a=dict(bufferView=len(views)-1,componentType=component,count=len(values),type=kind)
        if kind=='VEC3':a.update(min=values.min(axis=0).tolist(),max=values.max(axis=0).tolist())
        accessors.append(a);return len(accessors)-1
    attrs=dict(POSITION=add(pos,'VEC3'),NORMAL=add(normal,'VEC3'),COLOR_0=add(rgb,'VEC3'))
    index=add(indices.reshape(-1,1),'SCALAR',5125)
    doc=dict(asset=dict(version='2.0'),buffers=[dict(byteLength=len(binary))],bufferViews=views,accessors=accessors,
        materials=[dict(name='ResearchVertexColour',doubleSided=True,pbrMetallicRoughness=dict(metallicFactor=0.,roughnessFactor=.9))],
        meshes=[dict(primitives=[dict(attributes=attrs,indices=index,material=0)])],nodes=[dict(mesh=0)],scenes=[dict(nodes=[0])],scene=0)
    encoded=json.dumps(doc,separators=(',',':')).encode();encoded+=b' '*(-len(encoded)%4);binary+=b'\0'*(-len(binary)%4)
    (root/'Herbstosaurus.glb').write_bytes(struct.pack('<III',0x46546c67,2,28+len(encoded)+len(binary))+struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',len(binary),0x004e4942)+binary)
    print('Herbstosaurus preserved',nv,'vertices',nf,'triangles',flush=True)


if __name__=='__main__':convert(Path(__file__).resolve().parents[1]/'Saved/YunshaSource')
