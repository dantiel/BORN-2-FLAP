"""Summarize actual Unreal/Chaos tail fixtures; no simulator equations reused."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.setup/tail-coupling-20261112'

def read(path):
    rows=[]
    for line in path.read_text(errors='replace').splitlines():
        if 'TailAirTrace ' not in line:
            continue
        fields=dict(re.findall(r'(\w+)=(\([^)]*\)|[^ ]+)',line.split('TailAirTrace ',1)[1]))
        rows.append({k: [float(x) for x in v[1:-1].split(',')] if v.startswith('(') else float(v)
                     for k,v in fields.items()})
    return rows

if __name__=='__main__':
    result={}
    for path in sorted(OUT.glob('*.log')):
        rows=read(path)
        if not rows: continue
        result[path.stem]=rows
        for t in (3,3.5,4,5):
            r=min(rows,key=lambda x:abs(x['t']-t))
            print(f"{path.stem} t={r['t']:.3f} pose={tuple(round(x,3) for x in r['attitude'])} "
                  f"tailMz={r['momentWorld'][2]:+.7f} parts={tuple(round(x,7) for x in r['yawParts'])} "
                  f"alpha={r['alpha']:.3f} Fbody={tuple(round(x,6) for x in r['forceBody'])} failures={r['failures']}")
    (OUT/'runtime-tail.json').write_text(json.dumps(result,indent=2)+'\n')
