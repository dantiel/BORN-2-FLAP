"""Fetch the small CC0 source set used by create_nature_assets.py.

Sources remain in ignored Saved/NatureSource; imported Unreal assets are portable.
Poly Haven requires an identifying User-Agent for its public asset API.
"""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1] / 'Saved/NatureSource'
HEADERS = {'User-Agent': 'Born2Flap-development/1.0 (CC0 game asset import)'}


def read(url):
    for attempt in range(3):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=45) as response:
                return response.read()
        except (TimeoutError, OSError):
            if attempt == 2:
                raise
            print('Retrying', url.rsplit('/', 1)[-1], flush=True)


def fetch(item):
    target, info = item
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() and hashlib.md5(target.read_bytes()).hexdigest() == info['md5']:
        return
    data = read(info['url'])
    if hashlib.md5(data).hexdigest() != info['md5']:
        raise RuntimeError('Source checksum mismatch: ' + info['url'])
    target.write_bytes(data)
    print(target.name, len(data), flush=True)


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    jobs, manifest = [], {}
    for name, model in [('fir_sapling_medium', True), ('grass_medium_01', True),
                        ('rock_moss_set_01', True), ('aerial_grass_rock', False),
                        ('forest_ground_04', False), ('rock_04', False)]:
        files = json.loads(read('https://api.polyhaven.com/files/' + name))
        manifest[name] = {'source': 'https://polyhaven.com/a/' + name, 'license': 'CC0', 'files': {}}
        if model:
            entry = files['gltf']['1k']['gltf']
            jobs.append((ROOT / name / (name + '.gltf'), entry))
            for relative, info in entry['include'].items():
                jobs.append((ROOT / name / relative, info))
            manifest[name]['files']['gltf'] = entry
            # glTF does not always carry the cutout map: import it explicitly.
            for kind, resolutions in files.items():
                if 'alpha' in kind.lower():
                    formats = resolutions['1k']
                    info = formats.get('png', formats.get('jpg'))
                    if info:
                        jobs.append((ROOT / name / (kind + Path(info['url']).suffix), info))
                        manifest[name]['files'][kind] = info
        else:
            for kind in ['Diffuse', 'nor_dx', 'Rough']:
                info = files[kind]['1k']['jpg']
                jobs.append((ROOT / name / (kind + '.jpg'), info))
                manifest[name]['files'][kind] = info
    (ROOT / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        list(pool.map(fetch, jobs))
    print('B2F_NATURE_SOURCES_READY', flush=True)


if __name__ == '__main__':
    main()
