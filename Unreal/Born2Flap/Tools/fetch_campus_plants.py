"""Pinned CC0 ornamental planting source. Powered by Poly Haven."""
import concurrent.futures
import json
from pathlib import Path
from fetch_nature_assets import ROOT, read, fetch

def main():
    name='periwinkle_plant'
    files=json.loads(read('https://api.polyhaven.com/files/'+name))
    entry=files['gltf']['2k']['gltf']
    jobs=[(ROOT/name/(name+'.gltf'),entry)]
    jobs.extend((ROOT/name/relative,info) for relative,info in entry['include'].items())
    opacity=files['opacity']['2k'].get('png') or files['opacity']['2k']['jpg']
    jobs.append((ROOT/name/('opacity'+Path(opacity['url']).suffix),opacity))
    ROOT.joinpath(name).mkdir(parents=True,exist_ok=True)
    ROOT.joinpath(name,'source.json').write_text(json.dumps({'source':'https://polyhaven.com/a/'+name,'license':'CC0','gltf':entry,'opacity':opacity},indent=2))
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:list(pool.map(fetch,jobs))
    for ground in ('leafy_grass','forrest_ground_03','mossy_rock'):
        files=json.loads(read('https://api.polyhaven.com/files/'+ground))
        jobs=[(ROOT/ground/(channel+'.jpg'),files[channel]['2k']['jpg']) for channel in ('Diffuse','nor_dx','Rough')]
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:list(pool.map(fetch,jobs))
        ROOT.joinpath(ground,'source.json').write_text(json.dumps({'source':'https://polyhaven.com/a/'+ground,'license':'CC0','files':dict((p.name,info) for p,info in jobs)},indent=2))
    print('CAMPUS_PLANT_SOURCES_READY')

if __name__=='__main__':main()
