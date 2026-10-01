"""Fetch the CC0 texture set used by create_shiomori_map.py's photoreal pass.

Sources remain in ignored Saved/ShiomoriSource; imported Unreal assets are the
portable outputs. Poly Haven requires an identifying User-Agent for its API.
"""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1] / 'Saved/ShiomoriSource'
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


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    jobs, manifest = [], {}
    # Surface textures shared across the beach, promenade, industry and island.
    for name in ['sand_02', 'concrete_floor_02', 'aerial_rocks_02']:
        files = json.loads(read('https://api.polyhaven.com/files/' + name))
        manifest[name] = {'source': 'https://polyhaven.com/a/' + name, 'license': 'CC0', 'files': {}}
        for kind in ['Diffuse', 'nor_dx', 'Rough']:
            info = files[kind]['4k']['jpg']
            jobs.append((ROOT / name / (kind + '.jpg'), info))
            manifest[name]['files'][kind] = info
    (ROOT / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')

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

    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        list(pool.map(fetch, jobs))
    print('B2F_SHIOMORI_SOURCES_READY', flush=True)


if __name__ == '__main__':
    main()