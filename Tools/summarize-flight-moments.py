"""Report whole-cycle and up/downstroke moments; body +Y is nose-down."""
import argparse,csv,json,statistics
from pathlib import Path
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('directory',type=Path);args=ap.parse_args()
summary={}
for p in sorted(args.directory.glob('moments-*.csv')):
    with p.open() as f:r=[{k:float(v) for k,v in row.items()} for row in csv.DictReader(f)]
    r=[x for x in r if x['t']>=2]
    if not r:continue
    groups={}
    for side in (-1,1):
      for phase in ('cycle','up','down'):
        rr=[x for x in r if x['side']==side and (phase=='cycle' or (x['rate_deg_s']>0 if phase=='up' else x['rate_deg_s']<0))]
        if not rr:continue
        mean={k:statistics.mean(x[k] for x in rr) for k in rr[0]}
        groups[f'{side}/{phase}']=mean
    summary[p.stem]=groups
    print(p.stem)
    for side in (-1,1):
      x=groups[f'{side}/cycle'];u=groups.get(f'{side}/up',x);d=groups.get(f'{side}/down',x)
      print(f" side {side:+}: angle={x['angle']:.1f} Fx/Fz={x['Fx']:.4f}/{x['Fz']:.4f} "
            f"Mx/My/Mz={x['Mx']:.4f}/{x['My']:.4f}/{x['Mz']:.4f} "
            f"My thrust/vertical/couple={x['My_thrust']:.4f}/{x['My_vertical']:.4f}/{x['couple_y']:.4f} "
            f"Fz up/down={u['Fz']:.4f}/{d['Fz']:.4f} tail My={x['tail_My']:.4f}")
(args.directory/'moment-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
