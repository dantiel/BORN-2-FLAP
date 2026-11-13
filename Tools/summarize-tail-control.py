"""Summarize opt-in telemetry from normal play; never changes game state."""
import argparse, json, math, re
from pathlib import Path

def parse(path):
    rows=[]
    for line in path.read_text(errors='replace').splitlines():
        if 'TailControlTrace t=' not in line:continue
        fields=dict(re.findall(r'(\w+)=(\([^)]*\)|[^ ]+)',line.split('TailControlTrace ',1)[1]))
        r={k:[float(x) for x in v[1:-1].split(',')] if v.startswith('(') else float(v) for k,v in fields.items()}
        normal=r['wingNormalWorld']
        r['wingBank']=math.degrees(math.atan2(normal[1],normal[2]))
        r['relativeBank']=(r['pose'][0]-r['wingBank']+180)%360-180
        r['common']=sum(r['shoulders'])/2
        r['speed']=math.sqrt(sum(x*x for x in r['velocity']))
        r['slip']=math.degrees(math.atan2(r['velocity'][1],math.hypot(r['velocity'][0],r['velocity'][2])))
        rows.append(r)
    return rows

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('log',type=Path)
    ap.add_argument('--output',type=Path)
    ap.add_argument('--interval',type=float,default=.5)
    args=ap.parse_args();rows=parse(args.log)
    if args.output:args.output.write_text(json.dumps(rows,indent=2)+'\n')
    print(f'frames={len(rows)}')
    last=-float('inf')
    for r in rows:
        if r['t']<last:last=-float('inf')
        if r['t']-last<args.interval:continue
        last=r['t']
        print(f"t={r['t']:.3f} stick={tuple(round(x,3) for x in r['pilot'])} U={r['velocity'][0]:.2f} "
              f"slip={r['slip']:.1f} common={r['common']:.1f} wingBank={r['wingBank']:.1f} "
              f"bodyRelative={r['relativeBank']:.1f} tailAlpha={r['tailAlpha']:.2f} "
              f"tailYawPlane={r['tailYawPlane']:.6f} wingYawPlane={r['wingYawPlane']:.6f} "
              f"bodyRates={tuple(round(x,3) for x in r['rates'])} failures={r['failures']:g}")
if __name__=='__main__':main()
