"""Fetch licensed lava scans from the public Objaverse distribution and normalize GLBs.

Run with normal Python (numpy required), then import_lava_assets.py in Unreal.
Original metadata, license and downloaded bytes remain in Saved/LavaSource.
"""
import hashlib
import json
import struct
import urllib.request
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[1]/'Saved/LavaSource'
ASSETS=[('Lava','c365f6c43be04fc7a75ac232a384a04b','000-028'),
        ('Scoria','6dbd07399559472fba3f4c34c5bd7d0e','000-117')]

def normalize(source,target,name):
    blob=source.read_bytes()
    length=struct.unpack_from('<I',blob,12)[0]
    doc=json.loads(blob[20:20+length])
    binary=bytearray(blob[28+length:])
    rotation=np.array(doc['nodes'][0].get('matrix',np.eye(4).flatten()),dtype=float).reshape(4,4,order='F')[:3,:3]
    primitives=[p for m in doc['meshes'] for p in m['primitives'] if doc['accessors'][p['attributes']['POSITION']]['count']>3]
    def array(index):
        a=doc['accessors'][index];v=doc['bufferViews'][a['bufferView']]
        return np.ndarray((a['count'],3),dtype='<f4',buffer=binary,
            offset=v.get('byteOffset',0)+a.get('byteOffset',0),strides=(v.get('byteStride',12),4))
    positions={p['attributes']['POSITION'] for p in primitives}
    transformed={i:array(i).copy()@rotation.T for i in positions}
    combined=np.concatenate(list(transformed.values()))
    low,high=combined.min(axis=0),combined.max(axis=0)
    centre=(low+high)/2;centre[1]=low[1]
    scale=3./max(high-low)
    for i,data in transformed.items():
        values=(data-centre)*scale;array(i)[:]=values
        doc['accessors'][i]['min']=values.min(axis=0).tolist()
        doc['accessors'][i]['max']=values.max(axis=0).tolist()
    for i in {p['attributes']['NORMAL'] for p in primitives if 'NORMAL' in p['attributes']}:
        array(i)[:]=array(i).copy()@rotation.T
    for p in primitives:
        p['attributes'].pop('TANGENT',None)  # importer recomputes after orientation
    doc['meshes']=[{'name':name,'primitives':primitives}]
    doc['nodes']=[{'name':name,'mesh':0}];doc['scenes']=[{'nodes':[0]}];doc['scene']=0
    encoded=json.dumps(doc,separators=(',',':')).encode();encoded+=b' '*((-len(encoded))%4)
    binary+=b'\0'*((-len(binary))%4)
    target.write_bytes(struct.pack('<III',0x46546c67,2,28+len(encoded)+len(binary))+
        struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',len(binary),0x004e4942)+binary)

def main():
    ROOT.mkdir(exist_ok=True)
    manifest=[]
    for name,uid,group in ASSETS:
        source=ROOT/(uid+'.glb')
        url=f'https://huggingface.co/datasets/allenai/objaverse/resolve/main/glbs/{group}/{uid}.glb'
        if not source.exists():source.write_bytes(urllib.request.urlopen(url,timeout=60).read())
        meta=ROOT/(uid+'.json')
        if not meta.exists():meta.write_bytes(urllib.request.urlopen('https://api.sketchfab.com/v3/models/'+uid,timeout=30).read())
        info=json.loads(meta.read_text(encoding='utf8'))
        normalize(source,ROOT/(name+'.glb'),name)
        manifest.append(dict(name=name,source=info['viewerUrl'],author=info['user']['displayName'],
            license=info['license'],distribution=url,sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
            modifications='Merged scan sections, recentered and normalized to 3 metres; game instances recolored and scaled.'))
    (ROOT/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf8')
    print('LAVA_SOURCES_READY')

if __name__=='__main__':main()
