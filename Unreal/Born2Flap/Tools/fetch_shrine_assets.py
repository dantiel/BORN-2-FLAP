"""Attributed CC-BY shrine assets, obtained from the public Ai2 Objaverse mirror."""
import concurrent.futures
import hashlib
import json
from fetch_nature_assets import ROOT, read

SOURCES={
    'CoastalTorii':('e12d2fa1b2b94928b8b87cb7787e2462','000-023'),
    'CoastalShrine':('b78796a957a24cb092092a15efcec040','000-114'),
}

def fetch_model(item):
    name,(uid,group)=item
    metadata=json.loads(read('https://api.sketchfab.com/v3/models/'+uid))
    assert metadata['license']['slug']=='by', 'Review changed asset license before importing'
    folder=ROOT/name;folder.mkdir(parents=True,exist_ok=True)
    path=folder/(name+'.glb')
    url='https://huggingface.co/datasets/allenai/objaverse/resolve/main/glbs/'+group+'/'+uid+'.glb'
    if not path.exists():path.write_bytes(read(url))
    metadata['download_source']=url
    metadata['sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
    (folder/'source.json').write_text(json.dumps(metadata,indent=2),encoding='utf-8')
    (folder/'preview.jpg').write_bytes(read(metadata['thumbnails']['images'][0]['url']))
    print(name,path.stat().st_size,metadata['user']['displayName'],metadata['license']['url'],flush=True)

if __name__=='__main__':
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:list(pool.map(fetch_model,SOURCES.items()))
