"""Download the CC0 Ravenstonefield source palette, with publisher checksums."""
import concurrent.futures
import json
from pathlib import Path
from fetch_nature_assets import read, fetch

ROOT = Path(__file__).resolve().parents[1] / 'Saved/RavenstonefieldSource'
MODELS = ['tree_small_02', 'dead_tree_trunk']
TEXTURES = ['castle_wall_slates', 'clay_roof_tiles', 'concrete_moss',
            'clay_plaster', 'weathered_planks']


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    jobs, manifest = [], {}
    for name in MODELS + TEXTURES:
        files = json.loads(read('https://api.polyhaven.com/files/' + name))
        manifest[name] = {'source': 'https://polyhaven.com/a/' + name, 'license': 'CC0'}
        if name in MODELS:
            entry = files['gltf']['1k']['gltf']
            jobs.append((ROOT / name / (name + '.gltf'), entry))
            jobs.extend((ROOT / name / relative, info) for relative, info in entry['include'].items())
            for kind in files:
                if kind.endswith('_alpha'):
                    formats = files[kind]['1k']
                    info = formats.get('png', formats.get('jpg'))
                    jobs.append((ROOT / name / (kind + Path(info['url']).suffix), info))
        else:
            for kind in ['Diffuse', 'nor_dx', 'Rough']:
                info = files[kind]['2k']['jpg']
                jobs.append((ROOT / name / (kind + '.jpg'), info))
    manifest['downloads'] = [{'file': str(p.relative_to(ROOT)), 'url': i['url'], 'md5': i['md5']} for p, i in jobs]
    (ROOT / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        list(pool.map(fetch, jobs))
    print('RAVENSTONEFIELD_SOURCES_READY', flush=True)


if __name__ == '__main__':
    main()
