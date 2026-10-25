"""Open an axial flight passage in the licensed hall, preserving authored UVs.

Subtract a rectangular prism from triangles, interpolating all vertex attributes
at the cut. This changes visible geometry and collision together; it does not
disable walls or hide blocking collision. Coordinates are the centred source
model's units (Y up). The existing floor is at Y=7.5, lintels start at Y=48.7.
"""
import json
import struct
import numpy as np


def carve(path):
    raw=path.read_bytes();length=struct.unpack_from('<I',raw,12)[0]
    doc=json.loads(raw[20:20+length]);old=raw[28+length:]
    output=bytearray();views=[];accessors=[]
    def block(data):
        output.extend(b'\0'*(-len(output)%4));views.append(dict(buffer=0,byteOffset=len(output),byteLength=len(data)));output.extend(data)
        return len(views)-1
    def read(index):
        a=doc['accessors'][index];v=doc['bufferViews'][a['bufferView']]
        dim={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
        dtype='<f4' if a['componentType']==5126 else '<u4'
        return np.frombuffer(old,dtype=dtype,count=a['count']*dim,offset=v['byteOffset']).reshape(-1,dim)
    def write(data,kind,integer=False):
        a=dict(bufferView=block(data.astype('<u4' if integer else '<f4').tobytes()),componentType=5125 if integer else 5126,count=len(data),type=kind)
        if kind=='VEC3':a.update(min=data.min(axis=0).tolist(),max=data.max(axis=0).tolist())
        accessors.append(a);return len(accessors)-1
    def split(poly,axis,bound,sign):
        inside=[];outside=[]
        for a,b in zip(poly,poly[1:]+poly[:1]):
            da=sign*(a[axis]-bound);db=sign*(b[axis]-bound)
            (inside if da>=0 else outside).append(a)
            if (da>=0)!=(db>=0):
                p=a+(b-a)*(da/(da-db));inside.append(p);outside.append(p)
        return inside,outside
    for p in doc['meshes'][0]['primitives']:
        keys=['POSITION']+[k for k in p['attributes'] if k!='POSITION']
        arrays=[read(p['attributes'][k]) for k in keys]
        packed=np.concatenate(arrays,axis=1);points=arrays[0]
        indices=read(p['indices']).reshape(-1,3)
        lo=points[indices].min(axis=1);hi=points[indices].max(axis=1)
        intersects=(lo[:,0]<18)&(hi[:,0]>-18)&(lo[:,1]<46)&(hi[:,1]>8)
        contained=(lo[:,0]>=-18)&(hi[:,0]<=18)&(lo[:,1]>=8)&(hi[:,1]<=46)
        kept=indices[~intersects];extra=[]
        for tri in indices[intersects&~contained]:
            poly=[packed[i].copy() for i in tri]
            for axis,bound,sign in ((0,-18,1),(0,18,-1),(1,8,1),(1,46,-1)):
                if not poly:break
                poly,outside=split(poly,axis,bound,sign)
                for j in range(1,len(outside)-1):extra.extend((outside[0],outside[j],outside[j+1]))
        if extra:
            offset=len(packed);packed=np.concatenate((packed,np.array(extra)))
            kept=np.concatenate((kept,np.arange(offset,len(packed)).reshape(-1,3)))
        if len(kept)==0:
            p['_empty']=True;continue
        used,remap=np.unique(kept,return_inverse=True);packed=packed[used]
        attrs={};offset=0
        for key,array in zip(keys,arrays):
            data=packed[:,offset:offset+array.shape[1]];offset+=array.shape[1]
            if key=='NORMAL':data=data/np.maximum(np.linalg.norm(data,axis=1)[:,None],1e-10)
            attrs[key]=write(data,doc['accessors'][p['attributes'][key]]['type'])
        p['attributes']=attrs;p['indices']=write(remap.reshape(-1,1),'SCALAR',True)
    doc['meshes'][0]['primitives']=[p for p in doc['meshes'][0]['primitives'] if not p.pop('_empty',False)]
    for im in doc.get('images',[]):
        v=doc['bufferViews'][im['bufferView']];im['bufferView']=block(old[v['byteOffset']:v['byteOffset']+v['byteLength']])
    doc.update(accessors=accessors,bufferViews=views,buffers=[dict(byteLength=len(output))])
    encoded=json.dumps(doc,separators=(',',':')).encode();encoded+=b' '*(-len(encoded)%4);output+=b'\0'*(-len(output)%4)
    path.write_bytes(struct.pack('<III',0x46546c67,2,28+len(encoded)+len(output))+struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',len(output),0x004e4942)+output)
    print('Hall doors opened: width 36, floor 8, lintel 46 source units',flush=True)
