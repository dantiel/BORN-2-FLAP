"""CC0 photographed coastal surfaces; local sources and checksums retained."""
import concurrent.futures
import json
from pathlib import Path
from fetch_nature_assets import read, fetch

ROOT=Path(__file__).resolve().parents[1]/'Saved/CliffSource'
def main():
    jobs=[];manifest={}
    for name in ('rock_face_03','cliff_side','leafy_grass','forrest_ground_03'):
        files=json.loads(read('https://api.polyhaven.com/files/'+name))
        manifest[name]={'source':'https://polyhaven.com/a/'+name,'license':'CC0','files':{}}
        for channel in ('Diffuse','nor_dx','Rough'):
            info=files[channel]['4k']['jpg']
            jobs.append((ROOT/name/(channel+'.jpg'),info))
            manifest[name]['files'][channel]=info
    ROOT.mkdir(parents=True,exist_ok=True)
    (ROOT/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:list(pool.map(fetch,jobs))
    print('CLIFF_SOURCES_READY')
if __name__=='__main__':main()
