"""Fetch CC0 scanned shingle, coastal formations and sparse shrubs."""
import concurrent.futures,json
from pathlib import Path
from fetch_nature_assets import read,fetch
ROOT=Path(__file__).resolve().parents[1]/'Saved/EastShoreSource'
def main():
    jobs=[];manifest={}
    for name in ('coast_sand_rocks_02','dry_river_pebbles','gravelly_sand','shrub_02','coast_rocks_02','coast_land_rocks_02'):
        files=json.loads(read('https://api.polyhaven.com/files/'+name))
        record={'source':'https://polyhaven.com/a/'+name,'license':'CC0','files':{}};manifest[name]=record
        if 'gltf' in files and name in ('shrub_02','coast_rocks_02','coast_land_rocks_02'):
            entry=files['gltf']['2k']['gltf'];jobs.append((ROOT/name/(name+'.gltf'),entry));record['files']['gltf']=entry
            jobs.extend((ROOT/name/relative,info) for relative,info in entry['include'].items())
            if name=='shrub_02':
                entry=files['Alpha']['2k'].get('png',files['Alpha']['2k'].get('jpg'))
                jobs.append((ROOT/name/('Alpha'+Path(entry['url']).suffix),entry));record['files']['Alpha']=entry
        else:
            for channel in ('Diffuse','nor_dx','Rough'):
                entry=files[channel]['4k']['jpg'];jobs.append((ROOT/name/(channel+'.jpg'),entry));record['files'][channel]=entry
    ROOT.mkdir(parents=True,exist_ok=True)
    (ROOT/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:list(pool.map(fetch,jobs))
    print('EAST_SHORE_SOURCES_READY')
if __name__=='__main__':main()
