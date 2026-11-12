"""Compare real engine motion about the instantaneous wing-plane normal."""
import json, math, re
from pathlib import Path
OUT=Path(__file__).resolve().parents[1]/'.setup/knife-edge-20261112'
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def parse(path):
    traces={name:[] for name in ('TailAirTrace','TurnAirTrace','SpatialHostTrace')}
    for line in path.read_text(errors='replace').splitlines():
        for name in traces:
            if name+' ' not in line:continue
            f=dict(re.findall(r'(\w+)=(\([^)]*\)|[^ ]+)',line.split(name+' ',1)[1]))
            traces[name].append({k:[float(x) for x in v[1:-1].split(',')] if v.startswith('(') else float(v) for k,v in f.items()})
    rows=[]; angle=0; previous=None
    by_time={round(r['t'],6):r for r in traces['TurnAirTrace']}
    for a in traces['TailAirTrace']:
        b=by_time.get(round(a['t'],6))
        if b is None:continue
        if a['t']<3:continue
        if previous is not None:angle+=(a['t']-previous)*dot(a['rates'],b['normalWorld'])*180/math.pi
        previous=a['t']
        rows.append({**a,**b,'turnPlane':angle,'wingBank':math.degrees(math.atan2(b['normalWorld'][1],b['normalWorld'][2]))})
    errors=[math.sqrt(sum((x-y)**2 for x,y in zip(r['actual'],r['previousPredicted']))) for r in traces['SpatialHostTrace'] if 3.1<r['t']<4]
    return rows,max(errors,default=0)
result={}
for p in sorted(OUT.glob('d*-roll*.log')):
    rows,error=parse(p)
    if not rows:continue
    result[p.stem]={'rows':rows,'hostRateError':error}
    for t in (3.1,3.25,3.5,4):
        r=min(rows,key=lambda x:abs(x['t']-t))
        print(f"{p.stem} t={r['t']:.3f} wingBank={r['wingBank']:.2f} bodyBank={r['attitude'][0]:.2f} "
              f"turnPlane={r['turnPlane']:.2f} tailYaw={r['tailYawPlane']:.6f} wingYaw={r['wingYawPlane']:.6f} "
              f"wingRoll={r['wingRoll']:.6f} U={tuple(round(x,3) for x in r['meanU'])} "
              f"lift={tuple(round(x,3) for x in r['liftPlane'])} hostError={error:.6g} failures={r['failures']}")
(OUT/'runtime-summary.json').write_text(json.dumps(result,indent=2)+'\n')
