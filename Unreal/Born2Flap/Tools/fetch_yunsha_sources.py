"""Optional art-maintenance download; ordinary map builds use retained LFS imports."""
import hashlib
import json
from pathlib import Path
from fetch_nature_assets import read, fetch


def main():
    project=Path(__file__).resolve().parents[1]
    records=json.loads((project/'Content/ThirdParty/Yunsha-assets.json').read_text(encoding='utf8'))
    root=project/'Saved/YunshaSource';root.mkdir(parents=True,exist_ok=True)
    for record in records:
        name=record['name']
        if 'download' in record:
            path=root/(name+'.glb')
            if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest()!=record['sha256']:
                data=read(record['download']+'?download=true')
                assert hashlib.sha256(data).hexdigest()==record['sha256'], 'Changed source: '+name
                path.write_bytes(data)
        else:
            entry=record['files']
            for path,info in [(root/name/(name+'.gltf'),entry)]+[(root/name/k,v) for k,v in entry['include'].items()]:
                fetch((path,info))
    print('YUNSHA_SOURCES_VERIFIED')


if __name__=='__main__':main()
