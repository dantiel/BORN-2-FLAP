"""Pose a licensed skinned adventurer non-destructively; retain the original rig."""
import json,struct,copy,hashlib,urllib.request
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]/'Saved/NatureSource/Founder'
UID='9f51d396733b451bab550b406b067805'

def bake():
    ROOT.mkdir(parents=True,exist_ok=True)
    source=ROOT/'Founder_rigged.glb'
    url='https://huggingface.co/datasets/allenai/objaverse/resolve/main/glbs/000-122/'+UID+'.glb'
    if not source.exists():urllib.request.urlretrieve(url,source)
    metadata=json.load(urllib.request.urlopen('https://api.sketchfab.com/v3/models/'+UID))
    assert metadata['license']['slug']=='by'
    metadata.update(download_source=url,sha256=hashlib.sha256(source.read_bytes()).hexdigest())
    (ROOT/'source.json').write_text(json.dumps(metadata,indent=2),encoding='utf-8')
    raw=source.read_bytes();size=struct.unpack_from('<I',raw,12)[0]
    doc=json.loads(raw[20:20+size]);data=bytearray(raw[28+size:])
    def values(i):
        a=doc['accessors'][i];v=doc['bufferViews'][a['bufferView']]
        dt=np.dtype({5126:'<f4',5125:'<u4',5123:'<u2',5121:'u1'}[a['componentType']]);d={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
        return np.ndarray((a['count'],d),dtype=dt,buffer=data,offset=v.get('byteOffset',0)+a.get('byteOffset',0),strides=(v.get('byteStride',dt.itemsize*d),dt.itemsize)).copy()
    def local(n):
        if 'matrix' in n:return np.array(n['matrix']).reshape(4,4).T
        x,y,z,w=n.get('rotation',[0,0,0,1]);m=np.eye(4)
        m[:3,:3]=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])@np.diag(n.get('scale',[1,1,1]))
        m[:3,3]=n.get('translation',[0,0,0]);return m
    world={};parents={}
    def visit(i,p):
        world[i]=p@local(doc['nodes'][i])
        for c in doc['nodes'][i].get('children',[]):parents[c]=i;visit(c,world[i])
    for i in doc['scenes'][doc.get('scene',0)]['nodes']:visit(i,np.eye(4))
    # Work in a canonical upright glTF space derived from the rig itself.
    pos=lambda i:world[i][:3,3]
    up=pos(11)-pos(6);up/=np.linalg.norm(up)
    right=pos(40)-pos(16);right-=up*np.dot(up,right);right/=np.linalg.norm(right)
    forward=np.cross(right,up);basis=np.stack([right,up,forward])
    canon=np.eye(4);canon[:3,:3]=basis
    world={i:canon@m for i,m in world.items()}
    def aim(joint,child,direction):
        a=world[child][:3,3]-world[joint][:3,3];a/=np.linalg.norm(a)
        b=np.array(direction,dtype=float);b/=np.linalg.norm(b);v=np.cross(a,b);c=np.dot(a,b)
        k=np.array([[0,-v[2],v[1]],[v[2],0,-v[0]],[-v[1],v[0],0]])
        r=np.eye(3)+k+k@k/(1+c);t=np.eye(4);t[:3,:3]=r;t[:3,3]=world[joint][:3,3]-r@world[joint][:3,3]
        def move(i):
            world[i]=t@world[i]
            for ch in doc['nodes'][i].get('children',[]):move(ch)
        move(joint)
    # Relaxed right arm; left forearm presents the kestrel above an open hand.
    aim(14,15,[-.6,-.8,0]);aim(15,16,[-.25,.45,.85])
    aim(38,39,[.28,-.96,.02]);aim(39,40,[.10,-.9,.3])
    def append(a,kind):
        while len(data)%4:data.append(0)
        chunk=a.astype('<f4').tobytes();off=len(data);data.extend(chunk)
        doc['bufferViews'].append(dict(buffer=0,byteOffset=off,byteLength=len(chunk)))
        ac=dict(bufferView=len(doc['bufferViews'])-1,componentType=5126,count=len(a),type=kind)
        if kind=='VEC3':ac.update(min=a.min(0).tolist(),max=a.max(0).tolist())
        doc['accessors'].append(ac);return len(doc['accessors'])-1
    parts=[];allpos=[]
    for n in doc['nodes']:
        if 'mesh' not in n:continue
        skin=doc['skins'][n['skin']];ib=values(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
        matrices=np.array([world[j]@inv for j,inv in zip(skin['joints'],ib)])
        for original in doc['meshes'][n['mesh']]['primitives']:
            p=copy.deepcopy(original);a=p['attributes'];v=values(a['POSITION']);j=values(a['JOINTS_0']).astype(int);w=values(a['WEIGHTS_0']);w/=np.maximum(w.sum(1)[:,None],1e-9)
            transforms=(matrices[j]*w[:,:,None,None]).sum(1)
            xyz=np.einsum('nij,nj->ni',transforms,np.c_[v,np.ones(len(v))])[:,:3]
            normals=np.einsum('nij,nj->ni',np.linalg.inv(transforms[:,:3,:3]).transpose(0,2,1),values(a['NORMAL']));normals/=np.maximum(np.linalg.norm(normals,axis=1)[:,None],1e-9)
            parts.append((p,xyz,normals));allpos.append(xyz)
    points=np.concatenate(allpos);lo=points.min(0);hi=points.max(0);scale=2.8/(hi[1]-lo[1]);origin=np.array([(lo[0]+hi[0])/2,lo[1],(lo[2]+hi[2])/2])
    for p,xyz,normals in parts:
        a=p['attributes'];a['POSITION']=append((xyz-origin)*scale,'VEC3');a['NORMAL']=append(normals,'VEC3')
        for key in ['JOINTS_0','WEIGHTS_0','TANGENT']:a.pop(key,None)
    hand=(world[16][:3,3]-origin)*scale
    (ROOT/'pose.json').write_text(json.dumps(dict(height_cm=280,left_hand_gltf_m=hand.tolist()),indent=2))
    doc['meshes']=[dict(name='Founder',primitives=[p for p,_,_ in parts])];doc['nodes']=[dict(name='Founder',mesh=0)];doc['scenes']=[dict(nodes=[0])];doc['scene']=0
    doc.pop('skins',None);doc.pop('animations',None);doc['buffers']=[dict(byteLength=len(data))]
    enc=json.dumps(doc,separators=(',',':')).encode();enc+=b' '*((-len(enc))%4);data+=b'\0'*((-len(data))%4)
    (ROOT/'Founder_posed.glb').write_bytes(struct.pack('<III',0x46546c67,2,28+len(enc)+len(data))+struct.pack('<II',len(enc),0x4e4f534a)+enc+struct.pack('<II',len(data),0x004e4942)+data)
    print('FOUNDER_POSED',len(points),'vertices, hand',hand)
if __name__=='__main__':bake()
